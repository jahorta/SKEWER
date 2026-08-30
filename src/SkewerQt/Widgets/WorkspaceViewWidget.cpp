#include "WorkspaceViewWidget.h"

#include "../Viewport/ViewportWidget.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace skewer::qt {
namespace {

constexpr int kHandleWidth = 32;
constexpr int kMinimumPanelWidth = 280;
constexpr int kMaximumRememberedPanelWidth = 4096;

class DrawerHandle final : public QToolButton {
public:
    explicit DrawerHandle(QWidget* parent = nullptr)
        : QToolButton(parent) {}

    void setResizeCallback(std::function<void(int)> callback) {
        resizeCallback_ = std::move(callback);
    }

    void setResizable(const bool resizable) {
        resizable_ = resizable;
        setCursor(resizable ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QToolButton::mousePressEvent(event);
            return;
        }
        pressPosition_ = event->globalPosition().toPoint();
        lastPosition_ = pressPosition_;
        dragging_ = false;
        setDown(true);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if ((event->buttons() & Qt::LeftButton) == 0 || !resizable_) {
            event->accept();
            return;
        }
        const auto position = event->globalPosition().toPoint();
        if (!dragging_ &&
            (position - pressPosition_).manhattanLength() >= QApplication::startDragDistance()) {
            dragging_ = true;
        }
        if (dragging_ && resizeCallback_) {
            resizeCallback_(lastPosition_.x() - position.x());
        }
        lastPosition_ = position;
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QToolButton::mouseReleaseEvent(event);
            return;
        }
        setDown(false);
        if (!dragging_) click();
        event->accept();
    }

private:
    std::function<void(int)> resizeCallback_{};
    QPoint pressPosition_{};
    QPoint lastPosition_{};
    bool dragging_ = false;
    bool resizable_ = false;
};

} // namespace

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

    auto* drawerHandle = new DrawerHandle(this);
    handle_ = drawerHandle;
    handle_->setFixedSize(32, 72);
    handle_->setToolTip(QStringLiteral("Show Diagnostics"));
    handle_->setAccessibleName(QStringLiteral("Diagnostics"));

    connect(drawerHandle, &QToolButton::clicked, this, [this]() {
        setDiagnosticsExpanded(!expanded_);
    });
    drawerHandle->setResizeCallback([this](const int delta) {
        setDiagnosticsWidth(panel_->width() + delta);
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
    static_cast<DrawerHandle*>(handle_)->setResizable(expanded_);
    panel_->setVisible(expanded_);
    positionDrawer();
    updateHandle();
    emit diagnosticsExpandedChanged(expanded_);
}

bool WorkspaceViewWidget::diagnosticsExpanded() const noexcept {
    return expanded_;
}

void WorkspaceViewWidget::setDiagnosticsWidth(const int width) {
    preferredPanelWidth_ = std::clamp(
        width, kMinimumPanelWidth, kMaximumRememberedPanelWidth);
    positionDrawer();
}

int WorkspaceViewWidget::diagnosticsWidth() const noexcept {
    return preferredPanelWidth_;
}

void WorkspaceViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionDrawer();
}

void WorkspaceViewWidget::positionDrawer() {
    const int availableWidth = std::max(0, width() - kHandleWidth);
    const int maximumWidth = std::min(
        availableWidth,
        static_cast<int>(std::floor(static_cast<double>(width()) * 0.45)));
    const int minimumWidth = std::min(kMinimumPanelWidth, maximumWidth);
    const int panelWidth = std::clamp(
        preferredPanelWidth_, minimumWidth, maximumWidth);
    panel_->setGeometry(width() - panelWidth, 0, panelWidth, height());
    const int handleX = expanded_
        ? width() - panelWidth - kHandleWidth
        : width() - kHandleWidth;
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
