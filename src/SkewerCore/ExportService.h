#pragma once

#include "Diagnostics.h"
#include "FieldPatch.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace skewer::core {

struct ExportedAsset {
    std::string fieldStem{};
    std::filesystem::path sourcePath{};
    std::filesystem::path basename{};
    std::vector<std::uint8_t> bytes{};
    std::string sourceSha256{};
};

struct ExportPreflightResult {
    std::vector<ExportedAsset> assets{};
    std::vector<std::string> alreadyAppliedEntries{};
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(diagnostics); }
};

struct ExportPublicationResult {
    std::vector<std::filesystem::path> publishedFiles{};
    std::filesystem::path receiptPath{};
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(diagnostics); }
};

class ExportService final {
public:
    [[nodiscard]] static ExportPreflightResult preflight(
        const std::filesystem::path& fieldDirectory,
        std::span<const FieldPatch> patches);
    [[nodiscard]] static ExportPublicationResult publish(
        const ExportPreflightResult& plan,
        const std::filesystem::path& destinationDirectory);
};

} // namespace skewer::core
