#include "SkewerCore/ExportService.h"
#include "SkewerCore/FieldDocument.h"
#include "SkewerCore/FieldLoader.h"
#include "SkewerCore/FieldPatch.h"
#include "RealCorpus.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>

namespace {

class TempDirectory final {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> sequence{ 0 };
        path = std::filesystem::temp_directory_path() /
            ("skewer-authoring-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence++));
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() { std::error_code error{}; std::filesystem::remove_all(path, error); }
    std::filesystem::path path{};
};

skewer::core::FieldDocument documentWithTwoTriangles(std::uint8_t firstSelector = 1) {
    skewer::core::FieldDocument document{};
    document.assets = { "a001a", "a001a.ect", "a001a.mld" };
    document.scene.triangles = {
        { skewer::core::GrndTriangleKey{ 0x1000U, 0U }, 0U, {}, { 0U, 0U, static_cast<std::uint16_t>(firstSelector * 10U) }, firstSelector },
        { skewer::core::GobjTriangleKey{ 0x2000U, 2U, 0U }, 0U, {}, { 0U, 0U, 20U }, 2U },
    };
    spice::ect::EctFlatContent flat{};
    flat.tables.resize(8U);
    document.ect.content = flat;
    document.workingEct = document.ect;
    return document;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

} // namespace

TEST(FieldDocumentEditing, BulkSelectorAssignmentIsOneUndoableTransactionAndNineIsRejected) {
    auto document = documentWithTwoTriangles();
    const std::array<skewer::core::TriangleKey, 2> keys{
        document.scene.triangles[0].key, document.scene.triangles[1].key
    };
    const auto changed = document.setTriangleSelectors(keys, 6U);
    ASSERT_TRUE(changed.ok());
    ASSERT_TRUE(changed.changed);
    EXPECT_EQ(document.selectorEdits().size(), 2U);
    EXPECT_EQ(document.scene.triangles[0].selector, 6U);
    EXPECT_TRUE(document.undo());
    EXPECT_EQ(document.scene.triangles[0].selector, 1U);
    EXPECT_EQ(document.scene.triangles[1].selector, 2U);
    EXPECT_FALSE(document.isDirty());
    EXPECT_TRUE(document.redo());
    EXPECT_EQ(document.selectorEdits().size(), 2U);

    const auto malformed = document.setTriangleSelectors(keys, 9U);
    EXPECT_FALSE(malformed.ok());
    EXPECT_FALSE(malformed.changed);
    EXPECT_EQ(document.scene.triangles[0].selector, 6U);
}

TEST(FieldDocumentEditing, EctEditsUseFullUint16RangeAndReturnToBaselineSparsely) {
    auto document = documentWithTwoTriangles();
    const skewer::core::EctValueKey stage{ skewer::core::EctValueKind::Stage, 3U, 0U };
    ASSERT_TRUE(document.setEctValue(stage, 65535U).changed);
    EXPECT_EQ(document.effectiveEctValue(stage), 65535U);
    EXPECT_TRUE(document.isEctValueModified(stage));
    ASSERT_TRUE(document.setEctValue(stage, 0U).changed);
    EXPECT_FALSE(document.isEctValueModified(stage));
}

TEST(FieldPatch, DeterministicRoundTripAndConflictPreservation) {
    auto document = documentWithTwoTriangles();
    const std::array<skewer::core::TriangleKey, 1> key{ document.scene.triangles[0].key };
    ASSERT_TRUE(document.setTriangleSelectors(key, 4U).changed);
    ASSERT_TRUE(document.setEctValue({ skewer::core::EctValueKind::Weight, 1U, 7U }, 25U).changed);
    const auto patch = skewer::core::makeFieldPatch(document);
    const auto json = skewer::core::serializeFieldPatch(patch);
    const auto parsed = skewer::core::parseFieldPatch(json);
    ASSERT_TRUE(parsed.ok()) << (parsed.diagnostics.empty() ? "" : parsed.diagnostics.front().message);
    EXPECT_EQ(*parsed.patch, patch);
    EXPECT_EQ(skewer::core::serializeFieldPatch(*parsed.patch), json);

    auto changedSource = documentWithTwoTriangles(3U);
    const auto restored = skewer::core::restoreFieldPatch(changedSource, patch);
    ASSERT_EQ(restored.conflicts.size(), 1U);
    EXPECT_EQ(changedSource.scene.triangles[0].selector, 3U);
    EXPECT_EQ(restored.preservedTriangleEdits.size(), 1U);
    EXPECT_EQ(changedSource.effectiveEctValue({ skewer::core::EctValueKind::Weight, 1U, 7U }), 25U);
}

TEST(FieldPatch, SelectorNineIsNotSchemaRepresentable) {
    auto document = documentWithTwoTriangles();
    skewer::core::FieldPatch patch{ "a001a", "a001a.ect", "a001a.mld" };
    patch.triangleSelectorEdits.push_back({ document.scene.triangles[0].key, 1U, 9U });
    EXPECT_TRUE(skewer::core::hasErrors(skewer::core::validateFieldPatch(patch)));
    const auto malformed = skewer::core::parseFieldPatch(R"({
      "format":"skewer-field-patch","version":1,
      "field":{"stem":"a001a","platform":"dreamcast","ectFile":"a001a.ect","mldFile":"a001a.mld"},
      "mld":{"triangleSelectorEdits":[{"key":{"kind":"grnd","resourceAddress":"0x00001000","triangleIndex":0},"expectedSelector":1,"selector":9}]},
      "ect":{"valueEdits":[]}
    })");
    EXPECT_FALSE(malformed.ok());
}

TEST(FieldPatchStore, SavesLoadsAndRemovesCanonicalPatch) {
    TempDirectory temp{};
    skewer::core::FieldPatchStore store(temp.path);
    auto document = documentWithTwoTriangles();
    const std::array<skewer::core::TriangleKey, 1> key{ document.scene.triangles[0].key };
    ASSERT_TRUE(document.setTriangleSelectors(key, 5U).changed);
    const auto patch = skewer::core::makeFieldPatch(document);
    std::vector<skewer::core::Diagnostic> diagnostics{};
    ASSERT_TRUE(store.save(patch, diagnostics));
    ASSERT_TRUE(store.load("a001a").ok());
    const auto stems = store.listPatchStems(diagnostics);
    ASSERT_EQ(stems, std::vector<std::string>{ "a001a" });
    ASSERT_TRUE(store.remove("a001a", diagnostics));
    EXPECT_FALSE(std::filesystem::exists(store.patchPath("a001a")));
}

class DreamcastFieldExportTest : public ::testing::TestWithParam<const char*> {};

TEST_P(DreamcastFieldExportTest, ImportsEditsExportsAndReloadsWhenCorpusIsPresent) {
    const std::filesystem::path field = LR"(D:\SoADC\SoA(Usa)Disc1Assets\FIELD)";
    const std::string sourceStem = GetParam();
    std::string stem = sourceStem;
    std::transform(stem.begin(), stem.end(), stem.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const auto ect = field / (sourceStem + ".ECT");
    const auto mld = field / (sourceStem + ".MLD");
    if (!std::filesystem::exists(ect) || !std::filesystem::exists(mld)) GTEST_SKIP();
    auto loaded = skewer::core::FieldLoader::load({ stem, ect, mld });
    ASSERT_TRUE(loaded.ok());
    auto& document = *loaded.document;
    ASSERT_FALSE(document.readOnly) << document.readOnlyReason;
    const auto triangle = std::find_if(document.scene.triangles.begin(), document.scene.triangles.end(),
        [](const auto& value) { return value.selector <= 8U; });
    ASSERT_NE(triangle, document.scene.triangles.end());
    const auto replacement = static_cast<std::uint8_t>((triangle->selector + 1U) % 9U);
    const std::array<skewer::core::TriangleKey, 1> keys{ triangle->key };
    ASSERT_TRUE(document.setTriangleSelectors(keys, replacement).changed);
    const skewer::core::EctValueKey stage{ skewer::core::EctValueKind::Stage, 0U, 0U };
    const auto oldStage = *document.baselineEctValue(stage);
    ASSERT_TRUE(document.setEctValue(stage, static_cast<std::uint16_t>(oldStage == 65535U ? 0U : oldStage + 1U)).changed);
    const auto patch = skewer::core::makeFieldPatch(document);
    const auto sourceEct = readBytes(ect);
    const auto sourceMld = readBytes(mld);
    const auto preflight = skewer::core::ExportService::preflight(field, std::span<const skewer::core::FieldPatch>(&patch, 1U));
    ASSERT_TRUE(preflight.ok()) << (preflight.diagnostics.empty() ? "" : preflight.diagnostics.back().message);
    ASSERT_EQ(preflight.assets.size(), 2U);
    TempDirectory output{};
    const auto publication = skewer::core::ExportService::publish(preflight, output.path);
    ASSERT_TRUE(publication.ok());
    const auto outputEct = output.path / (stem + ".ect");
    const auto outputMld = output.path / (stem + ".mld");
    EXPECT_TRUE(std::filesystem::exists(outputEct));
    EXPECT_TRUE(std::filesystem::exists(outputMld));
    EXPECT_FALSE(publication.receiptPath.empty());
    EXPECT_EQ(readBytes(ect), sourceEct);
    EXPECT_EQ(readBytes(mld), sourceMld);

    const auto reloaded = skewer::core::FieldLoader::load({ stem, outputEct, outputMld });
    ASSERT_TRUE(reloaded.ok()) << (reloaded.diagnostics.empty() ? "" : reloaded.diagnostics.back().message);
    ASSERT_TRUE(reloaded.document.has_value());
    EXPECT_EQ(reloaded.document->baselineSelector(triangle->key), replacement);
    EXPECT_EQ(reloaded.document->baselineEctValue(stage),
        static_cast<std::uint16_t>(oldStage == 65535U ? 0U : oldStage + 1U));
}

INSTANTIATE_TEST_SUITE_P(
    ExtendedFields,
    DreamcastFieldExportTest,
    ::testing::ValuesIn(skewer::tests::kOrdinaryDreamcastFieldStems),
    [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });

TEST(ExportService, RandomTriangleSelectorPatchReloadsAtTheSameSemanticKey) {
    const std::filesystem::path field = LR"(D:\SoADC\SoA(Usa)Disc1Assets\FIELD)";
    const auto ect = field / "A111C.ECT";
    const auto mld = field / "A111C.MLD";
    if (!std::filesystem::exists(ect) || !std::filesystem::exists(mld)) GTEST_SKIP();
    auto loaded = skewer::core::FieldLoader::load({ "a111c", ect, mld });
    ASSERT_TRUE(loaded.ok());
    ASSERT_FALSE(loaded.document->readOnly) << loaded.document->readOnlyReason;
    ASSERT_FALSE(loaded.document->scene.triangles.empty());

    std::mt19937 random{ 0x534B4557U };
    std::uniform_int_distribution<std::size_t> choose(0U, loaded.document->scene.triangles.size() - 1U);
    const auto selectedIndex = choose(random);
    const auto selectedKey = loaded.document->scene.triangles[selectedIndex].key;
    const auto originalSelector = loaded.document->scene.triangles[selectedIndex].selector;
    ASSERT_LE(originalSelector, 8U);
    const auto replacement = static_cast<std::uint8_t>((originalSelector + 1U) % 9U);
    const std::array<skewer::core::TriangleKey, 1> selection{ selectedKey };
    ASSERT_TRUE(loaded.document->setTriangleSelectors(selection, replacement).changed);

    const auto patch = skewer::core::makeFieldPatch(*loaded.document);
    ASSERT_EQ(patch.triangleSelectorEdits.size(), 1U);
    ASSERT_TRUE(patch.ectValueEdits.empty());
    const auto preflight = skewer::core::ExportService::preflight(
        field, std::span<const skewer::core::FieldPatch>(&patch, 1U));
    ASSERT_TRUE(preflight.ok()) << (preflight.diagnostics.empty() ? "" : preflight.diagnostics.back().message);
    ASSERT_EQ(preflight.assets.size(), 1U);
    EXPECT_EQ(preflight.assets.front().basename, std::filesystem::path("a111c.mld"));

    TempDirectory output{};
    const auto publication = skewer::core::ExportService::publish(preflight, output.path);
    ASSERT_TRUE(publication.ok());
    const auto patchedMld = output.path / "a111c.mld";
    ASSERT_TRUE(std::filesystem::exists(patchedMld));
    const auto reloaded = skewer::core::FieldLoader::load({ "a111c", ect, patchedMld });
    ASSERT_TRUE(reloaded.ok()) << (reloaded.diagnostics.empty() ? "" : reloaded.diagnostics.back().message);
    ASSERT_TRUE(reloaded.document.has_value());
    EXPECT_EQ(reloaded.document->baselineSelector(selectedKey), replacement);
}
