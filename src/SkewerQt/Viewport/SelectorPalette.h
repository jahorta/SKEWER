#pragma once

#include <array>
#include <cstdint>

namespace skewer::qt {

struct SelectorColor {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

// Selectors 1-8 were optimized within ordered rainbow hue bands in D65
// CIELAB space. The quantized sRGB palette has a minimum pairwise CIEDE2000
// distance greater than 30 at neutral visual settings.
inline constexpr std::array<SelectorColor, 10> kSelectorPalette = {
    SelectorColor{ 87, 92, 102 },   // 0: no encounters
    SelectorColor{ 253, 35, 95 },   // 1: red
    SelectorColor{ 241, 118, 2 },   // 2: orange
    SelectorColor{ 255, 211, 5 },   // 3: yellow
    SelectorColor{ 34, 236, 22 },   // 4: green
    SelectorColor{ 3, 149, 118 },   // 5: teal
    SelectorColor{ 46, 223, 253 },  // 6: cyan
    SelectorColor{ 18, 121, 210 },  // 7: blue
    SelectorColor{ 192, 71, 254 },  // 8: violet
    SelectorColor{ 255, 0, 255 },   // invalid selector sentinel
};

} // namespace skewer::qt
