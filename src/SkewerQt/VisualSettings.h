#pragma once

#include <algorithm>

namespace skewer::qt {

inline constexpr int kVisualAdjustmentMinimumPercent = 0;
inline constexpr int kVisualAdjustmentMaximumPercent = 200;
inline constexpr int kVisualAdjustmentNeutralPercent = 100;

struct LayerVisualSettings {
    int brightnessPercent = kVisualAdjustmentNeutralPercent;
    int saturationPercent = kVisualAdjustmentNeutralPercent;
    int contrastPercent = kVisualAdjustmentNeutralPercent;
};

struct VisualSettings {
    LayerVisualSettings encounter{};
    LayerVisualSettings fieldContext{};
    bool encounterEdgesEnabled = false;
};

[[nodiscard]] inline LayerVisualSettings clampedLayerVisualSettings(
    LayerVisualSettings settings) {
    settings.brightnessPercent = std::clamp(settings.brightnessPercent,
        kVisualAdjustmentMinimumPercent, kVisualAdjustmentMaximumPercent);
    settings.saturationPercent = std::clamp(settings.saturationPercent,
        kVisualAdjustmentMinimumPercent, kVisualAdjustmentMaximumPercent);
    settings.contrastPercent = std::clamp(settings.contrastPercent,
        kVisualAdjustmentMinimumPercent, kVisualAdjustmentMaximumPercent);
    return settings;
}

[[nodiscard]] inline VisualSettings clampedVisualSettings(VisualSettings settings) {
    settings.encounter = clampedLayerVisualSettings(settings.encounter);
    settings.fieldContext = clampedLayerVisualSettings(settings.fieldContext);
    return settings;
}

} // namespace skewer::qt
