#pragma once

#include "SkewerCore/Diagnostics.h"

#include <QWidget>

#include <vector>

class QPlainTextEdit;

namespace skewer::qt {

class DiagnosticsWidget final : public QWidget {
public:
    explicit DiagnosticsWidget(QWidget* parent = nullptr);

    void setDiagnostics(
        const std::vector<skewer::core::Diagnostic>& generalDiagnostics,
        const std::vector<skewer::core::Diagnostic>& alxLoadDiagnostics,
        const std::vector<skewer::core::Diagnostic>& alxFieldDiagnostics);

private:
    void append(const std::vector<skewer::core::Diagnostic>& diagnostics);

    QPlainTextEdit* view_ = nullptr;
};

} // namespace skewer::qt
