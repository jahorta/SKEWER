#include "SkewerQt/Viewport/SelectorPalette.h"
#include "SkewerQt/VisualSettings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

struct LabColor {
    double lightness;
    double a;
    double b;
};

[[nodiscard]] double radians(const double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

[[nodiscard]] double degrees(const double radiansValue) {
    return radiansValue * 180.0 / std::numbers::pi;
}

[[nodiscard]] LabColor toLab(const std::array<double, 3>& srgb) {
    const auto linearChannel = [](const double channel) {
        return channel <= 0.04045
            ? channel / 12.92
            : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const auto red = linearChannel(srgb[0]);
    const auto green = linearChannel(srgb[1]);
    const auto blue = linearChannel(srgb[2]);
    const auto x = (0.4124564 * red + 0.3575761 * green + 0.1804375 * blue)
        / 0.95047;
    const auto y = 0.2126729 * red + 0.7151522 * green + 0.0721750 * blue;
    const auto z = (0.0193339 * red + 0.1191920 * green + 0.9503041 * blue)
        / 1.08883;
    const auto labChannel = [](const double channel) {
        constexpr double delta = 6.0 / 29.0;
        constexpr double threshold = delta * delta * delta;
        return channel > threshold
            ? std::cbrt(channel)
            : channel / (3.0 * delta * delta) + 4.0 / 29.0;
    };
    const auto fx = labChannel(x);
    const auto fy = labChannel(y);
    const auto fz = labChannel(z);
    return { 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
}

[[nodiscard]] double hueDegrees(const double a, const double b) {
    if (a == 0.0 && b == 0.0) return 0.0;
    const auto hue = degrees(std::atan2(b, a));
    return hue < 0.0 ? hue + 360.0 : hue;
}

[[nodiscard]] double ciede2000(const LabColor& lhs, const LabColor& rhs) {
    const auto chroma1 = std::hypot(lhs.a, lhs.b);
    const auto chroma2 = std::hypot(rhs.a, rhs.b);
    const auto meanChroma = (chroma1 + chroma2) / 2.0;
    const auto meanChroma7 = std::pow(meanChroma, 7.0);
    const auto g = 0.5 * (1.0 - std::sqrt(
        meanChroma7 / (meanChroma7 + std::pow(25.0, 7.0))));
    const auto adjustedA1 = (1.0 + g) * lhs.a;
    const auto adjustedA2 = (1.0 + g) * rhs.a;
    const auto adjustedChroma1 = std::hypot(adjustedA1, lhs.b);
    const auto adjustedChroma2 = std::hypot(adjustedA2, rhs.b);
    const auto hue1 = hueDegrees(adjustedA1, lhs.b);
    const auto hue2 = hueDegrees(adjustedA2, rhs.b);

    const auto deltaLightness = rhs.lightness - lhs.lightness;
    const auto deltaChroma = adjustedChroma2 - adjustedChroma1;
    double deltaHueDegrees = 0.0;
    if (adjustedChroma1 * adjustedChroma2 != 0.0) {
        deltaHueDegrees = hue2 - hue1;
        if (deltaHueDegrees > 180.0) deltaHueDegrees -= 360.0;
        else if (deltaHueDegrees < -180.0) deltaHueDegrees += 360.0;
    }
    const auto deltaHue = 2.0 * std::sqrt(adjustedChroma1 * adjustedChroma2)
        * std::sin(radians(deltaHueDegrees / 2.0));

    const auto meanLightness = (lhs.lightness + rhs.lightness) / 2.0;
    const auto adjustedMeanChroma = (adjustedChroma1 + adjustedChroma2) / 2.0;
    double meanHue = hue1 + hue2;
    if (adjustedChroma1 * adjustedChroma2 != 0.0) {
        meanHue = std::abs(hue1 - hue2) <= 180.0
            ? (hue1 + hue2) / 2.0
            : (hue1 + hue2 < 360.0
                ? (hue1 + hue2 + 360.0) / 2.0
                : (hue1 + hue2 - 360.0) / 2.0);
    }
    const auto t = 1.0
        - 0.17 * std::cos(radians(meanHue - 30.0))
        + 0.24 * std::cos(radians(2.0 * meanHue))
        + 0.32 * std::cos(radians(3.0 * meanHue + 6.0))
        - 0.20 * std::cos(radians(4.0 * meanHue - 63.0));
    const auto hueOffset = (meanHue - 275.0) / 25.0;
    const auto deltaTheta = 30.0 * std::exp(-(hueOffset * hueOffset));
    const auto adjustedMeanChroma7 = std::pow(adjustedMeanChroma, 7.0);
    const auto chromaRotation = 2.0 * std::sqrt(adjustedMeanChroma7
        / (adjustedMeanChroma7 + std::pow(25.0, 7.0)));
    const auto lightnessOffset = meanLightness - 50.0;
    const auto lightnessScale = 1.0 + 0.015 * lightnessOffset * lightnessOffset
        / std::sqrt(20.0 + lightnessOffset * lightnessOffset);
    const auto chromaScale = 1.0 + 0.045 * adjustedMeanChroma;
    const auto hueScale = 1.0 + 0.015 * adjustedMeanChroma * t;
    const auto rotation = -std::sin(radians(2.0 * deltaTheta)) * chromaRotation;
    const auto lightnessTerm = deltaLightness / lightnessScale;
    const auto chromaTerm = deltaChroma / chromaScale;
    const auto hueTerm = deltaHue / hueScale;
    return std::sqrt(lightnessTerm * lightnessTerm
        + chromaTerm * chromaTerm
        + hueTerm * hueTerm
        + rotation * chromaTerm * hueTerm);
}

[[nodiscard]] std::array<double, 3> adjustedSrgb(
    const skewer::qt::SelectorColor color,
    const double saturation) {
    constexpr double channelMaximum = 255.0;
    const std::array<double, 3> srgb{
        static_cast<double>(color.red) / channelMaximum,
        static_cast<double>(color.green) / channelMaximum,
        static_cast<double>(color.blue) / channelMaximum,
    };
    const auto luminance = 0.2126 * srgb[0]
        + 0.7152 * srgb[1]
        + 0.0722 * srgb[2];
    return {
        luminance + (srgb[0] - luminance) * saturation,
        luminance + (srgb[1] - luminance) * saturation,
        luminance + (srgb[2] - luminance) * saturation,
    };
}

[[nodiscard]] double minimumSelectorDistance(const double saturation) {
    double minimum = std::numeric_limits<double>::max();
    for (std::size_t first = 1; first <= 8; ++first) {
        const auto firstLab = toLab(adjustedSrgb(
            skewer::qt::kSelectorPalette[first], saturation));
        for (std::size_t second = first + 1; second <= 8; ++second) {
            const auto secondLab = toLab(adjustedSrgb(
                skewer::qt::kSelectorPalette[second], saturation));
            minimum = std::min(minimum, ciede2000(firstLab, secondLab));
        }
    }
    return minimum;
}

} // namespace

TEST(SelectorPalette, MaintainsPerceptualSeparationAtNeutralAndMinimumSaturation) {
    EXPECT_GT(minimumSelectorDistance(1.0), 30.0);
    constexpr auto minimumSaturation =
        static_cast<double>(skewer::qt::kVisualSaturationMinimumPercent) / 100.0;
    EXPECT_GT(minimumSelectorDistance(minimumSaturation), 15.0);
}

TEST(VisualSettings, EnforcesSaturationFloorWithoutChangingOtherAdjustmentFloors) {
    const auto clamped = skewer::qt::clampedLayerVisualSettings({ 0, 0, 0 });
    EXPECT_EQ(clamped.brightnessPercent, 0);
    EXPECT_EQ(clamped.saturationPercent,
        skewer::qt::kVisualSaturationMinimumPercent);
    EXPECT_EQ(clamped.contrastPercent, 0);
}

TEST(VisualSettings, TraversalBarrierHighlightDefaultsOff) {
    EXPECT_FALSE(skewer::qt::VisualSettings{}.traversalBarriersEnabled);
}
