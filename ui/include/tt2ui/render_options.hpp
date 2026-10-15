// What the renderer actually does with the IMPROVEMENTS choices (deviation
// UX-8), free of SDL so it is unit-tested. The file keeps the player's
// choice; this maps it onto what the running renderer can do, so a choice
// made on a GPU survives a launch on the software renderer.
#pragma once

#include "tt2/improvements.hpp"
#include "tt2ui/display.hpp"
#include "tt2ui/render_backend.hpp"

namespace tt2ui {

struct EffectiveOptions {
    tt2::TextureFilter filter = tt2::TextureFilter::Sharp;
    bool hires_layers = false;   // dialog layers at output resolution, not 800x600
    bool hires_art = false;      // placeholder art from data/placeholder/2x/ (P8)
    bool upscaler = false;       // Super-xBR 2x on the loaded art (P9, UX-9)
};

// The software renderer keeps the 0.2.x look: Sharp, 800x600 layers, the
// 1x placeholder art and no upscaler.
inline EffectiveOptions effective(const tt2::Improvements& imp, const RenderCaps& caps) {
    EffectiveOptions o;
    if (!caps.enhancements) return o;
    o.filter = imp.filter;
    o.hires_layers = true;
    o.hires_art = true;
    o.upscaler = imp.upscaler;
    return o;
}

// PixelArt needs SDL 3.4's SDL_SCALEMODE_PIXELART; a renderer that refuses
// it gets Smooth, the nearest look that still softens the scaling.
inline tt2::TextureFilter filter_with_support(tt2::TextureFilter f, bool pixelart_supported) {
    return f == tt2::TextureFilter::PixelArt && !pixelart_supported ? tt2::TextureFilter::Smooth : f;
}

struct LayerSize {
    int w = 0, h = 0;
    friend bool operator==(const LayerSize& a, const LayerSize& b) { return a.w == b.w && a.h == b.h; }
};

// The size of a dialog layer's render target. Hi-res layers match the 4:3
// game rectangle in output pixels, so a dialog is sampled once, at the size
// it shows; below 1x, or past the GPU's largest texture, they stay 800x600.
inline LayerSize layer_size(int out_w, int out_h, bool hires, int max_texture_size) {
    const LayerSize canvas{SCREEN_W, SCREEN_H};
    if (!hires) return canvas;
    const Fit4x3 f = fit_4x3(out_w, out_h);
    const LayerSize s{static_cast<int>(f.game.w), static_cast<int>(f.game.h)};
    if (s.w <= SCREEN_W || s.h <= SCREEN_H) return canvas;
    if (max_texture_size > 0 && (s.w > max_texture_size || s.h > max_texture_size)) return canvas;
    return s;
}

}  // namespace tt2ui
