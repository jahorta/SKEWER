#pragma once

#include "SelectorGeometry.h"

#include "SkewerCore/SceneModel.h"
#include "SkewerCore/TriangleKeys.h"

#include <QVariantList>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace skewer::qt {

class SceneAdapter final {
public:
    SceneAdapter() = default;

    void setScene(const skewer::core::SceneModel* scene);
    void refreshScene();
    [[nodiscard]] bool updateTriangleSelectors(
        std::span<const skewer::core::TriangleKey> keys);
    void setVisibility(std::vector<std::uint8_t> visibleBatches);
    void setSelection(const std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess>& selection);

    [[nodiscard]] QVariantList sceneMeshes() const;
    [[nodiscard]] static QVariantList selectorColors();
    [[nodiscard]] const std::vector<std::uint8_t>& visibility() const noexcept;

private:
    struct SceneGeometryEntry {
        std::unique_ptr<SelectorGeometry> geometry{};
        std::vector<RenderVertex> vertices{};
        std::vector<skewer::core::TriangleKey> triangleKeys{};
        std::size_t visibilityIndex = 0;
        bool context = false;
        bool traversalBarrier = false;
        bool doubleSided = true;
    };

    struct TriangleRenderLocation {
        std::size_t entryIndex = 0U;
        std::size_t sceneTriangleIndex = 0U;
        std::size_t firstVertex = 0U;
    };

    void rebuildScene();

    const skewer::core::SceneModel* scene_ = nullptr;
    std::vector<std::uint8_t> visibility_{};
    std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess> selection_{};
    std::vector<SceneGeometryEntry> sceneGeometry_{};
    std::map<skewer::core::TriangleKey, std::vector<TriangleRenderLocation>,
        skewer::core::TriangleKeyLess> triangleLocations_{};
};

} // namespace skewer::qt
