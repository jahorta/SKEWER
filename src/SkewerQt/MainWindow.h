#pragma once

#include "Session/FieldSessionController.h"
#include "Session/WorkspaceController.h"

#include "SkewerCore/Diagnostics.h"

#include <QMainWindow>

#include <cstdint>
#include <vector>

class QAction;
class QCloseEvent;
class QDialog;
class QPushButton;

namespace skewer::qt {

class DiagnosticsWidget;
class EncounterEditorWidget;
class FieldSceneWidget;
class FormationInspectorWidget;
class TriangleInspectorWidget;
class VisualSettingsDialog;
class ViewportController;
class ViewportWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool viewerReady() const noexcept;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void chooseGameDataRoot();
    void chooseAlxDataRoot();
    void refreshAlxData();
    void clearAlxData();
    void onDiscoveryFinished(bool success);
    void onFieldChanged(int catalogIndex);
    void onFieldLoadFinished(bool success);
    void onAlxLoadFinished(bool success);
    void onResourceVisibilityChanged(const std::vector<std::uint8_t>& visibility);
    void onGroundEntrySelectionChanged(qint64 entryTableIndex);
    void onRawEventGroundRequested();
    void onEventGroundPresetRequested(const QString& presetId);
    void onTableChanged(int tableIndex);
    void updateInspector();
    void jumpToSelectedTable();
    void applySelectedSelector();
    void onEncounterEditRequested(
        const skewer::core::EctValueKey& key,
        const QString& text);
    void onEncounterSelectionChanged(int tableIndex, int rowIndex);
    void undoEdit();
    void redoEdit();
    void showCurrentFieldChanges();
    void rebaseConflicts();
    bool exportPatches();
    void frameAll();
    void scheduleCheckpoint();
    void saveCheckpoint();

private:
    static constexpr int kDockLayoutVersion = 4;

    void buildUi();
    void connectControllers();
    void applyDocument();
    void handleSemanticChanges(const skewer::core::DocumentChangeSet& changes);
    void refreshEncounterTable(int tableIndex);
    void updateEditingState();
    void updateFormationDock();
    void refreshEctValidation();
    void requestAlxFieldValidation();
    void restoreDocumentState();
    void applyVisualSettings(const VisualSettings& settings, bool checkpoint);
    void showVisualSettingsDialog();
    void updateEditSummary();
    void updateDiagnosticsButton();
    void renderDiagnostics();
    void handleCheckpointOutcome(const WorkspaceCheckpointOutcome& outcome);
    [[nodiscard]] bool flushCheckpoint();
    void appendFieldDiagnostics(
        const std::vector<skewer::core::Diagnostic>& diagnostics,
        bool clearFirst);
    void appendBoundedDiagnostics(
        std::vector<skewer::core::Diagnostic>& target,
        const std::vector<skewer::core::Diagnostic>& diagnostics,
        bool clearFirst);
    [[nodiscard]] bool archiveOrDiscardWorkspacePatches(bool discard);
    [[nodiscard]] WorkspaceState captureState() const;

    WorkspaceController workspace_;
    FieldSessionController session_;
    std::vector<skewer::core::Diagnostic> fieldDiagnostics_{};
    std::vector<skewer::core::Diagnostic> editDiagnostics_{};
    std::vector<skewer::core::Diagnostic> ectValidationDiagnostics_{};
    std::vector<skewer::core::Diagnostic> alxLoadDiagnostics_{};
    std::vector<skewer::core::Diagnostic> alxFieldDiagnostics_{};
    std::vector<skewer::core::Diagnostic> viewportDiagnostics_{};
    std::vector<skewer::core::Diagnostic> workspaceDiagnostics_{};
    std::vector<skewer::core::Diagnostic> checkpointDiagnostics_{};
    std::vector<skewer::core::Diagnostic> exportDiagnostics_{};

    ViewportWidget* viewportWidget_ = nullptr;
    ViewportController* viewportController_ = nullptr;
    FieldSceneWidget* fieldScene_ = nullptr;
    TriangleInspectorWidget* triangleInspector_ = nullptr;
    EncounterEditorWidget* encounterEditor_ = nullptr;
    FormationInspectorWidget* formationInspector_ = nullptr;
    DiagnosticsWidget* diagnosticsWidget_ = nullptr;
    QDialog* diagnosticsWindow_ = nullptr;
    QPushButton* editSummaryButton_ = nullptr;
    QPushButton* diagnosticsSummaryButton_ = nullptr;
    VisualSettingsDialog* visualSettingsDialog_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* reviewChangesAction_ = nullptr;
    QAction* diagnosticsAction_ = nullptr;
    QAction* encounterEdgesAction_ = nullptr;
    QAction* traversalBarriersAction_ = nullptr;
    QAction* selectAlxAction_ = nullptr;
    QAction* refreshAlxAction_ = nullptr;
    QAction* clearAlxAction_ = nullptr;
    bool alxRefreshRequested_ = false;
};

} // namespace skewer::qt
