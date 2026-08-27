#pragma once

#include "Diagnostics.h"
#include "FieldDocument.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace skewer::core {

struct TriangleSelectorPatchEdit {
    TriangleKey key{};
    std::uint8_t expectedSelector = 0;
    std::uint8_t selector = 0;

    bool operator==(const TriangleSelectorPatchEdit&) const = default;
};

struct EctValuePatchEdit {
    EctValueKey key{};
    std::uint16_t expected = 0;
    std::uint16_t value = 0;

    bool operator==(const EctValuePatchEdit&) const = default;
};

struct FieldPatch {
    static constexpr int kVersion = 1;

    std::string stem{};
    std::string ectFile{};
    std::string mldFile{};
    std::vector<TriangleSelectorPatchEdit> triangleSelectorEdits{};
    std::vector<EctValuePatchEdit> ectValueEdits{};

    [[nodiscard]] bool empty() const noexcept;
    bool operator==(const FieldPatch&) const = default;
};

enum class PatchEntryState { Applied, AlreadyApplied, Conflict, Unresolved };

struct PatchConflict {
    std::optional<TriangleSelectorPatchEdit> triangle{};
    std::optional<EctValuePatchEdit> ect{};
    std::uint16_t current = 0;
    PatchEntryState state = PatchEntryState::Conflict;
    std::string message{};
};

struct PatchRestoreResult {
    std::vector<TriangleSelectorPatchEdit> preservedTriangleEdits{};
    std::vector<EctValuePatchEdit> preservedEctEdits{};
    std::vector<PatchConflict> conflicts{};
    std::size_t appliedCount = 0;
    std::size_t alreadyAppliedCount = 0;
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(diagnostics); }
};

struct FieldPatchReadResult {
    std::optional<FieldPatch> patch{};
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return patch.has_value() && !hasErrors(diagnostics); }
};

[[nodiscard]] FieldPatch makeFieldPatch(const FieldDocument& document,
    std::span<const TriangleSelectorPatchEdit> preservedTriangles = {},
    std::span<const EctValuePatchEdit> preservedEct = {});
[[nodiscard]] std::vector<Diagnostic> validateFieldPatch(const FieldPatch& patch);
[[nodiscard]] PatchRestoreResult restoreFieldPatch(FieldDocument& document, const FieldPatch& patch);
[[nodiscard]] std::string serializeFieldPatch(const FieldPatch& patch);
[[nodiscard]] FieldPatchReadResult parseFieldPatch(std::string_view json,
    const std::filesystem::path& sourcePath = {});

class FieldPatchStore final {
public:
    explicit FieldPatchStore(std::filesystem::path workspaceDirectory);

    [[nodiscard]] std::filesystem::path patchesDirectory() const;
    [[nodiscard]] std::filesystem::path patchPath(std::string_view stem) const;
    [[nodiscard]] FieldPatchReadResult load(std::string_view stem) const;
    [[nodiscard]] std::vector<std::string> listPatchStems(std::vector<Diagnostic>& diagnostics) const;
    [[nodiscard]] bool save(const FieldPatch& patch, std::vector<Diagnostic>& diagnostics) const;
    [[nodiscard]] bool remove(std::string_view stem, std::vector<Diagnostic>& diagnostics) const;

private:
    std::filesystem::path workspaceDirectory_{};
};

} // namespace skewer::core
