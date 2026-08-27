#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
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
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
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
    resourceTree_->setHeaderLabels({ QStringLiteral("GRND / GOBJ instances") });
    inspectorLayout->addWidget(resourceTree_, 1);
    inspectorLayout->addWidget(new QLabel(QStringLiteral("Selected triangle(s)"), inspectorPanel));
    selectorLabel_ = new QLabel(QStringLiteral("No selection"), inspectorPanel);
    selectorLabel_->setWordWrap(true);
    inspectorLayout->addWidget(selectorLabel_);
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
    encounterTable_ = new QTableWidget(32, 3, ectPanel);
    encounterTable_->setHorizontalHeaderLabels(
        { QStringLiteral("Slot"), QStringLiteral("Encounter ID"), QStringLiteral("Weight / rate") });
    encounterTable_->horizontalHeader()->setStretchLastSection(true);
    encounterTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
    connect(expertCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        expertView_->setVisible(checked);
        updateInspector();
        scheduleCheckpoint();
    });

    setWindowTitle(QStringLiteral("SKEWER - Skies of Arcadia Encounter Viewer"));
    resize(1500, 900);
    statusBar()->showMessage(QStringLiteral("Open a Dreamcast game-data root to begin."));
}

void MainWindow::chooseGameDataRoot() {
    const auto initial = currentRoot_.isEmpty() ? QDir::homePath() : currentRoot_;
    const auto directory = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select game-data root containing one FIELD directory"), initial);
    if (!directory.isEmpty()) beginDiscovery(directory);
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
    picker_ = std::make_unique<skewer::core::TrianglePicker>(document_->scene);
    sceneAdapter_.setScene(&document_->scene);
    selection_.clear();
    populateResources();
    syncSceneToQml();
    syncSelectionToQml();
    restoreDocumentState();
    populateEncounterTable(tableList_->currentRow() < 0 ? 0 : tableList_->currentRow());
    statusBar()->showMessage(QStringLiteral("Loaded %1: %2 triangles in %3 render batches.")
        .arg(QString::fromStdString(document_->assets.stem))
        .arg(document_->scene.triangles.size())
        .arg(document_->scene.batches.size()));
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
        return;
    }
    std::optional<std::uint8_t> commonSelector{};
    bool mixed = false;
    QStringList details{};
    for (const auto& key : selection_) {
        const auto* triangle = findTriangle(key);
        if (triangle == nullptr) continue;
        if (!commonSelector.has_value()) commonSelector = triangle->selector;
        else if (*commonSelector != triangle->selector) mixed = true;
        details.push_back(QStringLiteral("%1\n  raw metadata: 0x%2 0x%3 0x%4\n  decoded selector: %5")
            .arg(keyText(key))
            .arg(triangle->rawMetadata[0], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[1], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[2], 4, 16, QLatin1Char('0'))
            .arg(triangle->selector <= 8U ? QString::number(triangle->selector) : QStringLiteral("invalid")));
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
    }
    expertView_->setPlainText(details.join(QStringLiteral("\n\n")));
}

void MainWindow::jumpToSelectedTable() {
    const auto selector = jumpButton_->property("selector").toInt();
    if (selector >= 1 && selector <= 8) tableList_->setCurrentRow(selector - 1);
}

void MainWindow::onTableChanged(const int row) {
    populateEncounterTable(row);
    scheduleCheckpoint();
}

void MainWindow::populateEncounterTable(const int tableIndex) {
    encounterTable_->clearContents();
    if (document_ == nullptr || tableIndex < 0) {
        tableHeader_->setText(QStringLiteral("No ECT loaded"));
        return;
    }
    const auto* flat = std::get_if<spice::ect::EctFlatContent>(&document_->ect.content);
    if (flat == nullptr || static_cast<std::size_t>(tableIndex) >= flat->tables.size()) return;
    const auto& table = flat->tables[static_cast<std::size_t>(tableIndex)];
    tableHeader_->setText(QStringLiteral("Selector %1 | Stage %2 | Overall encounter rate %3")
        .arg(tableIndex + 1).arg(table.stage).arg(table.overallEncounterRate));
    for (int row = 0; row < static_cast<int>(table.encounters.size()); ++row) {
        const auto& encounter = table.encounters[static_cast<std::size_t>(row)];
        encounterTable_->setItem(row, 0, new QTableWidgetItem(QString::number(row)));
        encounterTable_->setItem(row, 1, new QTableWidgetItem(QString::number(encounter.encounterId)));
        encounterTable_->setItem(row, 2, new QTableWidgetItem(QString::number(encounter.encounterRate)));
    }
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
