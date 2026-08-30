#include "SkewerCore/TrianglePicker.h"

#include <gtest/gtest.h>

namespace {

skewer::core::SceneTriangle makeTriangle(float z, std::size_t batch,
    skewer::core::TriangleKey key) {
    return { std::move(key), batch,
        { skewer::core::SceneVec3{ -1, -1, z },
          skewer::core::SceneVec3{ 1, -1, z },
          skewer::core::SceneVec3{ 0, 1, z } } };
}

} // namespace

TEST(TrianglePicker, PicksNearestTriangleAndPreservesKeyStyle) {
    skewer::core::SceneModel scene{};
    scene.triangles.push_back(makeTriangle(5.0F, 0,
        skewer::core::GrndTriangleKey{ 0x1000, 3 }));
    scene.triangles.push_back(makeTriangle(2.0F, 1,
        skewer::core::GobjTriangleKey{ 0x2000, 4, 9 }));
    skewer::core::TrianglePicker picker(scene);
    const auto hit = picker.pick({ { 0, 0, 0 }, { 0, 0, 1 } });
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->distance, 2.0F);
    EXPECT_EQ(hit->batchIndex, 1U);
    ASSERT_TRUE(std::holds_alternative<skewer::core::GobjTriangleKey>(hit->key));
    EXPECT_EQ(std::get<skewer::core::GobjTriangleKey>(hit->key).nodeIndex, 4U);
}

TEST(TrianglePicker, IsTwoSidedAndHonorsHiddenBatches) {
    skewer::core::SceneModel scene{};
    scene.triangles.push_back(makeTriangle(2.0F, 0,
        skewer::core::GrndTriangleKey{ 0x1000, 0 }));
    skewer::core::TrianglePicker picker(scene);
    const std::vector<std::uint8_t> hidden{ 0 };
    EXPECT_FALSE(picker.pick({ { 0, 0, 0 }, { 0, 0, 1 } }, hidden).has_value());
    const std::vector<std::uint8_t> visible{ 1 };
    EXPECT_TRUE(picker.pick({ { 0, 0, 4 }, { 0, 0, -1 } }, visible).has_value());
}

TEST(TrianglePicker, ReturnsNoHitForMissOrDegenerateRay) {
    skewer::core::SceneModel scene{};
    scene.triangles.push_back(makeTriangle(2.0F, 0,
        skewer::core::GrndTriangleKey{ 0x1000, 0 }));
    skewer::core::TrianglePicker picker(scene);
    EXPECT_FALSE(picker.pick({ { 4, 4, 0 }, { 0, 0, 1 } }).has_value());
    EXPECT_FALSE(picker.pick({ { 0, 0, 0 }, { 0, 0, 0 } }).has_value());
}
