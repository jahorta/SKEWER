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
    const Color color,
    const float normalOffset = 0.0F) {
    const auto normal = faceNormal(triangle);
    for (const auto& point : triangle.positions) {
        vertices.push_back({
            point.x + normal.x * normalOffset,
            point.y + normal.y * normalOffset,
            point.z + normal.z * normalOffset,
            normal.x, normal.y, normal.z,
            color.r, color.g, color.b, color.a,
        });
    }
}

[[nodiscard]] Color contextColor(const skewer::core::ContextObjectKind kind) {
    switch (kind) {
    case skewer::core::ContextObjectKind::Wall:
        return { 0.38F, 0.43F, 0.50F, 0.38F };
    case skewer::core::ContextObjectKind::WallUv:
        return { 0.32F, 0.48F, 0.58F, 0.38F };
    case skewer::core::ContextObjectKind::DoorWall:
        return { 0.62F, 0.45F, 0.25F, 0.48F };
    }
    return { 0.45F, 0.45F, 0.45F, 0.38F };
}

void appendContextVertices(std::vector<RenderVertex>& vertices,
    const skewer::core::SceneContextBatch& batch) {
    const auto color = contextColor(batch.kind);
    vertices.reserve(vertices.size() + batch.vertices.size());
    for (const auto& vertex : batch.vertices) {
        vertices.push_back({
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            color.r, color.g, color.b, color.a,
        });
    }
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
    for (std::size_t index = 0; index < sceneGeometry_.size(); ++index) {
        QVariantMap mesh{};
        mesh.insert(QStringLiteral("geometry"), QVariant::fromValue<QObject*>(sceneGeometry_[index].get()));
        mesh.insert(QStringLiteral("visible"),
            index < visibility_.size() ? visibility_[index] != 0U : true);
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
    sceneGeometry_.reserve(scene_->batches.size() + scene_->contextBatches.size());
    for (const auto& batch : scene_->batches) {
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
        sceneGeometry_.push_back(std::move(geometry));
    }
    for (const auto& batch : scene_->contextBatches) {
        std::vector<RenderVertex> vertices{};
        appendContextVertices(vertices, batch);
        auto geometry = std::make_unique<SelectorGeometry>();
        geometry->setTriangles(vertices);
        QQmlEngine::setObjectOwnership(geometry.get(), QQmlEngine::CppOwnership);
        sceneGeometry_.push_back(std::move(geometry));
    }
}

void SceneAdapter::rebuildSelection() {
    selectionGeometry_.reset();
    if (scene_ == nullptr || selection_.empty()) return;
    std::vector<RenderVertex> vertices{};
    for (const auto& triangle : scene_->triangles) {
        if (selection_.find(triangle.key) == selection_.end()) continue;
        appendTriangle(vertices, triangle, Color{ 1.0F, 0.95F, 0.12F, 1.0F }, scene_->extent * 0.0004F);
    }
    selectionGeometry_ = std::make_unique<SelectorGeometry>();
    selectionGeometry_->setTriangles(vertices);
    QQmlEngine::setObjectOwnership(selectionGeometry_.get(), QQmlEngine::CppOwnership);
}

} // namespace skewer::qt
