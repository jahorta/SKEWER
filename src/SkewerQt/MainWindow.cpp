#include "MainWindow.h"

#include "Viewport/ViewportController.h"
#include "Viewport/ViewportWidget.h"
#include "Widgets/DiagnosticsWidget.h"
#include "Widgets/EditReviewDialog.h"
#include "Widgets/EncounterEditorWidget.h"
#include "Widgets/FieldSceneWidget.h"
#include "Widgets/FormationInspectorWidget.h"
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
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
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
        const auto& startupState = *workspace_.startupState();
        const auto& settings = startupState.visualSettings;
        visualSettingsDialog_->setSettings(settings);
        visualSettingsDialog_->setContextOpacityPercent(
            startupState.contextOpacityPercent);
        applyVisualSettings(settings, false);
        viewportController_->setContextOpacity(
            visualSettingsDialog_->contextOpacityPercent());
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
        if (!state.diagnosticsWindowGeometry.isEmpty() &&
            !diagnosticsWindow_->restoreGeometry(
                state.diagnosticsWindowGeometry)) {
            diagnosticsWindow_->resize(760, 520);
        }
    }
    diagnosticsWindow_->hide();

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
    visualSettingsDialog_ = new VisualSettingsDialog(this);
    fieldScene_ = new FieldSceneWidget(this);
    auto* fieldDock = new QDockWidget(
        QStringLiteral("Field and scene layers"), this);
    fieldDock->setObjectName(QStringLiteral("fieldSceneDock"));
    fieldDock->setWidget(fieldScene_);
    fieldDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, fieldDock);

    auto* encounterWorkspace = new QWidget(this);
    auto* encounterLayout = new QVBoxLayout(encounterWorkspace);
    encounterLayout->setContentsMargins(4, 4, 4, 4);

    auto* triangleGroup = new QGroupBox(
        QStringLiteral("Selected Triangles"), encounterWorkspace);
    auto* triangleLayout = new QVBoxLayout(triangleGroup);
    triangleInspector_ = new TriangleInspectorWidget(triangleGroup);
    triangleLayout->addWidget(triangleInspector_);
    encounterLayout->addWidget(triangleGroup);

    auto* tableGroup = new QGroupBox(
        QStringLiteral("Encounter Table"), encounterWorkspace);
    auto* tableLayout = new QVBoxLayout(tableGroup);
    encounterEditor_ = new EncounterEditorWidget(tableGroup);
    tableLayout->addWidget(encounterEditor_);
    encounterLayout->addWidget(tableGroup, 1);

    auto* formationToggle = new QToolButton(encounterWorkspace);
    formationToggle->setText(QStringLiteral("ALX Formation Details"));
    formationToggle->setCheckable(true);
    formationToggle->setChecked(false);
    formationToggle->setArrowType(Qt::RightArrow);
    formationToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    formationInspector_ = new FormationInspectorWidget(encounterWorkspace);
    formationInspector_->setVisible(false);
    formationInspector_->setMaximumHeight(300);
    encounterLayout->addWidget(formationToggle);
    encounterLayout->addWidget(formationInspector_);
    connect(formationToggle, &QToolButton::toggled,
        this, [formationToggle, this](const bool expanded) {
            formationToggle->setArrowType(
                expanded ? Qt::DownArrow : Qt::RightArrow);
            formationInspector_->setVisible(expanded);
        });

    auto* encounterDock = new QDockWidget(
        QStringLiteral("Encounter Workspace"), this);
    encounterDock->setObjectName(QStringLiteral("encounterWorkspaceDock"));
    encounterDock->setWidget(encounterWorkspace);
    encounterDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, encounterDock);

    viewportWidget_ = new ViewportWidget(this);
    viewportController_ = new ViewportController(viewportWidget_, this);
    viewportController_->setContextOpacity(
        visualSettingsDialog_->contextOpacityPercent());
    viewportController_->setVisualSettings(visualSettingsDialog_->settings());
    setCentralWidget(viewportWidget_);
    connect(viewportWidget_, &ViewportWidget::visualSettingsRequested,
        this, &MainWindow::showVisualSettingsDialog);

    diagnosticsWindow_ = new QDialog(this, Qt::Window);
    diagnosticsWindow_->setWindowTitle(QStringLiteral("SKEWER Diagnostics"));
    diagnosticsWindow_->setModal(false);
    diagnosticsWindow_->setMinimumSize(520, 300);
    diagnosticsWindow_->resize(760, 520);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsWindow_);
    diagnosticsWidget_ = new DiagnosticsWidget(diagnosticsWindow_);
    diagnosticsLayout->addWidget(diagnosticsWidget_);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto* openAction = fileMenu->addAction(QStringLiteral("Open Game Data Root..."));
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseGameDataRoot);
    fileMenu->addSeparator();
    auto* exportAction = fileMenu->addAction(
        QStringLiteral("Export Workspace Patches..."));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportPatches);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    undoAction_ = editMenu->addAction(QStringLiteral("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::undoEdit);
    redoAction_ = editMenu->addAction(QStringLiteral("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::redoEdit);
    editMenu->addSeparator();
    reviewChangesAction_ = editMenu->addAction(
        QStringLiteral("Review Current Field Changes..."));
    reviewChangesAction_->setEnabled(false);
    connect(reviewChangesAction_, &QAction::triggered,
        this, &MainWindow::showCurrentFieldChanges);

    auto* dataMenu = menuBar()->addMenu(QStringLiteral("ALX"));
    selectAlxAction_ = dataMenu->addAction(
        QStringLiteral("Select ALX Data Directory..."));
    refreshAlxAction_ = dataMenu->addAction(
        QStringLiteral("Refresh ALX Data"));
    refreshAlxAction_->setEnabled(false);
    clearAlxAction_ = dataMenu->addAction(QStringLiteral("Clear ALX Data"));
    clearAlxAction_->setEnabled(false);
    connect(selectAlxAction_, &QAction::triggered,
        this, &MainWindow::chooseAlxDataRoot);
    connect(refreshAlxAction_, &QAction::triggered,
        this, &MainWindow::refreshAlxData);
    connect(clearAlxAction_, &QAction::triggered,
        this, &MainWindow::clearAlxData);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    fieldDock->toggleViewAction()->setText(QStringLiteral("Field and Scene Layers"));
    encounterDock->toggleViewAction()->setText(QStringLiteral("Encounter Workspace"));
    viewMenu->addAction(fieldDock->toggleViewAction());
    viewMenu->addAction(encounterDock->toggleViewAction());
    diagnosticsAction_ = viewMenu->addAction(QStringLiteral("Diagnostics"));
    diagnosticsAction_->setCheckable(true);
    connect(diagnosticsAction_, &QAction::toggled,
        this, [this](const bool visible) {
            if (!visible) {
                diagnosticsWindow_->hide();
                return;
            }
            if (diagnosticsWindow_->isMinimized()) diagnosticsWindow_->showNormal();
            else diagnosticsWindow_->show();
            diagnosticsWindow_->raise();
            diagnosticsWindow_->activateWindow();
        });
    connect(diagnosticsWindow_, &QDialog::finished,
        this, [this]() { diagnosticsAction_->setChecked(false); });
    viewMenu->addSeparator();
    encounterEdgesAction_ = viewMenu->addAction(
        QStringLiteral("Show triangle edges"));
    encounterEdgesAction_->setCheckable(true);
    encounterEdgesAction_->setChecked(
        visualSettingsDialog_->settings().encounterEdgesEnabled);
    connect(encounterEdgesAction_, &QAction::toggled,
        this, [this](const bool enabled) {
            auto settings = visualSettingsDialog_->settings();
            settings.encounterEdgesEnabled = enabled;
            applyVisualSettings(settings, true);
        });
    traversalBarriersAction_ = viewMenu->addAction(
        QStringLiteral("Highlight traversal barriers"));
    traversalBarriersAction_->setCheckable(true);
    traversalBarriersAction_->setChecked(
        visualSettingsDialog_->settings().traversalBarriersEnabled);
    connect(traversalBarriersAction_, &QAction::toggled,
        this, [this](const bool enabled) {
            auto settings = visualSettingsDialog_->settings();
            settings.traversalBarriersEnabled = enabled;
            applyVisualSettings(settings, true);
        });
    viewMenu->addSeparator();
    auto* frameAction = viewMenu->addAction(QStringLiteral("Frame All"));
    connect(frameAction, &QAction::triggered, this, &MainWindow::frameAll);
    auto* visualSettingsAction = viewMenu->addAction(
        QStringLiteral("Visual Settings..."));
    connect(visualSettingsAction, &QAction::triggered,
        this, &MainWindow::showVisualSettingsDialog);

    connect(visualSettingsDialog_, &VisualSettingsDialog::settingsChanged,
        this, [this]() {
            applyVisualSettings(visualSettingsDialog_->settings(), false);
            viewportController_->setContextOpacity(
                visualSettingsDialog_->contextOpacityPercent());
            scheduleCheckpoint();
        });

    editSummaryButton_ = new QPushButton(this);
    editSummaryButton_->setFlat(true);
    editSummaryButton_->setVisible(false);
    connect(editSummaryButton_, &QPushButton::clicked,
        this, &MainWindow::showCurrentFieldChanges);
    statusBar()->addPermanentWidget(editSummaryButton_);

    diagnosticsSummaryButton_ = new QPushButton(QStringLiteral("Diagnostics"), this);
    diagnosticsSummaryButton_->setFlat(true);
    connect(diagnosticsSummaryButton_, &QPushButton::clicked,
        this, [this]() {
            diagnosticsAction_->setChecked(true);
            if (diagnosticsWindow_->isMinimized()) diagnosticsWindow_->showNormal();
            else diagnosticsWindow_->show();
            diagnosticsWindow_->raise();
            diagnosticsWindow_->activateWindow();
        });
    statusBar()->addPermanentWidget(diagnosticsSummaryButton_);
    updateDiagnosticsButton();

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
    resizeDocks({ encounterDock }, { 520 }, Qt::Horizontal);
    statusBar()->showMessage(QStringLiteral(
        "Open a Dreamcast game-data root to begin."));
}

void MainWindow::connectControllers() {
    connect(&session_, &FieldSessionController::diagnosticsProduced,
        this, &MainWindow::appendFieldDiagnostics);
    connect(&session_, &FieldSessionController::documentCleared,
        this, [this]() {
            viewportController_->setScene(nullptr);
            workspace_.clearActiveFieldState();
            editDiagnostics_.clear();
            ectValidationDiagnostics_.clear();
            alxFieldDiagnostics_.clear();
            refreshEncounterTable(encounterEditor_->currentTableIndex());
            updateInspector();
            updateEditSummary();
            renderDiagnostics();
        });
    connect(&session_, &FieldSessionController::discoveryStarted,
        this, [this]() {
            fieldScene_->setFieldSelectionEnabled(false);
            fieldDiagnostics_.clear();
            alxFieldDiagnostics_.clear();
            renderDiagnostics();
            statusBar()->showMessage(QStringLiteral("Scanning for FIELD..."));
        });
    connect(&session_, &FieldSessionController::discoveryFinished,
        this, &MainWindow::onDiscoveryFinished);
    connect(&session_, &FieldSessionController::fieldLoadStarted,
        this, [this](const QString& stem) {
            fieldScene_->setFieldSelectionEnabled(false);
            fieldDiagnostics_.clear();
            fieldScene_->clearScene();
            fieldScene_->clearGroundMetadata();
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
            refreshAlxAction_->setEnabled(false);
            clearAlxAction_->setEnabled(false);
            alxLoadDiagnostics_.clear();
            renderDiagnostics();
            formationInspector_->showLoading(rootPath);
            refreshEncounterTable(encounterEditor_->currentTableIndex());
            statusBar()->showMessage(alxRefreshRequested_
                ? QStringLiteral("Refreshing ALX enrichment data...")
                : QStringLiteral("Loading ALX enrichment data..."));
        });
    connect(&session_, &FieldSessionController::alxLoadFinished,
        this, &MainWindow::onAlxLoadFinished);
    connect(&session_, &FieldSessionController::semanticChangesApplied,
        this, &MainWindow::handleSemanticChanges);
    connect(&session_, &FieldSessionController::ectEditRejected,
        this, [this](const skewer::core::EctValueKey& key) {
            encounterEditor_->updateEctValue(session_.document(), key);
        });
    connect(&session_, &FieldSessionController::editDiagnosticsChanged,
        this, [this](const auto& diagnostics) {
            if (editDiagnostics_ == diagnostics) return;
            editDiagnostics_ = diagnostics;
            renderDiagnostics();
        });
    connect(&session_, &FieldSessionController::alxValidationChanged,
        this, [this](const auto& diagnostics) {
            alxFieldDiagnostics_ = diagnostics;
            renderDiagnostics();
        });

    connect(viewportController_, &ViewportController::selectionChanged,
        this, [this]() {
            updateInspector();
            scheduleCheckpoint();
        });
    connect(viewportController_, &ViewportController::sceneBatchDoubleClicked,
        this, [this](const qulonglong batchIndex) {
            fieldScene_->revealSceneBatch(
                static_cast<std::size_t>(batchIndex));
        });
    connect(viewportController_, &ViewportController::cameraStateChanged,
        this, &MainWindow::scheduleCheckpoint);
    connect(viewportController_, &ViewportController::loadDiagnosticsChanged,
        this, [this]() {
            viewportDiagnostics_ = viewportController_->loadDiagnostics();
            renderDiagnostics();
        });
    connect(&workspace_, &WorkspaceController::checkpointRequested,
        this, &MainWindow::saveCheckpoint);
    connect(&workspace_, &WorkspaceController::checkpointFinished,
        this, &MainWindow::handleCheckpointOutcome);
    if (!viewportController_->loadDiagnostics().empty()) {
        viewportDiagnostics_ = viewportController_->loadDiagnostics();
        renderDiagnostics();
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
        if (!flushCheckpoint()) return;
        std::vector<skewer::core::Diagnostic> diagnostics{};
        const auto patches = workspace_.listPatchStems(diagnostics);
        appendBoundedDiagnostics(workspaceDiagnostics_, diagnostics, false);
        renderDiagnostics();
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
    if (directory.isEmpty()) return;
    alxRefreshRequested_ = false;
    session_.beginAlxLoad(directory, true);
}

void MainWindow::refreshAlxData() {
    if (session_.alxLoadRunning() || session_.alxDataRoot().isEmpty()) return;
    alxRefreshRequested_ = true;
    if (!session_.beginAlxLoad(session_.alxDataRoot(), true)) {
        alxRefreshRequested_ = false;
    }
}

void MainWindow::clearAlxData() {
    if (session_.alxLoadRunning()) return;
    session_.clearAlxData();
    alxLoadDiagnostics_.clear();
    alxFieldDiagnostics_.clear();
    refreshAlxAction_->setEnabled(false);
    clearAlxAction_->setEnabled(false);
    refreshEncounterTable(encounterEditor_->currentTableIndex());
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
    if (!flushCheckpoint()) return;
    session_.beginFieldLoad(catalogIndex);
}

void MainWindow::onFieldLoadFinished(const bool success) {
    fieldScene_->setFieldSelectionEnabled(true);
    if (!success) {
        statusBar()->showMessage(QStringLiteral("Field import failed."));
        QMessageBox::critical(this, QStringLiteral("Cannot load field"),
            fieldDiagnostics_.empty()
                ? QStringLiteral("The field pair could not be parsed.")
                : QString::fromStdString(fieldDiagnostics_.back().message));
        return;
    }
    applyDocument();
}

void MainWindow::onAlxLoadFinished(const bool success) {
    const bool wasRefresh = alxRefreshRequested_;
    alxRefreshRequested_ = false;
    alxLoadDiagnostics_ = session_.alxLoadDiagnostics();
    selectAlxAction_->setEnabled(true);
    refreshAlxAction_->setEnabled(!session_.alxDataRoot().isEmpty());
    if (success) {
        clearAlxAction_->setEnabled(true);
        alxFieldDiagnostics_.clear();
        requestAlxFieldValidation();
        updateFormationDock();
        scheduleCheckpoint();
        statusBar()->showMessage(wasRefresh
            ? QStringLiteral("Refreshed ALX enrichment data.")
            : QStringLiteral("Loaded ALX enrichment data."), 10000);
    } else {
        clearAlxAction_->setEnabled(
            session_.hasAlxDataset() || !session_.alxDataRoot().isEmpty());
        requestAlxFieldValidation();
        updateFormationDock();
        statusBar()->showMessage(wasRefresh
            ? QStringLiteral(
                "ALX enrichment could not be refreshed; the previous data remains active.")
            : QStringLiteral(
                "ALX enrichment could not be loaded; native editing remains available."),
            10000);
        if (session_.completedAlxLoadWasInteractive()) {
            QMessageBox::warning(this, wasRefresh
                ? QStringLiteral("Cannot refresh ALX data")
                : QStringLiteral("Cannot load ALX data"),
                wasRefresh
                    ? QStringLiteral(
                        "The ALX data could not be refreshed. The previous dataset, "
                        "if any, remains active. Review the diagnostics for details.")
                    : QStringLiteral(
                        "The selected directory was not adopted. The existing ALX dataset, "
                        "if any, remains active. Review the diagnostics for details."));
        }
    }
    refreshEncounterTable(encounterEditor_->currentTableIndex());
    renderDiagnostics();
}

void MainWindow::applyDocument() {
    auto* document = session_.document();
    if (document == nullptr) return;
    if (document->readOnly) {
        appendBoundedDiagnostics(fieldDiagnostics_, { {
            skewer::core::DiagnosticSeverity::Error,
            document->readOnlyReason,
            document->assets.mldPath } }, false);
        renderDiagnostics();
    }
    appendBoundedDiagnostics(workspaceDiagnostics_,
        workspace_.restoreFieldPatch(*document), true);
    renderDiagnostics();
    viewportController_->setScene(&document->scene);
    fieldScene_->clearGroundMetadata();
    fieldScene_->setScene(&document->scene, document->eventGroundPresets);
    restoreDocumentState();
    refreshEncounterTable(
        encounterEditor_->currentTableIndex() < 0
            ? 0 : encounterEditor_->currentTableIndex());
    refreshEctValidation();
    requestAlxFieldValidation();
    updateFormationDock();
    updateInspector();
    fieldScene_->setEncounterBatchModified(
        session_.modifiedSceneBatches());
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
    fieldScene_->clearGroundMetadata();
    if (entryTableIndex < 0) return;
    const auto tblId = session_.groundTblIdForEntry(
        static_cast<std::size_t>(entryTableIndex));
    if (tblId.has_value()) fieldScene_->showGroundTblId(*tblId);
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

void MainWindow::handleSemanticChanges(
    const skewer::core::DocumentChangeSet& changes) {
    auto* document = session_.document();
    if (document == nullptr) return;
    const bool clearedEditDiagnostics = !editDiagnostics_.empty();
    editDiagnostics_.clear();
    if (!changes.triangleSelectorKeys.empty()) {
        viewportController_->updateTriangleSelectors(
            changes.triangleSelectorKeys);
        updateInspector();
        fieldScene_->setEncounterBatchModified(
            session_.modifiedSceneBatches());
    }
    bool validateAlx = false;
    bool updateSelectedFormation = false;
    for (const auto& key : changes.ectValueKeys) {
        encounterEditor_->updateEctValue(document, key);
        if (key.kind == skewer::core::EctValueKind::EncounterId) {
            encounterEditor_->updateFormation(
                static_cast<int>(key.tableIndex),
                static_cast<int>(key.rowIndex),
                session_.resolveFormation(
                    static_cast<int>(key.tableIndex),
                    static_cast<int>(key.rowIndex)),
                session_.hasAlxDataset());
            updateSelectedFormation = updateSelectedFormation ||
                encounterEditor_->currentTableIndex() ==
                    static_cast<int>(key.tableIndex) &&
                encounterEditor_->currentRowIndex() ==
                    static_cast<int>(key.rowIndex);
        }
        validateAlx = validateAlx ||
            key.kind == skewer::core::EctValueKind::EncounterId ||
            key.kind == skewer::core::EctValueKind::Weight;
    }
    if (!changes.ectValueKeys.empty() ||
        !changes.triangleSelectorKeys.empty()) refreshEctValidation();
    else if (clearedEditDiagnostics) renderDiagnostics();
    if (validateAlx) requestAlxFieldValidation();
    if (updateSelectedFormation) updateFormationDock();
    updateEditingState();
    scheduleCheckpoint();
}

void MainWindow::updateEditingState() {
    auto* document = session_.document();
    const bool writable = document != nullptr &&
        !document->readOnly && workspace_.isWritable();
    encounterEditor_->setWritable(writable);
    undoAction_->setEnabled(writable && document != nullptr && document->canUndo());
    redoAction_->setEnabled(writable && document != nullptr && document->canRedo());
    fieldScene_->setRebaseState(workspace_.hasConflicts(), writable);
    if (document != nullptr) {
        setWindowTitle(QStringLiteral("SKEWER - %1%2")
            .arg(QString::fromStdString(document->assets.stem))
            .arg(workspace_.hasPatchContent(document)
                ? QStringLiteral(" *") : QString{}));
    }
    updateEditSummary();
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
    appendBoundedDiagnostics(workspaceDiagnostics_, result.diagnostics, true);
    renderDiagnostics();
    if (result.changed) handleSemanticChanges(result.changes);
}

bool MainWindow::exportPatches() {
    if (!flushCheckpoint()) return false;
    exportDiagnostics_.clear();
    renderDiagnostics();
    const auto& catalog = session_.catalog();
    if (!catalog.fieldDirectory.has_value()) {
        QMessageBox::information(this, QStringLiteral("Nothing to export"),
            QStringLiteral("Open a FIELD workspace first."));
        return false;
    }

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto stems = workspace_.listPatchStems(diagnostics);
    appendBoundedDiagnostics(exportDiagnostics_, diagnostics, false);
    renderDiagnostics();
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
        appendBoundedDiagnostics(exportDiagnostics_, loaded.diagnostics, false);
        if (loaded.ok()) patches.push_back(std::move(*loaded.patch));
    }
    renderDiagnostics();
    if (patches.empty()) return false;

    statusBar()->showMessage(
        QStringLiteral("Validating selected patches..."));
    const auto preflight = skewer::core::ExportService::preflight(
        *catalog.fieldDirectory, patches);
    appendBoundedDiagnostics(exportDiagnostics_, preflight.diagnostics, false);
    renderDiagnostics();
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
    appendBoundedDiagnostics(exportDiagnostics_, published.diagnostics, false);
    renderDiagnostics();
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
    refreshEncounterTable(tableIndex);
    updateEditingState();
    updateFormationDock();
    scheduleCheckpoint();
}

void MainWindow::refreshEncounterTable(const int tableIndex) {
    std::vector<std::optional<skewer::core::FormationResolution>> formations{};
    if (session_.hasAlxDataset() && session_.pendingAlxRoot().isEmpty()) {
        constexpr int kEncounterRows = 32;
        formations.reserve(kEncounterRows);
        for (int row = 0; row < kEncounterRows; ++row) {
            formations.push_back(session_.resolveFormation(tableIndex, row));
        }
    }
    encounterEditor_->showTable(session_.document(), tableIndex, formations);
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

void MainWindow::refreshEctValidation() {
    ectValidationDiagnostics_ = session_.document() == nullptr
        ? std::vector<skewer::core::Diagnostic>{}
        : session_.document()->validateWorkingEct();
    renderDiagnostics();
}

void MainWindow::requestAlxFieldValidation() {
    session_.requestActiveFieldAlxValidation();
}

void MainWindow::frameAll() {
    const auto* document = session_.document();
    if (document == nullptr) return;
    viewportController_->frameAll(document->scene.bounds);
    scheduleCheckpoint();
}

void MainWindow::showVisualSettingsDialog() {
    visualSettingsDialog_->show();
    visualSettingsDialog_->raise();
    visualSettingsDialog_->activateWindow();
}

void MainWindow::applyVisualSettings(
    const VisualSettings& settings,
    const bool checkpoint) {
    const auto clamped = clampedVisualSettings(settings);
    visualSettingsDialog_->setSettings(clamped);
    viewportController_->setVisualSettings(clamped);
    if (encounterEdgesAction_ != nullptr) {
        const QSignalBlocker blocker(encounterEdgesAction_);
        encounterEdgesAction_->setChecked(clamped.encounterEdgesEnabled);
    }
    if (traversalBarriersAction_ != nullptr) {
        const QSignalBlocker blocker(traversalBarriersAction_);
        traversalBarriersAction_->setChecked(clamped.traversalBarriersEnabled);
    }
    if (checkpoint) scheduleCheckpoint();
}

void MainWindow::showCurrentFieldChanges() {
    const auto snapshot = workspace_.currentFieldPatchSnapshot(session_.document());
    if (!snapshot.has_value() || snapshot->patch.empty() ||
        session_.document() == nullptr) return;
    EditReviewDialog dialog(*snapshot, *session_.document(), this);
    dialog.exec();
}

void MainWindow::updateEditSummary() {
    const auto snapshot = workspace_.currentFieldPatchSnapshot(session_.document());
    const bool hasDocument = snapshot.has_value();
    editSummaryButton_->setVisible(hasDocument);
    if (!hasDocument) {
        reviewChangesAction_->setEnabled(false);
        return;
    }
    const auto triangleCount = snapshot->patch.triangleSelectorEdits.size();
    const auto ectCount = snapshot->patch.ectValueEdits.size();
    const bool hasChanges = triangleCount > 0U || ectCount > 0U;
    QString text = hasChanges
        ? QStringLiteral("%1 triangle edit(s) · %2 ECT edit(s)")
            .arg(triangleCount).arg(ectCount)
        : QStringLiteral("No edits");
    if (!snapshot->conflicts.empty()) {
        text += QStringLiteral(" · %1 conflict(s)").arg(snapshot->conflicts.size());
        editSummaryButton_->setStyleSheet(QStringLiteral(
            "QPushButton { color: #8a5700; font-weight: bold; }"));
    } else {
        editSummaryButton_->setStyleSheet(QString{});
    }
    editSummaryButton_->setText(text);
    editSummaryButton_->setEnabled(hasChanges);
    reviewChangesAction_->setEnabled(hasChanges);
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
            appendBoundedDiagnostics(workspaceDiagnostics_, { {
                skewer::core::DiagnosticSeverity::Warning,
                "The saved event-ground preset is stale; exact visibility was restored as Custom.",
                document->assets.mldPath } }, false);
            renderDiagnostics();
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
    state.contextOpacityPercent = visualSettingsDialog_->contextOpacityPercent();
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
    state.diagnosticsWindowGeometry = diagnosticsWindow_->saveGeometry();
    return state;
}

void MainWindow::saveCheckpoint() {
    if (!workspace_.isWritable()) return;
    workspace_.beginCheckpoint(session_.document(), captureState());
}

bool MainWindow::flushCheckpoint() {
    if (!workspace_.isWritable()) return true;
    const auto outcome = workspace_.flushCheckpoint(
        session_.document(), captureState());
    handleCheckpointOutcome(outcome);
    return outcome.ok();
}

void MainWindow::handleCheckpointOutcome(
    const WorkspaceCheckpointOutcome& outcome) {
    checkpointDiagnostics_ = outcome.diagnostics;
    if (outcome.result == WorkspaceCheckpointResult::StateFailed) {
        checkpointDiagnostics_.push_back({
            skewer::core::DiagnosticSeverity::Error,
            "Workspace checkpoint failed: " + outcome.errorString.toStdString(),
            std::filesystem::path(workspace_.workspaceDirectory().toStdWString()) /
                "workspace.json"
        });
    }
    renderDiagnostics();
    if (outcome.result == WorkspaceCheckpointResult::PatchFailed) {
        statusBar()->showMessage(QStringLiteral(
            "Field patch checkpoint failed; see diagnostics."), 10000);
    } else if (outcome.result == WorkspaceCheckpointResult::StateFailed) {
        statusBar()->showMessage(QStringLiteral(
            "Workspace checkpoint failed: %1")
            .arg(outcome.errorString), 10000);
    }
}

void MainWindow::appendFieldDiagnostics(
    const std::vector<skewer::core::Diagnostic>& diagnostics,
    const bool clearFirst) {
    appendBoundedDiagnostics(fieldDiagnostics_, diagnostics, clearFirst);
    renderDiagnostics();
}

void MainWindow::appendBoundedDiagnostics(
    std::vector<skewer::core::Diagnostic>& target,
    const std::vector<skewer::core::Diagnostic>& diagnostics,
    const bool clearFirst) {
    constexpr std::size_t maximumEntries = 2000U;
    if (clearFirst) target.clear();
    target.insert(target.end(), diagnostics.begin(), diagnostics.end());
    if (target.size() > maximumEntries) {
        target.erase(target.begin(),
            target.begin() + static_cast<std::ptrdiff_t>(target.size() - maximumEntries));
    }
}

void MainWindow::updateDiagnosticsButton() {
    const auto summary = diagnosticsWidget_->summary();
    QString text = QStringLiteral("Diagnostics");
    QString style{};
    if (summary.errors > 0U) {
        text += QStringLiteral(" · %1 error%2")
            .arg(summary.errors)
            .arg(summary.errors == 1U ? QString{} : QStringLiteral("s"));
        style = QStringLiteral(
            "QPushButton { color: #b3261e; font-weight: bold; }");
    } else if (summary.warnings > 0U) {
        text += QStringLiteral(" · %1 warning%2")
            .arg(summary.warnings)
            .arg(summary.warnings == 1U ? QString{} : QStringLiteral("s"));
        style = QStringLiteral(
            "QPushButton { color: #8a5700; font-weight: bold; }");
    } else if (summary.infos > 0U) {
        text += QStringLiteral(" · %1 info")
            .arg(summary.infos);
        style = QStringLiteral("QPushButton { color: #28527a; }");
    }
    diagnosticsSummaryButton_->setText(text);
    diagnosticsSummaryButton_->setStyleSheet(style);
    diagnosticsSummaryButton_->setToolTip(QStringLiteral(
        "Errors: %1 · Warnings: %2 · Information: %3")
        .arg(summary.errors).arg(summary.warnings).arg(summary.infos));
}

void MainWindow::renderDiagnostics() {
    diagnosticsWidget_->setDiagnostics({
        { DiagnosticCategory::FieldImport, QStringLiteral("Field Import"), fieldDiagnostics_ },
        { DiagnosticCategory::EditInput, QStringLiteral("Edit Input"), editDiagnostics_ },
        { DiagnosticCategory::EctValidation,
            QStringLiteral("ECT Validation"), ectValidationDiagnostics_ },
        { DiagnosticCategory::AlxLoad, QStringLiteral("ALX Load"), alxLoadDiagnostics_ },
        { DiagnosticCategory::AlxFieldValidation,
            QStringLiteral("ALX Field Validation"), alxFieldDiagnostics_ },
        { DiagnosticCategory::Viewport, QStringLiteral("Viewport"), viewportDiagnostics_ },
        { DiagnosticCategory::WorkspacePatch,
            QStringLiteral("Workspace / Patch"), workspaceDiagnostics_ },
        { DiagnosticCategory::Checkpoint,
            QStringLiteral("Checkpoint"), checkpointDiagnostics_ },
        { DiagnosticCategory::Export, QStringLiteral("Export"), exportDiagnostics_ },
    });
    updateDiagnosticsButton();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    workspace_.stopCheckpoint();
    if (!flushCheckpoint()) {
        const auto closeAnyway = QMessageBox::question(
            this,
            QStringLiteral("Workspace checkpoint failed"),
            QStringLiteral(
                "The latest workspace changes could not be saved. "
                "Close SKEWER without saving them?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (closeAnyway != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

} // namespace skewer::qt
