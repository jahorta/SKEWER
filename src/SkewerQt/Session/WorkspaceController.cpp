#include "WorkspaceController.h"

#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <filesystem>

namespace skewer::qt {

WorkspaceController::WorkspaceController(
    QString executableDirectory,
    QObject* parent)
    : QObject(parent),
      stateStore_(std::move(executableDirectory)),
      startupState_(stateStore_.load()),
      patchStore_(std::filesystem::path(
          stateStore_.workspaceDirectory().toStdWString())) {
    checkpointTimer_.setSingleShot(true);
    checkpointTimer_.setInterval(500);
    connect(&checkpointTimer_, &QTimer::timeout,
        this, &WorkspaceController::checkpointRequested);
    checkpointFinishedConnection_ = connect(
        &checkpointWatcher_, &QFutureWatcherBase::finished,
        this, &WorkspaceController::onCheckpointFinished);
}

bool WorkspaceController::isWritable() const noexcept {
    return stateStore_.isWritable();
}

QString WorkspaceController::workspaceDirectory() const {
    return stateStore_.workspaceDirectory();
}

QString WorkspaceController::errorString() const {
    return stateStore_.errorString();
}

const std::optional<WorkspaceState>& WorkspaceController::startupState() const noexcept {
    return startupState_;
}

void WorkspaceController::clearStartupState() {
    startupState_.reset();
}

void WorkspaceController::scheduleCheckpoint() {
    if (isWritable()) checkpointTimer_.start();
}

void WorkspaceController::stopCheckpoint() {
    checkpointTimer_.stop();
}

void WorkspaceController::beginCheckpoint(
    const skewer::core::FieldDocument* document,
    const WorkspaceState& state) {
    if (!isWritable()) return;
    auto payload = makeCheckpointPayload(document, state);
    if (checkpointWatcher_.isRunning()) {
        pendingCheckpoint_ = std::move(payload);
        return;
    }
    startCheckpoint(std::move(payload));
}

WorkspaceCheckpointOutcome WorkspaceController::flushCheckpoint(
    const skewer::core::FieldDocument* document,
    const WorkspaceState& state) {
    stopCheckpoint();
    disconnect(checkpointFinishedConnection_);
    if (checkpointWatcher_.isRunning()) {
        checkpointWatcher_.future().waitForFinished();
        (void)checkpointWatcher_.future().takeResult();
    }
    pendingCheckpoint_.reset();
    checkpointFinishedConnection_ = connect(
        &checkpointWatcher_, &QFutureWatcherBase::finished,
        this, &WorkspaceController::onCheckpointFinished);
    return performCheckpoint(
        std::filesystem::path(stateStore_.workspaceDirectory().toStdWString()),
        stateStore_.statePath(),
        makeCheckpointPayload(document, state));
}

WorkspaceController::WorkspaceCheckpointPayload
WorkspaceController::makeCheckpointPayload(
    const skewer::core::FieldDocument* document,
    const WorkspaceState& state) {
    WorkspaceCheckpointPayload payload{};
    payload.revision = ++nextCheckpointRevision_;
    payload.state = state;
    if (document != nullptr) {
        payload.hasDocument = true;
        payload.patch = skewer::core::makeFieldPatch(
            *document, preservedTriangleEdits_, preservedEctEdits_);
    }
    return payload;
}

void WorkspaceController::startCheckpoint(WorkspaceCheckpointPayload payload) {
    const auto workspaceDirectory = std::filesystem::path(
        stateStore_.workspaceDirectory().toStdWString());
    const auto statePath = stateStore_.statePath();
    checkpointWatcher_.setFuture(QtConcurrent::run(
        [workspaceDirectory, statePath, payload = std::move(payload)]() {
            return performCheckpoint(workspaceDirectory, statePath, payload);
        }));
}

void WorkspaceController::onCheckpointFinished() {
    const auto outcome = checkpointWatcher_.future().takeResult();
    if (pendingCheckpoint_.has_value()) {
        auto pending = std::move(*pendingCheckpoint_);
        pendingCheckpoint_.reset();
        startCheckpoint(std::move(pending));
        return;
    }
    emit checkpointFinished(outcome);
}

WorkspaceCheckpointOutcome WorkspaceController::performCheckpoint(
    const std::filesystem::path& workspaceDirectory,
    const QString& statePath,
    const WorkspaceCheckpointPayload& payload) {
    WorkspaceCheckpointOutcome outcome{};
    outcome.revision = payload.revision;
    if (payload.hasDocument) {
        skewer::core::FieldPatchStore patchStore(workspaceDirectory);
        const bool patchSaved = payload.patch.empty()
            ? patchStore.remove(payload.patch.stem, outcome.diagnostics)
            : patchStore.save(payload.patch, outcome.diagnostics);
        if (!patchSaved) {
            outcome.result = WorkspaceCheckpointResult::PatchFailed;
            return outcome;
        }
    }
    if (!WorkspaceStateStore::saveFile(
        statePath, payload.state, outcome.errorString)) {
        outcome.result = WorkspaceCheckpointResult::StateFailed;
    }
    return outcome;
}

std::vector<skewer::core::Diagnostic> WorkspaceController::restoreFieldPatch(
    skewer::core::FieldDocument& document) {
    clearActiveFieldState();
    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto path = patchStore_.patchPath(document.assets.stem);
    std::error_code error{};
    if (!std::filesystem::exists(path, error)) return diagnostics;
    auto loaded = patchStore_.load(document.assets.stem);
    diagnostics.insert(diagnostics.end(),
        loaded.diagnostics.begin(), loaded.diagnostics.end());
    if (!loaded.ok()) return diagnostics;
    if (document.readOnly) {
        preservedTriangleEdits_ = loaded.patch->triangleSelectorEdits;
        preservedEctEdits_ = loaded.patch->ectValueEdits;
        diagnostics.push_back({
            skewer::core::DiagnosticSeverity::Warning,
            "The saved patch was retained but not applied because this field is read-only.",
            path });
        return diagnostics;
    }
    auto restored = skewer::core::restoreFieldPatch(document, *loaded.patch);
    preservedTriangleEdits_ = std::move(restored.preservedTriangleEdits);
    preservedEctEdits_ = std::move(restored.preservedEctEdits);
    patchConflicts_ = std::move(restored.conflicts);
    diagnostics.insert(diagnostics.end(),
        restored.diagnostics.begin(), restored.diagnostics.end());
    for (const auto& conflict : patchConflicts_) {
        diagnostics.push_back({
            skewer::core::DiagnosticSeverity::Warning,
            "Patch conflict: " + conflict.message +
                " Current value: " + std::to_string(conflict.current),
            path });
    }
    return diagnostics;
}

WorkspaceRebaseResult WorkspaceController::rebaseConflicts(
    skewer::core::FieldDocument& document) {
    WorkspaceRebaseResult output{};
    std::vector<skewer::core::PatchConflict> unresolved{};
    for (const auto& conflict : patchConflicts_) {
        if (conflict.state == skewer::core::PatchEntryState::Unresolved) {
            unresolved.push_back(conflict);
            continue;
        }
        if (conflict.triangle.has_value()) {
            const std::array<skewer::core::TriangleKey, 1> keys{
                conflict.triangle->key
            };
            const auto result = document.setTriangleSelectors(
                keys, conflict.triangle->selector, "Rebase selector patch");
            output.changed = output.changed || result.changed;
            output.changes.merge(result.changes);
            output.diagnostics.insert(output.diagnostics.end(),
                result.diagnostics.begin(), result.diagnostics.end());
            const auto& key = conflict.triangle->key;
            preservedTriangleEdits_.erase(std::remove_if(
                preservedTriangleEdits_.begin(), preservedTriangleEdits_.end(),
                [&](const auto& edit) {
                    const skewer::core::TriangleKeyLess less{};
                    return !less(edit.key, key) && !less(key, edit.key);
                }), preservedTriangleEdits_.end());
        } else if (conflict.ect.has_value()) {
            const auto result = document.setEctValue(
                conflict.ect->key,
                conflict.ect->value,
                "Rebase ECT patch");
            output.changed = output.changed || result.changed;
            output.changes.merge(result.changes);
            output.diagnostics.insert(output.diagnostics.end(),
                result.diagnostics.begin(), result.diagnostics.end());
            preservedEctEdits_.erase(std::remove_if(
                preservedEctEdits_.begin(), preservedEctEdits_.end(),
                [&](const auto& edit) {
                    return edit.key == conflict.ect->key;
                }), preservedEctEdits_.end());
        }
    }
    patchConflicts_ = std::move(unresolved);
    return output;
}

void WorkspaceController::clearActiveFieldState() {
    preservedTriangleEdits_.clear();
    preservedEctEdits_.clear();
    patchConflicts_.clear();
}

bool WorkspaceController::hasConflicts() const noexcept {
    return !patchConflicts_.empty();
}

bool WorkspaceController::hasPatchContent(
    const skewer::core::FieldDocument* document) const noexcept {
    return (document != nullptr && document->isDirty()) ||
        !preservedTriangleEdits_.empty() || !preservedEctEdits_.empty();
}

std::optional<CurrentFieldPatchSnapshot>
WorkspaceController::currentFieldPatchSnapshot(
    const skewer::core::FieldDocument* document) const {
    if (document == nullptr) return std::nullopt;
    return CurrentFieldPatchSnapshot{
        skewer::core::makeFieldPatch(
            *document, preservedTriangleEdits_, preservedEctEdits_),
        patchConflicts_
    };
}

std::vector<std::string> WorkspaceController::listPatchStems(
    std::vector<skewer::core::Diagnostic>& diagnostics) const {
    return patchStore_.listPatchStems(diagnostics);
}

skewer::core::FieldPatchReadResult WorkspaceController::loadPatch(
    const std::string_view stem) const {
    return patchStore_.load(stem);
}

bool WorkspaceController::archiveOrDiscardPatches(
    const bool discard, QString& errorMessage) {
    const auto patchDirectory = patchStore_.patchesDirectory().lexically_normal();
    const auto workspaceDirectory = std::filesystem::path(
        stateStore_.workspaceDirectory().toStdWString()).lexically_normal();
    if (patchDirectory.parent_path() != workspaceDirectory) {
        errorMessage = QStringLiteral(
            "SKEWER refused to modify a patch directory outside its portable workspace.");
        return false;
    }
    std::error_code error{};
    if (!std::filesystem::exists(patchDirectory, error)) return true;
    if (discard) {
        std::filesystem::remove_all(patchDirectory, error);
    } else {
        const auto archiveRoot = workspaceDirectory / "archive";
        std::filesystem::create_directories(archiveRoot, error);
        if (!error) {
            const auto name = "patches-" +
                QDateTime::currentDateTime()
                    .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
                    .toStdString();
            std::filesystem::rename(patchDirectory, archiveRoot / name, error);
        }
    }
    if (error) {
        errorMessage = QStringLiteral("The saved field patches could not be %1.")
            .arg(discard ? QStringLiteral("discarded") : QStringLiteral("archived"));
        return false;
    }
    clearActiveFieldState();
    return true;
}

} // namespace skewer::qt
