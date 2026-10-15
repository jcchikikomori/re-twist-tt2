// Window sizes and the 4:3 game rectangle (deviation UX-5,
// display-and-timing.md §1), free of SDL so it is unit-tested.
//
// CONFIRMED: the original only runs at 800x600, windowed or as an
// exclusive 800x600 mode. The port offers tt2::RESOLUTIONS and keeps the
// 800x600 canvas, scaled into the window by SDL's letterbox presentation;
// fit_4x3 repeats SDL 3.4's arithmetic (UpdateLogicalPresentation) so the
// host can check it and the side fill (P6) can use it.
#pragma once

#include <string>
#include <vector>

#include "tt2/improvements.hpp"
#include "tt2ui/welcome_flow.hpp"   // SCREEN_W, SCREEN_H

namespace tt2ui {

struct ViewRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
};

struct Fit4x3 {
    ViewRect game;       // where the 800x600 canvas lands, in output pixels
    float scale = 0.0f;  // output pixels per canvas pixel
};

// SDL_LOGICAL_PRESENTATION_LETTERBOX for an 800x600 canvas: the scale that
// fits, the width or height floored, the rectangle centred (a half pixel
// stays a half pixel). An output of the same aspect fills completely.
inline Fit4x3 fit_4x3(int out_w, int out_h) {
    Fit4x3 f;
    if (out_w <= 0 || out_h <= 0) return f;
    const float ow = static_cast<float>(out_w), oh = static_cast<float>(out_h);
    const float lw = static_cast<float>(SCREEN_W), lh = static_cast<float>(SCREEN_H);
    const float want = lw / lh, real = ow / oh;
    const float d = want - real;
    if ((d < 0 ? -d : d) < 0.0001f) {
        f.game = {0, 0, ow, oh};
    } else if (want > real) {   // taller than 4:3: bars above and below
        const float scale = ow / lw;
        const float h = static_cast<float>(static_cast<int>(lh * scale));
        f.game = {0, (oh - h) / 2.0f, ow, h};
    } else {                    // wider: bars at the sides
        const float scale = oh / lh;
        const float w = static_cast<float>(static_cast<int>(lw * scale));
        f.game = {(ow - w) / 2.0f, 0, w, oh};
    }
    f.scale = f.game.w / lw;
    return f;
}

struct PillarBars {
    ViewRect left, right;   // both empty unless the output is wider than 4:3
};

inline PillarBars pillar_bars(int out_w, int out_h) {
    PillarBars b;
    const Fit4x3 f = fit_4x3(out_w, out_h);
    if (f.game.empty() || f.game.x <= 0.0f) return b;
    const float right_x = f.game.x + f.game.w;
    b.left = {0, 0, f.game.x, static_cast<float>(out_h)};
    b.right = {right_x, 0, static_cast<float>(out_w) - right_x, static_cast<float>(out_h)};
    return b;
}

// The offered sizes that fit inside `bounds_w` x `bounds_h`, in list order.
inline std::vector<tt2::Resolution> available_resolutions(int bounds_w, int bounds_h) {
    std::vector<tt2::Resolution> out;
    for (const tt2::Resolution& r : tt2::RESOLUTIONS)
        if (r.w <= bounds_w && r.h <= bounds_h) out.push_back(r);
    return out;
}

// `want` when it is offered; otherwise the largest offered size (by area)
// no bigger in either direction, then the first offered one; 800x600 when
// none is.
inline tt2::Resolution pick_resolution(const std::vector<tt2::Resolution>& offered, const tt2::Resolution& want) {
    const tt2::Resolution* best = nullptr;
    for (const tt2::Resolution& r : offered) {
        if (r == want) return r;
        if (r.w <= want.w && r.h <= want.h && (!best || r.w * r.h > best->w * best->h)) best = &r;
    }
    if (best) return *best;
    return offered.empty() ? tt2::Resolution{} : offered.front();
}

// What the display under the window allows; the host fills it from SDL.
struct DisplayInfo {
    tt2::Resolution desktop;                 // Borderless always uses this
    std::vector<tt2::Resolution> windowed{tt2::Resolution{}};   // fits the usable bounds
    std::vector<tt2::Resolution> native;     // has an exact fullscreen display mode
};

}  // namespace tt2ui
