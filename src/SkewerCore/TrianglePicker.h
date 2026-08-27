#pragma once

#include "SceneModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace skewer::core {

struct SceneRay {
    SceneVec3 origin{};
    SceneVec3 direction{};
};

struct TriangleHit {
    TriangleKey key{};
    std::size_t batchIndex = 0;
    float distance = 0.0F;
    SceneVec3 point{};
};

class TrianglePicker final {
public:
    TrianglePicker() = default;
    explicit TrianglePicker(const SceneModel& scene);

    void rebuild(const SceneModel& scene);

    [[nodiscard]] std::optional<TriangleHit> pick(
        const SceneRay& ray,
        std::span<const std::uint8_t> visibleBatches = {}) const;

private:
    struct Node {
        SceneBounds bounds{};
        std::size_t first = 0;
        std::size_t count = 0;
        std::size_t left = 0;
        std::size_t right = 0;
        bool leaf = true;
    };

    [[nodiscard]] std::size_t buildNode(std::size_t first, std::size_t count);

    std::vector<SceneTriangle> triangles_{};
    std::vector<std::size_t> order_{};
    std::vector<Node> nodes_{};
};

} // namespace skewer::core
