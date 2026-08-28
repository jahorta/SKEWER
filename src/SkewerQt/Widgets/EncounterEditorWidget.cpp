#include "EncounterEditorWidget.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <variant>

namespace skewer::qt {

EncounterEditorWidget::EncounterEditorWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    tableList_ = new QListWidget(this);
    for (int index = 0; index < 8; ++index) {
        tableList_->addItem(QStringLiteral("Selector %1 - Table %2").arg(index + 1).arg(index + 1));
    }
    tableList_->setCurrentRow(0);
    layout->addWidget(tableList_);

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
        { QStringLiteral("Slot"), QStringLiteral("Encounter ID"), QStringLiteral("Weight / rate") });
    encounterTable_->horizontalHeader()->setStretchLastSection(true);
    encounterTable_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    encounterTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(encounterTable_, 1);

    connect(tableList_, &QListWidget::currentRowChanged, this, [this](const int row) {
        if (!updating_) emit tableSelectionChanged(row);
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
    const int tableIndex) {
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
            ? QStringLiteral("QSpinBox { background: #fff4b4; }") : QString{});
    overallRateEditor_->setStyleSheet(document->isEctValueModified(
        { skewer::core::EctValueKind::OverallEncounterRate,
            static_cast<std::size_t>(tableIndex), 0U })
            ? QStringLiteral("QSpinBox { background: #fff4b4; }") : QString{});
    for (int row = 0; row < static_cast<int>(table.encounters.size()); ++row) {
        const auto& encounter = table.encounters[static_cast<std::size_t>(row)];
        auto* slot = new QTableWidgetItem(QString::number(row));
        slot->setFlags(slot->flags() & ~Qt::ItemIsEditable);
        encounterTable_->setItem(row, 0, slot);
        auto* encounterId = new QTableWidgetItem(QString::number(encounter.encounterId));
        auto* weight = new QTableWidgetItem(QString::number(encounter.encounterRate));
        if (document->isEctValueModified(
            { skewer::core::EctValueKind::EncounterId,
                static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) {
            encounterId->setBackground(QColor(255, 244, 180));
        }
        if (document->isEctValueModified(
            { skewer::core::EctValueKind::Weight,
                static_cast<std::size_t>(tableIndex), static_cast<std::size_t>(row) })) {
            weight->setBackground(QColor(255, 244, 180));
        }
        encounterTable_->setItem(row, 1, encounterId);
        encounterTable_->setItem(row, 2, weight);
    }
    encounterTable_->selectRow(selectedRow >= 0 && selectedRow < encounterTable_->rowCount()
        ? selectedRow : 0);
    updating_ = false;
}

void EncounterEditorWidget::selectTable(const int tableIndex) {
    tableList_->setCurrentRow(std::clamp(tableIndex, 0, 7));
}

void EncounterEditorWidget::restoreTable(const int tableIndex) {
    const QSignalBlocker blocker(tableList_);
    updating_ = true;
    tableList_->setCurrentRow(std::clamp(tableIndex, 0, 7));
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
    return tableList_->currentRow();
}

int EncounterEditorWidget::currentRowIndex() const {
    return encounterTable_->currentRow();
}

void EncounterEditorWidget::onEncounterItemChanged(QTableWidgetItem* item) {
    if (updating_ || item == nullptr || item->column() == 0 || currentTableIndex() < 0) return;
    const auto kind = item->column() == 1
        ? skewer::core::EctValueKind::EncounterId : skewer::core::EctValueKind::Weight;
    emit valueEditRequested(
        { kind, static_cast<std::size_t>(currentTableIndex()),
            static_cast<std::size_t>(item->row()) },
        item->text());
}

} // namespace skewer::qt
