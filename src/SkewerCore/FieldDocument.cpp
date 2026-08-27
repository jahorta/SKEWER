#include "FieldDocument.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <type_traits>

namespace skewer::core {
namespace {

const spice::ect::EctFlatContent* flatContent(const spice::ect::EctFile& file) {
    return std::get_if<spice::ect::EctFlatContent>(&file.content);
}

spice::ect::EctFlatContent* flatContent(spice::ect::EctFile& file) {
    return std::get_if<spice::ect::EctFlatContent>(&file.content);
}

std::optional<std::uint16_t> readEctValue(const spice::ect::EctFile& file, const EctValueKey& key) {
    const auto* flat = flatContent(file);
    if (flat == nullptr || key.tableIndex >= flat->tables.size()) return std::nullopt;
    const auto& table = flat->tables[key.tableIndex];
    switch (key.kind) {
    case EctValueKind::Stage: return table.stage;
    case EctValueKind::OverallEncounterRate: return table.overallEncounterRate;
    case EctValueKind::EncounterId:
        if (key.rowIndex < table.encounters.size()) return table.encounters[key.rowIndex].encounterId;
        break;
    case EctValueKind::Weight:
        if (key.rowIndex < table.encounters.size()) return table.encounters[key.rowIndex].encounterRate;
        break;
    }
    return std::nullopt;
}

bool sameKey(const TriangleKey& lhs, const TriangleKey& rhs) {
    const TriangleKeyLess less{};
    return !less(lhs, rhs) && !less(rhs, lhs);
}

} // namespace

bool EctValueKey::operator<(const EctValueKey& other) const noexcept {
    return std::tie(tableIndex, rowIndex, kind) < std::tie(other.tableIndex, other.rowIndex, other.kind);
}

std::optional<std::uint8_t> FieldDocument::baselineSelector(const TriangleKey& key) const {
    const auto found = std::find_if(scene.triangles.begin(), scene.triangles.end(),
        [&](const SceneTriangle& triangle) { return sameKey(triangle.key, key); });
    if (found == scene.triangles.end()) return std::nullopt;
    const auto selector = decodeEncounterSelector(found->rawMetadata[2]);
    return selector <= 9U ? std::optional<std::uint8_t>{ selector } : std::nullopt;
}

std::optional<std::uint8_t> FieldDocument::effectiveSelector(const TriangleKey& key) const {
    const auto edit = selectorEdits_.find(key);
    return edit != selectorEdits_.end() ? std::optional<std::uint8_t>{ edit->second } : baselineSelector(key);
}

std::optional<std::uint16_t> FieldDocument::baselineEctValue(const EctValueKey& key) const {
    return readEctValue(ect, key);
}

std::optional<std::uint16_t> FieldDocument::effectiveEctValue(const EctValueKey& key) const {
    return readEctValue(workingEct, key);
}

EditResult FieldDocument::setTriangleSelectors(const std::span<const TriangleKey> keys,
    const std::uint8_t selector, std::string label) {
    EditResult result{};
    if (readOnly) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, readOnlyReason, assets.mldPath });
        return result;
    }
    if (selector > 8U) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "Encounter selector must be between 0 and 8." });
        return result;
    }
    std::set<TriangleKey, TriangleKeyLess> unique(keys.begin(), keys.end());
    EditTransaction transaction{ std::move(label), {} };
    for (const auto& key : unique) {
        const auto baseline = baselineSelector(key);
        const auto current = effectiveSelector(key);
        if (!baseline.has_value() || !current.has_value() || *baseline > 8U) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error,
                "A selected triangle has malformed or unresolved selector metadata.", assets.mldPath });
            return result;
        }
        if (*current != selector) transaction.changes.push_back(TriangleSelectorChange{ key, *current, selector });
    }
    if (transaction.changes.empty()) return result;
    for (const auto& change : transaction.changes) applyChange(change, true);
    pushTransaction(std::move(transaction));
    result.changed = true;
    return result;
}

EditResult FieldDocument::setEctValue(const EctValueKey& key, const std::uint16_t value, std::string label) {
    EditResult result{};
    if (readOnly) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, readOnlyReason, assets.ectPath });
        return result;
    }
    const auto current = effectiveEctValue(key);
    if (!current.has_value()) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "The ECT table or row key is out of range." });
        return result;
    }
    if (*current == value) return result;
    EditTransaction transaction{ std::move(label), { EctValueChange{ key, *current, value } } };
    applyChange(transaction.changes.front(), true);
    pushTransaction(std::move(transaction));
    result.changed = true;
    result.diagnostics = validateWorkingEct();
    return result;
}

EditResult FieldDocument::restoreAll() {
    EditResult result{};
    if (readOnly) return result;
    EditTransaction transaction{ "Restore field to baseline", {} };
    for (const auto& [key, value] : selectorEdits_) {
        const auto baseline = baselineSelector(key);
        if (baseline.has_value()) transaction.changes.push_back(TriangleSelectorChange{ key, value, *baseline });
    }
    for (const auto& [key, value] : ectEdits_) {
        const auto baseline = baselineEctValue(key);
        if (baseline.has_value()) transaction.changes.push_back(EctValueChange{ key, value, *baseline });
    }
    if (transaction.changes.empty()) return result;
    for (const auto& change : transaction.changes) applyChange(change, true);
    pushTransaction(std::move(transaction));
    result.changed = true;
    return result;
}

bool FieldDocument::canUndo() const noexcept { return !undoStack_.empty(); }
bool FieldDocument::canRedo() const noexcept { return !redoStack_.empty(); }

bool FieldDocument::undo() {
    if (undoStack_.empty()) return false;
    auto transaction = std::move(undoStack_.back());
    undoStack_.pop_back();
    for (auto it = transaction.changes.rbegin(); it != transaction.changes.rend(); ++it) applyChange(*it, false);
    redoStack_.push_back(std::move(transaction));
    return true;
}

bool FieldDocument::redo() {
    if (redoStack_.empty()) return false;
    auto transaction = std::move(redoStack_.back());
    redoStack_.pop_back();
    for (const auto& change : transaction.changes) applyChange(change, true);
    undoStack_.push_back(std::move(transaction));
    return true;
}

bool FieldDocument::isDirty() const noexcept { return !selectorEdits_.empty() || !ectEdits_.empty(); }
bool FieldDocument::isTriangleModified(const TriangleKey& key) const { return selectorEdits_.contains(key); }
bool FieldDocument::isEctValueModified(const EctValueKey& key) const { return ectEdits_.contains(key); }
const std::map<TriangleKey, std::uint8_t, TriangleKeyLess>& FieldDocument::selectorEdits() const noexcept { return selectorEdits_; }
const std::map<EctValueKey, std::uint16_t>& FieldDocument::ectEdits() const noexcept { return ectEdits_; }

std::vector<Diagnostic> FieldDocument::validateWorkingEct() const {
    std::vector<Diagnostic> result{};
    const auto* flat = flatContent(workingEct);
    if (flat == nullptr) return result;
    for (std::size_t tableIndex = 0; tableIndex < flat->tables.size(); ++tableIndex) {
        std::uint64_t total = 0;
        bool largeWeight = false;
        for (const auto& row : flat->tables[tableIndex].encounters) {
            total += row.encounterRate;
            largeWeight = largeWeight || row.encounterRate > 100U;
        }
        if (total != 100U) result.push_back({ DiagnosticSeverity::Warning,
            "Encounter table " + std::to_string(tableIndex + 1U) + " has a row-weight total of " +
                std::to_string(total) + " rather than 100.", assets.ectPath });
        if (largeWeight) result.push_back({ DiagnosticSeverity::Warning,
            "Encounter table " + std::to_string(tableIndex + 1U) + " contains a row weight over 100.", assets.ectPath });
    }
    return result;
}

void FieldDocument::clearHistory() { undoStack_.clear(); redoStack_.clear(); }

void FieldDocument::applyChange(const SemanticChange& change, const bool forward) {
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, TriangleSelectorChange>) applySelectorValue(typed.key, forward ? typed.after : typed.before);
        else applyEctValue(typed.key, forward ? typed.after : typed.before);
    }, change);
}

void FieldDocument::applySelectorValue(const TriangleKey& key, const std::uint8_t value) {
    const auto baseline = baselineSelector(key);
    if (baseline.has_value() && *baseline == value) selectorEdits_.erase(key);
    else selectorEdits_[key] = value;
    for (auto& triangle : scene.triangles) if (sameKey(triangle.key, key)) triangle.selector = value;
}

void FieldDocument::applyEctValue(const EctValueKey& key, const std::uint16_t value) {
    auto* flat = flatContent(workingEct);
    if (flat == nullptr || key.tableIndex >= flat->tables.size()) return;
    auto& table = flat->tables[key.tableIndex];
    switch (key.kind) {
    case EctValueKind::Stage: table.stage = value; break;
    case EctValueKind::OverallEncounterRate: table.overallEncounterRate = value; break;
    case EctValueKind::EncounterId:
        if (key.rowIndex < table.encounters.size()) table.encounters[key.rowIndex].encounterId = value;
        break;
    case EctValueKind::Weight:
        if (key.rowIndex < table.encounters.size()) table.encounters[key.rowIndex].encounterRate = value;
        break;
    }
    const auto baseline = baselineEctValue(key);
    if (baseline.has_value() && *baseline == value) ectEdits_.erase(key);
    else ectEdits_[key] = value;
}

void FieldDocument::pushTransaction(EditTransaction transaction) {
    undoStack_.push_back(std::move(transaction));
    redoStack_.clear();
}

} // namespace skewer::core
