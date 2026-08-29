#pragma once

#include "Session/FieldSessionController.h"
#include "Session/WorkspaceController.h"

#include "SkewerCore/Diagnostics.h"

#include <QMainWindow>

#include <cstdint>
#include <vector>

class QAction;
class QCloseEvent;

namespace skewer::qt {

class DiagnosticsWidget;
class EncounterEditorWidget;
class FieldSceneWidget;
class FormationInspectorWidget;
class GroundMetadataWidget;
class TriangleInspectorWidget;
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
    void clearAlxData();
    void onDiscoveryFinished(bool success);
    void onFieldChanged(int catalogIndex);
    void onFieldLoadFinished(bool success);
    void onAlxLoadFinished(bool success);
    void onResourceVisibilityChanged(const std::vector<std::uint8_t>& visibility);
    void onGroundEntrySelectionChanged(qint64 entryTableIndex);
    void onRawEventGroundRequested();
    void onEventGroundPresetRequested(const QString& presetId);
    void onContextOpacityChanged(int percent);
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
    void rebaseConflicts();
    bool exportPatches();
    void frameAll();
    void scheduleCheckpoint();
    void saveCheckpoint();

private:
    static constexpr int kDockLayoutVersion = 1;

    void buildUi();
    void connectControllers();
    void applyDocument();
    void refreshAfterSemanticEdit();
    void updateEditingState();
    void updateFormationDock();
    void refreshAlxFieldDiagnostics();
    void restoreDocumentState();
    void renderDiagnostics();
    void appendDiagnostics(
        const std::vector<skewer::core::Diagnostic>& diagnostics,
        bool clearFirst);
    [[nodiscard]] bool archiveOrDiscardWorkspacePatches(bool discard);
    [[nodiscard]] WorkspaceState captureState() const;

    WorkspaceController workspace_;
    FieldSessionController session_;
    std::vector<skewer::core::Diagnostic> generalDiagnostics_{};
    std::vector<skewer::core::Diagnostic> alxLoadDiagnostics_{};
    std::vector<skewer::core::Diagnostic> alxFieldDiagnostics_{};

    ViewportWidget* viewportWidget_ = nullptr;
    ViewportController* viewportController_ = nullptr;
    FieldSceneWidget* fieldScene_ = nullptr;
    TriangleInspectorWidget* triangleInspector_ = nullptr;
    GroundMetadataWidget* groundMetadata_ = nullptr;
    EncounterEditorWidget* encounterEditor_ = nullptr;
    FormationInspectorWidget* formationInspector_ = nullptr;
    DiagnosticsWidget* diagnosticsWidget_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* selectAlxAction_ = nullptr;
    QAction* clearAlxAction_ = nullptr;
};

} // namespace skewer::qt
