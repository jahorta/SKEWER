#include "FieldSceneWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace skewer::qt {
namespace {

constexpr int kVisibilityIndexRole = Qt::UserRole;
constexpr int kVisibilityIdRole = Qt::UserRole + 1;
constexpr int kLegacyVisibilityLabelRole = Qt::UserRole + 2;
constexpr int kSceneBatchIndexRole = Qt::UserRole + 3;

template <typename Function>
void forEachVisibilityLeaf(QTreeWidgetItem* root, Function&& function) {
    if (root == nullptr) return;
    if (root->data(0, kVisibilityIndexRole).isValid()) {
        function(root);
        return;
    }
    for (int index = 0; index < root->childCount(); ++index) {
        forEachVisibilityLeaf(root->child(index), function);
    }
}

template <typename Function>
void forEachVisibilityLeaf(const QTreeWidgetItem* root, Function&& function) {
    if (root == nullptr) return;
    if (root->data(0, kVisibilityIndexRole).isValid()) {
        function(root);
        return;
    }
    for (int index = 0; index < root->childCount(); ++index) {
        forEachVisibilityLeaf(root->child(index), function);
    }
}

void updateGroupCheckState(QTreeWidgetItem* group) {
    if (group == nullptr || group->childCount() == 0) return;
    int checked = 0;
    int partial = 0;
    for (int index = 0; index < group->childCount(); ++index) {
        const auto state = group->child(index)->checkState(0);
        if (state == Qt::Checked) ++checked;
        else if (state == Qt::PartiallyChecked) ++partial;
    }
    if (checked == group->childCount()) group->setCheckState(0, Qt::Checked);
    else if (checked == 0 && partial == 0) group->setCheckState(0, Qt::Unchecked);
    else group->setCheckState(0, Qt::PartiallyChecked);
}

} // namespace

FieldSceneWidget::FieldSceneWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Field"), this));

    fieldCombo_ = new QComboBox(this);
    fieldCombo_->setToolTip(QStringLiteral(
        "Fields are enumerated from ECT files; unavailable pairs are disabled."));
    layout->addWidget(fieldCombo_);

    layout->addWidget(new QLabel(QStringLiteral("MLD resources"), this));
    resourceTree_ = new QTreeWidget(this);
    resourceTree_->setHeaderLabels({ QStringLiteral("Scene layers"), QStringLiteral("State") });
    layout->addWidget(resourceTree_, 1);

    auto* opacityRow = new QHBoxLayout();
    opacityRow->addWidget(new QLabel(QStringLiteral("Field context opacity"), this));
    contextOpacitySlider_ = new QSlider(Qt::Horizontal, this);
    contextOpacitySlider_->setRange(0, 100);
    contextOpacitySlider_->setValue(40);
    contextOpacitySlider_->setToolTip(QStringLiteral(
        "Adjust the opacity of the non-editable field context layer."));
    contextOpacitySlider_->setEnabled(false);
    contextOpacityValueLabel_ = new QLabel(QStringLiteral("40%"), this);
    contextOpacityValueLabel_->setMinimumWidth(36);
    opacityRow->addWidget(contextOpacitySlider_, 1);
    opacityRow->addWidget(contextOpacityValueLabel_);
    layout->addLayout(opacityRow);

    rebaseButton_ = new QPushButton(QStringLiteral("Review and rebase patch conflicts"), this);
    rebaseButton_->setVisible(false);
    layout->addWidget(rebaseButton_);

    connect(fieldCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](const int index) {
            if (updating_ || index < 0) return;
            emit fieldSelectionRequested(fieldCombo_->itemData(index).toInt());
        });
    connect(resourceTree_, &QTreeWidget::itemChanged,
        this, &FieldSceneWidget::onResourceItemChanged);
    connect(resourceTree_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) { onCurrentResourceChanged(current); });
    connect(contextOpacitySlider_, &QSlider::valueChanged, this, [this](const int percent) {
        updateOpacityLabel(percent);
        if (!updating_) emit contextOpacityChanged(percent);
    });
    connect(rebaseButton_, &QPushButton::clicked, this, &FieldSceneWidget::rebaseRequested);
}

std::optional<int> FieldSceneWidget::setFields(
    const std::vector<skewer::core::FieldCatalogEntry>& fields,
    const QString& restoreField) {
    const QSignalBlocker blocker(fieldCombo_);
    updating_ = true;
    fieldCombo_->clear();
    auto* model = qobject_cast<QStandardItemModel*>(fieldCombo_->model());
    int restoreIndex = -1;
    int firstAvailable = -1;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& field = fields[index];
        auto label = QString::fromStdString(field.stem);
        if (!field.isAvailable()) {
            label += QStringLiteral(" - %1").arg(QString::fromStdString(field.unavailableReason));
        }
        fieldCombo_->addItem(label, static_cast<int>(index));
        if (auto* item = model == nullptr ? nullptr : model->item(static_cast<int>(index)); item != nullptr) {
            item->setEnabled(field.isAvailable());
            if (!field.isAvailable()) {
                item->setToolTip(QString::fromStdString(field.unavailableReason));
            }
        }
        if (field.isAvailable() && firstAvailable < 0) firstAvailable = static_cast<int>(index);
        if (field.isAvailable() &&
            QString::compare(QString::fromStdString(field.stem), restoreField, Qt::CaseInsensitive) == 0) {
            restoreIndex = static_cast<int>(index);
        }
    }
    fieldCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : firstAvailable);
    updating_ = false;
    if (fieldCombo_->currentIndex() < 0) return std::nullopt;
    return fieldCombo_->currentData().toInt();
}

void FieldSceneWidget::clearFields() {
    const QSignalBlocker blocker(fieldCombo_);
    fieldCombo_->clear();
}

void FieldSceneWidget::setFieldSelectionEnabled(const bool enabled) {
    fieldCombo_->setEnabled(enabled);
}

void FieldSceneWidget::setScene(const skewer::core::SceneModel* scene) {
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    resourceTree_->clear();
    visibilityCount_ = scene == nullptr ? 0U : scene->batches.size() + scene->contextBatches.size();
    contextOpacitySlider_->setEnabled(scene != nullptr && !scene->contextBatches.empty());
    if (scene != nullptr) {
        auto* encounterGroup = new QTreeWidgetItem(resourceTree_);
        encounterGroup->setText(0, QStringLiteral("Encounter Surfaces"));
        encounterGroup->setText(1, QStringLiteral("%1 triangles").arg(scene->triangles.size()));
        encounterGroup->setFlags(encounterGroup->flags() | Qt::ItemIsUserCheckable);
        encounterGroup->setCheckState(0, Qt::Checked);
        encounterGroup->setExpanded(true);
        for (std::size_t index = 0; index < scene->batches.size(); ++index) {
            const auto& batch = scene->batches[index];
            auto* item = new QTreeWidgetItem(encounterGroup);
            item->setText(0, QString::fromStdString(batch.label));
            item->setData(0, kVisibilityIndexRole, static_cast<qulonglong>(index));
            item->setData(0, kSceneBatchIndexRole, static_cast<qulonglong>(index));
            item->setData(0, kVisibilityIdRole,
                QStringLiteral("encounter:%1").arg(QString::fromStdString(batch.label)));
            item->setData(0, kLegacyVisibilityLabelRole, QString::fromStdString(batch.label));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Checked);
        }
        if (!scene->contextBatches.empty()) {
            auto* contextGroup = new QTreeWidgetItem(resourceTree_);
            contextGroup->setText(0, QStringLiteral("Field Context"));
            contextGroup->setText(1, QStringLiteral("%1 entries / %2 triangles")
                .arg(scene->contextEntryCount()).arg(scene->contextTriangleCount()));
            contextGroup->setFlags(contextGroup->flags() | Qt::ItemIsUserCheckable);
            contextGroup->setCheckState(0, Qt::Checked);
            contextGroup->setExpanded(true);
            for (std::size_t index = 0; index < scene->contextBatches.size(); ++index) {
                const auto& batch = scene->contextBatches[index];
                auto* item = new QTreeWidgetItem(contextGroup);
                item->setText(0, QString::fromStdString(batch.label));
                item->setText(1, QStringLiteral("%1 entries / %2 triangles")
                    .arg(batch.sourceEntryCount).arg(batch.triangleCount()));
                item->setData(0, kVisibilityIndexRole,
                    static_cast<qulonglong>(scene->batches.size() + index));
                item->setData(0, kVisibilityIdRole, QString::fromStdString(batch.visibilityId));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Checked);
            }
        }
    }
    updating_ = false;
}

void FieldSceneWidget::clearScene() {
    setScene(nullptr);
}

void FieldSceneWidget::setContextOpacityEnabled(const bool enabled) {
    contextOpacitySlider_->setEnabled(enabled);
}

void FieldSceneWidget::setContextOpacity(const int percent) {
    const QSignalBlocker blocker(contextOpacitySlider_);
    updating_ = true;
    contextOpacitySlider_->setValue(std::clamp(percent, 0, 100));
    updateOpacityLabel(contextOpacitySlider_->value());
    updating_ = false;
}

int FieldSceneWidget::contextOpacity() const {
    return contextOpacitySlider_->value();
}

void FieldSceneWidget::restoreHiddenBatches(const QStringList& hiddenBatches) {
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        auto* group = resourceTree_->topLevelItem(row);
        forEachVisibilityLeaf(group, [&](QTreeWidgetItem* item) {
            const auto id = item->data(0, kVisibilityIdRole).toString();
            const auto legacy = item->data(0, kLegacyVisibilityLabelRole).toString();
            const bool hidden = hiddenBatches.contains(id) ||
                (!legacy.isEmpty() && hiddenBatches.contains(legacy));
            item->setCheckState(0, hidden ? Qt::Unchecked : Qt::Checked);
        });
        updateGroupCheckState(group);
    }
    updating_ = false;
}

QStringList FieldSceneWidget::hiddenBatchIds() const {
    QStringList hidden{};
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](const QTreeWidgetItem* item) {
            if (item->checkState(0) != Qt::Checked) {
                hidden.push_back(item->data(0, kVisibilityIdRole).toString());
            }
        });
    }
    return hidden;
}

std::vector<std::uint8_t> FieldSceneWidget::visibility() const {
    std::vector<std::uint8_t> result(visibilityCount_, 1U);
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](const QTreeWidgetItem* item) {
            const auto index = static_cast<std::size_t>(
                item->data(0, kVisibilityIndexRole).toULongLong());
            if (index < result.size()) result[index] = item->checkState(0) == Qt::Checked ? 1U : 0U;
        });
    }
    return result;
}

void FieldSceneWidget::setEncounterBatchModified(
    const std::vector<std::uint8_t>& modifiedBatches) {
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](QTreeWidgetItem* item) {
            if (!item->data(0, kSceneBatchIndexRole).isValid()) return;
            const auto index = static_cast<std::size_t>(
                item->data(0, kSceneBatchIndexRole).toULongLong());
            item->setText(1, index < modifiedBatches.size() && modifiedBatches[index] != 0U
                ? QStringLiteral("Modified") : QString{});
        });
    }
}

void FieldSceneWidget::setRebaseState(const bool visible, const bool enabled) {
    rebaseButton_->setVisible(visible);
    rebaseButton_->setEnabled(enabled);
}

void FieldSceneWidget::onResourceItemChanged(QTreeWidgetItem* item, const int column) {
    if (updating_ || item == nullptr || column != 0) return;
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    if (!item->data(0, kVisibilityIndexRole).isValid()) {
        const auto state = item->checkState(0) == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
        forEachVisibilityLeaf(item, [state](QTreeWidgetItem* leaf) {
            leaf->setCheckState(0, state);
        });
        item->setCheckState(0, state);
    } else {
        updateGroupCheckState(item->parent());
    }
    updating_ = false;
    emit visibilityChanged(visibility());
}

void FieldSceneWidget::onCurrentResourceChanged(QTreeWidgetItem* current) {
    if (updating_ || current == nullptr ||
        !current->data(0, kSceneBatchIndexRole).isValid()) {
        emit sceneBatchSelectionChanged(-1);
        return;
    }
    emit sceneBatchSelectionChanged(
        static_cast<qint64>(current->data(0, kSceneBatchIndexRole).toULongLong()));
}

void FieldSceneWidget::updateOpacityLabel(const int percent) {
    contextOpacityValueLabel_->setText(QStringLiteral("%1%").arg(percent));
}

} // namespace skewer::qt
