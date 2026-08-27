#pragma once

#include "Diagnostics.h"
#include "FieldDiscovery.h"
#include "FieldDocument.h"

#include <optional>
#include <vector>

namespace skewer::core {

struct FieldLoadResult {
    std::optional<FieldDocument> document{};
    std::vector<Diagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept {
        return document.has_value() && !hasErrors(diagnostics);
    }
};

class FieldLoader final {
public:
    [[nodiscard]] static FieldLoadResult load(const FieldAssetPair& assets);
};

} // namespace skewer::core
