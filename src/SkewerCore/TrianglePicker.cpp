#include "TrianglePicker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace skewer::core {
namespace {

[[nodiscard]] SceneVec3 subtract(const SceneVec3& lhs, const SceneVec3& rhs) {
    return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] SceneVec3 add(const SceneVec3& lhs, const SceneVec3& rhs) {
    return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] SceneVec3 scale(const SceneVec3& value, const float amount) {
    return { value.x * amount, value.y * amount, value.z * amount };
}

[[nodiscard]] float dot(const SceneVec3& lhs, const SceneVec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] SceneVec3 cross(const SceneVec3& lhs, const SceneVec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

void expand(SceneBounds& bounds, const SceneVec3& point) {
    if (!bounds.valid) {
        bounds.minimum = bounds.maximum = point;
        bounds.valid = true;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

[[nodiscard]] SceneBounds triangleBounds(const SceneTriangle& triangle) {
    SceneBounds bounds{};
    for (const auto& point : triangle.positions) expand(bounds, point);
    return bounds;
}

[[nodiscard]] SceneVec3 center(const SceneTriangle& triangle) {
    return scale(add(add(triangle.positions[0], triangle.positions[1]), triangle.positions[2]), 1.0F / 3.0F);
}

[[nodiscard]] bool intersectsBounds(const SceneRay& ray, const SceneBounds& bounds, const float maximum) {
    float nearDistance = 0.0F;
    float farDistance = maximum;
    const float origins[] = { ray.origin.x, ray.origin.y, ray.origin.z };
    const float directions[] = { ray.direction.x, ray.direction.y, ray.direction.z };
    const float minima[] = { bounds.minimum.x, bounds.minimum.y, bounds.minimum.z };
    const float maxima[] = { bounds.maximum.x, bounds.maximum.y, bounds.maximum.z };
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) < 1.0e-8F) {
            if (origins[axis] < minima[axis] || origins[axis] > maxima[axis]) return false;
            continue;
        }
        const float inverse = 1.0F / directions[axis];
        float a = (minima[axis] - origins[axis]) * inverse;
        float b = (maxima[axis] - origins[axis]) * inverse;
        if (a > b) std::swap(a, b);
        nearDistance = std::max(nearDistance, a);
        farDistance = std::min(farDistance, b);
        if (nearDistance > farDistance) return false;
    }
    return true;
}

[[nodiscard]] std::optional<float> intersectsTriangle(const SceneRay& ray, const SceneTriangle& triangle) {
    constexpr float epsilon = 1.0e-7F;
    const auto edge1 = subtract(triangle.positions[1], triangle.positions[0]);
    const auto edge2 = subtract(triangle.positions[2], triangle.positions[0]);
    const auto p = cross(ray.direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) < epsilon) return std::nullopt;
    const float inverse = 1.0F / determinant;
    const auto t = subtract(ray.origin, triangle.positions[0]);
    const float u = dot(t, p) * inverse;
    if (u < 0.0F || u > 1.0F) return std::nullopt;
    const auto q = cross(t, edge1);
    const float v = dot(ray.direction, q) * inverse;
    if (v < 0.0F || u + v > 1.0F) return std::nullopt;
    const float distance = dot(edge2, q) * inverse;
    return distance > epsilon ? std::optional<float>{ distance } : std::nullopt;
}

} // namespace

TrianglePicker::TrianglePicker(const SceneModel& scene) {
    rebuild(scene);
}

void TrianglePicker::rebuild(const SceneModel& scene) {
    triangles_ = scene.triangles;
    order_.resize(triangles_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{ 0 });
    nodes_.clear();
    nodes_.reserve(triangles_.size() * 2U);
    if (!triangles_.empty()) {
        static_cast<void>(buildNode(0, triangles_.size()));
    }
}

std::size_t TrianglePicker::buildNode(const std::size_t first, const std::size_t count) {
    Node node{};
    node.first = first;
    node.count = count;
    for (std::size_t i = first; i < first + count; ++i) {
        const auto bounds = triangleBounds(triangles_[order_[i]]);
        expand(node.bounds, bounds.minimum);
        expand(node.bounds, bounds.maximum);
    }
    const auto nodeIndex = nodes_.size();
    nodes_.push_back(node);
    if (count <= 8U) return nodeIndex;

    const float dimensions[] = {
        node.bounds.maximum.x - node.bounds.minimum.x,
        node.bounds.maximum.y - node.bounds.minimum.y,
        node.bounds.maximum.z - node.bounds.minimum.z,
    };
    std::size_t axis = 0;
    if (dimensions[1] > dimensions[axis]) axis = 1;
    if (dimensions[2] > dimensions[axis]) axis = 2;
    const auto coordinate = [axis, this](const std::size_t triangleIndex) {
        const auto value = center(triangles_[triangleIndex]);
        return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
    };
    const auto middle = first + count / 2U;
    std::nth_element(order_.begin() + static_cast<std::ptrdiff_t>(first),
        order_.begin() + static_cast<std::ptrdiff_t>(middle),
        order_.begin() + static_cast<std::ptrdiff_t>(first + count),
        [&](const std::size_t lhs, const std::size_t rhs) { return coordinate(lhs) < coordinate(rhs); });
    const auto left = buildNode(first, middle - first);
    const auto right = buildNode(middle, first + count - middle);
    nodes_[nodeIndex].leaf = false;
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = right;
    return nodeIndex;
}

std::optional<TriangleHit> TrianglePicker::pick(
    const SceneRay& inputRay,
    const std::span<const std::uint8_t> visibleBatches) const {
    if (nodes_.empty()) return std::nullopt;
    const float length = std::sqrt(dot(inputRay.direction, inputRay.direction));
    if (!(length > 1.0e-8F)) return std::nullopt;
    const SceneRay ray{ inputRay.origin, scale(inputRay.direction, 1.0F / length) };
    float nearest = std::numeric_limits<float>::infinity();
    std::optional<TriangleHit> hit{};
    std::vector<std::size_t> stack{ 0U };
    while (!stack.empty()) {
        const auto nodeIndex = stack.back();
        stack.pop_back();
        const auto& node = nodes_[nodeIndex];
        if (!intersectsBounds(ray, node.bounds, nearest)) continue;
        if (!node.leaf) {
            stack.push_back(node.left);
            stack.push_back(node.right);
            continue;
        }
        for (std::size_t i = node.first; i < node.first + node.count; ++i) {
            const auto& triangle = triangles_[order_[i]];
            if (!visibleBatches.empty() &&
                (triangle.batchIndex >= visibleBatches.size() || !visibleBatches[triangle.batchIndex])) {
                continue;
            }
            const auto distance = intersectsTriangle(ray, triangle);
            if (!distance.has_value() || *distance >= nearest) continue;
            nearest = *distance;
            hit = TriangleHit{ triangle.key, triangle.batchIndex, nearest,
                add(ray.origin, scale(ray.direction, nearest)) };
        }
    }
    return hit;
}

} // namespace skewer::core
