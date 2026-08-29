#include "SkewerCore/EventGround.h"

#include "SPICE/SpiceSCT/SctModel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string_view>

namespace {

spice::sct::SctParameter integerParameter(const std::int32_t value) {
    spice::sct::SctParameter parameter{};
    parameter.valueKind = spice::sct::SctParameterValueKind::Integer;
    parameter.rawWords = { std::bit_cast<std::uint32_t>(value) };
    parameter.displayValue = std::to_string(value);
    return parameter;
}

spice::sct::SctInstruction instruction(
    const std::uint32_t offset,
    const std::uint16_t opcode) {
    spice::sct::SctInstruction result{};
    result.offset = offset;
    result.payloadOffset = 100U + offset;
    result.opcode = opcode;
    result.sizeBytes = 4U;
    result.decodeOk = true;
    return result;
}

spice::sct::SctInstruction mutation(
    const std::uint32_t offset,
    const std::int32_t tblId,
    const std::int32_t ordinal) {
    auto result = instruction(offset, 114U);
    result.parameters = { integerParameter(tblId), integerParameter(ordinal) };
    return result;
}

spice::sct::SctParseResult script(
    std::vector<spice::sct::SctInstruction> instructions,
    std::vector<spice::sct::SctEdge> edges = {}) {
    spice::sct::SctParseResult result{};
    result.parseOk = true;
    spice::sct::SctSection section{};
    section.id = { 7U, "water2" };
    section.startOffset = 100U;
    const auto instructionBytes = instructions.empty() ? 0U
        : std::max_element(instructions.begin(), instructions.end(),
            [](const auto& left, const auto& right) { return left.offset < right.offset; })->offset + 4U;
    section.endOffset = section.startOffset + instructionBytes;
    section.kind = spice::sct::SctSectionKind::Script;
    section.instructions = std::move(instructions);
    section.edges = std::move(edges);
    result.file.sections.push_back(std::move(section));
    return result;
}

skewer::core::EventGroundGroup group(
    const std::size_t entryIndex,
    const std::int32_t tblId,
    const std::size_t variantCount) {
    skewer::core::EventGroundGroup result{};
    result.key.entryTableIndex = entryIndex;
    result.tblId = tblId;
    for (std::size_t ordinal = 0; ordinal < variantCount; ++ordinal) {
        result.variants.push_back({ ordinal, static_cast<std::uint32_t>(0x1000U + ordinal) });
    }
    return result;
}

bool diagnosticContains(
    const std::vector<skewer::core::Diagnostic>& diagnostics,
    const std::string_view text) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
}

} // namespace

TEST(EventGroundPreset, AppliesDefaultsSequentialMutationsAndDisabledState) {
    const std::vector groups{ group(4U, 4, 2U), group(13U, 13, 2U) };
    const auto parsed = script({
        mutation(0U, 4, 1),
        mutation(4U, 4, 0),
        mutation(8U, 13, -1),
        instruction(12U, 12U),
    });

    const auto result = skewer::core::buildEventGroundPresets(groups, parsed);
    ASSERT_EQ(result.presets.size(), 1U);
    EXPECT_EQ(result.presets.front().label, "water2");
    ASSERT_EQ(result.presets.front().assignments.size(), 2U);
    EXPECT_EQ(result.presets.front().assignments[0].state,
        skewer::core::EventGroundState::variant(0U));
    EXPECT_EQ(result.presets.front().assignments[1].state,
        skewer::core::EventGroundState::disabled());
}

TEST(EventGroundPreset, ProducesBranchSpecificLabelsAndDeduplicatesTerminalStates) {
    auto branch = instruction(0U, 0U);
    branch.parameters.push_back({ .displayValue = "BitVar 521 == 0" });
    std::vector<spice::sct::SctEdge> edges{
        { .type = spice::sct::SctEdgeType::BranchFalse,
          .fromPayloadOffset = 100U, .toPayloadOffset = 104U },
        { .type = spice::sct::SctEdgeType::BranchTrue,
          .fromPayloadOffset = 100U, .toPayloadOffset = 112U },
    };
    const auto parsed = script({
        branch,
        mutation(4U, 4, 0),
        instruction(8U, 12U),
        mutation(12U, 4, 1),
        instruction(16U, 12U),
    }, std::move(edges));

    const auto result = skewer::core::buildEventGroundPresets(
        { group(4U, 4, 2U) }, parsed);
    ASSERT_EQ(result.presets.size(), 2U);
    EXPECT_TRUE(std::any_of(result.presets.begin(), result.presets.end(), [](const auto& preset) {
        return preset.label.find("BitVar 521 == 0") != std::string::npos;
    }));

    const auto deduplicated = skewer::core::buildEventGroundPresets(
        { group(4U, 4, 2U) }, script({
            branch,
            mutation(4U, 4, 0),
            instruction(8U, 12U),
            mutation(12U, 4, 0),
            instruction(16U, 12U),
        }, {
            { .type = spice::sct::SctEdgeType::BranchFalse,
              .fromPayloadOffset = 100U, .toPayloadOffset = 104U },
            { .type = spice::sct::SctEdgeType::BranchTrue,
              .fromPayloadOffset = 100U, .toPayloadOffset = 112U },
        }));
    ASSERT_EQ(deduplicated.presets.size(), 1U);
    EXPECT_EQ(deduplicated.presets.front().label, "water2");
}

TEST(EventGroundPreset, DiagnosesAmbiguousMissingDynamicAndInvalidTargets) {
    auto dynamicMutation = mutation(12U, 4, 0);
    dynamicMutation.parameters[1].rawWords.clear();
    dynamicMutation.parameters[1].valueKind = spice::sct::SctParameterValueKind::Expression;
    const auto parsed = script({
        mutation(0U, 4, 1),
        mutation(4U, 99, 0),
        mutation(8U, 13, 3),
        dynamicMutation,
        mutation(16U, 13, -2),
        instruction(20U, 12U),
    });
    const auto result = skewer::core::buildEventGroundPresets(
        { group(4U, 4, 2U), group(5U, 4, 2U), group(13U, 13, 2U) }, parsed);
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "duplicate tblId 4"));
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "tblId 99"));
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "out-of-range"));
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "non-constant integral operand"));
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "unsupported negative"));
}

TEST(EventGroundPreset, DiagnosesAndTerminatesCrossSectionFlowPath) {
    const auto parsed = script({ mutation(0U, 4, 1) }, {
        { .type = spice::sct::SctEdgeType::Jump,
          .fromPayloadOffset = 100U, .toPayloadOffset = 300U },
    });
    const auto result = skewer::core::buildEventGroundPresets(
        { group(4U, 4, 2U) }, parsed);
    ASSERT_EQ(result.presets.size(), 1U);
    ASSERT_EQ(result.presets.front().assignments.size(), 1U);
    EXPECT_EQ(result.presets.front().assignments.front().state,
        skewer::core::EventGroundState::variant(1U));
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "cross-section"));
}

TEST(EventGroundPreset, BoundsCombinatorialSectionEvaluation) {
    constexpr std::size_t groupCount = 14U;
    std::vector<skewer::core::EventGroundGroup> groups{};
    std::vector<spice::sct::SctInstruction> instructions{};
    std::vector<spice::sct::SctEdge> edges{};
    for (std::size_t index = 0; index < groupCount; ++index) {
        const auto base = static_cast<std::uint32_t>(index * 16U);
        auto branch = instruction(base, 0U);
        branch.parameters.push_back({ .displayValue = "choice " + std::to_string(index) });
        instructions.push_back(std::move(branch));
        instructions.push_back(mutation(base + 4U, static_cast<std::int32_t>(index), 0));
        instructions.push_back(instruction(base + 8U, 10U));
        instructions.push_back(mutation(base + 12U, static_cast<std::int32_t>(index), 1));
        groups.push_back(group(index, static_cast<std::int32_t>(index), 2U));
        edges.push_back({ .type = spice::sct::SctEdgeType::BranchFalse,
            .fromPayloadOffset = 100U + base, .toPayloadOffset = 104U + base });
        edges.push_back({ .type = spice::sct::SctEdgeType::BranchTrue,
            .fromPayloadOffset = 100U + base, .toPayloadOffset = 112U + base });
        edges.push_back({ .type = spice::sct::SctEdgeType::Jump,
            .fromPayloadOffset = 108U + base, .toPayloadOffset = 116U + base });
    }
    instructions.push_back(instruction(static_cast<std::uint32_t>(groupCount * 16U), 12U));

    const auto result = skewer::core::buildEventGroundPresets(
        groups, script(std::move(instructions), std::move(edges)));
    EXPECT_TRUE(result.presets.empty());
    EXPECT_TRUE(diagnosticContains(result.diagnostics, "10000-state"));
}
