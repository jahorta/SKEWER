#include "DiagnosticsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

namespace skewer::qt {
namespace {

constexpr int kSeverityRole = Qt::UserRole;
constexpr int kMessageRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;
constexpr int kCategoryRole = Qt::UserRole + 3;
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

    list_ = new QListWidget(this);
    list_->setWordWrap(false);
    list_->setTextElideMode(Qt::ElideNone);
    list_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list_, 1);

    const auto filterChanged = [this]() { applyFilters(); };
    connect(errorsCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(warningsCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(infosCheck_, &QCheckBox::toggled, this, filterChanged);
    connect(list_, &QListWidget::customContextMenuRequested,
        this, &DiagnosticsWidget::showContextMenu);
}

void DiagnosticsWidget::setDiagnostics(const std::vector<DiagnosticGroup>& groups) {
    if (groups == lastGroups_) return;
    lastGroups_ = groups;
    list_->clear();
    summary_ = {};
    std::size_t rows = 0U;
    for (const auto& group : groups) {
        if (group.diagnostics.empty() || rows >= kMaximumDiagnosticRows) continue;
        for (const auto& diagnostic : group.diagnostics) {
            if (rows++ >= kMaximumDiagnosticRows) break;
            const auto severity = severityName(diagnostic.severity);
            const auto message = QString::fromStdString(diagnostic.message);
            const auto path = diagnostic.path.empty()
                ? QString{} : QString::fromStdWString(diagnostic.path.wstring());
            auto text = QStringLiteral("[%1] [%2] %3")
                .arg(severity, group.title, message);
            if (!path.isEmpty()) text += QStringLiteral(" — %1").arg(path);
            auto* item = new QListWidgetItem(text, list_);
            item->setToolTip(text);
            item->setData(kSeverityRole, static_cast<int>(diagnostic.severity));
            item->setData(kMessageRole, message);
            item->setData(kPathRole, path);
            item->setData(kCategoryRole, static_cast<int>(group.category));
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
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        const auto severity = static_cast<skewer::core::DiagnosticSeverity>(
            item->data(kSeverityRole).toInt());
        const bool visible =
            (severity == skewer::core::DiagnosticSeverity::Error && errorsCheck_->isChecked()) ||
            (severity == skewer::core::DiagnosticSeverity::Warning && warningsCheck_->isChecked()) ||
            (severity == skewer::core::DiagnosticSeverity::Info && infosCheck_->isChecked());
        item->setHidden(!visible);
    }
}

void DiagnosticsWidget::showContextMenu(const QPoint& position) {
    auto* item = list_->itemAt(position);
    if (item == nullptr) return;
    const auto message = item->data(kMessageRole).toString();
    const auto path = item->data(kPathRole).toString();
    QMenu menu(this);
    auto* copyMessage = menu.addAction(QStringLiteral("Copy message"));
    auto* copyPath = menu.addAction(QStringLiteral("Copy path"));
    copyPath->setEnabled(!path.isEmpty());
    const auto* selected = menu.exec(list_->viewport()->mapToGlobal(position));
    if (selected == copyMessage) QApplication::clipboard()->setText(message);
    else if (selected == copyPath) QApplication::clipboard()->setText(path);
}

} // namespace skewer::qt
