#include "DiagnosticsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace skewer::qt {
namespace {

constexpr int kSeverityRole = Qt::UserRole;
constexpr int kMessageRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;
constexpr std::size_t kMaximumDiagnosticRows = 2000U;

[[nodiscard]] QString severityName(const skewer::core::DiagnosticSeverity severity) {
    switch (severity) {
    case skewer::core::DiagnosticSeverity::Error: return QStringLiteral("Error");
    case skewer::core::DiagnosticSeverity::Warning: return QStringLiteral("Warning");
    case skewer::core::DiagnosticSeverity::Info: return QStringLiteral("Info");
    }
    return QStringLiteral("Info");
}

} // namespace

DiagnosticsWidget::DiagnosticsWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* filters = new QHBoxLayout();
    errorsCheck_ = new QCheckBox(QStringLiteral("Errors"), this);
    warningsCheck_ = new QCheckBox(QStringLiteral("Warnings"), this);
    infosCheck_ = new QCheckBox(QStringLiteral("Info"), this);
    errorsCheck_->setChecked(true);
    warningsCheck_->setChecked(true);
    infosCheck_->setChecked(true);
    filters->addWidget(errorsCheck_);
    filters->addWidget(warningsCheck_);
    filters->addWidget(infosCheck_);
    filters->addStretch(1);
    layout->addLayout(filters);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({
        QStringLiteral("Severity"), QStringLiteral("Message"), QStringLiteral("Path") });
    tree_->setUniformRowHeights(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(tree_, 1);

    const auto filterChanged = [this]() { applyFilters(); };
    connect(errorsCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(warningsCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(infosCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(tree_, &QTreeWidget::customContextMenuRequested,
        this, &DiagnosticsWidget::showContextMenu);
}

void DiagnosticsWidget::setDiagnostics(const std::vector<DiagnosticGroup>& groups) {
    tree_->clear();
    summary_ = {};
    std::size_t rows = 0U;
    for (const auto& group : groups) {
        if (group.diagnostics.empty() || rows >= kMaximumDiagnosticRows) continue;
        auto* section = new QTreeWidgetItem(tree_);
        section->setFirstColumnSpanned(true);
        section->setText(0, group.title);
        section->setExpanded(true);
        for (const auto& diagnostic : group.diagnostics) {
            if (rows++ >= kMaximumDiagnosticRows) break;
            auto* item = new QTreeWidgetItem(section);
            item->setText(0, severityName(diagnostic.severity));
            item->setText(1, QString::fromStdString(diagnostic.message));
            const auto path = diagnostic.path.empty()
                ? QString{} : QString::fromStdWString(diagnostic.path.wstring());
            item->setText(2, path);
            item->setData(0, kSeverityRole, static_cast<int>(diagnostic.severity));
            item->setData(0, kMessageRole, item->text(1));
            item->setData(0, kPathRole, path);
            switch (diagnostic.severity) {
            case skewer::core::DiagnosticSeverity::Error: ++summary_.errors; break;
            case skewer::core::DiagnosticSeverity::Warning: ++summary_.warnings; break;
            case skewer::core::DiagnosticSeverity::Info: ++summary_.infos; break;
            }
        }
    }
    applyFilters();
}

DiagnosticSummary DiagnosticsWidget::summary() const noexcept {
    return summary_;
}

void DiagnosticsWidget::applyFilters() {
    for (int sectionIndex = 0; sectionIndex < tree_->topLevelItemCount(); ++sectionIndex) {
        auto* section = tree_->topLevelItem(sectionIndex);
        bool anyVisible = false;
        for (int row = 0; row < section->childCount(); ++row) {
            auto* item = section->child(row);
            const auto severity = static_cast<skewer::core::DiagnosticSeverity>(
                item->data(0, kSeverityRole).toInt());
            const bool visible =
                (severity == skewer::core::DiagnosticSeverity::Error && errorsCheck_->isChecked()) ||
                (severity == skewer::core::DiagnosticSeverity::Warning && warningsCheck_->isChecked()) ||
                (severity == skewer::core::DiagnosticSeverity::Info && infosCheck_->isChecked());
            item->setHidden(!visible);
            anyVisible = anyVisible || visible;
        }
        section->setHidden(!anyVisible);
    }
}

void DiagnosticsWidget::showContextMenu(const QPoint& position) {
    auto* item = tree_->itemAt(position);
    if (item == nullptr || item->parent() == nullptr) return;
    const auto message = item->data(0, kMessageRole).toString();
    const auto path = item->data(0, kPathRole).toString();
    QMenu menu(this);
    auto* copyMessage = menu.addAction(QStringLiteral("Copy message"));
    auto* copyPath = menu.addAction(QStringLiteral("Copy path"));
    copyPath->setEnabled(!path.isEmpty());
    const auto* selected = menu.exec(tree_->viewport()->mapToGlobal(position));
    if (selected == copyMessage) QApplication::clipboard()->setText(message);
    else if (selected == copyPath) QApplication::clipboard()->setText(path);
}

} // namespace skewer::qt
