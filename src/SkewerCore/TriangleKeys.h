#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <variant>

namespace skewer::core {

struct GrndTriangleKey {
    std::uint32_t resourceAddress = 0;
    std::size_t triangleIndex = 0;

    bool operator==(const GrndTriangleKey&) const = default;
};

struct GobjTriangleKey {
    std::uint32_t resourceAddress = 0;
    std::size_t nodeIndex = 0;
    std::size_t triangleIndex = 0;

    bool operator==(const GobjTriangleKey&) const = default;
};

using TriangleKey = std::variant<GrndTriangleKey, GobjTriangleKey>;

struct TriangleKeyLess {
    [[nodiscard]] bool operator()(const TriangleKey& lhs, const TriangleKey& rhs) const noexcept {
        if (lhs.index() != rhs.index()) {
            return lhs.index() < rhs.index();
        }
        if (const auto* l = std::get_if<GrndTriangleKey>(&lhs)) {
            const auto& r = std::get<GrndTriangleKey>(rhs);
            return std::tie(l->resourceAddress, l->triangleIndex) <
                std::tie(r.resourceAddress, r.triangleIndex);
        }
        const auto& l = std::get<GobjTriangleKey>(lhs);
        const auto& r = std::get<GobjTriangleKey>(rhs);
        return std::tie(l.resourceAddress, l.nodeIndex, l.triangleIndex) <
            std::tie(r.resourceAddress, r.nodeIndex, r.triangleIndex);
    }
};

} // namespace skewer::core
