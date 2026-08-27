#include "SceneModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace skewer::core {
namespace {

struct Matrix4 {
    std::array<float, 16> m{};
};

[[nodiscard]] Matrix4 identityMatrix() {
    Matrix4 out{};
    out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0F;
    return out;
}

[[nodiscard]] Matrix4 multiply(const Matrix4& lhs, const Matrix4& rhs) {
    Matrix4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t k = 0; k < 4; ++k) {
                out.m[row * 4 + column] += lhs.m[row * 4 + k] * rhs.m[k * 4 + column];
            }
        }
    }
    return out;
}

[[nodiscard]] Matrix4 transformMatrix(const spice::mld::model::Transform& transform) {
    const auto& q = transform.rotation;
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Matrix4 out = identityMatrix();
    out.m[0] = (1.0F - 2.0F * (yy + zz)) * transform.scale.x;
    out.m[1] = (2.0F * (xy - wz)) * transform.scale.y;
    out.m[2] = (2.0F * (xz + wy)) * transform.scale.z;
    out.m[4] = (2.0F * (xy + wz)) * transform.scale.x;
    out.m[5] = (1.0F - 2.0F * (xx + zz)) * transform.scale.y;
    out.m[6] = (2.0F * (yz - wx)) * transform.scale.z;
    out.m[8] = (2.0F * (xz - wy)) * transform.scale.x;
    out.m[9] = (2.0F * (yz + wx)) * transform.scale.y;
    out.m[10] = (1.0F - 2.0F * (xx + yy)) * transform.scale.z;
    out.m[3] = transform.position.x;
    out.m[7] = transform.position.y;
    out.m[11] = transform.position.z;
    return out;
}

[[nodiscard]] SceneVec3 transformPoint(const Matrix4& matrix, const spice::mld::model::Vec3& point) {
    return SceneVec3{
        matrix.m[0] * point.x + matrix.m[1] * point.y + matrix.m[2] * point.z + matrix.m[3],
        matrix.m[4] * point.x + matrix.m[5] * point.y + matrix.m[6] * point.z + matrix.m[7],
        matrix.m[8] * point.x + matrix.m[9] * point.y + matrix.m[10] * point.z + matrix.m[11],
    };
}

void expandBounds(SceneBounds& bounds, const SceneVec3& point) {
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

[[nodiscard]] std::string hexAddress(const std::uint32_t address) {
    std::ostringstream out{};
    out << "0x" << std::hex << std::uppercase;
    out.width(8);
    out.fill('0');
    out << address;
    return out.str();
}

[[nodiscard]] std::vector<Matrix4> nodeMatrices(
    const spice::mld::model::GobjData& gobj,
    const std::uint32_t address,
    std::vector<Diagnostic>& diagnostics) {
    std::vector<std::optional<Matrix4>> memo(gobj.nodes.size());
    std::vector<bool> active(gobj.nodes.size(), false);
    std::function<Matrix4(std::size_t)> resolve = [&](const std::size_t index) -> Matrix4 {
        if (memo[index].has_value()) return *memo[index];
        if (active[index]) {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                "GOBJ " + hexAddress(address) + " contains a transform cycle; identity ancestry was used." });
            return identityMatrix();
        }
        active[index] = true;
        Matrix4 parent = identityMatrix();
        if (const auto parentIndex = gobj.nodes[index].parentNodeIndex; parentIndex.has_value()) {
            if (*parentIndex < gobj.nodes.size()) {
                parent = resolve(*parentIndex);
            } else {
                diagnostics.push_back({ DiagnosticSeverity::Warning,
                    "GOBJ " + hexAddress(address) + " contains an invalid parent-node index." });
            }
        }
        active[index] = false;
        memo[index] = multiply(parent, transformMatrix(gobj.nodes[index].transform));
        return *memo[index];
    };
    std::vector<Matrix4> result(gobj.nodes.size());
    for (std::size_t index = 0; index < gobj.nodes.size(); ++index) result[index] = resolve(index);
    return result;
}

struct ResourceReference {
    std::uint32_t address = 0;
    std::optional<std::size_t> entryTableIndex{};
    Matrix4 entryMatrix = identityMatrix();
    std::string role{};
};

void appendMesh(
    SceneModel& scene,
    const spice::mld::model::MeshData& mesh,
    const Matrix4& matrix,
    const RenderInstanceKey& instance,
    const std::string& label,
    std::vector<Diagnostic>& diagnostics) {
    SceneBatch batch{};
    batch.instance = instance;
    batch.label = label;
    const auto batchIndex = scene.batches.size();
    const auto triangleCount = mesh.indices.size() / 3U;
    if (mesh.indices.size() % 3U != 0U) {
        diagnostics.push_back({ DiagnosticSeverity::Warning, label + " has trailing non-triangle indices." });
    }
    for (std::size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
        const auto i0 = mesh.indices[triangleIndex * 3U + 0U];
        const auto i1 = mesh.indices[triangleIndex * 3U + 1U];
        const auto i2 = mesh.indices[triangleIndex * 3U + 2U];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                label + " contains an out-of-range triangle index at triangle " + std::to_string(triangleIndex) + "." });
            continue;
        }
        SceneTriangle triangle{};
        triangle.batchIndex = batchIndex;
        if (instance.kind == SceneResourceKind::Grnd) {
            triangle.key = GrndTriangleKey{ instance.resourceAddress, triangleIndex };
        } else {
            triangle.key = GobjTriangleKey{ instance.resourceAddress, instance.nodeIndex.value_or(0U), triangleIndex };
        }
        triangle.positions = {
            transformPoint(matrix, mesh.vertices[i0].position),
            transformPoint(matrix, mesh.vertices[i1].position),
            transformPoint(matrix, mesh.vertices[i2].position),
        };
        if (triangleIndex < mesh.triangleMetadata.size()) {
            triangle.rawMetadata = mesh.triangleMetadata[triangleIndex].rawU16;
            triangle.selector = decodeEncounterSelector(triangle.rawMetadata[2]);
        } else {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                label + " has no metadata for triangle " + std::to_string(triangleIndex) + "." });
        }
        const auto globalIndex = scene.triangles.size();
        for (const auto& position : triangle.positions) expandBounds(scene.bounds, position);
        scene.triangles.push_back(std::move(triangle));
        batch.triangleIndices.push_back(globalIndex);
    }
    if (!batch.triangleIndices.empty()) scene.batches.push_back(std::move(batch));
}

} // namespace

std::uint8_t decodeEncounterSelector(const std::uint16_t rawThirdWord) noexcept {
    const auto rawLow15 = static_cast<std::uint16_t>(rawThirdWord & 0x7FFFU);
    return static_cast<std::uint8_t>((rawLow15 / 10U) % 10U);
}

SceneModel buildSceneModel(const spice::mld::model::MldFile& file, std::vector<Diagnostic>& diagnostics) {
    SceneModel scene{};
    std::map<std::uint32_t, std::vector<ResourceReference>> references{};
    for (const auto& record : file.entries) {
        const auto append = [&](const std::shared_ptr<spice::mld::model::U32List>& values, const char* role) {
            if (!values) return;
            for (const auto address : values->values) {
                if (address == 0U || file.groundResources.find(address) == file.groundResources.end()) continue;
                auto& list = references[address];
                const auto duplicate = std::find_if(list.begin(), list.end(), [&](const ResourceReference& ref) {
                    return ref.entryTableIndex == record.entry.tableIndex;
                });
                if (duplicate == list.end()) {
                    list.push_back(ResourceReference{ address, record.entry.tableIndex,
                        transformMatrix(record.entry.transform), role });
                } else if (duplicate->role.find(role) == std::string::npos) {
                    duplicate->role += std::string("+") + role;
                }
            }
        };
        append(record.entry.groundAddresses, "ground");
        append(record.entry.objectAddresses, "object");
    }

    for (const auto& [address, resource] : file.groundResources) {
        auto resourceRefs = references[address];
        if (resourceRefs.empty()) {
            resourceRefs.push_back(ResourceReference{ address, std::nullopt, identityMatrix(), "unreferenced" });
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                "Decoded " + resource.tag + " resource " + hexAddress(address) + " is unreferenced; rendered at identity." });
        }
        for (const auto& reference : resourceRefs) {
            const auto entrySuffix = reference.entryTableIndex.has_value()
                ? " entry=" + std::to_string(*reference.entryTableIndex)
                : " unreferenced";
            if (resource.grnd.has_value()) {
                appendMesh(scene, resource.grnd->mesh, reference.entryMatrix,
                    RenderInstanceKey{ SceneResourceKind::Grnd, address, std::nullopt, reference.entryTableIndex },
                    "GRND " + hexAddress(address) + entrySuffix, diagnostics);
            }
            if (resource.gobj.has_value()) {
                const auto matrices = nodeMatrices(*resource.gobj, address, diagnostics);
                for (std::size_t nodeIndex = 0; nodeIndex < resource.gobj->nodes.size(); ++nodeIndex) {
                    const auto& node = resource.gobj->nodes[nodeIndex];
                    if (node.streamMesh.indices.empty()) continue;
                    appendMesh(scene, node.streamMesh, multiply(reference.entryMatrix, matrices[nodeIndex]),
                        RenderInstanceKey{ SceneResourceKind::Gobj, address, nodeIndex, reference.entryTableIndex },
                        "GOBJ " + hexAddress(address) + " node=" + std::to_string(nodeIndex) + entrySuffix,
                        diagnostics);
                }
            }
        }
    }

    if (!scene.bounds.valid) {
        diagnostics.push_back({ DiagnosticSeverity::Error, "The selected MLD produced no usable GRND/GOBJ triangles." });
        return scene;
    }
    scene.worldOrigin = SceneVec3{
        (scene.bounds.minimum.x + scene.bounds.maximum.x) * 0.5F,
        (scene.bounds.minimum.y + scene.bounds.maximum.y) * 0.5F,
        (scene.bounds.minimum.z + scene.bounds.maximum.z) * 0.5F,
    };
    for (auto& triangle : scene.triangles) {
        for (auto& position : triangle.positions) {
            position.x -= scene.worldOrigin.x;
            position.y -= scene.worldOrigin.y;
            position.z -= scene.worldOrigin.z;
        }
    }
    scene.bounds.minimum.x -= scene.worldOrigin.x;
    scene.bounds.minimum.y -= scene.worldOrigin.y;
    scene.bounds.minimum.z -= scene.worldOrigin.z;
    scene.bounds.maximum.x -= scene.worldOrigin.x;
    scene.bounds.maximum.y -= scene.worldOrigin.y;
    scene.bounds.maximum.z -= scene.worldOrigin.z;
    scene.extent = std::max({
        scene.bounds.maximum.x - scene.bounds.minimum.x,
        scene.bounds.maximum.y - scene.bounds.minimum.y,
        scene.bounds.maximum.z - scene.bounds.minimum.z,
        1.0F,
    });
    return scene;
}

} // namespace skewer::core
