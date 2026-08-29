#pragma once

#include "Diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::sct {
struct SctParseResult;
}

namespace skewer::core {

enum class SceneResourceKind {
    Grnd,
    Gobj,
};

enum class SceneReferenceRole {
    EventGround,
    OtherGround,
    OrdinaryObject,
    Unreferenced,
};

struct EventGroundGroupKey {
    std::size_t entryTableIndex = 0;

    bool operator==(const EventGroundGroupKey&) const = default;
    bool operator<(const EventGroundGroupKey& other) const noexcept {
        return entryTableIndex < other.entryTableIndex;
    }
};

struct EventGroundVariant {
    std::size_t ordinal = 0;
    std::uint32_t resourceAddress = 0;
    std::optional<SceneResourceKind> resourceKind{};
    std::vector<std::size_t> batchIndices{};
};

struct EventGroundGroup {
    EventGroundGroupKey key{};
    std::uint32_t entryId = 0;
    std::int32_t tblId = 0;
    std::string functionName{};
    std::vector<EventGroundVariant> variants{};
};

struct OtherGroundResource {
    std::size_t ordinal = 0;
    std::uint32_t resourceAddress = 0;
    SceneResourceKind resourceKind = SceneResourceKind::Grnd;
    std::vector<std::size_t> batchIndices{};
};

struct OtherGroundGroup {
    std::size_t entryTableIndex = 0;
    std::uint32_t entryId = 0;
    std::int32_t tblId = 0;
    std::string functionName{};
    std::vector<OtherGroundResource> resources{};
};

enum class EventGroundStateKind {
    Disabled,
    Variant,
};

struct EventGroundState {
    EventGroundStateKind kind = EventGroundStateKind::Variant;
    std::size_t variantOrdinal = 0;

    [[nodiscard]] static EventGroundState disabled() noexcept {
        return { EventGroundStateKind::Disabled, 0U };
    }
    [[nodiscard]] static EventGroundState variant(std::size_t ordinal) noexcept {
        return { EventGroundStateKind::Variant, ordinal };
    }
    bool operator==(const EventGroundState&) const = default;
};

struct EventGroundAssignment {
    EventGroundGroupKey group{};
    EventGroundState state{};
};

struct EventGroundPreset {
    std::string id{};
    std::string label{};
    std::string sectionName{};
    std::vector<std::string> conditions{};
    std::vector<EventGroundAssignment> assignments{};
};

struct EventGroundPresetBuildResult {
    std::vector<EventGroundPreset> presets{};
    std::vector<Diagnostic> diagnostics{};
};

struct SceneModel;

[[nodiscard]] EventGroundPresetBuildResult buildEventGroundPresets(
    const std::vector<EventGroundGroup>& groups,
    const spice::sct::SctParseResult& sct);

void applyEventGroundPresetVisibility(
    const SceneModel& scene,
    const EventGroundPreset& preset,
    std::vector<std::uint8_t>& visibility);

void applyRawEventGroundVisibility(
    const SceneModel& scene,
    std::vector<std::uint8_t>& visibility);

} // namespace skewer::core
