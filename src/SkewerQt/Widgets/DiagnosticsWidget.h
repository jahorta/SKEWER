#pragma once

#include "SkewerCore/Diagnostics.h"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <vector>

class QCheckBox;
class QTreeWidget;

namespace skewer::qt {

enum class DiagnosticCategory {
    FieldImport,
    AlxLoad,
    AlxFieldValidation,
    Viewport,
    WorkspacePatch,
    Export,
};

struct DiagnosticGroup {
    DiagnosticCategory category = DiagnosticCategory::FieldImport;
    QString title{};
    std::vector<skewer::core::Diagnostic> diagnostics{};
};

struct DiagnosticSummary {
    std::size_t errors = 0U;
    std::size_t warnings = 0U;
    std::size_t infos = 0U;

    [[nodiscard]] std::size_t total() const noexcept {
        return errors + warnings + infos;
    }
};

class DiagnosticsWidget final : public QWidget {
public:
    explicit DiagnosticsWidget(QWidget* parent = nullptr);

    void setDiagnostics(const std::vector<DiagnosticGroup>& groups);
    [[nodiscard]] DiagnosticSummary summary() const noexcept;

private:
    void applyFilters();
    void showContextMenu(const QPoint& position);

    QCheckBox* errorsCheck_ = nullptr;
    QCheckBox* warningsCheck_ = nullptr;
    QCheckBox* infosCheck_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    DiagnosticSummary summary_{};
};

} // namespace skewer::qt
