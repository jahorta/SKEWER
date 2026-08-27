#include "SkewerCore/FieldDiscovery.h"
#include "SkewerCore/FieldLoader.h"
#include "RealCorpus.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

class TempTree final {
public:
    TempTree() {
        static std::atomic<unsigned long long> sequence{ 0 };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("skewer-tests-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
        std::filesystem::create_directories(path);
    }
    ~TempTree() {
        std::error_code error{};
        std::filesystem::remove_all(path, error);
    }
    void file(const std::filesystem::path& relative, const std::vector<unsigned char>& bytes = { 0 }) const {
        const auto destination = path / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::filesystem::path path{};
};

} // namespace

TEST(FieldDiscovery, CountsSelectedFieldDirectoryAndDisablesUnpairedEntries) {
    TempTree tree{};
    const auto field = tree.path / "FiElD";
    std::filesystem::create_directories(field);
    tree.file("FiElD/A2.EcT");
    tree.file("FiElD/A2.mLd");
    tree.file("FiElD/A10.ECT");
    tree.file("FiElD/A099A.ECT");
    tree.file("FiElD/A099A.MLD");
    tree.file("FiElD/ONLY.MLD");

    const auto result = skewer::core::FieldDiscovery::discover(field);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.fields.size(), 3U);
    EXPECT_EQ(result.fields[0].stem, "a2");
    EXPECT_TRUE(result.fields[0].isAvailable());
    EXPECT_EQ(result.fields[1].stem, "a10");
    EXPECT_EQ(result.fields[1].availability, skewer::core::FieldAvailability::MissingMld);
    EXPECT_EQ(result.fields[2].stem, "a099a");
    EXPECT_EQ(result.fields[2].availability, skewer::core::FieldAvailability::Area99Deferred);
}

TEST(FieldDiscovery, RejectsZeroOrMultipleFieldDirectories) {
    TempTree empty{};
    EXPECT_FALSE(skewer::core::FieldDiscovery::discover(empty.path).ok());

    TempTree multiple{};
    std::filesystem::create_directories(multiple.path / "one/FIELD");
    std::filesystem::create_directories(multiple.path / "two/field");
    const auto result = skewer::core::FieldDiscovery::discover(multiple.path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.fieldDirectoryCandidates.size(), 2U);
}

TEST(FieldDiscovery, RejectsGameCubeAklzEct) {
    TempTree tree{};
    std::filesystem::create_directories(tree.path / "FIELD");
    tree.file("FIELD/a001a.mld");
    tree.file("FIELD/a001a.ect", {
        'A', 'K', 'L', 'Z', '~', '?', 'Q', 'd', '=', 0xCC, 0xCC, 0xCD, 0, 0, 0, 0,
    });
    const auto result = skewer::core::FieldDiscovery::discover(tree.path);
    EXPECT_FALSE(result.ok());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.back().message, "GameCube is not yet supported.");
}

class DreamcastFieldImportTest : public ::testing::TestWithParam<const char*> {};

TEST_P(DreamcastFieldImportTest, LoadsOrdinaryEctMldPairWhenCorpusIsPresent) {
    const std::filesystem::path field = LR"(D:\SoADC\SoA(Usa)Disc1Assets\FIELD)";
    const std::string stem = GetParam();
    const auto ect = field / (stem + ".ECT");
    const auto mld = field / (stem + ".MLD");
    if (!std::filesystem::exists(ect) || !std::filesystem::exists(mld)) GTEST_SKIP();
    const auto result = skewer::core::FieldLoader::load({ stem, ect, mld });
    ASSERT_TRUE(result.ok()) << (result.diagnostics.empty() ? "" : result.diagnostics.back().message);
    ASSERT_TRUE(result.document.has_value());
    EXPECT_EQ(result.document->mld.sourcePlatform, spice::mld::model::TargetPlatform::Dreamcast);
    EXPECT_FALSE(result.document->scene.batches.empty());
    EXPECT_FALSE(result.document->scene.triangles.empty());
    EXPECT_TRUE(result.document->scene.bounds.valid);
    const auto* flat = std::get_if<spice::ect::EctFlatContent>(&result.document->ect.content);
    ASSERT_NE(flat, nullptr);
    EXPECT_EQ(flat->tables.size(), 8U);

    std::size_t grndBatches = 0;
    std::size_t gobjBatches = 0;
    for (const auto& batch : result.document->scene.batches) {
        if (batch.instance.kind == skewer::core::SceneResourceKind::Grnd) ++grndBatches;
        else ++gobjBatches;
    }
    std::cout << stem << ": " << result.document->scene.triangles.size()
              << " triangles, " << grndBatches << " GRND batches, "
              << gobjBatches << " GOBJ batches, "
              << result.document->scene.contextTriangleCount() << " context triangles from "
              << result.document->scene.contextEntryCount() << " entries\n";

    if (stem == "A035B") {
        const auto hasKind = [&](const skewer::core::ContextObjectKind kind) {
            return std::any_of(result.document->scene.contextBatches.begin(),
                result.document->scene.contextBatches.end(),
                [&](const skewer::core::SceneContextBatch& batch) {
                    return batch.kind == kind && batch.sourceEntryCount > 0U && batch.triangleCount() > 0U;
                });
        };
        EXPECT_TRUE(hasKind(skewer::core::ContextObjectKind::Wall));
        EXPECT_TRUE(hasKind(skewer::core::ContextObjectKind::WallUv));
        EXPECT_TRUE(hasKind(skewer::core::ContextObjectKind::DoorWall));
    }
}

INSTANTIATE_TEST_SUITE_P(
    ExtendedOrdinaryFields,
    DreamcastFieldImportTest,
    ::testing::ValuesIn(skewer::tests::kOrdinaryDreamcastFieldStems),
    [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
