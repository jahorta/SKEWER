#include "SceneAdapter.h"

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

constexpr std::array<Color, 10> kPalette = {
    Color{ 0.34F, 0.36F, 0.40F, 0.72F },
    Color{ 0.15F, 0.62F, 0.95F, 0.80F },
    Color{ 0.15F, 0.78F, 0.42F, 0.80F },
    Color{ 0.98F, 0.73F, 0.18F, 0.80F },
    Color{ 0.92F, 0.28F, 0.31F, 0.80F },
    Color{ 0.65F, 0.35F, 0.91F, 0.80F },
    Color{ 0.08F, 0.77F, 0.77F, 0.80F },
    Color{ 0.95F, 0.43F, 0.78F, 0.80F },
    Color{ 0.72F, 0.82F, 0.24F, 0.80F },
    Color{ 1.00F, 0.00F, 1.00F, 0.90F },
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
    const Color color) {
    const auto normal = faceNormal(triangle);
    for (const auto& point : triangle.positions) {
        vertices.push_back({
            point.x, point.y, point.z,
            normal.x, normal.y, normal.z,
            color.r, color.g, color.b, color.a,
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
    const Color color) {
        vertices.push_back({
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            color.r, color.g, color.b, color.a,
        });
}

} // namespace

void SceneAdapter::setScene(const skewer::core::SceneModel* scene) {
    scene_ = scene;
    selection_.clear();
    visibility_.assign(scene_ == nullptr ? 0U : scene_->batches.size() + scene_->contextBatches.size(), 1U);
    rebuildScene();
    rebuildSelection();
}

void SceneAdapter::refreshScene() {
    rebuildScene();
    rebuildSelection();
}

void SceneAdapter::setVisibility(std::vector<std::uint8_t> visibleBatches) {
    visibility_ = std::move(visibleBatches);
}

void SceneAdapter::setSelection(
    const std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess>& selection) {
    selection_ = selection;
    rebuildSelection();
}

QVariantList SceneAdapter::sceneMeshes() const {
    QVariantList result{};
    for (const auto& entry : sceneGeometry_) {
        QVariantMap mesh{};
        mesh.insert(QStringLiteral("geometry"), QVariant::fromValue<QObject*>(entry.geometry.get()));
        mesh.insert(QStringLiteral("visible"),
            entry.visibilityIndex < visibility_.size() ? visibility_[entry.visibilityIndex] != 0U : true);
        mesh.insert(QStringLiteral("context"), entry.context);
        mesh.insert(QStringLiteral("doubleSided"), entry.doubleSided);
        result.push_back(mesh);
    }
    return result;
}

QVariantList SceneAdapter::selectionMeshes() const {
    QVariantList result{};
    if (selectionGeometry_ != nullptr && !selection_.empty()) {
        QVariantMap mesh{};
        mesh.insert(QStringLiteral("geometry"), QVariant::fromValue<QObject*>(selectionGeometry_.get()));
        result.push_back(mesh);
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
        std::vector<RenderVertex> vertices{};
        vertices.reserve(batch.triangleIndices.size() * 3U);
        for (const auto triangleIndex : batch.triangleIndices) {
            const auto& triangle = scene_->triangles[triangleIndex];
            const auto paletteIndex = triangle.selector <= 8U ? triangle.selector : 9U;
            appendTriangle(vertices, triangle, kPalette[paletteIndex]);
        }
        auto geometry = std::make_unique<SelectorGeometry>();
        geometry->setTriangles(vertices);
        QQmlEngine::setObjectOwnership(geometry.get(), QQmlEngine::CppOwnership);
        sceneGeometry_.push_back({ std::move(geometry), batchIndex, false, true });
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
                appendContextVertex(vertices, batch.vertices[triangleIndex * 3U + corner], color);
            }
        }
        for (std::size_t sidedness = 0; sidedness < verticesBySidedness.size(); ++sidedness) {
            if (verticesBySidedness[sidedness].empty()) continue;
            auto geometry = std::make_unique<SelectorGeometry>();
            geometry->setTriangles(verticesBySidedness[sidedness]);
            QQmlEngine::setObjectOwnership(geometry.get(), QQmlEngine::CppOwnership);
            sceneGeometry_.push_back({ std::move(geometry), scene_->batches.size() + batchIndex,
                true, sidedness != 0U });
        }
    }
}

void SceneAdapter::rebuildSelection() {
    selectionGeometry_.reset();
    if (scene_ == nullptr || selection_.empty()) return;
    std::vector<RenderVertex> vertices{};
    for (const auto& triangle : scene_->triangles) {
        if (selection_.find(triangle.key) == selection_.end()) continue;
        appendTriangle(vertices, triangle, Color{ 1.0F, 0.95F, 0.12F, 1.0F });
    }
    selectionGeometry_ = std::make_unique<SelectorGeometry>();
    selectionGeometry_->setTriangles(vertices);
    QQmlEngine::setObjectOwnership(selectionGeometry_.get(), QQmlEngine::CppOwnership);
}

} // namespace skewer::qt
