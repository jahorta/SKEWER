#include "SkewerCore/SceneModel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

spice::mld::model::MeshData triangleMesh(const std::uint16_t metadata) {
    spice::mld::model::MeshData mesh{};
    mesh.vertices = {
        { .position = { 0.0F, 0.0F, 0.0F } },
        { .position = { 1.0F, 0.0F, 0.0F } },
        { .position = { 0.0F, 1.0F, 0.0F } },
    };
    mesh.indices = { 0, 1, 2 };
    mesh.triangleMetadata.push_back({ { 0, 0, metadata } });
    return mesh;
}

spice::mld::model::BlenderIrMesh contextTriangleMesh() {
    using namespace spice::mld::model;
    BlenderIrMesh mesh{};
    mesh.label = "context-triangle";
    mesh.sourceObjectAddress = 0x1200U;
    mesh.vertices = {
        BlenderIrVertex{ .position = { 0.0F, 0.0F, 0.0F }, .normal = { 0.0F, 0.0F, 1.0F }, .hasPosition = true, .hasNormal = true },
        BlenderIrVertex{ .position = { 1.0F, 0.0F, 0.0F }, .normal = { 0.0F, 0.0F, 1.0F }, .hasPosition = true, .hasNormal = true },
        BlenderIrVertex{ .position = { 0.0F, 1.0F, 0.0F }, .normal = { 0.0F, 0.0F, 1.0F }, .hasPosition = true, .hasNormal = true },
    };
    BlenderIrTriangleSet triangles{};
    triangles.corners = {
        BlenderIrCorner{ .vertexIndex = 0U },
        BlenderIrCorner{ .vertexIndex = 1U },
        BlenderIrCorner{ .vertexIndex = 2U },
    };
    mesh.materials.push_back(BlenderIrMaterial{ .doubleSided = false });
    mesh.triangleSets.push_back(std::move(triangles));
    return mesh;
}

spice::mld::model::BlenderIrObjectTree contextTree(const bool childMesh = false) {
    using namespace spice::mld::model;
    BlenderIrObjectTree tree{};
    tree.sourceObjectAddress = 0x1200U;
    BlenderIrNode root{};
    root.localTransform.position.x = 2.0F;
    if (!childMesh) root.meshIndex = 0U;
    tree.nodes.push_back(root);
    tree.rootNodeIndices.push_back(0U);
    if (childMesh) {
        BlenderIrNode child{};
        child.localTransform.position.x = 3.0F;
        child.parentNodeIndex = 0U;
        child.meshIndex = 0U;
        tree.nodes[0].childNodeIndices.push_back(1U);
        tree.nodes.push_back(child);
    }
    return tree;
}

spice::mld::model::BlenderIrInstance contextInstance(
    std::string functionName,
    const std::size_t tableIndex,
    const float x = 0.0F) {
    spice::mld::model::BlenderIrInstance instance{};
    instance.fxnName = std::move(functionName);
    instance.tableIndex = tableIndex;
    instance.sourceEntryId = static_cast<std::uint32_t>(tableIndex + 100U);
    instance.transform.position.x = x;
    instance.objectAddresses = { 0x1200U };
    instance.objectTreeIndices = { 0U };
    return instance;
}

bool diagnosticContains(const std::vector<skewer::core::Diagnostic>& diagnostics, const std::string_view text) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
}

} // namespace

TEST(SceneModel, DecodesEncounterSelectorDigit) {
    EXPECT_EQ(skewer::core::decodeEncounterSelector(0), 0);
    EXPECT_EQ(skewer::core::decodeEncounterSelector(10), 1);
    EXPECT_EQ(skewer::core::decodeEncounterSelector(80), 8);
    EXPECT_EQ(skewer::core::decodeEncounterSelector(0x8000U + 40U), 4);
    EXPECT_EQ(skewer::core::decodeEncounterSelector(90), 9);
}

TEST(SceneModel, ProducesDistinctGrndAndGobjKeysWithTransforms) {
    spice::mld::model::MldFile file{};
    constexpr std::uint32_t grndAddress = 0x1000U;
    constexpr std::uint32_t gobjAddress = 0x2000U;
    spice::mld::model::MldGroundResource grnd{};
    grnd.sourceAddress = grndAddress;
    grnd.tag = "GRND";
    grnd.grnd = spice::mld::model::GrndData{};
    grnd.grnd->mesh = triangleMesh(20);
    file.groundResources.emplace(grndAddress, grnd);
    spice::mld::model::MldGroundResource gobj{};
    gobj.sourceAddress = gobjAddress;
    gobj.tag = "GOBJ";
    gobj.gobj = spice::mld::model::GobjData{};
    spice::mld::model::GobjNode node{};
    node.transform.position = { 5.0F, 0.0F, 0.0F };
    node.streamMesh = triangleMesh(70);
    gobj.gobj->nodes.push_back(node);
    gobj.gobj->rootNodeIndices.push_back(0);
    file.groundResources.emplace(gobjAddress, gobj);
    spice::mld::model::MldIndexEntryRecord entry{};
    entry.entry.tableIndex = 4;
    entry.entry.groundAddresses = std::make_shared<spice::mld::model::U32List>();
    entry.entry.groundAddresses->values = { grndAddress, gobjAddress };
    entry.entry.objectAddresses = std::make_shared<spice::mld::model::U32List>();
    file.entries.push_back(entry);

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto scene = skewer::core::buildSceneModel(file, diagnostics);
    ASSERT_EQ(scene.triangles.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<skewer::core::GrndTriangleKey>(scene.triangles[0].key));
    EXPECT_TRUE(std::holds_alternative<skewer::core::GobjTriangleKey>(scene.triangles[1].key));
    EXPECT_EQ(scene.triangles[0].selector, 2);
    EXPECT_EQ(scene.triangles[1].selector, 7);
    EXPECT_TRUE(scene.bounds.valid);
}

TEST(SceneModel, ProjectsOnlySupportedContextHandlersThroughEntryAndNodeTransforms) {
    spice::mld::model::BlenderIrScene source{};
    source.meshes.push_back(contextTriangleMesh());
    source.objectTrees.push_back(contextTree());
    source.indexEntries = {
        contextInstance(" WALL ", 1U, 10.0F),
        contextInstance("walluv", 2U, 20.0F),
        contextInstance("DoorWall", 3U, 30.0F),
        contextInstance("wallmot", 4U, 40.0F),
        contextInstance("door2", 5U, 50.0F),
    };

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto context = skewer::core::buildContextGeometry(source, diagnostics);
    ASSERT_EQ(context.batches.size(), 3U);
    EXPECT_EQ(context.batches[0].kind, skewer::core::ContextObjectKind::Wall);
    EXPECT_EQ(context.batches[1].kind, skewer::core::ContextObjectKind::WallUv);
    EXPECT_EQ(context.batches[2].kind, skewer::core::ContextObjectKind::DoorWall);
    for (const auto& batch : context.batches) {
        EXPECT_EQ(batch.sourceEntryCount, 1U);
        EXPECT_EQ(batch.triangleCount(), 1U);
    }
    EXPECT_FLOAT_EQ(context.batches[0].vertices[0].position.x, 12.0F);
    EXPECT_FLOAT_EQ(context.batches[1].vertices[0].position.x, 22.0F);
    EXPECT_FLOAT_EQ(context.batches[2].vertices[0].position.x, 32.0F);
    EXPECT_TRUE(context.bounds.valid);
}

TEST(SceneModel, PreservesContextMaterialSidednessPerTriangleSet) {
    spice::mld::model::BlenderIrScene source{};
    auto mesh = contextTriangleMesh();
    mesh.materials.push_back(spice::mld::model::BlenderIrMaterial{ .doubleSided = true });
    auto doubleSidedTriangles = mesh.triangleSets.front();
    doubleSidedTriangles.materialIndex = 1U;
    mesh.triangleSets.push_back(std::move(doubleSidedTriangles));
    source.meshes.push_back(std::move(mesh));
    source.objectTrees.push_back(contextTree());
    source.indexEntries.push_back(contextInstance("wall", 1U));

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto context = skewer::core::buildContextGeometry(source, diagnostics);
    ASSERT_EQ(context.batches.size(), 1U);
    ASSERT_EQ(context.batches[0].triangleDoubleSided.size(), 2U);
    EXPECT_EQ(context.batches[0].triangleDoubleSided[0], 0U);
    EXPECT_EQ(context.batches[0].triangleDoubleSided[1], 1U);
}

TEST(SceneModel, PlacesWeightedContextMeshAtBindRootAndKeepsRepeatedInstances) {
    spice::mld::model::BlenderIrScene source{};
    auto mesh = contextTriangleMesh();
    mesh.weightedBinding = spice::mld::model::BlenderIrWeightedBinding{
        .rootNodeIndex = 0U,
        .sourceNodeIndex = 1U,
        .nodeIndices = { 0U, 1U },
    };
    source.meshes.push_back(std::move(mesh));
    source.objectTrees.push_back(contextTree(true));
    source.indexEntries = {
        contextInstance("wall", 1U, 10.0F),
        contextInstance("wall", 2U, 100.0F),
    };

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto context = skewer::core::buildContextGeometry(source, diagnostics);
    ASSERT_EQ(context.batches.size(), 1U);
    EXPECT_EQ(context.batches[0].sourceEntryCount, 2U);
    EXPECT_EQ(context.batches[0].triangleCount(), 2U);
    EXPECT_FLOAT_EQ(context.batches[0].vertices[0].position.x, 12.0F);
    EXPECT_FLOAT_EQ(context.batches[0].vertices[3].position.x, 102.0F);
}

TEST(SceneModel, SkipsHiddenOrInvalidContextGeometryWithWarnings) {
    spice::mld::model::BlenderIrScene source{};
    auto mesh = contextTriangleMesh();
    mesh.triangleSets[0].corners[2].vertexIndex = 99U;
    source.meshes.push_back(std::move(mesh));
    auto tree = contextTree(true);
    tree.nodes[0].sourceEvalFlags = 1U << 4U;
    source.objectTrees.push_back(std::move(tree));
    source.indexEntries.push_back(contextInstance("wall", 7U));

    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto hidden = skewer::core::buildContextGeometry(source, diagnostics);
    EXPECT_TRUE(hidden.batches.empty());
    EXPECT_TRUE(diagnosticContains(diagnostics, "produced no usable"));

    source.objectTrees[0].nodes[0].sourceEvalFlags = 0U;
    source.objectTrees[0].nodes[1].sourceEvalFlags = 1U << 3U;
    diagnostics.clear();
    const auto skipDraw = skewer::core::buildContextGeometry(source, diagnostics);
    EXPECT_TRUE(skipDraw.batches.empty());
    EXPECT_TRUE(diagnosticContains(diagnostics, "produced no usable"));

    source.objectTrees[0].nodes[1].sourceEvalFlags = 0U;
    diagnostics.clear();
    const auto invalid = skewer::core::buildContextGeometry(source, diagnostics);
    EXPECT_TRUE(invalid.batches.empty());
    EXPECT_TRUE(diagnosticContains(diagnostics, "rejected 1 triangle"));
}
