#pragma once

#include "Diagnostics.h"
#include "TriangleKeys.h"

#include "SPICE/SpiceMLD/Model/BlenderIrModel.h"
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

enum class ContextObjectKind {
    Wall,
    WallUv,
    DoorWall,
};

struct SceneContextVertex {
    SceneVec3 position{};
    SceneVec3 normal{ 0.0F, 1.0F, 0.0F };
};

struct SceneContextBatch {
    ContextObjectKind kind = ContextObjectKind::Wall;
    std::string label{};
    std::string visibilityId{};
    std::size_t sourceEntryCount = 0;
    std::vector<SceneContextVertex> vertices{};
    std::vector<std::uint8_t> triangleDoubleSided{};

    [[nodiscard]] std::size_t triangleCount() const noexcept { return vertices.size() / 3U; }
};

struct ContextGeometryModel {
    std::vector<SceneContextBatch> batches{};
    SceneBounds bounds{};
};

struct SceneBuildOptions {
    bool includeContext = true;
};

struct SceneModel {
    std::vector<SceneBatch> batches{};
    std::vector<SceneTriangle> triangles{};
    std::vector<SceneContextBatch> contextBatches{};
    SceneBounds bounds{};
    SceneVec3 worldOrigin{};
    float extent = 1.0F;

    [[nodiscard]] std::size_t contextTriangleCount() const noexcept;
    [[nodiscard]] std::size_t contextEntryCount() const noexcept;
};

[[nodiscard]] std::uint8_t decodeEncounterSelector(std::uint16_t rawThirdWord) noexcept;

[[nodiscard]] ContextGeometryModel buildContextGeometry(
    const spice::mld::model::BlenderIrScene& scene,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] SceneModel buildSceneModel(
    const spice::mld::model::MldFile& file,
    std::vector<Diagnostic>& diagnostics,
    const SceneBuildOptions& options = {});

} // namespace skewer::core
