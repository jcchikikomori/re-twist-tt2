// The placeholder palette, sampled from the original install and rounded to
// 16 levels. Copied from tools/gen_placeholders.py ("Palette, sampled from
// the original install"); test_side_fill.cpp reads that file and fails when
// the two drift apart.
#pragma once

#include <cstdint>

namespace tt2ui {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
    friend bool operator!=(const Rgb& a, const Rgb& b) { return !(a == b); }
};

namespace palette {
inline constexpr Rgb ROYAL{0x00, 0x30, 0xB0};
inline constexpr Rgb ROYAL_DARK{0x00, 0x20, 0xA0};
inline constexpr Rgb NAVY{0x00, 0x10, 0x80};
inline constexpr Rgb DEEP{0x00, 0x10, 0x40};
inline constexpr Rgb GOLD{0xE0, 0xC0, 0x00};
inline constexpr Rgb YELLOW{0xF0, 0xE0, 0x00};
inline constexpr Rgb ORANGE{0xF0, 0x70, 0x00};
inline constexpr Rgb CREAM{0xF0, 0xF0, 0xC0};
inline constexpr Rgb WHITE{0xF0, 0xF0, 0xF0};
inline constexpr Rgb CYAN{0x20, 0xD0, 0xF0};
inline constexpr Rgb SKY{0x10, 0x50, 0xD0};
}  // namespace palette

}  // namespace tt2ui
