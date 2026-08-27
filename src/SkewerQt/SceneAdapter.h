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
    void setVisibility(std::vector<std::uint8_t> visibleBatches);
    void setSelection(const std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess>& selection);

    [[nodiscard]] QVariantList sceneMeshes() const;
    [[nodiscard]] QVariantList selectionMeshes() const;
    [[nodiscard]] const std::vector<std::uint8_t>& visibility() const noexcept;

private:
    void rebuildScene();
    void rebuildSelection();

    const skewer::core::SceneModel* scene_ = nullptr;
    std::vector<std::uint8_t> visibility_{};
    std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess> selection_{};
    std::vector<std::unique_ptr<SelectorGeometry>> sceneGeometry_{};
    std::unique_ptr<SelectorGeometry> selectionGeometry_{};
};

} // namespace skewer::qt
