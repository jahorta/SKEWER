#pragma once

#include "WorkspaceStateStore.h"

#include "SkewerCore/FieldPatch.h"

#include <QObject>
#include <QFutureWatcher>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace skewer::qt {

enum class WorkspaceCheckpointResult {
    Success,
    PatchFailed,
    StateFailed,
};

struct WorkspaceCheckpointOutcome {
    std::uint64_t revision = 0U;
    WorkspaceCheckpointResult result = WorkspaceCheckpointResult::Success;
    std::vector<skewer::core::Diagnostic> diagnostics{};
    QString errorString{};

    [[nodiscard]] bool ok() const noexcept {
        return result == WorkspaceCheckpointResult::Success;
    }
};

struct WorkspaceRebaseResult {
    bool changed = false;
    skewer::core::DocumentChangeSet changes{};
    std::vector<skewer::core::Diagnostic> diagnostics{};
};

struct CurrentFieldPatchSnapshot {
    skewer::core::FieldPatch patch{};
    std::vector<skewer::core::PatchConflict> conflicts{};
};

class WorkspaceController final : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceController(
        QString executableDirectory,
        QObject* parent = nullptr);

    [[nodiscard]] bool isWritable() const noexcept;
    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] const std::optional<WorkspaceState>& startupState() const noexcept;
    void clearStartupState();

    void scheduleCheckpoint();
    void stopCheckpoint();
    void beginCheckpoint(
        const skewer::core::FieldDocument* document,
        const WorkspaceState& state);
    [[nodiscard]] WorkspaceCheckpointOutcome flushCheckpoint(
        const skewer::core::FieldDocument* document,
        const WorkspaceState& state);

    [[nodiscard]] std::vector<skewer::core::Diagnostic> restoreFieldPatch(
        skewer::core::FieldDocument& document);
    [[nodiscard]] WorkspaceRebaseResult rebaseConflicts(
        skewer::core::FieldDocument& document);
    void clearActiveFieldState();
    [[nodiscard]] bool hasConflicts() const noexcept;
    [[nodiscard]] bool hasPatchContent(
        const skewer::core::FieldDocument* document) const noexcept;
    [[nodiscard]] std::optional<CurrentFieldPatchSnapshot>
        currentFieldPatchSnapshot(
            const skewer::core::FieldDocument* document) const;

    [[nodiscard]] std::vector<std::string> listPatchStems(
        std::vector<skewer::core::Diagnostic>& diagnostics) const;
    [[nodiscard]] skewer::core::FieldPatchReadResult loadPatch(
        std::string_view stem) const;
    [[nodiscard]] bool archiveOrDiscardPatches(
        bool discard, QString& errorMessage);

signals:
    void checkpointRequested();
    void checkpointFinished(const WorkspaceCheckpointOutcome& outcome);

private:
    struct WorkspaceCheckpointPayload {
        std::uint64_t revision = 0U;
        bool hasDocument = false;
        skewer::core::FieldPatch patch{};
        WorkspaceState state{};
    };

    [[nodiscard]] WorkspaceCheckpointPayload makeCheckpointPayload(
        const skewer::core::FieldDocument* document,
        const WorkspaceState& state);
    void startCheckpoint(WorkspaceCheckpointPayload payload);
    void onCheckpointFinished();
    [[nodiscard]] static WorkspaceCheckpointOutcome performCheckpoint(
        const std::filesystem::path& workspaceDirectory,
        const QString& statePath,
        const WorkspaceCheckpointPayload& payload);

    WorkspaceStateStore stateStore_;
    std::optional<WorkspaceState> startupState_{};
    QTimer checkpointTimer_{};
    QFutureWatcher<WorkspaceCheckpointOutcome> checkpointWatcher_{};
    QMetaObject::Connection checkpointFinishedConnection_{};
    std::optional<WorkspaceCheckpointPayload> pendingCheckpoint_{};
    skewer::core::FieldPatchStore patchStore_;
    std::vector<skewer::core::TriangleSelectorPatchEdit> preservedTriangleEdits_{};
    std::vector<skewer::core::EctValuePatchEdit> preservedEctEdits_{};
    std::vector<skewer::core::PatchConflict> patchConflicts_{};
    std::uint64_t nextCheckpointRevision_ = 0U;
};

} // namespace skewer::qt
