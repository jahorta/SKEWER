#include "DiagnosticsWidget.h"

#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace skewer::qt {
namespace {

[[nodiscard]] QString severityName(const skewer::core::DiagnosticSeverity severity) {
    switch (severity) {
    case skewer::core::DiagnosticSeverity::Error: return QStringLiteral("ERROR");
    case skewer::core::DiagnosticSeverity::Warning: return QStringLiteral("WARNING");
    case skewer::core::DiagnosticSeverity::Info: return QStringLiteral("INFO");
    }
    return QStringLiteral("INFO");
}

} // namespace

DiagnosticsWidget::DiagnosticsWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(2000);
    layout->addWidget(view_);
}

void DiagnosticsWidget::setDiagnostics(
    const std::vector<skewer::core::Diagnostic>& generalDiagnostics,
    const std::vector<skewer::core::Diagnostic>& alxLoadDiagnostics,
    const std::vector<skewer::core::Diagnostic>& alxFieldDiagnostics) {
    view_->clear();
    append(generalDiagnostics);
    append(alxLoadDiagnostics);
    append(alxFieldDiagnostics);
}

void DiagnosticsWidget::append(
    const std::vector<skewer::core::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        auto line = QStringLiteral("%1: %2")
            .arg(severityName(diagnostic.severity), QString::fromStdString(diagnostic.message));
        if (!diagnostic.path.empty()) {
            line += QStringLiteral(" [%1]")
                .arg(QString::fromStdWString(diagnostic.path.wstring()));
        }
        view_->appendPlainText(line);
    }
}

} // namespace skewer::qt
