#pragma once

#include "SkewerCore/FieldDiscovery.h"
#include "SkewerCore/SceneModel.h"

#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace skewer::qt {

enum class EventGroundDisplayMode {
    Raw,
    Preset,
    Custom,
};

class FieldSceneWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FieldSceneWidget(QWidget* parent = nullptr);

    [[nodiscard]] std::optional<int> setFields(
        const std::vector<skewer::core::FieldCatalogEntry>& fields,
        const QString& restoreField);
    void clearFields();
    void setFieldSelectionEnabled(bool enabled);

    void setScene(
        const skewer::core::SceneModel* scene,
        const std::vector<skewer::core::EventGroundPreset>& presets = {});
    void clearScene();
    void revealSceneBatch(std::size_t batchIndex);
    void showGroundTblId(std::int32_t tblId);
    void clearGroundMetadata();

    void restoreHiddenBatches(const QStringList& hiddenBatches);
    void setVisibility(const std::vector<std::uint8_t>& visibility);
    [[nodiscard]] QStringList hiddenBatchIds() const;
    [[nodiscard]] std::vector<std::uint8_t> visibility() const;
    void setEventGroundDisplayMode(
        EventGroundDisplayMode mode,
        const QString& presetId = {});
    [[nodiscard]] EventGroundDisplayMode eventGroundDisplayMode() const noexcept;
    [[nodiscard]] QString selectedEventGroundPresetId() const;

    void setEncounterBatchModified(const std::vector<std::uint8_t>& modifiedBatches);
    void setRebaseState(bool visible, bool enabled);

signals:
    void fieldSelectionRequested(int catalogIndex);
    void visibilityChanged(const std::vector<std::uint8_t>& visibility);
    void groundEntrySelectionChanged(qint64 entryTableIndex);
    void rawEventGroundRequested();
    void eventGroundPresetRequested(const QString& presetId);
    void rebaseRequested();

private:
    void onResourceItemChanged(QTreeWidgetItem* item, int column);
    void onCurrentResourceChanged(QTreeWidgetItem* current);
    void updateTreeState();

    QComboBox* fieldCombo_ = nullptr;
    QComboBox* fieldStateCombo_ = nullptr;
    QTreeWidget* resourceTree_ = nullptr;
    QLabel* groundMetadataLabel_ = nullptr;
    QPushButton* rebaseButton_ = nullptr;
    std::size_t visibilityCount_ = 0U;
    std::vector<skewer::core::EventGroundPreset> presets_{};
    EventGroundDisplayMode eventGroundDisplayMode_ = EventGroundDisplayMode::Raw;
    QString selectedEventGroundPresetId_{};
    bool updating_ = false;
};

} // namespace skewer::qt
