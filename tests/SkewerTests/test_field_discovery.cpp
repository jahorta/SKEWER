#include "SkewerCore/FieldDiscovery.h"
#include "SkewerCore/FieldLoader.h"
#include "RealCorpus.h"

#include "SPICE/SpiceMLD/Parsing/MldParser.h"
#include "SPICE/SpiceSCT/SctParser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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

TEST(FieldDiscovery, PairsOptionalSctByFieldStemCaseInsensitively) {
    TempTree tree{};
    tree.file("FIELD/A111C.ECT");
    tree.file("FIELD/A111C.MLD");
    tree.file("FIELD/mE111c.sCt");
    tree.file("FIELD/ME999A.SCT");

    const auto result = skewer::core::FieldDiscovery::discover(tree.path);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.fields.size(), 1U);
    ASSERT_TRUE(result.fields.front().sctPath.has_value());
    EXPECT_EQ(result.fields.front().sctPath->filename().string(), "mE111c.sCt");
    const auto pair = result.fields.front().assetPair();
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->sctPath, result.fields.front().sctPath);
}

TEST(FieldDiscovery, MissingSctDoesNotMakeFieldUnavailable) {
    TempTree tree{};
    tree.file("FIELD/A103B.ECT");
    tree.file("FIELD/A103B.MLD");

    const auto result = skewer::core::FieldDiscovery::discover(tree.path);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.fields.size(), 1U);
    EXPECT_TRUE(result.fields.front().isAvailable());
    EXPECT_FALSE(result.fields.front().sctPath.has_value());
}

class DreamcastFieldImportTest : public ::testing::TestWithParam<const char*> {};

TEST_P(DreamcastFieldImportTest, LoadsOrdinaryEctMldPairWhenCorpusIsPresent) {
    const std::filesystem::path field = LR"(D:\SoADC\SoA(Usa)Disc1Assets\FIELD)";
    const std::string stem = GetParam();
    const auto ect = field / (stem + ".ECT");
    const auto mld = field / (stem + ".MLD");
    if (!std::filesystem::exists(ect) || !std::filesystem::exists(mld)) GTEST_SKIP();
    const auto sctCandidate = field / ("ME" + stem.substr(1) + ".SCT");
    const std::optional<std::filesystem::path> sct = std::filesystem::exists(sctCandidate)
        ? std::optional<std::filesystem::path>{ sctCandidate } : std::nullopt;
    const auto result = skewer::core::FieldLoader::load({ stem, ect, mld, sct });
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
    if (stem == "A103B") {
        const auto hasGrndPair = std::any_of(
            result.document->scene.eventGroundGroups.begin(),
            result.document->scene.eventGroundGroups.end(), [](const auto& group) {
                return group.variants.size() >= 2U &&
                    group.variants[0].resourceKind == skewer::core::SceneResourceKind::Grnd &&
                    group.variants[1].resourceKind == skewer::core::SceneResourceKind::Grnd;
            });
        EXPECT_TRUE(hasGrndPair);
        if (sct.has_value()) {
            EXPECT_TRUE(std::any_of(result.document->eventGroundPresets.begin(),
                result.document->eventGroundPresets.end(), [](const auto& preset) {
                    return std::any_of(preset.assignments.begin(), preset.assignments.end(),
                        [](const auto& assignment) {
                            return assignment.state.kind == skewer::core::EventGroundStateKind::Disabled;
                        });
                }));
        }
    }
    if (stem == "A111C") {
        const auto groupByTblId = [&](const std::int32_t tblId) {
            return std::find_if(result.document->scene.eventGroundGroups.begin(),
                result.document->scene.eventGroundGroups.end(), [tblId](const auto& group) {
                    return group.tblId == tblId;
                });
        };
        const auto tbl4 = groupByTblId(4);
        ASSERT_NE(tbl4, result.document->scene.eventGroundGroups.end());
        ASSERT_GE(tbl4->variants.size(), 2U);
        // Dreamcast-native MLD offsets corresponding to the researched
        // GameCube-US 0x0001B700/0x0001B920 resources.
        EXPECT_EQ(tbl4->variants[0].resourceAddress, 0x0000F0C0U);
        EXPECT_EQ(tbl4->variants[1].resourceAddress, 0x0000F2E0U);
        EXPECT_EQ(tbl4->variants[0].resourceKind, skewer::core::SceneResourceKind::Gobj);
        EXPECT_EQ(tbl4->variants[1].resourceKind, skewer::core::SceneResourceKind::Gobj);
        EXPECT_FALSE(tbl4->variants[0].batchIndices.empty());
        EXPECT_FALSE(tbl4->variants[1].batchIndices.empty());
        const auto tbl13 = groupByTblId(13);
        ASSERT_NE(tbl13, result.document->scene.eventGroundGroups.end());
        ASSERT_GE(tbl13->variants.size(), 2U);
        // Dreamcast-native MLD offsets corresponding to the researched
        // GameCube-US 0x0001EF80/0x0001F260 resources.
        EXPECT_EQ(tbl13->variants[0].resourceAddress, 0x00012940U);
        EXPECT_EQ(tbl13->variants[1].resourceAddress, 0x00012C20U);
        EXPECT_EQ(tbl13->variants[0].resourceKind, skewer::core::SceneResourceKind::Gobj);
        EXPECT_EQ(tbl13->variants[1].resourceKind, skewer::core::SceneResourceKind::Gobj);
        if (sct.has_value()) {
            const auto hasPresetState = [&](const skewer::core::EventGroundGroupKey key,
                                            const std::size_t ordinal) {
                return std::any_of(result.document->eventGroundPresets.begin(),
                    result.document->eventGroundPresets.end(), [&](const auto& preset) {
                        return std::any_of(preset.assignments.begin(), preset.assignments.end(),
                            [&](const auto& assignment) {
                                return assignment.group == key &&
                                    assignment.state == skewer::core::EventGroundState::variant(ordinal);
                            });
                    });
            };
            EXPECT_TRUE(hasPresetState(tbl4->key, 0U));
            EXPECT_TRUE(hasPresetState(tbl4->key, 1U));
            EXPECT_TRUE(hasPresetState(tbl13->key, 0U));
            EXPECT_TRUE(hasPresetState(tbl13->key, 1U));
        }
    }
    if (stem == "A109B" || stem == "A109C" || stem == "A109D") {
        EXPECT_TRUE(std::any_of(result.document->scene.otherGroundGroups.begin(),
            result.document->scene.otherGroundGroups.end(), [](const auto& group) {
                return group.functionName == "tamaue" && !group.resources.empty();
            }));
    }
    if (stem == "A112B") {
        const auto wallUvGroup = [&](const std::size_t entryIndex) {
            return std::find_if(result.document->scene.eventGroundGroups.begin(),
                result.document->scene.eventGroundGroups.end(), [entryIndex](const auto& group) {
                    return group.key.entryTableIndex == entryIndex &&
                        group.functionName == "walluv";
                });
        };
        for (const auto entryIndex : { 50U, 51U }) {
            const auto group = wallUvGroup(entryIndex);
            ASSERT_NE(group, result.document->scene.eventGroundGroups.end());
            EXPECT_EQ(group->tblId, static_cast<std::int32_t>(entryIndex));
            ASSERT_FALSE(group->variants.empty());
            EXPECT_EQ(group->variants[0].resourceKind,
                skewer::core::SceneResourceKind::Gobj);
            EXPECT_EQ(group->variants[0].resourceAddress, 0x000E59C0U);
            EXPECT_TRUE(std::any_of(result.document->eventGroundPresets.begin(),
                result.document->eventGroundPresets.end(), [&](const auto& preset) {
                    return std::any_of(preset.assignments.begin(), preset.assignments.end(),
                        [&](const auto& assignment) {
                            return assignment.group == group->key &&
                                assignment.state == skewer::core::EventGroundState::disabled();
                        });
                }));
        }
        for (const auto functionName : { "fstone", "goscript" }) {
            EXPECT_TRUE(std::any_of(result.document->scene.otherGroundGroups.begin(),
                result.document->scene.otherGroundGroups.end(),
                [functionName](const auto& group) {
                    return group.functionName == functionName && !group.resources.empty();
                }));
        }
    }
    if (stem == "A115B") {
        EXPECT_TRUE(std::any_of(result.document->scene.eventGroundGroups.begin(),
            result.document->scene.eventGroundGroups.end(), [](const auto& group) {
                const bool hasGrnd = std::any_of(group.variants.begin(), group.variants.end(),
                    [](const auto& variant) {
                        return variant.resourceKind == skewer::core::SceneResourceKind::Grnd;
                    });
                const bool hasGobj = std::any_of(group.variants.begin(), group.variants.end(),
                    [](const auto& variant) {
                        return variant.resourceKind == skewer::core::SceneResourceKind::Gobj;
                    });
                return hasGrnd && hasGobj;
            }));
    }
}

TEST(DreamcastFieldImport, A112CWallUvGroupsRemainAtLoaderDefaultWithoutOpcode114Targets) {
    const std::filesystem::path field = LR"(D:\SoADC\SoA(Usa)Disc1Assets\FIELD)";
    const auto mld = field / "A112C.MLD";
    const auto sct = field / "ME112C.SCT";
    if (!std::filesystem::exists(mld) || !std::filesystem::exists(sct)) {
        GTEST_SKIP();
    }

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto mldBytes = readBytes(mld);
    ASSERT_FALSE(mldBytes.empty());
    const auto parsedMld = spice::mld::parsing::MldParser{}.parseBytes(mldBytes);
    std::vector<skewer::core::Diagnostic> diagnostics{};
    const auto scene = skewer::core::buildSceneModel(
        parsedMld, diagnostics, { .includeContext = false });
    const auto sctBytes = readBytes(sct);
    ASSERT_FALSE(sctBytes.empty());
    const auto parsedSct = spice::sct::SctParser{}.parse(sctBytes, sct.string());
    ASSERT_TRUE(parsedSct.parseOk);
    const auto presetResult = skewer::core::buildEventGroundPresets(
        scene.eventGroundGroups, parsedSct);
    for (const auto entryIndex : { 6U, 7U }) {
        const auto group = std::find_if(scene.eventGroundGroups.begin(),
            scene.eventGroundGroups.end(), [entryIndex](const auto& candidate) {
                return candidate.key.entryTableIndex == entryIndex &&
                    candidate.functionName == "walluv";
            });
        ASSERT_NE(group, scene.eventGroundGroups.end());
        ASSERT_FALSE(group->variants.empty());
        EXPECT_FALSE(group->variants[0].batchIndices.empty());
        for (const auto& preset : presetResult.presets) {
            const auto assignment = std::find_if(preset.assignments.begin(),
                preset.assignments.end(), [&](const auto& candidate) {
                    return candidate.group == group->key;
                });
            if (assignment != preset.assignments.end()) {
                EXPECT_EQ(assignment->state, skewer::core::EventGroundState::variant(0U));
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    ExtendedOrdinaryFields,
    DreamcastFieldImportTest,
    ::testing::ValuesIn(skewer::tests::kOrdinaryDreamcastFieldStems),
    [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
