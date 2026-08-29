#include "EditReviewDialog.h"

#include "SkewerCore/FieldDocument.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <variant>

namespace skewer::qt {
namespace {

[[nodiscard]] bool sameTriangleKey(
    const skewer::core::TriangleKey& left,
    const skewer::core::TriangleKey& right) {
    const skewer::core::TriangleKeyLess less{};
    return !less(left, right) && !less(right, left);
}

[[nodiscard]] QString patchStateName(const skewer::core::PatchEntryState state) {
    switch (state) {
    case skewer::core::PatchEntryState::Applied: return QStringLiteral("Active edit");
    case skewer::core::PatchEntryState::AlreadyApplied: return QStringLiteral("Already in source");
    case skewer::core::PatchEntryState::Conflict: return QStringLiteral("Conflict");
    case skewer::core::PatchEntryState::Unresolved: return QStringLiteral("Unresolved");
    }
    return QStringLiteral("Retained");
}

[[nodiscard]] QString triangleState(
    const skewer::core::TriangleSelectorPatchEdit& edit,
    const CurrentFieldPatchSnapshot& snapshot,
    const skewer::core::FieldDocument& document) {
    const auto conflict = std::find_if(snapshot.conflicts.begin(), snapshot.conflicts.end(),
        [&](const auto& candidate) {
            return candidate.triangle.has_value() &&
                sameTriangleKey(candidate.triangle->key, edit.key);
        });
    if (conflict != snapshot.conflicts.end()) return patchStateName(conflict->state);
    const auto baseline = document.baselineSelector(edit.key);
    if (baseline.has_value() && *baseline == edit.selector) {
        return QStringLiteral("Already in source");
    }
    return document.isTriangleModified(edit.key)
        ? QStringLiteral("Active edit") : QStringLiteral("Retained");
}

[[nodiscard]] QString ectState(
    const skewer::core::EctValuePatchEdit& edit,
    const CurrentFieldPatchSnapshot& snapshot,
    const skewer::core::FieldDocument& document) {
    const auto conflict = std::find_if(snapshot.conflicts.begin(), snapshot.conflicts.end(),
        [&](const auto& candidate) {
            return candidate.ect.has_value() && candidate.ect->key == edit.key;
        });
    if (conflict != snapshot.conflicts.end()) return patchStateName(conflict->state);
    const auto baseline = document.baselineEctValue(edit.key);
    if (baseline.has_value() && *baseline == edit.value) {
        return QStringLiteral("Already in source");
    }
    return document.isEctValueModified(edit.key)
        ? QStringLiteral("Active edit") : QStringLiteral("Retained");
}

[[nodiscard]] QString resourceName(const skewer::core::TriangleKey& key) {
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        return QStringLiteral("GRND 0x%1")
            .arg(grnd->resourceAddress, 8, 16, QLatin1Char('0'));
    }
    const auto& gobj = std::get<skewer::core::GobjTriangleKey>(key);
    return QStringLiteral("GOBJ 0x%1 node %2")
        .arg(gobj.resourceAddress, 8, 16, QLatin1Char('0')).arg(gobj.nodeIndex);
}

[[nodiscard]] std::size_t triangleIndex(const skewer::core::TriangleKey& key) {
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        return grnd->triangleIndex;
    }
    return std::get<skewer::core::GobjTriangleKey>(key).triangleIndex;
}

[[nodiscard]] QString ectKindName(const skewer::core::EctValueKind kind) {
    switch (kind) {
    case skewer::core::EctValueKind::Stage: return QStringLiteral("Stage");
    case skewer::core::EctValueKind::OverallEncounterRate: return QStringLiteral("Overall rate");
    case skewer::core::EctValueKind::EncounterId: return QStringLiteral("Encounter ID");
    case skewer::core::EctValueKind::Weight: return QStringLiteral("Weight / rate");
    }
    return QStringLiteral("Value");
}

void configureTable(QTableWidget* table) {
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
}

} // namespace

EditReviewDialog::EditReviewDialog(
    const CurrentFieldPatchSnapshot& snapshot,
    const skewer::core::FieldDocument& document,
    QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Review Current Field Changes — %1")
        .arg(QString::fromStdString(snapshot.patch.stem)));
    resize(900, 520);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    auto* triangles = new QTableWidget(
        static_cast<int>(snapshot.patch.triangleSelectorEdits.size()), 5, tabs);
    triangles->setHorizontalHeaderLabels({ QStringLiteral("Resource"),
        QStringLiteral("Triangle"), QStringLiteral("Expected selector"),
        QStringLiteral("Requested selector"), QStringLiteral("State") });
    configureTable(triangles);
    for (int row = 0; row < triangles->rowCount(); ++row) {
        const auto& edit = snapshot.patch.triangleSelectorEdits[static_cast<std::size_t>(row)];
        triangles->setItem(row, 0, new QTableWidgetItem(resourceName(edit.key)));
        triangles->setItem(row, 1, new QTableWidgetItem(
            QString::number(triangleIndex(edit.key))));
        triangles->setItem(row, 2, new QTableWidgetItem(
            QString::number(edit.expectedSelector)));
        triangles->setItem(row, 3, new QTableWidgetItem(
            QString::number(edit.selector)));
        triangles->setItem(row, 4, new QTableWidgetItem(
            triangleState(edit, snapshot, document)));
    }
    tabs->addTab(triangles, QStringLiteral("Triangles (%1)").arg(triangles->rowCount()));

    auto* ect = new QTableWidget(
        static_cast<int>(snapshot.patch.ectValueEdits.size()), 6, tabs);
    ect->setHorizontalHeaderLabels({ QStringLiteral("Table"), QStringLiteral("Field"),
        QStringLiteral("Row"), QStringLiteral("Expected"),
        QStringLiteral("Requested"), QStringLiteral("State") });
    configureTable(ect);
    for (int row = 0; row < ect->rowCount(); ++row) {
        const auto& edit = snapshot.patch.ectValueEdits[static_cast<std::size_t>(row)];
        ect->setItem(row, 0, new QTableWidgetItem(
            QString::number(edit.key.tableIndex + 1U)));
        ect->setItem(row, 1, new QTableWidgetItem(ectKindName(edit.key.kind)));
        const bool hasRow = edit.key.kind == skewer::core::EctValueKind::EncounterId ||
            edit.key.kind == skewer::core::EctValueKind::Weight;
        ect->setItem(row, 2, new QTableWidgetItem(
            hasRow ? QString::number(edit.key.rowIndex) : QStringLiteral("—")));
        ect->setItem(row, 3, new QTableWidgetItem(QString::number(edit.expected)));
        ect->setItem(row, 4, new QTableWidgetItem(QString::number(edit.value)));
        ect->setItem(row, 5, new QTableWidgetItem(ectState(edit, snapshot, document)));
    }
    tabs->addTab(ect, QStringLiteral("ECT (%1)").arg(ect->rowCount()));
    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace skewer::qt
