#include "EncounterEditorWidget.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

namespace skewer::qt {
namespace {

[[nodiscard]] QString formationSummary(
    const skewer::core::FormationResolution& formation) {
    std::vector<std::pair<QString, int>> enemies{};
    for (const auto& enemy : formation.enemies) {
        if (enemy.empty()) continue;
        auto name = QString::fromStdString(enemy.displayName);
        if (name.isEmpty()) {
            name = QStringLiteral("Enemy %1").arg(enemy.enemyId);
        }
        const auto found = std::find_if(enemies.begin(), enemies.end(),
            [&name](const auto& entry) { return entry.first == name; });
        if (found == enemies.end()) enemies.emplace_back(std::move(name), 1);
        else ++found->second;
    }
    if (enemies.empty()) return QStringLiteral("Empty");
    QStringList parts{};
    for (const auto& [name, count] : enemies) {
        parts.push_back(QStringLiteral("%1x %2").arg(count).arg(name));
    }
    return parts.join(QStringLiteral(", "));
}

[[nodiscard]] QString unavailableFormationTooltip(
    const skewer::core::FormationResolutionStatus status) {
    return status == skewer::core::FormationResolutionStatus::Ambiguous
        ? QStringLiteral("More than one ALX formation matches this encounter ID.")
        : QStringLiteral("No ALX formation matches this encounter ID.");
}

void setFormationPresentation(
    QTableWidgetItem& item,
    const std::optional<skewer::core::FormationResolution>& formation,
    const bool alxAvailable) {
    if (!alxAvailable) {
        item.setText(QStringLiteral("---"));
        item.setToolTip(QStringLiteral(
            "Load ALX data to show enemy formations."));
        return;
    }
    if (formation.has_value() &&
        formation->status == skewer::core::FormationResolutionStatus::Unique) {
        item.setText(formationSummary(*formation));
        item.setToolTip(QString{});
        return;
    }
    item.setText(QStringLiteral("—"));
    item.setToolTip(unavailableFormationTooltip(formation.has_value()
        ? formation->status : skewer::core::FormationResolutionStatus::Missing));
}

} // namespace

EncounterEditorWidget::EncounterEditorWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    tableTabs_ = new QTabBar(this);
    tableTabs_->setExpanding(true);
    for (int index = 0; index < 8; ++index) {
        tableTabs_->addTab(QString::number(index + 1));
        tableTabs_->setTabToolTip(index,
            QStringLiteral("Selector %1 / Table %1").arg(index + 1));
    }
    tableTabs_->setCurrentIndex(0);
    layout->addWidget(tableTabs_);

    tableHeader_ = new QLabel(QStringLiteral("No ECT loaded"), this);
    tableHeader_->setWordWrap(true);
    layout->addWidget(tableHeader_);

    auto* valueRow = new QHBoxLayout();
    valueRow->addWidget(new QLabel(QStringLiteral("Stage"), this));
    stageEditor_ = new QSpinBox(this);
    stageEditor_->setRange(0, 65535);
    valueRow->addWidget(stageEditor_);
    valueRow->addWidget(new QLabel(QStringLiteral("Overall rate"), this));
    overallRateEditor_ = new QSpinBox(this);
    overallRateEditor_->setRange(0, 65535);
    valueRow->addWidget(overallRateEditor_);
    layout->addLayout(valueRow);

    encounterTable_ = new QTableWidget(32, 3, this);
    encounterTable_->setHorizontalHeaderLabels(
        { QStringLiteral("Enc. ID"), QStringLiteral("Enc. Weight"),
            QStringLiteral("Enemy Formation") });
    QStringList slotLabels{};
    for (int slot = 0; slot < encounterTable_->rowCount(); ++slot) {
        slotLabels.push_back(QString::number(slot));
    }
    encounterTable_->setVerticalHeaderLabels(slotLabels);
    encounterTable_->verticalHeader()->setToolTip(QStringLiteral("Slot"));
    encounterTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    encounterTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    encounterTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    encounterTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    encounterTable_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    encounterTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(encounterTable_, 1);

    connect(tableTabs_, &QTabBar::currentChanged, this, [this](const int index) {
        if (!updating_) emit tableSelectionChanged(index);
    });
    connect(encounterTable_, &QTableWidget::itemChanged,
        this, &EncounterEditorWidget::onEncounterItemChanged);
    connect(encounterTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!updating_) emit encounterSelectionChanged(currentTableIndex(), currentRowIndex());
    });
    connect(stageEditor_, qOverload<int>(&QSpinBox::valueChanged), this, [this](const int value) {
        if (updating_ || currentTableIndex() < 0) return;
        emit valueEditRequested(
            { skewer::core::EctValueKind::Stage,
                static_cast<std::size_t>(currentTableIndex()), 0U },
            QString::number(value));
    });
    connect(overallRateEditor_, qOverload<int>(&QSpinBox::valueChanged), this,
        [this](const int value) {
            if (updating_ || currentTableIndex() < 0) return;
            emit valueEditRequested(
                { skewer::core::EctValueKind::OverallEncounterRate,
                    static_cast<std::size_t>(currentTableIndex()), 0U },
                QString::number(value));
        });
}

void EncounterEditorWidget::showTable(
    const skewer::core::FieldDocument* document,
    const int tableIndex,
    const std::vector<std::optional<skewer::core::FormationResolution>>& formations) {
    const int selectedRow = encounterTable_->currentRow();
    const QSignalBlocker tableBlocker(encounterTable_);
    const QSignalBlocker stageBlocker(stageEditor_);
    const QSignalBlocker rateBlocker(overallRateEditor_);
    updating_ = true;
    encounterTable_->clearContents();
    if (document == nullptr || tableIndex < 0) {
        tableHeader_->setText(QStringLiteral("No ECT loaded"));
        updating_ = false;
        return;
    }
    const auto* flat = std::get_if<spice::ect::EctFlatContent>(&document->workingEct.content);
    if (flat == nullptr || static_cast<std::size_t>(tableIndex) >= flat->tables.size()) {
        updating_ = false;
        return;
    }
    const auto& table = flat->tables[static_cast<std::size_t>(tableIndex)];
    tableHeader_->setText(QStringLiteral("Selector %1 / Table %1").arg(tableIndex + 1));
    stageEditor_->setValue(table.stage);
    overallRateEditor_->setValue(table.overallEncounterRate);
    stageEditor_->setStyleSheet(document->isEctValueModified(
        { skewer::core::EctValueKind::Stage, static_cast<std::size_t>(tableIndex), 0U })
            ? QStringLiteral("QSpinBox { background: #006444; }") : QString{});
    overallRateEditor_->setStyleSheet(document->isEctValueModified(
        { skewer::core::EctValueKind::OverallEncounterRate,
            static_cast<std::size_t>(tableIndex), 0U })
            ? QStringLiteral("QSpinBox { background: #006444; }") : QString{});
    for (int row = 0; row < static_cast<int>(table.encounters.size()); ++row) {
        const auto& encounter = table.encounters[static_cast<std::size_t>(row)];
        auto* encounterId = new QTableWidgetItem(QString::number(encounter.encounterId));
        auto* weight = new QTableWidgetItem(QString::number(encounter.encounterRate));
        auto* formation = new QTableWidgetItem();
        formation->setFlags(formation->flags() & ~Qt::ItemIsEditable);
        const auto resolved = static_cast<std::size_t>(row) < formations.size()
            ? formations[static_cast<std::size_t>(row)] : std::nullopt;
        setFormationPresentation(*formation, resolved, !formations.empty());
        if (document->isEctValueModified(
            { skewer::core::EctValueKind::EncounterId,
                static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) {
            encounterId->setBackground(QColor(0, 100, 84));
        }
        if (document->isEctValueModified(
            { skewer::core::EctValueKind::Weight,
                static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) {
            weight->setBackground(QColor(0, 100, 84));
        }
        encounterTable_->setItem(row, 0, encounterId);
        encounterTable_->setItem(row, 1, weight);
        encounterTable_->setItem(row, 2, formation);
    }
    encounterTable_->selectRow(selectedRow >= 0 && selectedRow < encounterTable_->rowCount()
        ? selectedRow : 0);
    updating_ = false;
}

void EncounterEditorWidget::updateEctValue(
    const skewer::core::FieldDocument* document,
    const skewer::core::EctValueKey& key) {
    if (document == nullptr || currentTableIndex() < 0 ||
        key.tableIndex != static_cast<std::size_t>(currentTableIndex())) return;
    const auto value = document->effectiveEctValue(key);
    if (!value.has_value()) return;
    const QSignalBlocker tableBlocker(encounterTable_);
    const QSignalBlocker stageBlocker(stageEditor_);
    const QSignalBlocker rateBlocker(overallRateEditor_);
    updating_ = true;
    const auto modified = document->isEctValueModified(key);
    const auto modifiedStyle = modified
        ? QStringLiteral("QSpinBox { background: #fff4b4; }") : QString{};
    switch (key.kind) {
    case skewer::core::EctValueKind::Stage:
        stageEditor_->setValue(*value);
        stageEditor_->setStyleSheet(modifiedStyle);
        break;
    case skewer::core::EctValueKind::OverallEncounterRate:
        overallRateEditor_->setValue(*value);
        overallRateEditor_->setStyleSheet(modifiedStyle);
        break;
    case skewer::core::EctValueKind::EncounterId:
    case skewer::core::EctValueKind::Weight: {
        const auto column = key.kind == skewer::core::EctValueKind::EncounterId ? 0 : 1;
        if (key.rowIndex < static_cast<std::size_t>(encounterTable_->rowCount())) {
            auto* item = encounterTable_->item(static_cast<int>(key.rowIndex), column);
            if (item != nullptr) {
                item->setText(QString::number(*value));
                item->setBackground(modified
                    ? QColor(255, 244, 180) : QColor{});
            }
        }
        break;
    }
    }
    updating_ = false;
}

void EncounterEditorWidget::updateFormation(
    const int tableIndex,
    const int rowIndex,
    const std::optional<skewer::core::FormationResolution>& formation,
    const bool alxAvailable) {
    if (tableIndex != currentTableIndex() || rowIndex < 0 ||
        rowIndex >= encounterTable_->rowCount()) return;
    auto* item = encounterTable_->item(rowIndex, 2);
    if (item == nullptr) return;
    const QSignalBlocker blocker(encounterTable_);
    setFormationPresentation(*item, formation, alxAvailable);
}

void EncounterEditorWidget::selectTable(const int tableIndex) {
    tableTabs_->setCurrentIndex(std::clamp(tableIndex, 0, 7));
}

void EncounterEditorWidget::restoreTable(const int tableIndex) {
    const QSignalBlocker blocker(tableTabs_);
    updating_ = true;
    tableTabs_->setCurrentIndex(std::clamp(tableIndex, 0, 7));
    updating_ = false;
}

void EncounterEditorWidget::setWritable(const bool writable) {
    stageEditor_->setEnabled(writable);
    overallRateEditor_->setEnabled(writable);
    encounterTable_->setEditTriggers(writable
        ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        : QAbstractItemView::NoEditTriggers);
}

int EncounterEditorWidget::currentTableIndex() const {
    return tableTabs_->currentIndex();
}

int EncounterEditorWidget::currentRowIndex() const {
    return encounterTable_->currentRow();
}

void EncounterEditorWidget::onEncounterItemChanged(QTableWidgetItem* item) {
    if (updating_ || item == nullptr || item->column() < 0 ||
        item->column() > 1 || currentTableIndex() < 0) return;
    const auto kind = item->column() == 0
        ? skewer::core::EctValueKind::EncounterId : skewer::core::EctValueKind::Weight;
    emit valueEditRequested(
        { kind, static_cast<std::size_t>(currentTableIndex()),
            static_cast<std::size_t>(item->row()) },
        item->text());
}

} // namespace skewer::qt
