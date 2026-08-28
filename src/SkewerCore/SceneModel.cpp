#include "SceneModel.h"

#include "SPICE/SpiceMLD/Parsing/Sa3dBlenderIrBuilder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <functional>
#include <map>
#include <numeric>
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

[[nodiscard]] SceneVec3 subtract(const SceneVec3& lhs, const SceneVec3& rhs) {
    return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] SceneVec3 cross(const SceneVec3& lhs, const SceneVec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] SceneVec3 normalize(const SceneVec3& value) {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= 1.0e-12F) return { 0.0F, 1.0F, 0.0F };
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return { value.x * inverseLength, value.y * inverseLength, value.z * inverseLength };
}

[[nodiscard]] SceneVec3 transformNormal(const Matrix4& matrix, const spice::mld::model::Vec3& source) {
    const float a00 = matrix.m[0];
    const float a01 = matrix.m[1];
    const float a02 = matrix.m[2];
    const float a10 = matrix.m[4];
    const float a11 = matrix.m[5];
    const float a12 = matrix.m[6];
    const float a20 = matrix.m[8];
    const float a21 = matrix.m[9];
    const float a22 = matrix.m[10];

    const float cofactor00 = a11 * a22 - a12 * a21;
    const float cofactor01 = a12 * a20 - a10 * a22;
    const float cofactor02 = a10 * a21 - a11 * a20;
    const float determinant = a00 * cofactor00 + a01 * cofactor01 + a02 * cofactor02;
    if (std::abs(determinant) <= 1.0e-8F) return normalize({ source.x, source.y, source.z });

    const float inverseDeterminant = 1.0F / determinant;
    const float inverse00 = cofactor00 * inverseDeterminant;
    const float inverse01 = (a02 * a21 - a01 * a22) * inverseDeterminant;
    const float inverse02 = (a01 * a12 - a02 * a11) * inverseDeterminant;
    const float inverse10 = cofactor01 * inverseDeterminant;
    const float inverse11 = (a00 * a22 - a02 * a20) * inverseDeterminant;
    const float inverse12 = (a02 * a10 - a00 * a12) * inverseDeterminant;
    const float inverse20 = cofactor02 * inverseDeterminant;
    const float inverse21 = (a01 * a20 - a00 * a21) * inverseDeterminant;
    const float inverse22 = (a00 * a11 - a01 * a10) * inverseDeterminant;

    return normalize({
        inverse00 * source.x + inverse10 * source.y + inverse20 * source.z,
        inverse01 * source.x + inverse11 * source.y + inverse21 * source.z,
        inverse02 * source.x + inverse12 * source.y + inverse22 * source.z,
    });
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

void mergeBounds(SceneBounds& destination, const SceneBounds& source) {
    if (!source.valid) return;
    expandBounds(destination, source.minimum);
    expandBounds(destination, source.maximum);
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


[[nodiscard]] std::string normalizedFunctionName(std::string value) {
    const auto nonSpace = [](const unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonSpace).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::optional<ContextObjectKind> contextKind(const std::string& functionName) {
    const auto normalized = normalizedFunctionName(functionName);
    if (normalized == "wall") return ContextObjectKind::Wall;
    if (normalized == "walluv") return ContextObjectKind::WallUv;
    if (normalized == "doorwall") return ContextObjectKind::DoorWall;
    return std::nullopt;
}

[[nodiscard]] std::size_t contextKindIndex(const ContextObjectKind kind) {
    switch (kind) {
    case ContextObjectKind::Wall: return 0U;
    case ContextObjectKind::WallUv: return 1U;
    case ContextObjectKind::DoorWall: return 2U;
    }
    return 0U;
}

[[nodiscard]] std::vector<Matrix4> contextNodeMatrices(
    const spice::mld::model::BlenderIrObjectTree& tree,
    const spice::mld::model::BlenderIrInstance& instance,
    std::vector<Diagnostic>& diagnostics) {
    std::vector<std::optional<Matrix4>> memo(tree.nodes.size());
    std::vector<bool> active(tree.nodes.size(), false);
    std::function<Matrix4(std::size_t)> resolve = [&](const std::size_t index) -> Matrix4 {
        if (memo[index].has_value()) return *memo[index];
        if (active[index]) {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                "Context entry=" + std::to_string(instance.tableIndex) + " contains an object-tree transform cycle; identity ancestry was used." });
            return identityMatrix();
        }
        active[index] = true;
        Matrix4 parent = identityMatrix();
        if (const auto parentIndex = tree.nodes[index].parentNodeIndex; parentIndex.has_value()) {
            if (*parentIndex < tree.nodes.size()) {
                parent = resolve(*parentIndex);
            } else {
                diagnostics.push_back({ DiagnosticSeverity::Warning,
                    "Context entry=" + std::to_string(instance.tableIndex) + " contains an invalid parent-node index." });
            }
        }
        active[index] = false;
        memo[index] = multiply(parent, transformMatrix(tree.nodes[index].localTransform));
        return *memo[index];
    };

    std::vector<Matrix4> result(tree.nodes.size());
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) result[index] = resolve(index);
    return result;
}

[[nodiscard]] bool suppressedByAncestor(
    const spice::mld::model::BlenderIrObjectTree& tree,
    const std::size_t nodeIndex) {
    constexpr std::uint32_t skipChildren = 1U << 4U;
    std::set<std::size_t> visited{};
    auto parent = tree.nodes[nodeIndex].parentNodeIndex;
    while (parent.has_value() && *parent < tree.nodes.size() && visited.insert(*parent).second) {
        if ((tree.nodes[*parent].sourceEvalFlags & skipChildren) != 0U) return true;
        parent = tree.nodes[*parent].parentNodeIndex;
    }
    return false;
}

[[nodiscard]] std::string contextEntryLabel(const spice::mld::model::BlenderIrInstance& instance) {
    return instance.fxnName + " entry=" + std::to_string(instance.tableIndex) +
        " id=" + std::to_string(instance.sourceEntryId);
}

std::size_t appendContextMesh(
    SceneContextBatch& batch,
    SceneBounds& bounds,
    const spice::mld::model::BlenderIrMesh& mesh,
    const Matrix4& world,
    const std::string& entryLabel,
    std::vector<Diagnostic>& diagnostics) {
    std::size_t appendedTriangles = 0U;
    std::size_t invalidTriangles = 0U;
    std::size_t trailingCorners = 0U;
    for (const auto& triangleSet : mesh.triangleSets) {
        bool doubleSided = true;
        if (triangleSet.materialIndex < mesh.materials.size()) {
            doubleSided = mesh.materials[triangleSet.materialIndex].doubleSided;
        } else {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                entryLabel + " mesh=" + mesh.label + " references missing material " +
                    std::to_string(triangleSet.materialIndex) + "; affected triangles were rendered double-sided." });
        }
        trailingCorners += triangleSet.corners.size() % 3U;
        for (std::size_t base = 0; base + 2U < triangleSet.corners.size(); base += 3U) {
            const auto a = triangleSet.corners[base + 0U].vertexIndex;
            const auto b = triangleSet.corners[base + 1U].vertexIndex;
            const auto c = triangleSet.corners[base + 2U].vertexIndex;
            if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size() ||
                !mesh.vertices[a].hasPosition || !mesh.vertices[b].hasPosition || !mesh.vertices[c].hasPosition) {
                ++invalidTriangles;
                continue;
            }

            const std::array<std::uint32_t, 3> indices{ a, b, c };
            std::array<SceneVec3, 3> positions{};
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                positions[corner] = transformPoint(world, mesh.vertices[indices[corner]].position);
            }
            const auto faceNormal = normalize(cross(
                subtract(positions[1], positions[0]), subtract(positions[2], positions[0])));
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                const auto& source = mesh.vertices[indices[corner]];
                const auto normal = source.hasNormal ? transformNormal(world, source.normal) : faceNormal;
                batch.vertices.push_back({ positions[corner], normal });
                expandBounds(bounds, positions[corner]);
            }
            batch.triangleDoubleSided.push_back(doubleSided ? 1U : 0U);
            ++appendedTriangles;
        }
    }

    if (trailingCorners > 0U) {
        diagnostics.push_back({ DiagnosticSeverity::Warning,
            entryLabel + " mesh=" + mesh.label + " has trailing non-triangle corners; they were ignored." });
    }
    if (invalidTriangles > 0U) {
        diagnostics.push_back({ DiagnosticSeverity::Warning,
            entryLabel + " mesh=" + mesh.label + " rejected " + std::to_string(invalidTriangles) +
                " triangle(s) with invalid vertices." });
    }
    return appendedTriangles;
}

} // namespace

std::uint8_t decodeEncounterSelector(const std::uint16_t rawThirdWord) noexcept {
    const auto rawLow15 = static_cast<std::uint16_t>(rawThirdWord & 0x7FFFU);
    return static_cast<std::uint8_t>((rawLow15 / 10U) % 10U);
}

ContextGeometryModel buildContextGeometry(
    const spice::mld::model::BlenderIrScene& scene,
    std::vector<Diagnostic>& diagnostics) {
    ContextGeometryModel result{};
    std::array<SceneContextBatch, 3> batches{
        SceneContextBatch{ .kind = ContextObjectKind::Wall, .label = "Wall", .visibilityId = "context:wall" },
        SceneContextBatch{ .kind = ContextObjectKind::WallUv, .label = "WallUV", .visibilityId = "context:walluv" },
        SceneContextBatch{ .kind = ContextObjectKind::DoorWall, .label = "Doorwall", .visibilityId = "context:doorwall" },
    };
    constexpr std::uint32_t skipDraw = 1U << 3U;

    for (const auto& instance : scene.indexEntries) {
        const auto kind = contextKind(instance.fxnName);
        if (!kind.has_value()) continue;
        auto& batch = batches[contextKindIndex(*kind)];
        const auto entryLabel = contextEntryLabel(instance);
        const auto entryWorld = transformMatrix(instance.transform);
        const auto initialVertexCount = batch.vertices.size();

        for (const auto treeIndex : instance.objectTreeIndices) {
            if (treeIndex >= scene.objectTrees.size()) {
                diagnostics.push_back({ DiagnosticSeverity::Warning,
                    entryLabel + " references missing object tree " + std::to_string(treeIndex) + "." });
                continue;
            }
            const auto& tree = scene.objectTrees[treeIndex];
            if (std::find(instance.objectAddresses.begin(), instance.objectAddresses.end(),
                    tree.sourceObjectAddress) == instance.objectAddresses.end()) {
                continue;
            }
            const auto nodeWorld = contextNodeMatrices(tree, instance, diagnostics);
            for (std::size_t nodeIndex = 0; nodeIndex < tree.nodes.size(); ++nodeIndex) {
                const auto& node = tree.nodes[nodeIndex];
                if (!node.meshIndex.has_value() || (node.sourceEvalFlags & skipDraw) != 0U ||
                    suppressedByAncestor(tree, nodeIndex)) continue;
                if (*node.meshIndex >= scene.meshes.size()) {
                    diagnostics.push_back({ DiagnosticSeverity::Warning,
                        entryLabel + " references missing mesh " + std::to_string(*node.meshIndex) + "." });
                    continue;
                }
                const auto& mesh = scene.meshes[*node.meshIndex];
                std::size_t transformNodeIndex = nodeIndex;
                if (mesh.weightedBinding.has_value()) transformNodeIndex = mesh.weightedBinding->rootNodeIndex;
                if (transformNodeIndex >= nodeWorld.size()) {
                    diagnostics.push_back({ DiagnosticSeverity::Warning,
                        entryLabel + " mesh=" + mesh.label + " has an invalid weighted root node." });
                    continue;
                }
                appendContextMesh(batch, result.bounds, mesh,
                    multiply(entryWorld, nodeWorld[transformNodeIndex]), entryLabel, diagnostics);
            }
        }

        if (batch.vertices.size() == initialVertexCount) {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                entryLabel + " produced no usable bind-pose context geometry." });
        } else {
            ++batch.sourceEntryCount;
        }
    }

    for (auto& batch : batches) {
        if (!batch.vertices.empty()) result.batches.push_back(std::move(batch));
    }
    return result;
}

std::size_t SceneModel::contextTriangleCount() const noexcept {
    return std::accumulate(contextBatches.begin(), contextBatches.end(), std::size_t{ 0 },
        [](const std::size_t count, const SceneContextBatch& batch) { return count + batch.triangleCount(); });
}

std::size_t SceneModel::contextEntryCount() const noexcept {
    return std::accumulate(contextBatches.begin(), contextBatches.end(), std::size_t{ 0 },
        [](const std::size_t count, const SceneContextBatch& batch) { return count + batch.sourceEntryCount; });
}

SceneModel buildSceneModel(
    const spice::mld::model::MldFile& file,
    std::vector<Diagnostic>& diagnostics,
    const SceneBuildOptions& options) {
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

    if (options.includeContext) {
        try {
            const auto blenderIr = spice::mld::parsing::Sa3dBlenderIrBuilder{}.build(file);
            auto context = buildContextGeometry(blenderIr, diagnostics);
            mergeBounds(scene.bounds, context.bounds);
            scene.contextBatches = std::move(context.batches);
        } catch (const std::exception& error) {
            diagnostics.push_back({ DiagnosticSeverity::Warning,
                "Field context geometry could not be built: " + std::string(error.what()) });
        }
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
    for (auto& batch : scene.contextBatches) {
        for (auto& vertex : batch.vertices) {
            vertex.position.x -= scene.worldOrigin.x;
            vertex.position.y -= scene.worldOrigin.y;
            vertex.position.z -= scene.worldOrigin.z;
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
