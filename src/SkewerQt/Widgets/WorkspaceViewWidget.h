#pragma once

#include "DiagnosticsWidget.h"

#include <QWidget>

class QFrame;
class QResizeEvent;
class QToolButton;

namespace skewer::qt {

class DiagnosticsWidget;
class ViewportWidget;

class WorkspaceViewWidget final : public QWidget {
    Q_OBJECT

public:
    WorkspaceViewWidget(
        ViewportWidget* viewport,
        DiagnosticsWidget* diagnostics,
        QWidget* parent = nullptr);

    void setDiagnosticsSummary(const DiagnosticSummary& summary);
    void setDiagnosticsExpanded(bool expanded);
    [[nodiscard]] bool diagnosticsExpanded() const noexcept;

signals:
    void diagnosticsExpandedChanged(bool expanded);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void positionDrawer();
    void updateHandle();

    QFrame* panel_ = nullptr;
    QToolButton* handle_ = nullptr;
    DiagnosticSummary summary_{};
    bool expanded_ = false;
};

} // namespace skewer::qt
