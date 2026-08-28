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
class QSlider;
class QTreeWidget;
class QTreeWidgetItem;

namespace skewer::qt {

class FieldSceneWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FieldSceneWidget(QWidget* parent = nullptr);

    [[nodiscard]] std::optional<int> setFields(
        const std::vector<skewer::core::FieldCatalogEntry>& fields,
        const QString& restoreField);
    void clearFields();
    void setFieldSelectionEnabled(bool enabled);

    void setScene(const skewer::core::SceneModel* scene);
    void clearScene();
    void setContextOpacityEnabled(bool enabled);
    void setContextOpacity(int percent);
    [[nodiscard]] int contextOpacity() const;

    void restoreHiddenBatches(const QStringList& hiddenBatches);
    [[nodiscard]] QStringList hiddenBatchIds() const;
    [[nodiscard]] std::vector<std::uint8_t> visibility() const;

    void setEncounterBatchModified(const std::vector<std::uint8_t>& modifiedBatches);
    void setRebaseState(bool visible, bool enabled);

signals:
    void fieldSelectionRequested(int catalogIndex);
    void visibilityChanged(const std::vector<std::uint8_t>& visibility);
    void sceneBatchSelectionChanged(qint64 sceneBatchIndex);
    void contextOpacityChanged(int percent);
    void rebaseRequested();

private:
    void onResourceItemChanged(QTreeWidgetItem* item, int column);
    void onCurrentResourceChanged(QTreeWidgetItem* current);
    void updateOpacityLabel(int percent);

    QComboBox* fieldCombo_ = nullptr;
    QTreeWidget* resourceTree_ = nullptr;
    QSlider* contextOpacitySlider_ = nullptr;
    QLabel* contextOpacityValueLabel_ = nullptr;
    QPushButton* rebaseButton_ = nullptr;
    std::size_t visibilityCount_ = 0U;
    bool updating_ = false;
};

} // namespace skewer::qt
