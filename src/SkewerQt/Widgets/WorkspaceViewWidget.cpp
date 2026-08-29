#include "WorkspaceViewWidget.h"

#include "../Viewport/ViewportWidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace skewer::qt {

WorkspaceViewWidget::WorkspaceViewWidget(
    ViewportWidget* viewport,
    DiagnosticsWidget* diagnostics,
    QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(viewport);

    panel_ = new QFrame(this);
    panel_->setFrameShape(QFrame::StyledPanel);
    panel_->setAutoFillBackground(true);
    auto* panelLayout = new QVBoxLayout(panel_);
    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Diagnostics"), panel_);
    auto* closeButton = new QToolButton(panel_);
    closeButton->setText(QStringLiteral("\u00D7"));
    closeButton->setToolTip(QStringLiteral("Close Diagnostics"));
    closeButton->setAccessibleName(QStringLiteral("Close Diagnostics"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(closeButton);
    panelLayout->addLayout(header);
    panelLayout->addWidget(diagnostics, 1);

    handle_ = new QToolButton(this);
    handle_->setFixedSize(32, 72);
    handle_->setToolTip(QStringLiteral("Show Diagnostics"));
    handle_->setAccessibleName(QStringLiteral("Diagnostics"));

    connect(handle_, &QToolButton::clicked, this, [this]() {
        setDiagnosticsExpanded(!expanded_);
    });
    connect(closeButton, &QToolButton::clicked, this, [this]() {
        setDiagnosticsExpanded(false);
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), panel_);
    escape->setContext(Qt::WindowShortcut);
    connect(escape, &QShortcut::activated, this, [this]() {
        setDiagnosticsExpanded(false);
    });

    panel_->setVisible(false);
    updateHandle();
    positionDrawer();
}

void WorkspaceViewWidget::setDiagnosticsSummary(const DiagnosticSummary& summary) {
    summary_ = summary;
    updateHandle();
}

void WorkspaceViewWidget::setDiagnosticsExpanded(const bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    panel_->setVisible(expanded_);
    positionDrawer();
    updateHandle();
    emit diagnosticsExpandedChanged(expanded_);
}

bool WorkspaceViewWidget::diagnosticsExpanded() const noexcept {
    return expanded_;
}

void WorkspaceViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionDrawer();
}

void WorkspaceViewWidget::positionDrawer() {
    constexpr int handleWidth = 32;
    const int desiredWidth = std::clamp(
        static_cast<int>(static_cast<double>(width()) * 0.45), 280, 420);
    const int panelWidth = std::max(0, std::min(desiredWidth, width() - handleWidth));
    panel_->setGeometry(width() - panelWidth, 0, panelWidth, height());
    const int handleX = expanded_ ? width() - panelWidth - handleWidth : width() - handleWidth;
    handle_->move(std::max(0, handleX), 12);
    panel_->raise();
    handle_->raise();
}

void WorkspaceViewWidget::updateHandle() {
    QString text = QStringLiteral("\u2713");
    QString tooltip = expanded_ ? QStringLiteral("Hide Diagnostics")
                                : QStringLiteral("Show Diagnostics");
    QString color = QStringLiteral("#455a64");
    if (summary_.errors > 0U) {
        text = QStringLiteral("E\n%1").arg(summary_.errors);
        tooltip += QStringLiteral(" — %1 error(s)").arg(summary_.errors);
        color = QStringLiteral("#b3261e");
    } else if (summary_.warnings > 0U) {
        text = QStringLiteral("W\n%1").arg(summary_.warnings);
        tooltip += QStringLiteral(" — %1 warning(s)").arg(summary_.warnings);
        color = QStringLiteral("#8a5700");
    } else if (summary_.infos > 0U) {
        text = QStringLiteral("I\n%1").arg(summary_.infos);
        tooltip += QStringLiteral(" — %1 informational message(s)").arg(summary_.infos);
        color = QStringLiteral("#28527a");
    } else {
        tooltip += QStringLiteral(" — no messages");
    }
    handle_->setText(text);
    handle_->setToolTip(tooltip);
    handle_->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; color: white; border: none; "
        "border-radius: 3px 0 0 3px; font-weight: bold; }")
        .arg(color));
}

} // namespace skewer::qt
