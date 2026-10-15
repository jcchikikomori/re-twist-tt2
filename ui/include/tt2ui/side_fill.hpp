// The side bars beside the 4:3 game on a wider screen (deviation UX-6,
// display-and-timing.md §1). The original never shows them: it only runs at
// 800x600. The port draws them instead of black: a dark vertical gradient,
// faint rings centred on the game's edge (echoing the menu background's
// rings), and a 2 px gold line along that edge. Pure arithmetic in the
// placeholder palette, no RNG and no original art, so it looks the same with
// or without an import.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "tt2ui/palette.hpp"

namespace tt2ui {

enum class Side { Left, Right };

inline constexpr int SIDE_FILL_EDGE_PX = 2;          // the gold line
inline constexpr float SIDE_FILL_RING_AMOUNT = 0.22f;   // strongest blend towards ROYAL, at the edge
inline constexpr float SIDE_FILL_RINGS_PER_HEIGHT = 12.0f;

namespace detail {
inline std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
}
inline Rgb mix(const Rgb& a, const Rgb& b, float t) { return {mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t)}; }
}  // namespace detail

// RGBA bytes, row by row, for a `w` x `h` bar; empty when either is 0 or
// less. The right bar is the left one mirrored, so the gold line always
// touches the game.
inline std::vector<std::uint8_t> side_fill_rgba(int w, int h, Side side) {
    std::vector<std::uint8_t> out;
    if (w <= 0 || h <= 0) return out;
    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const float period = static_cast<float>(h) / SIDE_FILL_RINGS_PER_HEIGHT;
    const float cy = static_cast<float>(h) / 2.0f;
    for (int y = 0; y < h; ++y) {
        const float t = h > 1 ? static_cast<float>(y) / static_cast<float>(h - 1) : 0.0f;
        const Rgb base = detail::mix(palette::DEEP, palette::ROYAL_DARK, t);
        for (int x = 0; x < w; ++x) {
            // Distance from the game's edge, counted outwards.
            const int from_edge = side == Side::Left ? w - 1 - x : x;
            Rgb c = palette::GOLD;
            if (from_edge >= SIDE_FILL_EDGE_PX) {
                const float dx = static_cast<float>(from_edge) + 0.5f;
                const float dy = static_cast<float>(y) + 0.5f - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float ring = 0.5f + 0.5f * std::cos(6.2831853f * dist / period);
                const float fade = std::exp(-dist / static_cast<float>(h));
                c = detail::mix(base, palette::ROYAL, SIDE_FILL_RING_AMOUNT * ring * fade);
            }
            std::uint8_t* p = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
    return out;
}

}  // namespace tt2ui
