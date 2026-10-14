// Dialog show / hide fades and the darkened backdrop behind every dialog.
//
// CONFIRMED (REA, docs/reverse-engineering/game-scene.md §6):
//   - Show 0x40ccc0 adds ModifierAlpha(0 -> 1, DIALOG_FADE_TIME) to the
//     dialog; hide 0x40ce80 adds ModifierAlpha(1 -> 0, DIALOG_FADE_TIME)
//     and removes the dialog (0x43fcd0) when it ends. DIALOG_FADE_TIME is
//     200 ms (default.xml); ModifierAlpha 0x451030 is linear.
//   - dodarkenbackground="true" makes show 0x40cba0 call 0x40d190, which
//     sets the dialog's colour to Color(0.5, 0, 0.25, 0.6). GF2 colours
//     are stored alpha first (0x44ed00 unpacks 0xAARRGGBB into A, R, G, B),
//     so the backdrop is RGB (0, 64, 153) at 50 %: a dark blue, not black.
//     default.xml's DIALOG_BACKGROUND_ALPHA (0.7) is never read; the exe
//     has no such string.
// INFERRED: the backdrop fades with its dialog. The routine that draws the
// colour was not found, so it is assumed to follow the dialog's alpha.
//
// DialogFade tracks one dialog slot. The renderer keeps the last frame of a
// dialog that has just closed and fades it out from that frame.
#pragma once

#include <algorithm>
#include <cstdint>

namespace tt2ui {

inline constexpr int DIALOG_FADE_MS = 200;   // DIALOG_FADE_TIME

// Color(0.5, 0, 0.25, 0.6) from 0x40d190, in GF2's A, R, G, B order.
inline constexpr float DARKEN_A = 0.5f;
inline constexpr float DARKEN_R = 0.0f;
inline constexpr float DARKEN_G = 0.25f;
inline constexpr float DARKEN_B = 0.6f;

struct Rgba8 {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

// 0x44ed70: a channel becomes c * 255 + 0.5, truncated.
inline std::uint8_t color_byte(float c) {
    const float v = std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f;
    return static_cast<std::uint8_t>(v);
}

// The backdrop at `fade` (0 = invisible, 1 = fully shown).
inline Rgba8 darken_color(float fade) {
    return Rgba8{color_byte(DARKEN_R), color_byte(DARKEN_G), color_byte(DARKEN_B), color_byte(DARKEN_A * fade)};
}

// One dialog slot. Call update() once per frame with the id of the dialog
// open in the slot now (0 = none). A new id fades in from 0; the dialog it
// replaces starts closing from the alpha it had.
class DialogFade {
public:
    // Returns true when the previously open dialog started closing on this
    // call: the renderer then keeps its last frame for the fade-out.
    bool update(int id, int dt_ms) {
        bool started_closing = false;
        if (id != open_id_) {
            if (open_id_ != 0) {
                closing_id_ = open_id_;
                closing_alpha_ = open_alpha_;
                started_closing = true;
            }
            open_id_ = id;
            open_alpha_ = 0.0f;
        }
        const float step = dt_ms > 0 ? static_cast<float>(dt_ms) / DIALOG_FADE_MS : 0.0f;
        if (open_id_ != 0) open_alpha_ = std::min(1.0f, open_alpha_ + step);
        if (closing_id_ != 0) {
            closing_alpha_ = std::max(0.0f, closing_alpha_ - step);
            if (closing_alpha_ <= 0.0f) closing_id_ = 0;
        }
        return started_closing;
    }

    int open_id() const { return open_id_; }
    float open_alpha() const { return open_id_ != 0 ? open_alpha_ : 0.0f; }
    bool closing() const { return closing_id_ != 0; }
    int closing_id() const { return closing_id_; }
    float closing_alpha() const { return closing_id_ != 0 ? closing_alpha_ : 0.0f; }
    // One backdrop per slot: the stronger of the two fades.
    float backdrop() const { return std::max(open_alpha(), closing_alpha()); }
    bool visible() const { return open_id_ != 0 || closing_id_ != 0; }

private:
    int open_id_ = 0;
    float open_alpha_ = 0.0f;
    int closing_id_ = 0;
    float closing_alpha_ = 0.0f;
};

}  // namespace tt2ui
