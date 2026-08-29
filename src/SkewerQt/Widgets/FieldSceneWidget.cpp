#include "FieldSceneWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace skewer::qt {
namespace {

constexpr int kVisibilityIndicesRole = Qt::UserRole;
constexpr int kVisibilityIdRole = Qt::UserRole + 1;
constexpr int kLegacyVisibilityLabelsRole = Qt::UserRole + 2;
constexpr int kEntryTableIndexRole = Qt::UserRole + 3;
constexpr int kEventGroundRole = Qt::UserRole + 4;
constexpr int kEventGroundGroupRole = Qt::UserRole + 5;
constexpr int kVariantOrdinalRole = Qt::UserRole + 6;

QString kindName(const std::optional<skewer::core::SceneResourceKind> kind) {
    if (!kind.has_value()) return QStringLiteral("Unknown");
    return *kind == skewer::core::SceneResourceKind::Grnd
        ? QStringLiteral("GRND") : QStringLiteral("GOBJ");
}

QString kindId(const skewer::core::SceneResourceKind kind) {
    return kind == skewer::core::SceneResourceKind::Grnd
        ? QStringLiteral("grnd") : QStringLiteral("gobj");
}

QString addressText(const std::uint32_t address) {
    return QStringLiteral("0x%1").arg(
        QString::number(address, 16).rightJustified(8, QLatin1Char('0')).toUpper());
}

QVariantList toVariantList(const std::vector<std::size_t>& indices) {
    QVariantList result{};
    result.reserve(static_cast<qsizetype>(indices.size()));
    for (const auto index : indices) result.push_back(QVariant::fromValue<qulonglong>(index));
    return result;
}

std::vector<std::size_t> visibilityIndices(const QTreeWidgetItem* item) {
    std::vector<std::size_t> result{};
    for (const auto& value : item->data(0, kVisibilityIndicesRole).toList()) {
        result.push_back(static_cast<std::size_t>(value.toULongLong()));
    }
    return result;
}

template <typename Function>
void forEachVisibilityLeaf(QTreeWidgetItem* root, Function&& function) {
    if (root == nullptr) return;
    if (root->data(0, kVisibilityIndicesRole).isValid()) {
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
    if (root->data(0, kVisibilityIndicesRole).isValid()) {
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

bool containsEventGround(const QTreeWidgetItem* item) {
    if (item == nullptr) return false;
    if (item->data(0, kEventGroundRole).toBool()) return true;
    for (int index = 0; index < item->childCount(); ++index) {
        if (containsEventGround(item->child(index))) return true;
    }
    return false;
}

void configureLeaf(
    QTreeWidgetItem* item,
    const std::vector<std::size_t>& indices,
    const QString& id,
    const QStringList& legacyLabels) {
    item->setData(0, kVisibilityIndicesRole, toVariantList(indices));
    item->setData(0, kVisibilityIdRole, id);
    item->setData(0, kLegacyVisibilityLabelsRole, legacyLabels);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Checked);
}

QStringList legacyLabelsFor(
    const skewer::core::SceneModel& scene,
    const std::vector<std::size_t>& indices) {
    QStringList labels{};
    for (const auto index : indices) {
        if (index >= scene.batches.size()) continue;
        const auto& batch = scene.batches[index];
        labels.push_back(QString::fromStdString(batch.label));
        QString legacy = QStringLiteral("%1 %2")
            .arg(kindName(batch.instance.kind)).arg(addressText(batch.instance.resourceAddress));
        if (batch.instance.kind == skewer::core::SceneResourceKind::Gobj &&
            batch.instance.nodeIndex.has_value()) {
            legacy += QStringLiteral(" node=%1").arg(*batch.instance.nodeIndex);
        }
        legacy += batch.instance.entryTableIndex.has_value()
            ? QStringLiteral(" entry=%1").arg(*batch.instance.entryTableIndex)
            : QStringLiteral(" unreferenced");
        if (!labels.contains(legacy)) labels.push_back(legacy);
    }
    return labels;
}

std::size_t triangleCountFor(
    const skewer::core::SceneModel& scene,
    const std::vector<std::size_t>& batchIndices) {
    std::size_t count = 0U;
    for (const auto index : batchIndices) {
        if (index < scene.batches.size()) count += scene.batches[index].triangleIndices.size();
    }
    return count;
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

    layout->addWidget(new QLabel(QStringLiteral("Field state"), this));
    fieldStateCombo_ = new QComboBox(this);
    fieldStateCombo_->setEnabled(false);
    layout->addWidget(fieldStateCombo_);

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
    connect(fieldStateCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](const int index) {
            if (updating_ || index < 0) return;
            const auto mode = fieldStateCombo_->itemData(index, Qt::UserRole).toString();
            if (mode == QStringLiteral("raw")) {
                setEventGroundDisplayMode(EventGroundDisplayMode::Raw);
                emit rawEventGroundRequested();
            } else if (mode == QStringLiteral("preset")) {
                const auto presetId = fieldStateCombo_->itemData(index, Qt::UserRole + 1).toString();
                setEventGroundDisplayMode(EventGroundDisplayMode::Preset, presetId);
                emit eventGroundPresetRequested(presetId);
            } else {
                setEventGroundDisplayMode(EventGroundDisplayMode::Custom);
            }
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
            if (!field.isAvailable()) item->setToolTip(QString::fromStdString(field.unavailableReason));
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

void FieldSceneWidget::setScene(
    const skewer::core::SceneModel* scene,
    const std::vector<skewer::core::EventGroundPreset>& presets) {
    const QSignalBlocker treeBlocker(resourceTree_);
    const QSignalBlocker stateBlocker(fieldStateCombo_);
    updating_ = true;
    resourceTree_->clear();
    presets_ = presets;
    fieldStateCombo_->clear();
    fieldStateCombo_->addItem(QStringLiteral("Raw — all event-ground resources"), QStringLiteral("raw"));
    fieldStateCombo_->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    for (const auto& preset : presets) {
        fieldStateCombo_->addItem(QString::fromStdString(preset.label), QStringLiteral("preset"));
        fieldStateCombo_->setItemData(fieldStateCombo_->count() - 1,
            QString::fromStdString(preset.id), Qt::UserRole + 1);
    }
    fieldStateCombo_->setCurrentIndex(0);
    fieldStateCombo_->setEnabled(scene != nullptr && !scene->eventGroundGroups.empty());
    eventGroundDisplayMode_ = EventGroundDisplayMode::Raw;
    selectedEventGroundPresetId_.clear();
    visibilityCount_ = scene == nullptr ? 0U : scene->batches.size() + scene->contextBatches.size();
    contextOpacitySlider_->setEnabled(scene != nullptr && !scene->contextBatches.empty());

    if (scene != nullptr) {
        std::set<std::size_t> groupedBatchIndices{};
        if (!scene->eventGroundGroups.empty()) {
            auto* encounterRoot = new QTreeWidgetItem(resourceTree_);
            encounterRoot->setText(0, QStringLiteral("Encounter Surfaces"));
            encounterRoot->setFlags(encounterRoot->flags() | Qt::ItemIsUserCheckable);
            encounterRoot->setCheckState(0, Qt::Checked);
            encounterRoot->setExpanded(true);
            std::size_t encounterTriangleCount = 0U;
            for (const auto& group : scene->eventGroundGroups) {
                auto* groupItem = new QTreeWidgetItem(encounterRoot);
                groupItem->setText(0, QStringLiteral("Entry %1 — %2 — tblId %3 (%4)")
                    .arg(group.key.entryTableIndex)
                    .arg(QString::fromStdString(group.functionName))
                    .arg(addressText(static_cast<std::uint32_t>(group.tblId)))
                    .arg(group.tblId));
                groupItem->setData(0, kEntryTableIndexRole,
                    QVariant::fromValue<qulonglong>(group.key.entryTableIndex));
                groupItem->setData(0, kEventGroundGroupRole, true);
                groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable);
                groupItem->setCheckState(0, Qt::Checked);
                for (const auto& variant : group.variants) {
                    auto* item = new QTreeWidgetItem(groupItem);
                    item->setText(0, QStringLiteral("Variant %1 — %2 %3")
                        .arg(variant.ordinal).arg(kindName(variant.resourceKind))
                        .arg(addressText(variant.resourceAddress)));
                    configureLeaf(item, variant.batchIndices,
                        QStringLiteral("event-ground:%1:%2")
                            .arg(group.key.entryTableIndex).arg(variant.ordinal),
                        legacyLabelsFor(*scene, variant.batchIndices));
                    item->setData(0, kEntryTableIndexRole,
                        QVariant::fromValue<qulonglong>(group.key.entryTableIndex));
                    item->setData(0, kEventGroundRole, true);
                    item->setData(0, kVariantOrdinalRole,
                        QVariant::fromValue<qulonglong>(variant.ordinal));
                    for (const auto index : variant.batchIndices) {
                        groupedBatchIndices.insert(index);
                    }
                    encounterTriangleCount += triangleCountFor(*scene, variant.batchIndices);
                }
            }
            encounterRoot->setText(1,
                QStringLiteral("%1 triangles").arg(encounterTriangleCount));
        }

        QTreeWidgetItem* otherRoot = nullptr;
        std::size_t otherTriangleCount = 0U;
        const auto ensureOtherRoot = [&]() {
            if (otherRoot == nullptr) {
                otherRoot = new QTreeWidgetItem(resourceTree_);
                otherRoot->setText(0, QStringLiteral("Other GRND/GOBJ"));
                otherRoot->setFlags(otherRoot->flags() | Qt::ItemIsUserCheckable);
                otherRoot->setCheckState(0, Qt::Checked);
                otherRoot->setExpanded(true);
            }
            return otherRoot;
        };

        for (const auto& group : scene->otherGroundGroups) {
            auto* groupItem = new QTreeWidgetItem(ensureOtherRoot());
            groupItem->setText(0, QStringLiteral("Entry %1 — %2 — tblId %3 (%4)")
                .arg(group.entryTableIndex)
                .arg(QString::fromStdString(group.functionName))
                .arg(addressText(static_cast<std::uint32_t>(group.tblId)))
                .arg(group.tblId));
            groupItem->setData(0, kEntryTableIndexRole,
                QVariant::fromValue<qulonglong>(group.entryTableIndex));
            groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable);
            groupItem->setCheckState(0, Qt::Checked);
            for (const auto& resource : group.resources) {
                auto* item = new QTreeWidgetItem(groupItem);
                item->setText(0, QStringLiteral("Ordinal %1 — %2 %3")
                    .arg(resource.ordinal).arg(kindName(resource.resourceKind))
                    .arg(addressText(resource.resourceAddress)));
                auto legacy = legacyLabelsFor(*scene, resource.batchIndices);
                legacy.push_back(QStringLiteral("event-ground:%1:%2")
                    .arg(group.entryTableIndex).arg(resource.ordinal));
                configureLeaf(item, resource.batchIndices,
                    QStringLiteral("other-ground:%1:%2")
                        .arg(group.entryTableIndex).arg(resource.ordinal), legacy);
                item->setData(0, kEntryTableIndexRole,
                    QVariant::fromValue<qulonglong>(group.entryTableIndex));
                for (const auto index : resource.batchIndices) groupedBatchIndices.insert(index);
                otherTriangleCount += triangleCountFor(*scene, resource.batchIndices);
            }
        }

        using ResourceKey = std::tuple<skewer::core::SceneReferenceRole, std::size_t,
            skewer::core::SceneResourceKind, std::uint32_t>;
        std::map<ResourceKey, std::vector<std::size_t>> resources{};
        for (std::size_t index = 0; index < scene->batches.size(); ++index) {
            if (groupedBatchIndices.contains(index)) continue;
            const auto& instance = scene->batches[index].instance;
            const auto entryIndex = instance.entryTableIndex.value_or(0U);
            resources[{ instance.referenceRole, entryIndex, instance.kind,
                instance.resourceAddress }].push_back(index);
        }

        QTreeWidgetItem* ordinaryRoot = nullptr;
        QTreeWidgetItem* unreferencedRoot = nullptr;
        for (const auto& [key, indices] : resources) {
            const auto [role, entryIndex, kind, address] = key;
            auto*& root = role == skewer::core::SceneReferenceRole::OrdinaryObject
                ? ordinaryRoot : unreferencedRoot;
            if (root == nullptr) {
                root = new QTreeWidgetItem(ensureOtherRoot());
                root->setText(0, role == skewer::core::SceneReferenceRole::OrdinaryObject
                    ? QStringLiteral("Ordinary Objects") : QStringLiteral("Unreferenced Resources"));
                root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
                root->setCheckState(0, Qt::Checked);
                root->setExpanded(true);
            }
            auto* item = new QTreeWidgetItem(root);
            item->setText(0, role == skewer::core::SceneReferenceRole::OrdinaryObject
                ? QStringLiteral("Entry %1 — %2 %3").arg(entryIndex).arg(kindName(kind)).arg(addressText(address))
                : QStringLiteral("%1 %2").arg(kindName(kind)).arg(addressText(address)));
            const auto legacy = legacyLabelsFor(*scene, indices);
            const auto id = role == skewer::core::SceneReferenceRole::OrdinaryObject
                ? QStringLiteral("object:%1:%2:%3").arg(entryIndex).arg(kindId(kind)).arg(address, 8, 16, QLatin1Char('0'))
                : QStringLiteral("unreferenced:%1:%2").arg(kindId(kind)).arg(address, 8, 16, QLatin1Char('0'));
            configureLeaf(item, indices, id, legacy);
            otherTriangleCount += triangleCountFor(*scene, indices);
        }
        if (otherRoot != nullptr) {
            otherRoot->setText(1, QStringLiteral("%1 triangles").arg(otherTriangleCount));
        }

        if (!scene->contextBatches.empty()) {
            auto* contextRoot = new QTreeWidgetItem(resourceTree_);
            contextRoot->setText(0, QStringLiteral("Field Context"));
            contextRoot->setText(1, QStringLiteral("%1 entries / %2 triangles")
                .arg(scene->contextEntryCount()).arg(scene->contextTriangleCount()));
            contextRoot->setFlags(contextRoot->flags() | Qt::ItemIsUserCheckable);
            contextRoot->setCheckState(0, Qt::Checked);
            contextRoot->setExpanded(true);
            for (std::size_t index = 0; index < scene->contextBatches.size(); ++index) {
                const auto& batch = scene->contextBatches[index];
                auto* item = new QTreeWidgetItem(contextRoot);
                item->setText(0, QString::fromStdString(batch.label));
                item->setText(1, QStringLiteral("%1 entries / %2 triangles")
                    .arg(batch.sourceEntryCount).arg(batch.triangleCount()));
                configureLeaf(item, { scene->batches.size() + index },
                    QString::fromStdString(batch.visibilityId), {});
            }
        }
    }
    updateTreeState();
    updating_ = false;
}

void FieldSceneWidget::clearScene() { setScene(nullptr); }

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

int FieldSceneWidget::contextOpacity() const { return contextOpacitySlider_->value(); }

void FieldSceneWidget::restoreHiddenBatches(const QStringList& hiddenBatches) {
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](QTreeWidgetItem* item) {
            const auto id = item->data(0, kVisibilityIdRole).toString();
            bool hidden = hiddenBatches.contains(id);
            for (const auto& legacy : item->data(0, kLegacyVisibilityLabelsRole).toStringList()) {
                hidden = hidden || hiddenBatches.contains(legacy) ||
                    hiddenBatches.contains(QStringLiteral("encounter:%1").arg(legacy));
            }
            item->setCheckState(0, hidden ? Qt::Unchecked : Qt::Checked);
        });
    }
    updateTreeState();
    updating_ = false;
}

void FieldSceneWidget::setVisibility(const std::vector<std::uint8_t>& visibility) {
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](QTreeWidgetItem* item) {
            const auto indices = visibilityIndices(item);
            const auto visible = std::count_if(indices.begin(), indices.end(), [&](const auto index) {
                return index < visibility.size() && visibility[index] != 0U;
            });
            item->setCheckState(0, indices.empty() ? Qt::Checked
                : visible == 0 ? Qt::Unchecked
                : visible == static_cast<std::ptrdiff_t>(indices.size()) ? Qt::Checked
                : Qt::PartiallyChecked);
        });
    }
    updateTreeState();
    updating_ = false;
}

QStringList FieldSceneWidget::hiddenBatchIds() const {
    QStringList hidden{};
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](const QTreeWidgetItem* item) {
            if (item->checkState(0) != Qt::Checked) hidden.push_back(item->data(0, kVisibilityIdRole).toString());
        });
    }
    return hidden;
}

std::vector<std::uint8_t> FieldSceneWidget::visibility() const {
    std::vector<std::uint8_t> result(visibilityCount_, 1U);
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](const QTreeWidgetItem* item) {
            for (const auto index : visibilityIndices(item)) {
                if (index < result.size()) result[index] = item->checkState(0) == Qt::Checked ? 1U : 0U;
            }
        });
    }
    return result;
}

void FieldSceneWidget::setEventGroundDisplayMode(
    const EventGroundDisplayMode mode,
    const QString& presetId) {
    const QSignalBlocker stateBlocker(fieldStateCombo_);
    const QSignalBlocker treeBlocker(resourceTree_);
    eventGroundDisplayMode_ = mode;
    selectedEventGroundPresetId_ = mode == EventGroundDisplayMode::Preset ? presetId : QString{};
    int selected = mode == EventGroundDisplayMode::Raw ? 0 : 1;
    if (mode == EventGroundDisplayMode::Preset) {
        for (int index = 2; index < fieldStateCombo_->count(); ++index) {
            if (fieldStateCombo_->itemData(index, Qt::UserRole + 1).toString() == presetId) {
                selected = index;
                break;
            }
        }
    }
    fieldStateCombo_->setCurrentIndex(selected);
    if (mode != EventGroundDisplayMode::Custom) {
        const auto preset = mode == EventGroundDisplayMode::Preset
            ? std::find_if(presets_.begin(), presets_.end(), [&presetId](const auto& candidate) {
                return QString::fromStdString(candidate.id) == presetId;
            }) : presets_.end();
        for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
            forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](QTreeWidgetItem* item) {
                if (!item->data(0, kEventGroundRole).toBool()) return;
                bool visible = mode == EventGroundDisplayMode::Raw;
                if (preset != presets_.end()) {
                    const auto entryIndex = static_cast<std::size_t>(
                        item->data(0, kEntryTableIndexRole).toULongLong());
                    const auto ordinal = static_cast<std::size_t>(
                        item->data(0, kVariantOrdinalRole).toULongLong());
                    const auto assignment = std::find_if(preset->assignments.begin(),
                        preset->assignments.end(), [entryIndex](const auto& candidate) {
                            return candidate.group.entryTableIndex == entryIndex;
                        });
                    visible = assignment != preset->assignments.end() &&
                        assignment->state.kind == skewer::core::EventGroundStateKind::Variant &&
                        assignment->state.variantOrdinal == ordinal;
                }
                item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            });
        }
        updateTreeState();
    }
}

EventGroundDisplayMode FieldSceneWidget::eventGroundDisplayMode() const noexcept {
    return eventGroundDisplayMode_;
}

QString FieldSceneWidget::selectedEventGroundPresetId() const {
    return selectedEventGroundPresetId_;
}

void FieldSceneWidget::setEncounterBatchModified(const std::vector<std::uint8_t>& modifiedBatches) {
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        forEachVisibilityLeaf(resourceTree_->topLevelItem(row), [&](QTreeWidgetItem* item) {
            const auto indices = visibilityIndices(item);
            const bool modified = std::any_of(indices.begin(), indices.end(), [&](const auto index) {
                return index < modifiedBatches.size() && modifiedBatches[index] != 0U;
            });
            if (modified) item->setText(1, QStringLiteral("Modified"));
            else item->setText(1, QString{});
        });
    }
    updateTreeState();
    updating_ = false;
}

void FieldSceneWidget::setRebaseState(const bool visible, const bool enabled) {
    rebaseButton_->setVisible(visible);
    rebaseButton_->setEnabled(enabled);
}

void FieldSceneWidget::onResourceItemChanged(QTreeWidgetItem* item, const int column) {
    if (updating_ || item == nullptr || column != 0) return;
    const QSignalBlocker blocker(resourceTree_);
    updating_ = true;
    if (!item->data(0, kVisibilityIndicesRole).isValid()) {
        const auto state = item->checkState(0) == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
        forEachVisibilityLeaf(item, [state](QTreeWidgetItem* leaf) { leaf->setCheckState(0, state); });
        item->setCheckState(0, state);
    }
    if (containsEventGround(item)) setEventGroundDisplayMode(EventGroundDisplayMode::Custom);
    updateTreeState();
    updating_ = false;
    emit visibilityChanged(visibility());
}

void FieldSceneWidget::onCurrentResourceChanged(QTreeWidgetItem* current) {
    if (updating_ || current == nullptr ||
        !current->data(0, kEntryTableIndexRole).isValid()) {
        emit groundEntrySelectionChanged(-1);
        return;
    }
    emit groundEntrySelectionChanged(
        static_cast<qint64>(current->data(0, kEntryTableIndexRole).toULongLong()));
}

void FieldSceneWidget::updateTreeState() {
    for (int row = 0; row < resourceTree_->topLevelItemCount(); ++row) {
        auto* root = resourceTree_->topLevelItem(row);
        std::vector<QTreeWidgetItem*> stack{ root };
        std::vector<QTreeWidgetItem*> groups{};
        while (!stack.empty()) {
            auto* item = stack.back();
            stack.pop_back();
            if (item->childCount() > 0) groups.push_back(item);
            for (int index = 0; index < item->childCount(); ++index) stack.push_back(item->child(index));
        }
        for (auto iterator = groups.rbegin(); iterator != groups.rend(); ++iterator) updateGroupCheckState(*iterator);
        for (auto* group : groups) {
            if (!group->data(0, kEventGroundGroupRole).toBool()) continue;
            int visibleCount = 0;
            QString visibleVariant{};
            for (int index = 0; index < group->childCount(); ++index) {
                if (group->child(index)->checkState(0) == Qt::Checked) {
                    ++visibleCount;
                    visibleVariant = QStringLiteral("Variant %1").arg(
                        group->child(index)->data(0, kVariantOrdinalRole).toULongLong());
                }
            }
            group->setText(1, visibleCount == 0 ? QStringLiteral("Disabled")
                : visibleCount == 1 ? visibleVariant
                : visibleCount == group->childCount() ? QStringLiteral("All visible")
                : QStringLiteral("Custom"));
        }
    }
}

void FieldSceneWidget::updateOpacityLabel(const int percent) {
    contextOpacityValueLabel_->setText(QStringLiteral("%1%").arg(percent));
}

} // namespace skewer::qt
