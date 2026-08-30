#include "SceneAdapter.h"

#include "SelectorPalette.h"

#include <QColor>
#include <QQmlEngine>
#include <QVariantMap>

#include <array>
#include <cmath>

namespace skewer::qt {
namespace {

struct Color {
    float r;
    float g;
    float b;
    float a;
};

[[nodiscard]] constexpr Color renderColor(const SelectorColor color) noexcept {
    constexpr float kChannelMaximum = 255.0F;
    return {
        static_cast<float>(color.red) / kChannelMaximum,
        static_cast<float>(color.green) / kChannelMaximum,
        static_cast<float>(color.blue) / kChannelMaximum,
        1.0F,
    };
}

constexpr std::array<std::array<float, 2>, 3> kTriangleCoordinates = {
    std::array<float, 2>{ 1.0F, 0.0F },
    std::array<float, 2>{ 0.0F, 1.0F },
    std::array<float, 2>{ 0.0F, 0.0F },
};

[[nodiscard]] skewer::core::SceneVec3 subtract(
    const skewer::core::SceneVec3& lhs,
    const skewer::core::SceneVec3& rhs) {
    return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] skewer::core::SceneVec3 cross(
    const skewer::core::SceneVec3& lhs,
    const skewer::core::SceneVec3& rhs) {
    return { lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x };
}

[[nodiscard]] skewer::core::SceneVec3 faceNormal(const skewer::core::SceneTriangle& triangle) {
    auto normal = cross(subtract(triangle.positions[1], triangle.positions[0]),
        subtract(triangle.positions[2], triangle.positions[0]));
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 1.0e-8F) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
    } else {
        normal = { 0.0F, 1.0F, 0.0F };
    }
    return normal;
}

void appendTriangle(std::vector<RenderVertex>& vertices,
    const skewer::core::SceneTriangle& triangle,
    const Color color,
    const bool selected) {
    const auto normal = faceNormal(triangle);
    for (std::size_t corner = 0; corner < triangle.positions.size(); ++corner) {
        const auto& point = triangle.positions[corner];
        vertices.push_back({
            point.x, point.y, point.z,
            normal.x, normal.y, normal.z,
            color.r, color.g, color.b, color.a,
            kTriangleCoordinates[corner][0], kTriangleCoordinates[corner][1],
            selected ? 1.0F : 0.0F, 0.0F,
        });
    }
}

[[nodiscard]] Color contextColor(const skewer::core::ContextObjectKind kind) {
    switch (kind) {
    case skewer::core::ContextObjectKind::Wall:
        return { 0.38F, 0.43F, 0.50F, 1.0F };
    case skewer::core::ContextObjectKind::WallUv:
        return { 0.32F, 0.48F, 0.58F, 1.0F };
    case skewer::core::ContextObjectKind::DoorWall:
        return { 0.62F, 0.45F, 0.25F, 1.0F };
    }
    return { 0.45F, 0.45F, 0.45F, 1.0F };
}

void appendContextVertex(std::vector<RenderVertex>& vertices,
    const skewer::core::SceneContextVertex& vertex,
    const Color color,
    const std::size_t corner) {
        vertices.push_back({
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            color.r, color.g, color.b, color.a,
            kTriangleCoordinates[corner][0], kTriangleCoordinates[corner][1],
            0.0F, 0.0F,
        });
}

} // namespace

void SceneAdapter::setScene(const skewer::core::SceneModel* scene) {
    scene_ = scene;
    selection_.clear();
    visibility_.assign(scene_ == nullptr ? 0U : scene_->batches.size() + scene_->contextBatches.size(), 1U);
    rebuildScene();
}

void SceneAdapter::refreshScene() {
    rebuildScene();
}

void SceneAdapter::setVisibility(std::vector<std::uint8_t> visibleBatches) {
    visibility_ = std::move(visibleBatches);
}

void SceneAdapter::setSelection(
    const std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess>& selection) {
    selection_ = selection;
    for (auto& entry : sceneGeometry_) {
        if (entry.context) continue;
        bool changed = false;
        for (std::size_t triangleIndex = 0;
            triangleIndex < entry.triangleKeys.size(); ++triangleIndex) {
            const float selected = selection_.contains(entry.triangleKeys[triangleIndex])
                ? 1.0F : 0.0F;
            const auto firstVertex = triangleIndex * 3U;
            if (entry.vertices[firstVertex].selected == selected) continue;
            changed = true;
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                entry.vertices[firstVertex + corner].selected = selected;
            }
        }
        if (changed) entry.geometry->setTriangles(entry.vertices);
    }
}

QVariantList SceneAdapter::sceneMeshes() const {
    QVariantList result{};
    for (const auto& entry : sceneGeometry_) {
        QVariantMap mesh{};
        mesh.insert(QStringLiteral("geometry"), QVariant::fromValue<QObject*>(entry.geometry.get()));
        mesh.insert(QStringLiteral("visible"),
            entry.visibilityIndex < visibility_.size() ? visibility_[entry.visibilityIndex] != 0U : true);
        mesh.insert(QStringLiteral("context"), entry.context);
        mesh.insert(QStringLiteral("traversalBarrier"), entry.traversalBarrier);
        mesh.insert(QStringLiteral("doubleSided"), entry.doubleSided);
        result.push_back(mesh);
    }
    return result;
}

QVariantList SceneAdapter::selectorColors() {
    QVariantList result{};
    result.reserve(8);
    for (std::size_t selector = 1; selector <= 8; ++selector) {
        const auto& color = kSelectorPalette[selector];
        result.push_back(QColor::fromRgb(color.red, color.green, color.blue));
    }
    return result;
}

const std::vector<std::uint8_t>& SceneAdapter::visibility() const noexcept {
    return visibility_;
}

void SceneAdapter::rebuildScene() {
    sceneGeometry_.clear();
    if (scene_ == nullptr) return;
    sceneGeometry_.reserve(scene_->batches.size() + scene_->contextBatches.size() * 2U);
    for (std::size_t batchIndex = 0; batchIndex < scene_->batches.size(); ++batchIndex) {
        const auto& batch = scene_->batches[batchIndex];
        std::array<std::vector<RenderVertex>, 2> verticesByTraversal{};
        std::array<std::vector<skewer::core::TriangleKey>, 2> keysByTraversal{};
        for (const auto triangleIndex : batch.triangleIndices) {
            const auto& triangle = scene_->triangles[triangleIndex];
            const auto metadata = skewer::core::interpretTriangleMetadata(
                triangle.rawMetadata[2], batch.instance.referenceRole);
            const bool traversalBarrier = metadata.traversal ==
                skewer::core::TraversalClassification::BarrierMaskPresent;
            const auto traversalIndex = traversalBarrier ? 1U : 0U;
            auto& vertices = verticesByTraversal[traversalIndex];
            vertices.reserve(vertices.size() + 3U);
            keysByTraversal[traversalIndex].push_back(triangle.key);
            const auto paletteIndex = triangle.selector <= 8U ? triangle.selector : 9U;
            appendTriangle(vertices, triangle,
                renderColor(kSelectorPalette[paletteIndex]),
                selection_.contains(triangle.key));
        }
        for (std::size_t traversal = 0;
            traversal < verticesByTraversal.size(); ++traversal) {
            if (verticesByTraversal[traversal].empty()) continue;
            SceneGeometryEntry entry{};
            entry.geometry = std::make_unique<SelectorGeometry>();
            entry.vertices = std::move(verticesByTraversal[traversal]);
            entry.triangleKeys = std::move(keysByTraversal[traversal]);
            entry.visibilityIndex = batchIndex;
            entry.traversalBarrier = traversal != 0U;
            entry.geometry->setTriangles(entry.vertices);
            QQmlEngine::setObjectOwnership(entry.geometry.get(), QQmlEngine::CppOwnership);
            sceneGeometry_.push_back(std::move(entry));
        }
    }
    for (std::size_t batchIndex = 0; batchIndex < scene_->contextBatches.size(); ++batchIndex) {
        const auto& batch = scene_->contextBatches[batchIndex];
        std::array<std::vector<RenderVertex>, 2> verticesBySidedness{};
        const auto color = contextColor(batch.kind);
        for (std::size_t triangleIndex = 0; triangleIndex < batch.triangleCount(); ++triangleIndex) {
            const bool doubleSided = triangleIndex >= batch.triangleDoubleSided.size() ||
                batch.triangleDoubleSided[triangleIndex] != 0U;
            auto& vertices = verticesBySidedness[doubleSided ? 1U : 0U];
            vertices.reserve(vertices.size() + 3U);
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                appendContextVertex(
                    vertices, batch.vertices[triangleIndex * 3U + corner], color, corner);
            }
        }
        for (std::size_t sidedness = 0; sidedness < verticesBySidedness.size(); ++sidedness) {
            if (verticesBySidedness[sidedness].empty()) continue;
            SceneGeometryEntry entry{};
            entry.geometry = std::make_unique<SelectorGeometry>();
            entry.vertices = std::move(verticesBySidedness[sidedness]);
            entry.visibilityIndex = scene_->batches.size() + batchIndex;
            entry.context = true;
            entry.doubleSided = sidedness != 0U;
            entry.geometry->setTriangles(entry.vertices);
            QQmlEngine::setObjectOwnership(entry.geometry.get(), QQmlEngine::CppOwnership);
            sceneGeometry_.push_back(std::move(entry));
        }
    }
}

} // namespace skewer::qt
