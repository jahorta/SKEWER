#pragma once

#include "Diagnostics.h"
#include "TriangleKeys.h"

#include "SPICE/SpiceMLD/Model/MldFile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace skewer::core {

struct SceneVec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct SceneBounds {
    SceneVec3 minimum{};
    SceneVec3 maximum{};
    bool valid = false;
};

enum class SceneResourceKind {
    Grnd,
    Gobj,
};

struct RenderInstanceKey {
    SceneResourceKind kind = SceneResourceKind::Grnd;
    std::uint32_t resourceAddress = 0;
    std::optional<std::size_t> nodeIndex{};
    std::optional<std::size_t> entryTableIndex{};

    bool operator==(const RenderInstanceKey&) const = default;
};

struct SceneTriangle {
    TriangleKey key{};
    std::size_t batchIndex = 0;
    std::array<SceneVec3, 3> positions{};
    std::array<std::uint16_t, 3> rawMetadata{};
    std::uint8_t selector = 0xFFU;
};

struct SceneBatch {
    RenderInstanceKey instance{};
    std::string label{};
    std::vector<std::size_t> triangleIndices{};
};

struct SceneModel {
    std::vector<SceneBatch> batches{};
    std::vector<SceneTriangle> triangles{};
    SceneBounds bounds{};
    SceneVec3 worldOrigin{};
    float extent = 1.0F;
};

[[nodiscard]] std::uint8_t decodeEncounterSelector(std::uint16_t rawThirdWord) noexcept;

[[nodiscard]] SceneModel buildSceneModel(
    const spice::mld::model::MldFile& file,
    std::vector<Diagnostic>& diagnostics);

} // namespace skewer::core
