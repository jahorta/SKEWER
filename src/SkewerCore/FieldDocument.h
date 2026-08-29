#pragma once

#include "Diagnostics.h"
#include "FieldDiscovery.h"
#include "SceneModel.h"

#include "SPICE/SpiceEct/EctModel.h"
#include "SPICE/SpiceMLD/Model/MldFile.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace skewer::core {

enum class EctValueKind { Stage, OverallEncounterRate, EncounterId, Weight };

struct EctValueKey {
    EctValueKind kind = EctValueKind::Stage;
    std::size_t tableIndex = 0;
    std::size_t rowIndex = 0;

    bool operator==(const EctValueKey&) const = default;
    bool operator<(const EctValueKey& other) const noexcept;
};

struct TriangleSelectorChange {
    TriangleKey key{};
    std::uint8_t before = 0;
    std::uint8_t after = 0;
};

struct EctValueChange {
    EctValueKey key{};
    std::uint16_t before = 0;
    std::uint16_t after = 0;
};

using SemanticChange = std::variant<TriangleSelectorChange, EctValueChange>;

struct EditTransaction {
    std::string label{};
    std::vector<SemanticChange> changes{};
};

struct EditResult {
    bool changed = false;
    std::vector<Diagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return !hasErrors(diagnostics); }
};

struct FieldDocument {
    FieldAssetPair assets{};
    spice::mld::model::MldFile mld{};
    spice::ect::EctFile ect{};
    spice::ect::EctFile workingEct{};
    SceneModel scene{};
    std::vector<EventGroundPreset> eventGroundPresets{};
    std::vector<Diagnostic> diagnostics{};
    bool readOnly = false;
    std::string readOnlyReason{};

    [[nodiscard]] std::optional<std::uint8_t> baselineSelector(const TriangleKey& key) const;
    [[nodiscard]] std::optional<std::uint8_t> effectiveSelector(const TriangleKey& key) const;
    [[nodiscard]] std::optional<std::uint16_t> baselineEctValue(const EctValueKey& key) const;
    [[nodiscard]] std::optional<std::uint16_t> effectiveEctValue(const EctValueKey& key) const;
    [[nodiscard]] EditResult setTriangleSelectors(std::span<const TriangleKey> keys,
        std::uint8_t selector, std::string label = "Set encounter selector");
    [[nodiscard]] EditResult setEctValue(const EctValueKey& key, std::uint16_t value,
        std::string label = "Edit encounter table");
    [[nodiscard]] EditResult restoreAll();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] bool isTriangleModified(const TriangleKey& key) const;
    [[nodiscard]] bool isEctValueModified(const EctValueKey& key) const;
    [[nodiscard]] std::vector<Diagnostic> validateWorkingEct() const;
    [[nodiscard]] const std::map<TriangleKey, std::uint8_t, TriangleKeyLess>& selectorEdits() const noexcept;
    [[nodiscard]] const std::map<EctValueKey, std::uint16_t>& ectEdits() const noexcept;
    void clearHistory();

private:
    void applyChange(const SemanticChange& change, bool forward);
    void applySelectorValue(const TriangleKey& key, std::uint8_t value);
    void applyEctValue(const EctValueKey& key, std::uint16_t value);
    void pushTransaction(EditTransaction transaction);

    std::map<TriangleKey, std::uint8_t, TriangleKeyLess> selectorEdits_{};
    std::map<EctValueKey, std::uint16_t> ectEdits_{};
    std::vector<EditTransaction> undoStack_{};
    std::vector<EditTransaction> redoStack_{};
};

} // namespace skewer::core
