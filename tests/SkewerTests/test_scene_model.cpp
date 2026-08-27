#include "SkewerCore/SceneModel.h"

#include <gtest/gtest.h>

#include <memory>

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
