#pragma once

#include "SceneAdapter.h"
#include "WorkspaceStateStore.h"

#include "SkewerCore/FieldDiscovery.h"
#include "SkewerCore/FieldDocument.h"
#include "SkewerCore/FieldLoader.h"
#include "SkewerCore/TrianglePicker.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <optional>
#include <set>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QQuickWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace skewer::qt {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool viewerReady() const noexcept;

    Q_INVOKABLE void handleSceneClick(float nearX, float nearY, float nearZ,
        float farX, float farY, float farZ, int modifiers);
    Q_INVOKABLE void cameraChanged();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void chooseGameDataRoot();
    void onDiscoveryFinished();
    void onFieldChanged(int index);
    void onFieldLoadFinished();
    void onResourceVisibilityChanged(QTreeWidgetItem* item, int column);
    void onTableChanged(int row);
    void updateInspector();
    void jumpToSelectedTable();
    void frameAll();
    void scheduleCheckpoint();
    void saveCheckpoint();

private:
    void buildUi();
    void beginDiscovery(const QString& rootPath, QString restoreField = {});
    void beginLoad(const skewer::core::FieldAssetPair& assets);
    void applyDocument(std::unique_ptr<skewer::core::FieldDocument> document);
    void populateFields(const skewer::core::FieldDiscoveryResult& result, const QString& restoreField);
    void populateResources();
    void populateEncounterTable(int tableIndex);
    void syncSceneToQml();
    void syncSelectionToQml();
    void restoreDocumentState();
    void appendDiagnostics(const std::vector<skewer::core::Diagnostic>& diagnostics, bool clearFirst);
    [[nodiscard]] WorkspaceState captureState() const;
    [[nodiscard]] const skewer::core::SceneTriangle* findTriangle(
        const skewer::core::TriangleKey& key) const;

    WorkspaceStateStore stateStore_;
    std::optional<WorkspaceState> startupState_{};
    QTimer checkpointTimer_{};
    QFutureWatcher<skewer::core::FieldDiscoveryResult> discoveryWatcher_{};
    QFutureWatcher<skewer::core::FieldLoadResult> loadWatcher_{};
    skewer::core::FieldDiscoveryResult catalog_{};
    QString pendingRestoreField_{};
    QString currentRoot_{};
    std::unique_ptr<skewer::core::FieldDocument> document_{};
    std::unique_ptr<skewer::core::TrianglePicker> picker_{};
    SceneAdapter sceneAdapter_{};
    std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess> selection_{};
    bool populating_ = false;

    QQuickWidget* quickView_ = nullptr;
    QComboBox* fieldCombo_ = nullptr;
    QTreeWidget* resourceTree_ = nullptr;
    QLabel* selectorLabel_ = nullptr;
    QPushButton* jumpButton_ = nullptr;
    QCheckBox* expertCheck_ = nullptr;
    QPlainTextEdit* expertView_ = nullptr;
    QListWidget* tableList_ = nullptr;
    QLabel* tableHeader_ = nullptr;
    QTableWidget* encounterTable_ = nullptr;
    QPlainTextEdit* diagnosticsView_ = nullptr;
};

} // namespace skewer::qt
