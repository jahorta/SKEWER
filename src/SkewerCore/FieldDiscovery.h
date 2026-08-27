#pragma once

#include "Diagnostics.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace skewer::core {

enum class FieldAvailability {
    Available,
    MissingMld,
    Ambiguous,
    Area99Deferred,
};

struct FieldAssetPair {
    std::string stem{};
    std::filesystem::path ectPath{};
    std::filesystem::path mldPath{};
};

struct FieldCatalogEntry {
    std::string stem{};
    std::filesystem::path ectPath{};
    std::optional<std::filesystem::path> mldPath{};
    FieldAvailability availability = FieldAvailability::MissingMld;
    std::string unavailableReason{};

    [[nodiscard]] bool isAvailable() const noexcept {
        return availability == FieldAvailability::Available && mldPath.has_value();
    }

    [[nodiscard]] std::optional<FieldAssetPair> assetPair() const {
        if (!isAvailable()) {
            return std::nullopt;
        }
        return FieldAssetPair{ stem, ectPath, *mldPath };
    }
};

struct FieldDiscoveryResult {
    std::filesystem::path selectedRoot{};
    std::optional<std::filesystem::path> fieldDirectory{};
    std::vector<std::filesystem::path> fieldDirectoryCandidates{};
    std::vector<FieldCatalogEntry> fields{};
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept {
        return fieldDirectory.has_value() && !hasErrors(diagnostics);
    }
};

class FieldDiscovery final {
public:
    [[nodiscard]] static FieldDiscoveryResult discover(const std::filesystem::path& selectedRoot);
};

} // namespace skewer::core
