#pragma once

#include "SkewerCore/AlxEnrichment.h"
#include "SkewerCore/FieldDocument.h"

#include <QWidget>

#include <optional>
#include <vector>

class QLabel;
class QSpinBox;
class QTabBar;
class QTableWidget;
class QTableWidgetItem;

namespace skewer::qt {

class EncounterEditorWidget final : public QWidget {
    Q_OBJECT

public:
    explicit EncounterEditorWidget(QWidget* parent = nullptr);

    void showTable(
        const skewer::core::FieldDocument* document,
        int tableIndex,
        const std::vector<std::optional<skewer::core::FormationResolution>>& formations);
    void selectTable(int tableIndex);
    void restoreTable(int tableIndex);
    void setWritable(bool writable);

    [[nodiscard]] int currentTableIndex() const;
    [[nodiscard]] int currentRowIndex() const;

signals:
    void tableSelectionChanged(int tableIndex);
    void encounterSelectionChanged(int tableIndex, int rowIndex);
    void valueEditRequested(const skewer::core::EctValueKey& key, const QString& text);

private:
    void onEncounterItemChanged(QTableWidgetItem* item);

    QTabBar* tableTabs_ = nullptr;
    QLabel* tableHeader_ = nullptr;
    QSpinBox* stageEditor_ = nullptr;
    QSpinBox* overallRateEditor_ = nullptr;
    QTableWidget* encounterTable_ = nullptr;
    bool updating_ = false;
};

} // namespace skewer::qt
