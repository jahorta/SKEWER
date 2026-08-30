#pragma once

#include "SelectorGeometry.h"

#include "SkewerCore/SceneModel.h"
#include "SkewerCore/TriangleKeys.h"

#include <QVariantList>

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace skewer::qt {

class SceneAdapter final {
public:
    SceneAdapter() = default;

    void setScene(const skewer::core::SceneModel* scene);
    void refreshScene();
    void setVisibility(std::vector<std::uint8_t> visibleBatches);
    void setSelection(const std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess>& selection);

    [[nodiscard]] QVariantList sceneMeshes() const;
    [[nodiscard]] QVariantList selectionMeshes() const;
    [[nodiscard]] static QVariantList selectorColors();
    [[nodiscard]] const std::vector<std::uint8_t>& visibility() const noexcept;

private:
    struct SceneGeometryEntry {
        std::unique_ptr<SelectorGeometry> geometry{};
        std::size_t visibilityIndex = 0;
        bool context = false;
        bool doubleSided = true;
    };

    void rebuildScene();
    void rebuildSelection();

    const skewer::core::SceneModel* scene_ = nullptr;
    std::vector<std::uint8_t> visibility_{};
    std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess> selection_{};
    std::vector<SceneGeometryEntry> sceneGeometry_{};
    std::unique_ptr<SelectorGeometry> selectionGeometry_{};
};

} // namespace skewer::qt
