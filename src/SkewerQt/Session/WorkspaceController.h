#pragma once

#include "WorkspaceStateStore.h"

#include "SkewerCore/FieldPatch.h"

#include <QObject>
#include <QString>
#include <QTimer>

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

struct WorkspaceRebaseResult {
    bool changed = false;
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
    [[nodiscard]] WorkspaceCheckpointResult checkpoint(
        const skewer::core::FieldDocument* document,
        const WorkspaceState& state,
        std::vector<skewer::core::Diagnostic>& diagnostics);

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

private:
    [[nodiscard]] bool checkpointFieldPatch(
        const skewer::core::FieldDocument* document,
        std::vector<skewer::core::Diagnostic>& diagnostics);

    WorkspaceStateStore stateStore_;
    std::optional<WorkspaceState> startupState_{};
    QTimer checkpointTimer_{};
    skewer::core::FieldPatchStore patchStore_;
    std::vector<skewer::core::TriangleSelectorPatchEdit> preservedTriangleEdits_{};
    std::vector<skewer::core::EctValuePatchEdit> preservedEctEdits_{};
    std::vector<skewer::core::PatchConflict> patchConflicts_{};
};

} // namespace skewer::qt
