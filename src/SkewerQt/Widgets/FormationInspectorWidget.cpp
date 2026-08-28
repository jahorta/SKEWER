#include "FormationInspectorWidget.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace skewer::qt {

FormationInspectorWidget::FormationInspectorWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    statusLabel_ = new QLabel(QStringLiteral("No ALX data selected."), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);
    headerLabel_ = new QLabel(
        QStringLiteral("Select an ECT row to inspect its formation."), this);
    headerLabel_->setWordWrap(true);
    layout->addWidget(headerLabel_);
    formationTable_ = new QTableWidget(8, 3, this);
    formationTable_->setHorizontalHeaderLabels(
        { QStringLiteral("Slot"), QStringLiteral("Enemy ID"), QStringLiteral("Enemy") });
    formationTable_->horizontalHeader()->setStretchLastSection(true);
    formationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    formationTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(formationTable_, 1);
}

void FormationInspectorWidget::showLoading(const QString& rootPath) {
    statusLabel_->setText(QStringLiteral("Loading ALX data from %1...").arg(rootPath));
    headerLabel_->setText(QStringLiteral("Formation enrichment is loading."));
    clearRows();
}

void FormationInspectorWidget::showUnavailable(const bool rememberedRoot) {
    statusLabel_->setText(rememberedRoot
        ? QStringLiteral("The remembered ALX data is unavailable. Native editing is unaffected.")
        : QStringLiteral("No ALX data selected."));
    headerLabel_->setText(
        QStringLiteral("Select a valid ALX 5.0.0 data directory to inspect formations."));
    clearRows();
}

void FormationInspectorWidget::showLoadedSource(
    const QString& locale,
    const QString& rootPath) {
    statusLabel_->setText(QStringLiteral("Loaded %1 ALX data: %2").arg(locale, rootPath));
}

void FormationInspectorWidget::showSelectionPrompt() {
    headerLabel_->setText(QStringLiteral("Select an ECT row to inspect its formation."));
    clearRows();
}

void FormationInspectorWidget::showInvalidSelection() {
    headerLabel_->setText(QStringLiteral("The selected ECT row is unavailable."));
    clearRows();
}

void FormationInspectorWidget::showFormation(
    const skewer::core::FormationResolution& formation) {
    clearRows();
    if (formation.status != skewer::core::FormationResolutionStatus::Unique) {
        headerLabel_->setText(QStringLiteral("%1 / encounter %2: no unique formation is available.")
            .arg(QString::fromStdString(formation.filter)).arg(formation.encounterId));
        return;
    }
    headerLabel_->setText(QStringLiteral("%1 / encounter %2 — Initiative %3, Magic EXP %4")
        .arg(QString::fromStdString(formation.filter)).arg(formation.encounterId)
        .arg(*formation.initiative).arg(*formation.magicExperience));
    for (int row = 0; row < static_cast<int>(formation.enemies.size()); ++row) {
        const auto& enemy = formation.enemies[static_cast<std::size_t>(row)];
        formationTable_->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        formationTable_->setItem(row, 1, new QTableWidgetItem(
            enemy.empty() ? QStringLiteral("—") : QString::number(enemy.enemyId)));
        formationTable_->setItem(row, 2, new QTableWidgetItem(
            enemy.empty() ? QStringLiteral("Empty") : QString::fromStdString(enemy.displayName)));
    }
}

void FormationInspectorWidget::clearRows() {
    formationTable_->clearContents();
}

} // namespace skewer::qt
