#include "MainWindow.h"

#include "Viewport/ViewportController.h"
#include "Viewport/ViewportWidget.h"
#include "Widgets/DiagnosticsWidget.h"
#include "Widgets/EncounterEditorWidget.h"
#include "Widgets/FieldSceneWidget.h"
#include "Widgets/FormationInspectorWidget.h"
#include "Widgets/GroundMetadataWidget.h"
#include "Widgets/TriangleInspectorWidget.h"
#include "Widgets/VisualSettingsDialog.h"

#include "SkewerCore/ExportService.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace skewer::qt {
namespace {

QStringList eventGroundHiddenIds(
    const skewer::core::SceneModel& scene,
    const skewer::core::EventGroundPreset* preset) {
    QStringList result{};
    for (const auto& group : scene.eventGroundGroups) {
        std::optional<std::size_t> visibleOrdinal{};
        if (preset == nullptr) continue;
        const auto assignment = std::find_if(preset->assignments.begin(),
            preset->assignments.end(), [&](const auto& candidate) {
                return candidate.group == group.key;
            });
        if (assignment != preset->assignments.end() &&
            assignment->state.kind == skewer::core::EventGroundStateKind::Variant) {
            visibleOrdinal = assignment->state.variantOrdinal;
        }
        for (const auto& variant : group.variants) {
            if (!visibleOrdinal.has_value() || *visibleOrdinal != variant.ordinal) {
                result.push_back(QStringLiteral("event-ground:%1:%2")
                    .arg(group.key.entryTableIndex).arg(variant.ordinal));
            }
        }
    }
    result.sort();
    return result;
}

QStringList currentEventGroundHiddenIds(const FieldSceneWidget& widget) {
    QStringList result{};
    for (const auto& id : widget.hiddenBatchIds()) {
        if (id.startsWith(QStringLiteral("event-ground:"))) result.push_back(id);
    }
    result.sort();
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      workspace_(QCoreApplication::applicationDirPath()) {
    buildUi();
    connectControllers();

    if (workspace_.startupState().has_value()) {
        const auto& settings = workspace_.startupState()->visualSettings;
        visualSettingsDialog_->setSettings(settings);
        viewportController_->setVisualSettings(settings);
    }

    const auto defaultGeometry = saveGeometry();
    const auto defaultDockState = saveState(kDockLayoutVersion);
    if (workspace_.startupState().has_value()) {
        const auto& state = *workspace_.startupState();
        if (!state.mainWindowGeometry.isEmpty() &&
            !restoreGeometry(state.mainWindowGeometry)) {
            restoreGeometry(defaultGeometry);
        }
        if (!state.mainWindowState.isEmpty() &&
            !restoreState(state.mainWindowState, kDockLayoutVersion)) {
            restoreState(defaultDockState, kDockLayoutVersion);
        }
    }

    if (!workspace_.isWritable()) {
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::warning(this,
                QStringLiteral("Portable workspace unavailable"),
                QStringLiteral(
                    "SKEWER cannot write beside the executable. Resume state is disabled. "
                    "Move the portable application to a writable location.\n\n%1")
                    .arg(workspace_.errorString()));
        });
    } else if (!workspace_.startupState().has_value()) {
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::warning(this,
                QStringLiteral("Workspace state ignored"),
                QStringLiteral(
                    "The saved workspace could not be read and will not be restored.\n\n%1")
                    .arg(workspace_.errorString()));
        });
    } else {
        const auto state = *workspace_.startupState();
        if (!state.gameDataRoot.isEmpty()) {
            QTimer::singleShot(0, this, [this, state]() {
                session_.beginDiscovery(state.gameDataRoot, state.activeField);
            });
        }
        if (!state.alxDataRoot.isEmpty()) {
            session_.restoreRememberedAlxRoot(state.alxDataRoot);
            QTimer::singleShot(0, this, [this, state]() {
                session_.beginAlxLoad(state.alxDataRoot, false);
            });
        }
    }
}

MainWindow::~MainWindow() = default;

bool MainWindow::viewerReady() const noexcept {
    return viewportController_ != nullptr && viewportController_->isReady();
}

void MainWindow::buildUi() {
    auto* openAction = menuBar()->addAction(
        QStringLiteral("Open Game Data Root..."));
    connect(openAction, &QAction::triggered,
        this, &MainWindow::chooseGameDataRoot);

    auto* alxMenu = menuBar()->addMenu(QStringLiteral("ALX"));
    selectAlxAction_ = alxMenu->addAction(
        QStringLiteral("Select ALX Data Directory..."));
    clearAlxAction_ = alxMenu->addAction(QStringLiteral("Clear ALX Data"));
    clearAlxAction_->setEnabled(false);
    connect(selectAlxAction_, &QAction::triggered,
        this, &MainWindow::chooseAlxDataRoot);
    connect(clearAlxAction_, &QAction::triggered,
        this, &MainWindow::clearAlxData);

    undoAction_ = menuBar()->addAction(QStringLiteral("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::undoEdit);
    redoAction_ = menuBar()->addAction(QStringLiteral("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::redoEdit);

    auto* exportAction = menuBar()->addAction(
        QStringLiteral("Export Workspace Patches..."));
    connect(exportAction, &QAction::triggered,
        this, &MainWindow::exportPatches);
    auto* frameAction = menuBar()->addAction(QStringLiteral("Frame All"));
    connect(frameAction, &QAction::triggered, this, &MainWindow::frameAll);
    auto* visualsMenu = menuBar()->addMenu(QStringLiteral("Visuals"));
    auto* visualSettingsAction = visualsMenu->addAction(
        QStringLiteral("Visual Settings..."));
    visualSettingsDialog_ = new VisualSettingsDialog(this);
    connect(visualSettingsAction, &QAction::triggered, this, [this]() {
        visualSettingsDialog_->show();
        visualSettingsDialog_->raise();
        visualSettingsDialog_->activateWindow();
    });
    connect(visualSettingsDialog_, &VisualSettingsDialog::settingsChanged,
        this, [this]() {
            viewportController_->setVisualSettings(visualSettingsDialog_->settings());
            scheduleCheckpoint();
        });
    menuBar()->addSeparator();
    auto* exitAction = menuBar()->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    fieldScene_ = new FieldSceneWidget(this);
    auto* fieldDock = new QDockWidget(
        QStringLiteral("Field and scene layers"), this);
    fieldDock->setObjectName(QStringLiteral("fieldSceneDock"));
    fieldDock->setWidget(fieldScene_);
    fieldDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, fieldDock);

    triangleInspector_ = new TriangleInspectorWidget(this);
    auto* triangleDock = new QDockWidget(
        QStringLiteral("Selected triangles"), this);
    triangleDock->setObjectName(QStringLiteral("triangleInspectorDock"));
    triangleDock->setWidget(triangleInspector_);
    triangleDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, triangleDock);
    splitDockWidget(fieldDock, triangleDock, Qt::Vertical);

    groundMetadata_ = new GroundMetadataWidget(this);
    auto* groundDock = new QDockWidget(
        QStringLiteral("Ground metadata"), this);
    groundDock->setObjectName(QStringLiteral("groundMetadataDock"));
    groundDock->setWidget(groundMetadata_);
    groundDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, groundDock);
    splitDockWidget(triangleDock, groundDock, Qt::Vertical);

    encounterEditor_ = new EncounterEditorWidget(this);
    auto* ectDock = new QDockWidget(
        QStringLiteral("ECT encounter tables"), this);
    ectDock->setObjectName(QStringLiteral("encounterEditorDock"));
    ectDock->setWidget(encounterEditor_);
    ectDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, ectDock);

    formationInspector_ = new FormationInspectorWidget(this);
    auto* formationDock = new QDockWidget(
        QStringLiteral("ALX formation"), this);
    formationDock->setObjectName(QStringLiteral("formationInspectorDock"));
    formationDock->setWidget(formationInspector_);
    formationDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, formationDock);

    diagnosticsWidget_ = new DiagnosticsWidget(this);
    auto* diagnosticsDock = new QDockWidget(
        QStringLiteral("Import diagnostics"), this);
    diagnosticsDock->setObjectName(QStringLiteral("diagnosticsDock"));
    diagnosticsDock->setWidget(diagnosticsWidget_);
    diagnosticsDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock);

    viewportWidget_ = new ViewportWidget(this);
    viewportController_ = new ViewportController(viewportWidget_, this);
    viewportController_->setContextOpacity(fieldScene_->contextOpacity());
    viewportController_->setVisualSettings(visualSettingsDialog_->settings());
    setCentralWidget(viewportWidget_);

    connect(fieldScene_, &FieldSceneWidget::fieldSelectionRequested,
        this, &MainWindow::onFieldChanged);
    connect(fieldScene_, &FieldSceneWidget::visibilityChanged,
        this, &MainWindow::onResourceVisibilityChanged);
    connect(fieldScene_, &FieldSceneWidget::groundEntrySelectionChanged,
        this, &MainWindow::onGroundEntrySelectionChanged);
    connect(fieldScene_, &FieldSceneWidget::rawEventGroundRequested,
        this, &MainWindow::onRawEventGroundRequested);
    connect(fieldScene_, &FieldSceneWidget::eventGroundPresetRequested,
        this, &MainWindow::onEventGroundPresetRequested);
    connect(fieldScene_, &FieldSceneWidget::contextOpacityChanged,
        this, &MainWindow::onContextOpacityChanged);
    connect(fieldScene_, &FieldSceneWidget::rebaseRequested,
        this, &MainWindow::rebaseConflicts);
    connect(encounterEditor_, &EncounterEditorWidget::tableSelectionChanged,
        this, &MainWindow::onTableChanged);
    connect(encounterEditor_, &EncounterEditorWidget::encounterSelectionChanged,
        this, &MainWindow::onEncounterSelectionChanged);
    connect(encounterEditor_, &EncounterEditorWidget::valueEditRequested,
        this, &MainWindow::onEncounterEditRequested);
    connect(triangleInspector_, &TriangleInspectorWidget::jumpRequested,
        this, &MainWindow::jumpToSelectedTable);
    connect(triangleInspector_, &TriangleInspectorWidget::applyRequested,
        this, &MainWindow::applySelectedSelector);
    connect(triangleInspector_, &TriangleInspectorWidget::expertModeChanged,
        this, [this](const bool) {
            updateInspector();
            scheduleCheckpoint();
        });

    setWindowTitle(QStringLiteral(
        "SKEWER - Skies of Arcadia Encounter Editor"));
    resize(1500, 900);
    statusBar()->showMessage(QStringLiteral(
        "Open a Dreamcast game-data root to begin."));
}

void MainWindow::connectControllers() {
    connect(&session_, &FieldSessionController::diagnosticsProduced,
        this, &MainWindow::appendDiagnostics);
    connect(&session_, &FieldSessionController::documentCleared,
        this, [this]() {
            viewportController_->setScene(nullptr);
            workspace_.clearActiveFieldState();
            updateInspector();
        });
    connect(&session_, &FieldSessionController::discoveryStarted,
        this, [this]() {
            fieldScene_->setContextOpacityEnabled(false);
            fieldScene_->setFieldSelectionEnabled(false);
            alxFieldDiagnostics_.clear();
            renderDiagnostics();
            statusBar()->showMessage(QStringLiteral("Scanning for FIELD..."));
        });
    connect(&session_, &FieldSessionController::discoveryFinished,
        this, &MainWindow::onDiscoveryFinished);
    connect(&session_, &FieldSessionController::fieldLoadStarted,
        this, [this](const QString& stem) {
            fieldScene_->setFieldSelectionEnabled(false);
            fieldScene_->setContextOpacityEnabled(false);
            fieldScene_->clearScene();
            groundMetadata_->clear();
            alxFieldDiagnostics_.clear();
            renderDiagnostics();
            updateFormationDock();
            statusBar()->showMessage(
                QStringLiteral("Loading %1...").arg(stem));
        });
    connect(&session_, &FieldSessionController::fieldLoadFinished,
        this, &MainWindow::onFieldLoadFinished);
    connect(&session_, &FieldSessionController::alxLoadStarted,
        this, [this](const QString& rootPath) {
            selectAlxAction_->setEnabled(false);
            clearAlxAction_->setEnabled(false);
            formationInspector_->showLoading(rootPath);
        });
    connect(&session_, &FieldSessionController::alxLoadFinished,
        this, &MainWindow::onAlxLoadFinished);
    connect(&session_, &FieldSessionController::documentChanged,
        this, &MainWindow::refreshAfterSemanticEdit);
    connect(&session_, &FieldSessionController::ectEditRejected,
        this, [this]() {
            encounterEditor_->showTable(
                session_.document(), encounterEditor_->currentTableIndex());
        });

    connect(viewportController_, &ViewportController::selectionChanged,
        this, [this]() {
            updateInspector();
            scheduleCheckpoint();
        });
    connect(viewportController_, &ViewportController::cameraStateChanged,
        this, &MainWindow::scheduleCheckpoint);
    connect(viewportController_, &ViewportController::loadDiagnosticsChanged,
        this, [this]() {
            if (!viewportController_->loadDiagnostics().empty()) {
                appendDiagnostics(viewportController_->loadDiagnostics(), false);
            }
        });
    connect(&workspace_, &WorkspaceController::checkpointRequested,
        this, &MainWindow::saveCheckpoint);
    if (!viewportController_->loadDiagnostics().empty()) {
        appendDiagnostics(viewportController_->loadDiagnostics(), false);
    }
}

void MainWindow::chooseGameDataRoot() {
    const auto initial = session_.currentRoot().isEmpty()
        ? QDir::homePath() : session_.currentRoot();
    const auto directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select game-data root containing one FIELD directory"),
        initial);
    if (directory.isEmpty()) return;

    if (!session_.currentRoot().isEmpty() &&
        QDir::cleanPath(directory) != QDir::cleanPath(session_.currentRoot())) {
        saveCheckpoint();
        std::vector<skewer::core::Diagnostic> diagnostics{};
        const auto patches = workspace_.listPatchStems(diagnostics);
        appendDiagnostics(diagnostics, false);
        if (!patches.empty()) {
            QMessageBox choice(this);
            choice.setWindowTitle(QStringLiteral("Change FIELD workspace"));
            choice.setText(QStringLiteral(
                "The current workspace contains %1 saved field patch(es). "
                "Choose how to close it before opening different game data.")
                .arg(patches.size()));
            auto* exportArchive = choice.addButton(
                QStringLiteral("Export and archive"), QMessageBox::AcceptRole);
            auto* archive = choice.addButton(
                QStringLiteral("Archive without export"), QMessageBox::ActionRole);
            auto* discard = choice.addButton(
                QStringLiteral("Discard patches"), QMessageBox::DestructiveRole);
            auto* cancel = choice.addButton(QMessageBox::Cancel);
            choice.exec();
            if (choice.clickedButton() == cancel) return;
            if (choice.clickedButton() == exportArchive && !exportPatches()) return;
            if (choice.clickedButton() == discard) {
                if (!archiveOrDiscardWorkspacePatches(true)) return;
            } else if (choice.clickedButton() == archive ||
                choice.clickedButton() == exportArchive) {
                if (!archiveOrDiscardWorkspacePatches(false)) return;
            }
        }
    }
    session_.beginDiscovery(directory);
}

void MainWindow::chooseAlxDataRoot() {
    if (session_.alxLoadRunning()) return;
    const auto initial = session_.alxDataRoot().isEmpty()
        ? QDir::homePath() : session_.alxDataRoot();
    const auto directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select ALX 5.0.0 data directory"), initial);
    if (!directory.isEmpty()) session_.beginAlxLoad(directory, true);
}

void MainWindow::clearAlxData() {
    if (session_.alxLoadRunning()) return;
    session_.clearAlxData();
    alxLoadDiagnostics_.clear();
    alxFieldDiagnostics_.clear();
    clearAlxAction_->setEnabled(false);
    updateFormationDock();
    renderDiagnostics();
    scheduleCheckpoint();
    statusBar()->showMessage(
        QStringLiteral("ALX enrichment disabled."), 10000);
}

void MainWindow::onDiscoveryFinished(const bool success) {
    const auto& catalog = session_.catalog();
    if (!success) {
        fieldScene_->clearFields();
        statusBar()->showMessage(QStringLiteral("FIELD discovery failed."));
        const auto message = catalog.diagnostics.empty()
            ? QStringLiteral("The selected root could not be imported.")
            : QString::fromStdString(catalog.diagnostics.front().message);
        QMessageBox::critical(
            this, QStringLiteral("Cannot open game data"), message);
        return;
    }

    const auto selectedCatalogIndex = fieldScene_->setFields(
        catalog.fields, session_.takePendingRestoreField());
    fieldScene_->setFieldSelectionEnabled(true);
    if (selectedCatalogIndex.has_value()) onFieldChanged(*selectedCatalogIndex);
    statusBar()->showMessage(
        QStringLiteral("FIELD discovered. Select a field to inspect."));
    scheduleCheckpoint();
}

void MainWindow::onFieldChanged(const int catalogIndex) {
    const auto& catalog = session_.catalog();
    if (catalogIndex < 0 || session_.fieldLoadRunning()) return;
    if (static_cast<std::size_t>(catalogIndex) >= catalog.fields.size()) return;
    if (!catalog.fields[static_cast<std::size_t>(catalogIndex)]
            .assetPair().has_value()) return;
    saveCheckpoint();
    session_.beginFieldLoad(catalogIndex);
}

void MainWindow::onFieldLoadFinished(const bool success) {
    fieldScene_->setFieldSelectionEnabled(true);
    if (!success) {
        statusBar()->showMessage(QStringLiteral("Field import failed."));
        QMessageBox::critical(this, QStringLiteral("Cannot load field"),
            generalDiagnostics_.empty()
                ? QStringLiteral("The field pair could not be parsed.")
                : QString::fromStdString(generalDiagnostics_.back().message));
        return;
    }
    applyDocument();
}

void MainWindow::onAlxLoadFinished(const bool success) {
    alxLoadDiagnostics_ = session_.alxLoadDiagnostics();
    selectAlxAction_->setEnabled(true);
    if (success) {
        clearAlxAction_->setEnabled(true);
        refreshAlxFieldDiagnostics();
        updateFormationDock();
        scheduleCheckpoint();
        statusBar()->showMessage(
            QStringLiteral("Loaded ALX enrichment data."), 10000);
    } else {
        clearAlxAction_->setEnabled(
            session_.hasAlxDataset() || !session_.alxDataRoot().isEmpty());
        refreshAlxFieldDiagnostics();
        updateFormationDock();
        statusBar()->showMessage(QStringLiteral(
            "ALX enrichment could not be loaded; native editing remains available."),
            10000);
        if (session_.completedAlxLoadWasInteractive()) {
            QMessageBox::warning(this, QStringLiteral("Cannot load ALX data"),
                QStringLiteral(
                    "The selected directory was not adopted. The existing ALX dataset, "
                    "if any, remains active. Review the diagnostics for details."));
        }
    }
    renderDiagnostics();
}

void MainWindow::applyDocument() {
    auto* document = session_.document();
    if (document == nullptr) return;
    if (document->readOnly) {
        appendDiagnostics({ {
            skewer::core::DiagnosticSeverity::Error,
            document->readOnlyReason,
            document->assets.mldPath } }, false);
    }
    appendDiagnostics(workspace_.restoreFieldPatch(*document), false);
    viewportController_->setScene(&document->scene);
    groundMetadata_->clear();
    fieldScene_->setScene(&document->scene, document->eventGroundPresets);
    restoreDocumentState();
    encounterEditor_->showTable(document,
        encounterEditor_->currentTableIndex() < 0
            ? 0 : encounterEditor_->currentTableIndex());
    refreshAlxFieldDiagnostics();
    updateFormationDock();
    updateEditingState();
    statusBar()->showMessage(QStringLiteral(
        "Loaded %1: %2 encounter triangles in %3 batches; "
        "%4 context triangles from %5 entries.%6")
        .arg(QString::fromStdString(document->assets.stem))
        .arg(document->scene.triangles.size())
        .arg(document->scene.batches.size())
        .arg(document->scene.contextTriangleCount())
        .arg(document->scene.contextEntryCount())
        .arg(document->readOnly
            ? QStringLiteral(" Read-only due to malformed selector data.")
            : QString{}));
    scheduleCheckpoint();
}

void MainWindow::onResourceVisibilityChanged(
    const std::vector<std::uint8_t>& visibility) {
    if (session_.document() == nullptr) return;
    viewportController_->setVisibility(visibility);
    scheduleCheckpoint();
}

void MainWindow::onGroundEntrySelectionChanged(const qint64 entryTableIndex) {
    groundMetadata_->clear();
    if (entryTableIndex < 0) return;
    const auto tblId = session_.groundTblIdForEntry(
        static_cast<std::size_t>(entryTableIndex));
    if (tblId.has_value()) groundMetadata_->setTblId(*tblId);
}

void MainWindow::onRawEventGroundRequested() {
    const auto* document = session_.document();
    if (document == nullptr) return;
    auto visibility = fieldScene_->visibility();
    skewer::core::applyRawEventGroundVisibility(document->scene, visibility);
    fieldScene_->setVisibility(visibility);
    fieldScene_->setEventGroundDisplayMode(EventGroundDisplayMode::Raw);
    viewportController_->setVisibility(visibility);
    scheduleCheckpoint();
}

void MainWindow::onEventGroundPresetRequested(const QString& presetId) {
    const auto* document = session_.document();
    if (document == nullptr) return;
    const auto found = std::find_if(document->eventGroundPresets.begin(),
        document->eventGroundPresets.end(), [&presetId](const auto& preset) {
            return QString::fromStdString(preset.id) == presetId;
        });
    if (found == document->eventGroundPresets.end()) return;
    auto visibility = fieldScene_->visibility();
    skewer::core::applyEventGroundPresetVisibility(document->scene, *found, visibility);
    fieldScene_->setVisibility(visibility);
    fieldScene_->setEventGroundDisplayMode(EventGroundDisplayMode::Preset, presetId);
    viewportController_->setVisibility(visibility);
    scheduleCheckpoint();
}

void MainWindow::onContextOpacityChanged(const int percent) {
    viewportController_->setContextOpacity(percent);
    scheduleCheckpoint();
}

void MainWindow::updateInspector() {
    const bool writable = session_.document() != nullptr &&
        !session_.document()->readOnly && workspace_.isWritable();
    triangleInspector_->showSelection(
        session_.document(), viewportController_->selection(), writable);
}

void MainWindow::jumpToSelectedTable() {
    const auto selector = triangleInspector_->jumpSelector();
    if (selector >= 1 && selector <= 8) {
        encounterEditor_->selectTable(selector - 1);
    }
}

void MainWindow::applySelectedSelector() {
    if (session_.document() == nullptr ||
        viewportController_->selection().empty()) return;
    const std::vector<skewer::core::TriangleKey> keys(
        viewportController_->selection().begin(),
        viewportController_->selection().end());
    session_.applyTriangleSelectors(keys,
        static_cast<std::uint8_t>(triangleInspector_->selectorValue()));
}

void MainWindow::onEncounterEditRequested(
    const skewer::core::EctValueKey& key,
    const QString& text) {
    session_.applyEctValue(key, text);
}

void MainWindow::onEncounterSelectionChanged(const int, const int) {
    updateFormationDock();
}

void MainWindow::undoEdit() {
    session_.undo();
}

void MainWindow::redoEdit() {
    session_.redo();
}

void MainWindow::refreshAfterSemanticEdit() {
    auto* document = session_.document();
    if (document == nullptr) return;
    viewportController_->refreshScene();
    updateInspector();
    encounterEditor_->showTable(
        document, encounterEditor_->currentTableIndex());
    refreshAlxFieldDiagnostics();
    updateFormationDock();
    updateEditingState();
    scheduleCheckpoint();
}

void MainWindow::updateEditingState() {
    auto* document = session_.document();
    const bool writable = document != nullptr &&
        !document->readOnly && workspace_.isWritable();
    updateInspector();
    encounterEditor_->setWritable(writable);
    undoAction_->setEnabled(writable && document != nullptr && document->canUndo());
    redoAction_->setEnabled(writable && document != nullptr && document->canRedo());
    fieldScene_->setRebaseState(workspace_.hasConflicts(), writable);
    if (document != nullptr) {
        fieldScene_->setEncounterBatchModified(
            session_.modifiedSceneBatches());
        setWindowTitle(QStringLiteral("SKEWER - %1%2")
            .arg(QString::fromStdString(document->assets.stem))
            .arg(workspace_.hasPatchContent(document)
                ? QStringLiteral(" *") : QString{}));
    }
}

void MainWindow::rebaseConflicts() {
    auto* document = session_.document();
    if (document == nullptr || !workspace_.hasConflicts()) return;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Rebase patch conflicts"),
        QStringLiteral(
            "Rebase every resolvable conflict against the current source? "
            "The saved requested values will then become active edits. "
            "Unresolved keys will remain blocked."));
    if (answer != QMessageBox::Yes) return;
    const auto result = workspace_.rebaseConflicts(*document);
    appendDiagnostics(result.diagnostics, false);
    refreshAfterSemanticEdit();
}

bool MainWindow::exportPatches() {
    saveCheckpoint();
    const auto& catalog = session_.catalog();
    if (!catalog.fieldDirectory.has_value()) {
        QMessageBox::information(this, QStringLiteral("Nothing to export"),
            QStringLiteral("Open a FIELD workspace first."));
        return false;
    }

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto stems = workspace_.listPatchStems(diagnostics);
    appendDiagnostics(diagnostics, false);
    if (stems.empty()) {
        QMessageBox::information(this, QStringLiteral("Nothing to export"),
            QStringLiteral("This workspace has no field patches."));
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Select workspace patches"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral(
        "Select the fields to export. Every selected patch is preflighted "
        "before any output is written."), &dialog));
    auto* list = new QListWidget(&dialog);
    for (const auto& stem : stems) {
        auto* item = new QListWidgetItem(QString::fromStdString(stem), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(list);
    auto* selectAll = new QPushButton(
        QStringLiteral("Select all workspace patches"), &dialog);
    connect(selectAll, &QPushButton::clicked, list, [list]() {
        for (int row = 0; row < list->count(); ++row) {
            list->item(row)->setCheckState(Qt::Checked);
        }
    });
    layout->addWidget(selectAll);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;

    std::vector<skewer::core::FieldPatch> patches{};
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->checkState() != Qt::Checked) continue;
        auto loaded = workspace_.loadPatch(
            list->item(row)->text().toStdString());
        appendDiagnostics(loaded.diagnostics, false);
        if (loaded.ok()) patches.push_back(std::move(*loaded.patch));
    }
    if (patches.empty()) return false;

    statusBar()->showMessage(
        QStringLiteral("Validating selected patches..."));
    const auto preflight = skewer::core::ExportService::preflight(
        *catalog.fieldDirectory, patches);
    appendDiagnostics(preflight.diagnostics, false);
    if (!preflight.ok()) {
        QMessageBox::critical(this,
            QStringLiteral("Export preflight failed"),
            QStringLiteral(
                "No files were written. Review the export diagnostics for "
                "the exact field and edit errors."));
        return false;
    }
    if (preflight.assets.empty()) {
        QMessageBox::information(this, QStringLiteral("No changed files"),
            QStringLiteral(
                "All selected edits are already present in the current source. "
                "No output files are needed."));
        return true;
    }

    const auto destinationText = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select export directory"), session_.currentRoot());
    if (destinationText.isEmpty()) return false;
    const auto destination = std::filesystem::path(
        destinationText.toStdWString());
    std::error_code pathError{};
    if (std::filesystem::equivalent(
        destination, *catalog.fieldDirectory, pathError) && !pathError) {
        QMessageBox::warning(this,
            QStringLiteral("Choose a different directory"),
            QStringLiteral(
                "The export directory must not be the source FIELD directory."));
        return false;
    }

    QStringList outputs{};
    QStringList overwrites{};
    for (const auto& asset : preflight.assets) {
        const auto path = destination / asset.basename;
        outputs.push_back(QString::fromStdWString(path.wstring()));
        if (std::filesystem::exists(path)) {
            overwrites.push_back(QString::fromStdWString(path.wstring()));
        }
    }
    QString confirmation = QStringLiteral(
        "The validated export will write:\n\n%1")
        .arg(outputs.join(QLatin1Char('\n')));
    if (!overwrites.empty()) {
        confirmation += QStringLiteral(
            "\n\nThe following existing files will be replaced:\n%1")
            .arg(overwrites.join(QLatin1Char('\n')));
    }
    if (QMessageBox::question(this,
        QStringLiteral("Publish validated export"), confirmation) !=
        QMessageBox::Yes) return false;

    const auto published = skewer::core::ExportService::publish(
        preflight, destination);
    appendDiagnostics(published.diagnostics, false);
    if (!published.ok()) {
        QMessageBox::critical(this,
            QStringLiteral("Export publication failed"),
            QStringLiteral(
                "The staged export could not be published. Existing destination "
                "files were restored where necessary."));
        return false;
    }
    QMessageBox::information(this, QStringLiteral("Export complete"),
        QStringLiteral("Published %1 changed file(s).\nReceipt: %2")
            .arg(published.publishedFiles.size())
            .arg(QString::fromStdWString(
                published.receiptPath.wstring())));
    statusBar()->showMessage(
        QStringLiteral("Export completed successfully."), 10000);
    return true;
}

bool MainWindow::archiveOrDiscardWorkspacePatches(const bool discard) {
    QString errorMessage{};
    if (!workspace_.archiveOrDiscardPatches(discard, errorMessage)) {
        const auto title = errorMessage.contains(
            QStringLiteral("outside its portable workspace"))
            ? QStringLiteral("Workspace safety check failed")
            : QStringLiteral("Workspace transition failed");
        QMessageBox::critical(this, title, errorMessage);
        return false;
    }
    statusBar()->showMessage(discard
        ? QStringLiteral("The current workspace patches were discarded.")
        : QStringLiteral("The current workspace patches were archived for audit."),
        10000);
    return true;
}

void MainWindow::onTableChanged(const int tableIndex) {
    encounterEditor_->showTable(session_.document(), tableIndex);
    updateEditingState();
    updateFormationDock();
    scheduleCheckpoint();
}

void MainWindow::updateFormationDock() {
    if (session_.alxLoadRunning()) return;
    if (!session_.hasAlxDataset()) {
        formationInspector_->showUnavailable(
            !session_.alxDataRoot().isEmpty());
        return;
    }
    formationInspector_->showLoadedSource(
        session_.alxLocaleName(), session_.alxSourceRoot());
    if (session_.document() == nullptr ||
        encounterEditor_->currentTableIndex() < 0 ||
        encounterEditor_->currentRowIndex() < 0) {
        formationInspector_->showSelectionPrompt();
        return;
    }
    const auto formation = session_.resolveFormation(
        encounterEditor_->currentTableIndex(),
        encounterEditor_->currentRowIndex());
    if (!formation.has_value()) {
        formationInspector_->showInvalidSelection();
        return;
    }
    formationInspector_->showFormation(*formation);
}

void MainWindow::refreshAlxFieldDiagnostics() {
    alxFieldDiagnostics_ = session_.validateActiveFieldAlx();
    renderDiagnostics();
}

void MainWindow::frameAll() {
    const auto* document = session_.document();
    if (document == nullptr) return;
    viewportController_->frameAll(document->scene.bounds);
    scheduleCheckpoint();
}

void MainWindow::restoreDocumentState() {
    const auto* document = session_.document();
    if (!workspace_.startupState().has_value() || document == nullptr ||
        QString::compare(workspace_.startupState()->activeField,
            QString::fromStdString(document->assets.stem),
            Qt::CaseInsensitive) != 0) {
        frameAll();
        return;
    }

    const auto state = *workspace_.startupState();
    encounterEditor_->restoreTable(
        std::clamp(state.encounterTable, 0, 7));
    triangleInspector_->setExpertMode(state.expertMetadata);
    fieldScene_->setContextOpacity(
        std::clamp(state.contextOpacityPercent, 0, 100));
    viewportController_->setContextOpacity(fieldScene_->contextOpacity());
    viewportController_->setCameraState({
        state.orbitCenter,
        std::max(20.0F, state.orbitDistance),
        state.orbitYaw,
        state.orbitPitch });
    fieldScene_->restoreHiddenBatches(state.hiddenBatches);
    const auto restoredVisibility = fieldScene_->visibility();
    const auto restoredEventGroundHidden = currentEventGroundHiddenIds(*fieldScene_);
    bool modeRestored = false;
    if (state.eventGroundMode == QStringLiteral("preset")) {
        const auto found = std::find_if(document->eventGroundPresets.begin(),
            document->eventGroundPresets.end(), [&state](const auto& preset) {
                return QString::fromStdString(preset.id) == state.eventGroundPresetId;
            });
        if (found != document->eventGroundPresets.end()) {
            if (restoredEventGroundHidden == eventGroundHiddenIds(document->scene, &*found)) {
                fieldScene_->setEventGroundDisplayMode(
                    EventGroundDisplayMode::Preset, state.eventGroundPresetId);
                modeRestored = true;
            }
        }
        if (!modeRestored) {
            appendDiagnostics({ {
                skewer::core::DiagnosticSeverity::Warning,
                "The saved event-ground preset is stale; exact visibility was restored as Custom.",
                document->assets.mldPath } }, false);
        }
    } else if (state.eventGroundMode == QStringLiteral("raw")) {
        if (restoredEventGroundHidden.empty()) {
            fieldScene_->setEventGroundDisplayMode(EventGroundDisplayMode::Raw);
            modeRestored = true;
        }
    }
    if (!modeRestored) fieldScene_->setEventGroundDisplayMode(EventGroundDisplayMode::Custom);
    viewportController_->setVisibility(restoredVisibility);
    viewportController_->restoreSelection(state.selection);
    workspace_.clearStartupState();
}

void MainWindow::scheduleCheckpoint() {
    workspace_.scheduleCheckpoint();
}

WorkspaceState MainWindow::captureState() const {
    WorkspaceState state{};
    state.gameDataRoot = session_.currentRoot();
    if (session_.catalog().fieldDirectory.has_value()) {
        state.fieldDirectory = QString::fromStdWString(
            session_.catalog().fieldDirectory->wstring());
    }
    state.alxDataRoot = session_.alxDataRoot();
    if (session_.document() != nullptr) {
        state.activeField = QString::fromStdString(
            session_.document()->assets.stem);
    }
    state.encounterTable = std::max(
        0, encounterEditor_->currentTableIndex());
    state.expertMetadata = triangleInspector_->expertMode();
    state.contextOpacityPercent = fieldScene_->contextOpacity();
    state.visualSettings = visualSettingsDialog_->settings();
    const auto camera = viewportController_->cameraState();
    state.orbitCenter = camera.center;
    state.orbitDistance = camera.distance;
    state.orbitYaw = camera.yaw;
    state.orbitPitch = camera.pitch;
    state.hiddenBatches = fieldScene_->hiddenBatchIds();
    switch (fieldScene_->eventGroundDisplayMode()) {
    case EventGroundDisplayMode::Raw:
        state.eventGroundMode = QStringLiteral("raw");
        break;
    case EventGroundDisplayMode::Preset:
        state.eventGroundMode = QStringLiteral("preset");
        state.eventGroundPresetId = fieldScene_->selectedEventGroundPresetId();
        break;
    case EventGroundDisplayMode::Custom:
        state.eventGroundMode = QStringLiteral("custom");
        break;
    }
    state.selection.assign(
        viewportController_->selection().begin(),
        viewportController_->selection().end());
    state.mainWindowGeometry = saveGeometry();
    state.mainWindowState = saveState(kDockLayoutVersion);
    return state;
}

void MainWindow::saveCheckpoint() {
    if (!workspace_.isWritable()) return;
    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto result = workspace_.checkpoint(
        session_.document(), captureState(), diagnostics);
    appendDiagnostics(diagnostics, false);
    if (result == WorkspaceCheckpointResult::PatchFailed) {
        statusBar()->showMessage(QStringLiteral(
            "Field patch checkpoint failed; see diagnostics."), 10000);
    } else if (result == WorkspaceCheckpointResult::StateFailed) {
        statusBar()->showMessage(QStringLiteral(
            "Workspace checkpoint failed: %1")
            .arg(workspace_.errorString()), 10000);
    }
}

void MainWindow::appendDiagnostics(
    const std::vector<skewer::core::Diagnostic>& diagnostics,
    const bool clearFirst) {
    if (clearFirst) generalDiagnostics_.clear();
    generalDiagnostics_.insert(generalDiagnostics_.end(),
        diagnostics.begin(), diagnostics.end());
    renderDiagnostics();
}

void MainWindow::renderDiagnostics() {
    diagnosticsWidget_->setDiagnostics(
        generalDiagnostics_, alxLoadDiagnostics_, alxFieldDiagnostics_);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    workspace_.stopCheckpoint();
    saveCheckpoint();
    QMainWindow::closeEvent(event);
}

} // namespace skewer::qt
