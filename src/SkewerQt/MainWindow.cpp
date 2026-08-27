#include "MainWindow.h"

#include "SkewerCore/ExportService.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlError>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <variant>

namespace skewer::qt {
namespace {

[[nodiscard]] QString severityName(const skewer::core::DiagnosticSeverity severity) {
    switch (severity) {
    case skewer::core::DiagnosticSeverity::Error: return QStringLiteral("ERROR");
    case skewer::core::DiagnosticSeverity::Warning: return QStringLiteral("WARNING");
    case skewer::core::DiagnosticSeverity::Info: return QStringLiteral("INFO");
    }
    return QStringLiteral("INFO");
}

[[nodiscard]] QString keyText(const skewer::core::TriangleKey& key) {
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        return QStringLiteral("GRND 0x%1 triangle %2")
            .arg(grnd->resourceAddress, 8, 16, QLatin1Char('0'))
            .arg(grnd->triangleIndex);
    }
    const auto& gobj = std::get<skewer::core::GobjTriangleKey>(key);
    return QStringLiteral("GOBJ 0x%1 node %2 triangle %3")
        .arg(gobj.resourceAddress, 8, 16, QLatin1Char('0'))
        .arg(gobj.nodeIndex)
        .arg(gobj.triangleIndex);
}

[[nodiscard]] bool sameKey(const skewer::core::TriangleKey& lhs, const skewer::core::TriangleKey& rhs) {
    const skewer::core::TriangleKeyLess less{};
    return !less(lhs, rhs) && !less(rhs, lhs);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      stateStore_(QCoreApplication::applicationDirPath()) {
    patchStore_ = std::make_unique<skewer::core::FieldPatchStore>(
        std::filesystem::path(stateStore_.workspaceDirectory().toStdWString()));
    buildUi();
    checkpointTimer_.setSingleShot(true);
    checkpointTimer_.setInterval(500);
    connect(&checkpointTimer_, &QTimer::timeout, this, &MainWindow::saveCheckpoint);
    connect(&discoveryWatcher_, &QFutureWatcherBase::finished, this, &MainWindow::onDiscoveryFinished);
    connect(&loadWatcher_, &QFutureWatcherBase::finished, this, &MainWindow::onFieldLoadFinished);

    startupState_ = stateStore_.load();
    if (!stateStore_.isWritable()) {
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::warning(this, QStringLiteral("Portable workspace unavailable"),
                QStringLiteral("SKEWER cannot write beside the executable. Resume state is disabled. "
                    "Move the portable application to a writable location.\n\n%1")
                    .arg(stateStore_.errorString()));
        });
    } else if (!startupState_.has_value()) {
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::warning(this, QStringLiteral("Workspace state ignored"),
                QStringLiteral("The saved workspace could not be read and will not be restored.\n\n%1")
                    .arg(stateStore_.errorString()));
        });
    } else if (!startupState_->gameDataRoot.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            beginDiscovery(startupState_->gameDataRoot, startupState_->activeField);
        });
    }
}

MainWindow::~MainWindow() = default;

bool MainWindow::viewerReady() const noexcept {
    return quickView_ != nullptr && quickView_->status() == QQuickWidget::Ready &&
        quickView_->rootObject() != nullptr;
}

void MainWindow::buildUi() {
    auto* openAction = menuBar()->addAction(QStringLiteral("Open Game Data Root..."));
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseGameDataRoot);
    undoAction_ = menuBar()->addAction(QStringLiteral("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::undoEdit);
    redoAction_ = menuBar()->addAction(QStringLiteral("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::redoEdit);
    exportAction_ = menuBar()->addAction(QStringLiteral("Export Workspace Patches..."));
    connect(exportAction_, &QAction::triggered, this, &MainWindow::exportPatches);
    auto* frameAction = menuBar()->addAction(QStringLiteral("Frame All"));
    connect(frameAction, &QAction::triggered, this, &MainWindow::frameAll);
    menuBar()->addSeparator();
    auto* exitAction = menuBar()->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* inspectorPanel = new QWidget(this);
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->addWidget(new QLabel(QStringLiteral("Field"), inspectorPanel));
    fieldCombo_ = new QComboBox(inspectorPanel);
    fieldCombo_->setToolTip(QStringLiteral("Fields are enumerated from ECT files; unavailable pairs are disabled."));
    inspectorLayout->addWidget(fieldCombo_);
    inspectorLayout->addWidget(new QLabel(QStringLiteral("MLD resources"), inspectorPanel));
    resourceTree_ = new QTreeWidget(inspectorPanel);
    resourceTree_->setHeaderLabels({ QStringLiteral("GRND / GOBJ instances"), QStringLiteral("State") });
    inspectorLayout->addWidget(resourceTree_, 1);
    inspectorLayout->addWidget(new QLabel(QStringLiteral("Selected triangle(s)"), inspectorPanel));
    selectorLabel_ = new QLabel(QStringLiteral("No selection"), inspectorPanel);
    selectorLabel_->setWordWrap(true);
    inspectorLayout->addWidget(selectorLabel_);
    auto* selectorRow = new QHBoxLayout();
    selectorEditor_ = new QComboBox(inspectorPanel);
    selectorEditor_->addItem(QStringLiteral("0 - No encounters"), 0);
    for (int selector = 1; selector <= 8; ++selector) {
        selectorEditor_->addItem(QStringLiteral("%1 - Table %1").arg(selector), selector);
    }
    applySelectorButton_ = new QPushButton(QStringLiteral("Apply selector"), inspectorPanel);
    selectorRow->addWidget(selectorEditor_, 1);
    selectorRow->addWidget(applySelectorButton_);
    inspectorLayout->addLayout(selectorRow);
    rebaseButton_ = new QPushButton(QStringLiteral("Review and rebase patch conflicts"), inspectorPanel);
    rebaseButton_->setVisible(false);
    inspectorLayout->addWidget(rebaseButton_);
    jumpButton_ = new QPushButton(QStringLiteral("Jump to encounter table"), inspectorPanel);
    jumpButton_->setEnabled(false);
    inspectorLayout->addWidget(jumpButton_);
    expertCheck_ = new QCheckBox(QStringLiteral("Show full triangle metadata"), inspectorPanel);
    inspectorLayout->addWidget(expertCheck_);
    expertView_ = new QPlainTextEdit(inspectorPanel);
    expertView_->setReadOnly(true);
    expertView_->setMaximumBlockCount(500);
    expertView_->setVisible(false);
    inspectorLayout->addWidget(expertView_, 1);
    auto* inspectorDock = new QDockWidget(QStringLiteral("Field and triangle inspector"), this);
    inspectorDock->setWidget(inspectorPanel);
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, inspectorDock);

    auto* ectPanel = new QWidget(this);
    auto* ectLayout = new QVBoxLayout(ectPanel);
    tableList_ = new QListWidget(ectPanel);
    for (int index = 0; index < 8; ++index) {
        tableList_->addItem(QStringLiteral("Selector %1 - Table %2").arg(index + 1).arg(index + 1));
    }
    tableList_->setCurrentRow(0);
    ectLayout->addWidget(tableList_);
    tableHeader_ = new QLabel(QStringLiteral("No ECT loaded"), ectPanel);
    tableHeader_->setWordWrap(true);
    ectLayout->addWidget(tableHeader_);
    auto* tableValueRow = new QHBoxLayout();
    tableValueRow->addWidget(new QLabel(QStringLiteral("Stage"), ectPanel));
    stageEditor_ = new QSpinBox(ectPanel);
    stageEditor_->setRange(0, 65535);
    tableValueRow->addWidget(stageEditor_);
    tableValueRow->addWidget(new QLabel(QStringLiteral("Overall rate"), ectPanel));
    overallRateEditor_ = new QSpinBox(ectPanel);
    overallRateEditor_->setRange(0, 65535);
    tableValueRow->addWidget(overallRateEditor_);
    ectLayout->addLayout(tableValueRow);
    encounterTable_ = new QTableWidget(32, 3, ectPanel);
    encounterTable_->setHorizontalHeaderLabels(
        { QStringLiteral("Slot"), QStringLiteral("Encounter ID"), QStringLiteral("Weight / rate") });
    encounterTable_->horizontalHeader()->setStretchLastSection(true);
    encounterTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    encounterTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    ectLayout->addWidget(encounterTable_, 1);
    auto* ectDock = new QDockWidget(QStringLiteral("ECT encounter tables"), this);
    ectDock->setWidget(ectPanel);
    ectDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, ectDock);

    diagnosticsView_ = new QPlainTextEdit(this);
    diagnosticsView_->setReadOnly(true);
    diagnosticsView_->setMaximumBlockCount(2000);
    auto* diagnosticsDock = new QDockWidget(QStringLiteral("Import diagnostics"), this);
    diagnosticsDock->setWidget(diagnosticsView_);
    diagnosticsDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock);

    quickView_ = new QQuickWidget(this);
    quickView_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    connect(quickView_, &QQuickWidget::statusChanged, this, [this](const QQuickWidget::Status status) {
        if (status == QQuickWidget::Ready) {
            quickView_->rootObject()->setProperty("backend", QVariant::fromValue<QObject*>(this));
            syncSceneToQml();
            syncSelectionToQml();
        } else if (status == QQuickWidget::Error) {
            diagnosticsView_->appendPlainText(QStringLiteral("ERROR: The Qt Quick 3D viewer failed to load."));
            for (const auto& error : quickView_->errors()) diagnosticsView_->appendPlainText(error.toString());
        }
    });
    quickView_->setSource(QUrl(QStringLiteral("qrc:/qml/Qml/ViewerScene.qml")));
    setCentralWidget(quickView_);

    connect(fieldCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onFieldChanged);
    connect(resourceTree_, &QTreeWidget::itemChanged, this, &MainWindow::onResourceVisibilityChanged);
    connect(tableList_, &QListWidget::currentRowChanged, this, &MainWindow::onTableChanged);
    connect(jumpButton_, &QPushButton::clicked, this, &MainWindow::jumpToSelectedTable);
    connect(applySelectorButton_, &QPushButton::clicked, this, &MainWindow::applySelectedSelector);
    connect(rebaseButton_, &QPushButton::clicked, this, &MainWindow::rebaseConflicts);
    connect(encounterTable_, &QTableWidget::itemChanged, this, &MainWindow::onEncounterItemChanged);
    connect(stageEditor_, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onTableHeaderEdited);
    connect(overallRateEditor_, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onTableHeaderEdited);
    connect(expertCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        expertView_->setVisible(checked);
        updateInspector();
        scheduleCheckpoint();
    });

    setWindowTitle(QStringLiteral("SKEWER - Skies of Arcadia Encounter Editor"));
    resize(1500, 900);
    statusBar()->showMessage(QStringLiteral("Open a Dreamcast game-data root to begin."));
}

void MainWindow::chooseGameDataRoot() {
    const auto initial = currentRoot_.isEmpty() ? QDir::homePath() : currentRoot_;
    const auto directory = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select game-data root containing one FIELD directory"), initial);
    if (directory.isEmpty()) return;
    if (!currentRoot_.isEmpty() && QDir::cleanPath(directory) != QDir::cleanPath(currentRoot_) && patchStore_ != nullptr) {
        saveCheckpoint();
        std::vector<skewer::core::Diagnostic> diagnostics{};
        const auto patches = patchStore_->listPatchStems(diagnostics);
        appendDiagnostics(diagnostics, false);
        if (!patches.empty()) {
            QMessageBox choice(this);
            choice.setWindowTitle(QStringLiteral("Change FIELD workspace"));
            choice.setText(QStringLiteral("The current workspace contains %1 saved field patch(es). Choose how to close it before opening different game data.").arg(patches.size()));
            auto* exportArchive = choice.addButton(QStringLiteral("Export and archive"), QMessageBox::AcceptRole);
            auto* archive = choice.addButton(QStringLiteral("Archive without export"), QMessageBox::ActionRole);
            auto* discard = choice.addButton(QStringLiteral("Discard patches"), QMessageBox::DestructiveRole);
            auto* cancel = choice.addButton(QMessageBox::Cancel);
            choice.exec();
            if (choice.clickedButton() == cancel) return;
            if (choice.clickedButton() == exportArchive && !exportPatches()) return;
            if (choice.clickedButton() == discard) {
                if (!archiveOrDiscardWorkspacePatches(true)) return;
            } else if (choice.clickedButton() == archive || choice.clickedButton() == exportArchive) {
                if (!archiveOrDiscardWorkspacePatches(false)) return;
            }
            document_.reset();
        }
    }
    beginDiscovery(directory);
}

void MainWindow::beginDiscovery(const QString& rootPath, QString restoreField) {
    if (discoveryWatcher_.isRunning() || loadWatcher_.isRunning()) return;
    if (!currentRoot_.isEmpty()) saveCheckpoint();
    currentRoot_ = QDir::cleanPath(rootPath);
    pendingRestoreField_ = std::move(restoreField);
    document_.reset();
    picker_.reset();
    selection_.clear();
    sceneAdapter_.setScene(nullptr);
    syncSceneToQml();
    syncSelectionToQml();
    fieldCombo_->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("Scanning for FIELD..."));
    const auto path = std::filesystem::path(currentRoot_.toStdWString());
    discoveryWatcher_.setFuture(QtConcurrent::run([path]() {
        return skewer::core::FieldDiscovery::discover(path);
    }));
}

void MainWindow::onDiscoveryFinished() {
    catalog_ = discoveryWatcher_.future().takeResult();
    appendDiagnostics(catalog_.diagnostics, true);
    if (!catalog_.ok()) {
        fieldCombo_->clear();
        statusBar()->showMessage(QStringLiteral("FIELD discovery failed."));
        const auto message = catalog_.diagnostics.empty()
            ? QStringLiteral("The selected root could not be imported.")
            : QString::fromStdString(catalog_.diagnostics.front().message);
        QMessageBox::critical(this, QStringLiteral("Cannot open game data"), message);
        return;
    }
    populateFields(catalog_, pendingRestoreField_);
    pendingRestoreField_.clear();
    fieldCombo_->setEnabled(true);
    statusBar()->showMessage(QStringLiteral("FIELD discovered. Select a field to inspect."));
    scheduleCheckpoint();
}

void MainWindow::populateFields(
    const skewer::core::FieldDiscoveryResult& result,
    const QString& restoreField) {
    populating_ = true;
    fieldCombo_->clear();
    auto* model = qobject_cast<QStandardItemModel*>(fieldCombo_->model());
    int restoreIndex = -1;
    int firstAvailable = -1;
    for (std::size_t index = 0; index < result.fields.size(); ++index) {
        const auto& field = result.fields[index];
        auto label = QString::fromStdString(field.stem);
        if (!field.isAvailable()) label += QStringLiteral(" - %1").arg(QString::fromStdString(field.unavailableReason));
        fieldCombo_->addItem(label, static_cast<int>(index));
        if (auto* item = model->item(static_cast<int>(index)); item != nullptr) {
            item->setEnabled(field.isAvailable());
            if (!field.isAvailable()) item->setToolTip(QString::fromStdString(field.unavailableReason));
        }
        if (field.isAvailable() && firstAvailable < 0) firstAvailable = static_cast<int>(index);
        if (QString::compare(QString::fromStdString(field.stem), restoreField, Qt::CaseInsensitive) == 0 &&
            field.isAvailable()) {
            restoreIndex = static_cast<int>(index);
        }
    }
    fieldCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : firstAvailable);
    populating_ = false;
    if (fieldCombo_->currentIndex() >= 0) onFieldChanged(fieldCombo_->currentIndex());
}

void MainWindow::onFieldChanged(const int index) {
    if (populating_ || index < 0 || loadWatcher_.isRunning()) return;
    const auto catalogIndex = fieldCombo_->itemData(index).toInt();
    if (catalogIndex < 0 || static_cast<std::size_t>(catalogIndex) >= catalog_.fields.size()) return;
    const auto assets = catalog_.fields[static_cast<std::size_t>(catalogIndex)].assetPair();
    if (!assets.has_value()) return;
    saveCheckpoint();
    beginLoad(*assets);
}

void MainWindow::beginLoad(const skewer::core::FieldAssetPair& assets) {
    fieldCombo_->setEnabled(false);
    document_.reset();
    picker_.reset();
    selection_.clear();
    sceneAdapter_.setScene(nullptr);
    syncSceneToQml();
    syncSelectionToQml();
    resourceTree_->clear();
    statusBar()->showMessage(QStringLiteral("Loading %1...").arg(QString::fromStdString(assets.stem)));
    loadWatcher_.setFuture(QtConcurrent::run([assets]() {
        return skewer::core::FieldLoader::load(assets);
    }));
}

void MainWindow::onFieldLoadFinished() {
    auto result = loadWatcher_.future().takeResult();
    appendDiagnostics(result.diagnostics, true);
    fieldCombo_->setEnabled(true);
    if (!result.ok()) {
        statusBar()->showMessage(QStringLiteral("Field import failed."));
        QMessageBox::critical(this, QStringLiteral("Cannot load field"),
            result.diagnostics.empty() ? QStringLiteral("The field pair could not be parsed.")
                : QString::fromStdString(result.diagnostics.back().message));
        return;
    }
    applyDocument(std::make_unique<skewer::core::FieldDocument>(std::move(*result.document)));
}

void MainWindow::applyDocument(std::unique_ptr<skewer::core::FieldDocument> document) {
    document_ = std::move(document);
    if (document_->readOnly) {
        diagnosticsView_->appendPlainText(QStringLiteral("ERROR: %1")
            .arg(QString::fromStdString(document_->readOnlyReason)));
    }
    restoreFieldPatch();
    picker_ = std::make_unique<skewer::core::TrianglePicker>(document_->scene);
    sceneAdapter_.setScene(&document_->scene);
    selection_.clear();
    populateResources();
    syncSceneToQml();
    syncSelectionToQml();
    restoreDocumentState();
    populateEncounterTable(tableList_->currentRow() < 0 ? 0 : tableList_->currentRow());
    updateEditingState();
    statusBar()->showMessage(QStringLiteral("Loaded %1: %2 triangles in %3 render batches.%4")
        .arg(QString::fromStdString(document_->assets.stem))
        .arg(document_->scene.triangles.size())
        .arg(document_->scene.batches.size())
        .arg(document_->readOnly ? QStringLiteral(" Read-only due to malformed selector data.") : QString{}));
    scheduleCheckpoint();
}

void MainWindow::populateResources() {
    populating_ = true;
    resourceTree_->clear();
    if (document_ != nullptr) {
        for (std::size_t index = 0; index < document_->scene.batches.size(); ++index) {
            const auto& batch = document_->scene.batches[index];
            auto* item = new QTreeWidgetItem(resourceTree_);
            item->setText(0, QString::fromStdString(batch.label));
            item->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Checked);
        }
    }
    populating_ = false;
}

void MainWindow::onResourceVisibilityChanged(QTreeWidgetItem* item, const int column) {
    if (populating_ || document_ == nullptr || item == nullptr || column != 0) return;
    auto visibility = sceneAdapter_.visibility();
    const auto index = static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong());
    if (index < visibility.size()) visibility[index] = item->checkState(0) == Qt::Checked ? 1U : 0U;
    sceneAdapter_.setVisibility(std::move(visibility));
    syncSceneToQml();
    scheduleCheckpoint();
}

void MainWindow::syncSceneToQml() {
    if (quickView_ == nullptr || quickView_->rootObject() == nullptr) return;
    quickView_->rootObject()->setProperty("sceneMeshes", sceneAdapter_.sceneMeshes());
}

void MainWindow::syncSelectionToQml() {
    sceneAdapter_.setSelection(selection_);
    if (quickView_ != nullptr && quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty("selectionMeshes", sceneAdapter_.selectionMeshes());
    }
    updateInspector();
}

void MainWindow::handleSceneClick(const float nearX, const float nearY, const float nearZ,
    const float farX, const float farY, const float farZ, const int modifiers) {
    if (picker_ == nullptr) return;
    const skewer::core::SceneRay ray{ { nearX, nearY, nearZ },
        { farX - nearX, farY - nearY, farZ - nearZ } };
    const auto hit = picker_->pick(ray, sceneAdapter_.visibility());
    const bool control = (modifiers & static_cast<int>(Qt::ControlModifier)) != 0;
    const bool shift = (modifiers & static_cast<int>(Qt::ShiftModifier)) != 0;
    if (!hit.has_value()) {
        if (!control && !shift) selection_.clear();
    } else if (control) {
        const auto found = selection_.find(hit->key);
        if (found == selection_.end()) selection_.insert(hit->key);
        else selection_.erase(found);
    } else if (shift) {
        selection_.insert(hit->key);
    } else {
        selection_.clear();
        selection_.insert(hit->key);
    }
    syncSelectionToQml();
    scheduleCheckpoint();
}

const skewer::core::SceneTriangle* MainWindow::findTriangle(const skewer::core::TriangleKey& key) const {
    if (document_ == nullptr) return nullptr;
    const auto found = std::find_if(document_->scene.triangles.begin(), document_->scene.triangles.end(),
        [&](const auto& triangle) { return sameKey(triangle.key, key); });
    return found == document_->scene.triangles.end() ? nullptr : &*found;
}

void MainWindow::updateInspector() {
    if (selection_.empty()) {
        selectorLabel_->setText(QStringLiteral("No selection"));
        expertView_->clear();
        jumpButton_->setEnabled(false);
        applySelectorButton_->setEnabled(false);
        return;
    }
    std::optional<std::uint8_t> commonSelector{};
    bool mixed = false;
    QStringList details{};
    for (const auto& key : selection_) {
        const auto* triangle = findTriangle(key);
        if (triangle == nullptr) continue;
        const auto baseline = document_ == nullptr ? std::optional<std::uint8_t>{} : document_->baselineSelector(key);
        if (!commonSelector.has_value()) commonSelector = triangle->selector;
        else if (*commonSelector != triangle->selector) mixed = true;
        details.push_back(QStringLiteral("%1\n  raw metadata: 0x%2 0x%3 0x%4\n  selector: %5%6")
            .arg(keyText(key))
            .arg(triangle->rawMetadata[0], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[1], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[2], 4, 16, QLatin1Char('0'))
            .arg(triangle->selector <= 8U ? QString::number(triangle->selector) : QStringLiteral("invalid"))
            .arg(baseline.has_value() && *baseline != triangle->selector
                ? QStringLiteral(" (baseline %1, modified)").arg(*baseline) : QString{}));
    }
    if (mixed || !commonSelector.has_value()) {
        selectorLabel_->setText(QStringLiteral("%1 triangles selected; encounter selector is mixed.")
            .arg(selection_.size()));
        jumpButton_->setEnabled(false);
    } else {
        selectorLabel_->setText(QStringLiteral("%1 triangle(s); encounter selector %2%3")
            .arg(selection_.size())
            .arg(*commonSelector)
            .arg(*commonSelector == 0U ? QStringLiteral(" (no encounters)") : QString{}));
        jumpButton_->setEnabled(*commonSelector >= 1U && *commonSelector <= 8U);
        jumpButton_->setProperty("selector", static_cast<int>(*commonSelector));
        selectorEditor_->setCurrentIndex(static_cast<int>(*commonSelector));
    }
    applySelectorButton_->setEnabled(document_ != nullptr && !document_->readOnly &&
        stateStore_.isWritable() && !selection_.empty());
    expertView_->setPlainText(details.join(QStringLiteral("\n\n")));
}

void MainWindow::jumpToSelectedTable() {
    const auto selector = jumpButton_->property("selector").toInt();
    if (selector >= 1 && selector <= 8) tableList_->setCurrentRow(selector - 1);
}

void MainWindow::applySelectedSelector() {
    if (document_ == nullptr || selection_.empty()) return;
    std::vector<skewer::core::TriangleKey> keys(selection_.begin(), selection_.end());
    const auto selector = static_cast<std::uint8_t>(selectorEditor_->currentData().toInt());
    const auto result = document_->setTriangleSelectors(keys, selector,
        selection_.size() == 1U ? "Set triangle encounter selector" : "Set triangle encounter selectors");
    appendDiagnostics(result.diagnostics, false);
    if (result.changed) refreshAfterSemanticEdit();
}

void MainWindow::onEncounterItemChanged(QTableWidgetItem* item) {
    if (populating_ || document_ == nullptr || item == nullptr || item->column() == 0) return;
    bool valid = false;
    const auto value = item->text().toUInt(&valid);
    if (!valid || value > 65535U) {
        diagnosticsView_->appendPlainText(QStringLiteral("WARNING: ECT values must be integers from 0 through 65535."));
        populateEncounterTable(tableList_->currentRow());
        return;
    }
    const auto kind = item->column() == 1
        ? skewer::core::EctValueKind::EncounterId : skewer::core::EctValueKind::Weight;
    const skewer::core::EctValueKey key{ kind, static_cast<std::size_t>(tableList_->currentRow()), static_cast<std::size_t>(item->row()) };
    const auto result = document_->setEctValue(key, static_cast<std::uint16_t>(value));
    appendDiagnostics(result.diagnostics, false);
    if (result.changed) refreshAfterSemanticEdit();
}

void MainWindow::onTableHeaderEdited() {
    if (populating_ || document_ == nullptr || tableList_->currentRow() < 0) return;
    const auto table = static_cast<std::size_t>(tableList_->currentRow());
    const auto* editor = qobject_cast<QSpinBox*>(sender());
    if (editor == nullptr) return;
    const auto kind = editor == stageEditor_ ? skewer::core::EctValueKind::Stage
                                             : skewer::core::EctValueKind::OverallEncounterRate;
    const auto result = document_->setEctValue({ kind, table, 0 }, static_cast<std::uint16_t>(editor->value()));
    appendDiagnostics(result.diagnostics, false);
    if (result.changed) refreshAfterSemanticEdit();
}

void MainWindow::undoEdit() {
    if (document_ != nullptr && document_->undo()) refreshAfterSemanticEdit();
}

void MainWindow::redoEdit() {
    if (document_ != nullptr && document_->redo()) refreshAfterSemanticEdit();
}

void MainWindow::refreshAfterSemanticEdit() {
    sceneAdapter_.refreshScene();
    syncSceneToQml();
    syncSelectionToQml();
    populateEncounterTable(tableList_->currentRow());
    updateEditingState();
    scheduleCheckpoint();
}

void MainWindow::updateEditingState() {
    const bool writable = document_ != nullptr && !document_->readOnly && stateStore_.isWritable();
    selectorEditor_->setEnabled(writable);
    applySelectorButton_->setEnabled(writable && !selection_.empty());
    stageEditor_->setEnabled(writable);
    overallRateEditor_->setEnabled(writable);
    encounterTable_->setEditTriggers(writable
        ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        : QAbstractItemView::NoEditTriggers);
    undoAction_->setEnabled(writable && document_->canUndo());
    redoAction_->setEnabled(writable && document_->canRedo());
    rebaseButton_->setVisible(!patchConflicts_.empty());
    rebaseButton_->setEnabled(writable);
    if (document_ != nullptr) {
        for (int row = 0; row < resourceTree_->topLevelItemCount() && static_cast<std::size_t>(row) < document_->scene.batches.size(); ++row) {
            const auto& batch = document_->scene.batches[static_cast<std::size_t>(row)];
            const bool modified = std::any_of(batch.triangleIndices.begin(), batch.triangleIndices.end(), [&](const std::size_t index) {
                return index < document_->scene.triangles.size() && document_->isTriangleModified(document_->scene.triangles[index].key);
            });
            resourceTree_->topLevelItem(row)->setText(1, modified ? QStringLiteral("Modified") : QString{});
        }
        setWindowTitle(QStringLiteral("SKEWER - %1%2")
            .arg(QString::fromStdString(document_->assets.stem))
            .arg(document_->isDirty() || !preservedTriangleEdits_.empty() || !preservedEctEdits_.empty()
                ? QStringLiteral(" *") : QString{}));
    }
}

void MainWindow::restoreFieldPatch() {
    preservedTriangleEdits_.clear();
    preservedEctEdits_.clear();
    patchConflicts_.clear();
    if (document_ == nullptr || patchStore_ == nullptr) return;
    const auto path = patchStore_->patchPath(document_->assets.stem);
    std::error_code error{};
    if (!std::filesystem::exists(path, error)) return;
    auto loaded = patchStore_->load(document_->assets.stem);
    appendDiagnostics(loaded.diagnostics, false);
    if (!loaded.ok()) return;
    if (document_->readOnly) {
        preservedTriangleEdits_ = loaded.patch->triangleSelectorEdits;
        preservedEctEdits_ = loaded.patch->ectValueEdits;
        diagnosticsView_->appendPlainText(QStringLiteral("WARNING: The saved patch was retained but not applied because this field is read-only."));
        return;
    }
    auto restored = skewer::core::restoreFieldPatch(*document_, *loaded.patch);
    preservedTriangleEdits_ = std::move(restored.preservedTriangleEdits);
    preservedEctEdits_ = std::move(restored.preservedEctEdits);
    patchConflicts_ = std::move(restored.conflicts);
    appendDiagnostics(restored.diagnostics, false);
    for (const auto& conflict : patchConflicts_) {
        diagnosticsView_->appendPlainText(QStringLiteral("WARNING: Patch conflict: %1 Current value: %2")
            .arg(QString::fromStdString(conflict.message)).arg(conflict.current));
    }
}

bool MainWindow::checkpointFieldPatch() {
    if (document_ == nullptr || patchStore_ == nullptr || !stateStore_.isWritable()) return true;
    const auto patch = skewer::core::makeFieldPatch(*document_, preservedTriangleEdits_, preservedEctEdits_);
    std::vector<skewer::core::Diagnostic> diagnostics{};
    const bool ok = patch.empty()
        ? patchStore_->remove(document_->assets.stem, diagnostics)
        : patchStore_->save(patch, diagnostics);
    if (!ok) appendDiagnostics(diagnostics, false);
    return ok;
}

void MainWindow::rebaseConflicts() {
    if (document_ == nullptr || patchConflicts_.empty()) return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Rebase patch conflicts"),
        QStringLiteral("Rebase every resolvable conflict against the current source? The saved requested values will then become active edits. Unresolved keys will remain blocked."));
    if (answer != QMessageBox::Yes) return;
    std::vector<skewer::core::PatchConflict> unresolved{};
    for (const auto& conflict : patchConflicts_) {
        if (conflict.state == skewer::core::PatchEntryState::Unresolved) { unresolved.push_back(conflict); continue; }
        if (conflict.triangle.has_value()) {
            const std::array<skewer::core::TriangleKey, 1> keys{ conflict.triangle->key };
            const auto result = document_->setTriangleSelectors(keys, conflict.triangle->selector, "Rebase selector patch");
            appendDiagnostics(result.diagnostics, false);
            const auto& key = conflict.triangle->key;
            preservedTriangleEdits_.erase(std::remove_if(preservedTriangleEdits_.begin(), preservedTriangleEdits_.end(), [&](const auto& edit) {
                const skewer::core::TriangleKeyLess less{}; return !less(edit.key, key) && !less(key, edit.key);
            }), preservedTriangleEdits_.end());
        } else if (conflict.ect.has_value()) {
            const auto result = document_->setEctValue(conflict.ect->key, conflict.ect->value, "Rebase ECT patch");
            appendDiagnostics(result.diagnostics, false);
            preservedEctEdits_.erase(std::remove_if(preservedEctEdits_.begin(), preservedEctEdits_.end(), [&](const auto& edit) {
                return edit.key == conflict.ect->key;
            }), preservedEctEdits_.end());
        }
    }
    patchConflicts_ = std::move(unresolved);
    refreshAfterSemanticEdit();
}

bool MainWindow::exportPatches() {
    saveCheckpoint();
    if (patchStore_ == nullptr || !catalog_.fieldDirectory.has_value()) {
        QMessageBox::information(this, QStringLiteral("Nothing to export"), QStringLiteral("Open a FIELD workspace first."));
        return false;
    }
    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto stems = patchStore_->listPatchStems(diagnostics);
    appendDiagnostics(diagnostics, false);
    if (stems.empty()) {
        QMessageBox::information(this, QStringLiteral("Nothing to export"), QStringLiteral("This workspace has no field patches."));
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Select workspace patches"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Select the fields to export. Every selected patch is preflighted before any output is written."), &dialog));
    auto* list = new QListWidget(&dialog);
    for (const auto& stem : stems) {
        auto* item = new QListWidgetItem(QString::fromStdString(stem), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(list);
    auto* selectAll = new QPushButton(QStringLiteral("Select all workspace patches"), &dialog);
    connect(selectAll, &QPushButton::clicked, list, [list]() {
        for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Checked);
    });
    layout->addWidget(selectAll);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;

    std::vector<skewer::core::FieldPatch> patches{};
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->checkState() != Qt::Checked) continue;
        auto loaded = patchStore_->load(list->item(row)->text().toStdString());
        appendDiagnostics(loaded.diagnostics, false);
        if (loaded.ok()) patches.push_back(std::move(*loaded.patch));
    }
    if (patches.empty()) return false;
    statusBar()->showMessage(QStringLiteral("Validating selected patches..."));
    const auto preflight = skewer::core::ExportService::preflight(*catalog_.fieldDirectory, patches);
    appendDiagnostics(preflight.diagnostics, false);
    if (!preflight.ok()) {
        QMessageBox::critical(this, QStringLiteral("Export preflight failed"),
            QStringLiteral("No files were written. Review the export diagnostics for the exact field and edit errors."));
        return false;
    }
    if (preflight.assets.empty()) {
        QMessageBox::information(this, QStringLiteral("No changed files"),
            QStringLiteral("All selected edits are already present in the current source. No output files are needed."));
        return true;
    }
    const auto destinationText = QFileDialog::getExistingDirectory(this, QStringLiteral("Select export directory"), currentRoot_);
    if (destinationText.isEmpty()) return false;
    const auto destination = std::filesystem::path(destinationText.toStdWString());
    std::error_code pathError{};
    if (std::filesystem::equivalent(destination, *catalog_.fieldDirectory, pathError) && !pathError) {
        QMessageBox::warning(this, QStringLiteral("Choose a different directory"),
            QStringLiteral("The export directory must not be the source FIELD directory."));
        return false;
    }
    QStringList outputs{};
    QStringList overwrites{};
    for (const auto& asset : preflight.assets) {
        const auto path = destination / asset.basename;
        outputs.push_back(QString::fromStdWString(path.wstring()));
        if (std::filesystem::exists(path)) overwrites.push_back(QString::fromStdWString(path.wstring()));
    }
    QString confirmation = QStringLiteral("The validated export will write:\n\n%1").arg(outputs.join(QLatin1Char('\n')));
    if (!overwrites.empty()) confirmation += QStringLiteral("\n\nThe following existing files will be replaced:\n%1").arg(overwrites.join(QLatin1Char('\n')));
    if (QMessageBox::question(this, QStringLiteral("Publish validated export"), confirmation) != QMessageBox::Yes) return false;
    const auto published = skewer::core::ExportService::publish(preflight, destination);
    appendDiagnostics(published.diagnostics, false);
    if (!published.ok()) {
        QMessageBox::critical(this, QStringLiteral("Export publication failed"),
            QStringLiteral("The staged export could not be published. Existing destination files were restored where necessary."));
        return false;
    }
    QMessageBox::information(this, QStringLiteral("Export complete"),
        QStringLiteral("Published %1 changed file(s).\nReceipt: %2")
            .arg(published.publishedFiles.size())
            .arg(QString::fromStdWString(published.receiptPath.wstring())));
    statusBar()->showMessage(QStringLiteral("Export completed successfully."), 10000);
    return true;
}

bool MainWindow::archiveOrDiscardWorkspacePatches(const bool discard) {
    if (patchStore_ == nullptr) return true;
    const auto patchDirectory = patchStore_->patchesDirectory().lexically_normal();
    const auto workspaceDirectory = std::filesystem::path(stateStore_.workspaceDirectory().toStdWString()).lexically_normal();
    if (patchDirectory.parent_path() != workspaceDirectory) {
        QMessageBox::critical(this, QStringLiteral("Workspace safety check failed"),
            QStringLiteral("SKEWER refused to modify a patch directory outside its portable workspace."));
        return false;
    }
    std::error_code error{};
    if (!std::filesystem::exists(patchDirectory, error)) return true;
    if (discard) {
        std::filesystem::remove_all(patchDirectory, error);
        if (!error) statusBar()->showMessage(QStringLiteral("The current workspace patches were discarded."), 10000);
    } else {
        const auto archiveRoot = workspaceDirectory / "archive";
        std::filesystem::create_directories(archiveRoot, error);
        if (!error) {
            const auto name = "patches-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")).toStdString();
            std::filesystem::rename(patchDirectory, archiveRoot / name, error);
        }
        if (!error) statusBar()->showMessage(QStringLiteral("The current workspace patches were archived for audit."), 10000);
    }
    if (error) {
        QMessageBox::critical(this, QStringLiteral("Workspace transition failed"),
            QStringLiteral("The saved field patches could not be %1.").arg(discard ? QStringLiteral("discarded") : QStringLiteral("archived")));
        return false;
    }
    return true;
}

void MainWindow::onTableChanged(const int row) {
    populateEncounterTable(row);
    scheduleCheckpoint();
}

void MainWindow::populateEncounterTable(const int tableIndex) {
    populating_ = true;
    encounterTable_->clearContents();
    if (document_ == nullptr || tableIndex < 0) {
        tableHeader_->setText(QStringLiteral("No ECT loaded"));
        populating_ = false;
        return;
    }
    const auto* flat = std::get_if<spice::ect::EctFlatContent>(&document_->workingEct.content);
    if (flat == nullptr || static_cast<std::size_t>(tableIndex) >= flat->tables.size()) { populating_ = false; return; }
    const auto& table = flat->tables[static_cast<std::size_t>(tableIndex)];
    tableHeader_->setText(QStringLiteral("Selector %1 / Table %1").arg(tableIndex + 1));
    stageEditor_->setValue(table.stage);
    overallRateEditor_->setValue(table.overallEncounterRate);
    stageEditor_->setStyleSheet(document_->isEctValueModified({ skewer::core::EctValueKind::Stage, static_cast<std::size_t>(tableIndex), 0U })
        ? QStringLiteral("QSpinBox { background: #fff4b4; }") : QString{});
    overallRateEditor_->setStyleSheet(document_->isEctValueModified({ skewer::core::EctValueKind::OverallEncounterRate, static_cast<std::size_t>(tableIndex), 0U })
        ? QStringLiteral("QSpinBox { background: #fff4b4; }") : QString{});
    for (int row = 0; row < static_cast<int>(table.encounters.size()); ++row) {
        const auto& encounter = table.encounters[static_cast<std::size_t>(row)];
        auto* slot = new QTableWidgetItem(QString::number(row));
        slot->setFlags(slot->flags() & ~Qt::ItemIsEditable);
        encounterTable_->setItem(row, 0, slot);
        auto* encounterId = new QTableWidgetItem(QString::number(encounter.encounterId));
        auto* weight = new QTableWidgetItem(QString::number(encounter.encounterRate));
        if (document_->isEctValueModified({ skewer::core::EctValueKind::EncounterId, static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) encounterId->setBackground(QColor(255, 244, 180));
        if (document_->isEctValueModified({ skewer::core::EctValueKind::Weight, static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) weight->setBackground(QColor(255, 244, 180));
        encounterTable_->setItem(row, 1, encounterId);
        encounterTable_->setItem(row, 2, weight);
    }
    populating_ = false;
    updateEditingState();
}

void MainWindow::frameAll() {
    if (document_ == nullptr || quickView_ == nullptr || quickView_->rootObject() == nullptr) return;
    auto* root = quickView_->rootObject();
    root->setProperty("orbitCenter", QVector3D(0.0F, 0.0F, 0.0F));
    root->setProperty("orbitDistance", std::max(20.0F, document_->scene.extent * 1.35F));
    root->setProperty("orbitYaw", 0.0F);
    root->setProperty("orbitPitch", -25.0F);
    scheduleCheckpoint();
}

void MainWindow::restoreDocumentState() {
    if (!startupState_.has_value() || document_ == nullptr ||
        QString::compare(startupState_->activeField,
            QString::fromStdString(document_->assets.stem), Qt::CaseInsensitive) != 0) {
        frameAll();
        return;
    }
    populating_ = true;
    tableList_->setCurrentRow(std::clamp(startupState_->encounterTable, 0, 7));
    expertCheck_->setChecked(startupState_->expertMetadata);
    if (quickView_ != nullptr && quickView_->rootObject() != nullptr) {
        auto* root = quickView_->rootObject();
        root->setProperty("orbitCenter", startupState_->orbitCenter);
        root->setProperty("orbitDistance", std::max(20.0F, startupState_->orbitDistance));
        root->setProperty("orbitYaw", startupState_->orbitYaw);
        root->setProperty("orbitPitch", startupState_->orbitPitch);
    }
    auto visibility = sceneAdapter_.visibility();
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        auto* item = resourceTree_->topLevelItem(row);
        if (startupState_->hiddenBatches.contains(item->text(0))) {
            item->setCheckState(0, Qt::Unchecked);
            if (static_cast<std::size_t>(row) < visibility.size()) visibility[static_cast<std::size_t>(row)] = 0U;
        }
    }
    sceneAdapter_.setVisibility(std::move(visibility));
    for (const auto& key : startupState_->selection) {
        if (findTriangle(key) != nullptr) selection_.insert(key);
    }
    populating_ = false;
    syncSceneToQml();
    syncSelectionToQml();
    startupState_.reset();
}

void MainWindow::cameraChanged() {
    scheduleCheckpoint();
}

void MainWindow::scheduleCheckpoint() {
    if (stateStore_.isWritable()) checkpointTimer_.start();
}

WorkspaceState MainWindow::captureState() const {
    WorkspaceState state{};
    state.gameDataRoot = currentRoot_;
    if (catalog_.fieldDirectory.has_value()) state.fieldDirectory = QString::fromStdWString(catalog_.fieldDirectory->wstring());
    if (document_ != nullptr) state.activeField = QString::fromStdString(document_->assets.stem);
    state.encounterTable = std::max(0, tableList_->currentRow());
    state.expertMetadata = expertCheck_->isChecked();
    if (quickView_ != nullptr && quickView_->rootObject() != nullptr) {
        auto* root = quickView_->rootObject();
        state.orbitCenter = root->property("orbitCenter").value<QVector3D>();
        state.orbitDistance = root->property("orbitDistance").toFloat();
        state.orbitYaw = root->property("orbitYaw").toFloat();
        state.orbitPitch = root->property("orbitPitch").toFloat();
    }
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        const auto* item = resourceTree_->topLevelItem(row);
        if (item->checkState(0) != Qt::Checked) state.hiddenBatches.push_back(item->text(0));
    }
    state.selection.assign(selection_.begin(), selection_.end());
    return state;
}

void MainWindow::saveCheckpoint() {
    if (!stateStore_.isWritable()) return;
    if (!checkpointFieldPatch()) {
        statusBar()->showMessage(QStringLiteral("Field patch checkpoint failed; see diagnostics."), 10000);
        return;
    }
    if (!stateStore_.save(captureState())) {
        statusBar()->showMessage(QStringLiteral("Workspace checkpoint failed: %1").arg(stateStore_.errorString()), 10000);
    }
}

void MainWindow::appendDiagnostics(
    const std::vector<skewer::core::Diagnostic>& diagnostics,
    const bool clearFirst) {
    if (clearFirst) diagnosticsView_->clear();
    for (const auto& diagnostic : diagnostics) {
        auto line = QStringLiteral("%1: %2")
            .arg(severityName(diagnostic.severity), QString::fromStdString(diagnostic.message));
        if (!diagnostic.path.empty()) line += QStringLiteral(" [%1]").arg(QString::fromStdWString(diagnostic.path.wstring()));
        diagnosticsView_->appendPlainText(line);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    checkpointTimer_.stop();
    saveCheckpoint();
    QMainWindow::closeEvent(event);
}

} // namespace skewer::qt
