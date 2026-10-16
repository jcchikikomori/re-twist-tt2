// Sprite model for the UI layer: a GF2-style sprite is an image (or a
// sprite sheet cut into equal frames) placed at x/y with an anchor, a
// flip, an alpha, an optional clip rectangle, and an optional
// auto-advancing frame timer (`frametime` in dialogs.xml).
//
// This header has no SDL dependency so the scene logic can be unit
// tested headless. The renderer in `ui/src/` only reads these structs.
#pragma once

#include <string>

namespace tt2ui {

// Geometry of a sprite sheet. `frames` may be smaller than
// `columns * rows` (the last cells are unused).
struct SheetSpec {
    std::string image_id;   // resources.xml <Image id="...">
    int columns = 1;
    int rows = 1;
    int frames = 1;
    int frame_w = 0;        // size of one frame in pixels
    int frame_h = 0;

    int sheet_w() const { return frame_w * columns; }
    int sheet_h() const { return frame_h * rows; }
    // Source rectangle (x, y) of frame `f` inside the sheet.
    int frame_x(int f) const { return (f % columns) * frame_w; }
    int frame_y(int f) const { return (f / columns) * frame_h; }
};

enum class Flip { None, Horizontal, Vertical, Both };

// `anchorpoint` / `pivotpoint` in dialogs.xml. Anchor decides which
// point of the frame sits at (x, y). Pivot is the point the flip
// mirrors around; the shipped XML only ever uses it together with a
// flip so that a flipped quadrant stays inside its own quarter of the
// screen. INFERRED semantics, see docs/reverse-engineering/welcome-flow.md.
enum class Anchor { TopLeft, Top, TopRight, Center, BottomLeft };

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

struct SpriteState {
    std::string name;       // component name in dialogs.xml
    SheetSpec sheet;
    float x = 0;
    float y = 0;
    int frame = 0;
    float alpha = 1.0f;
    float scale = 1.0f;     // drawn scaled about (x, y), the anchor point
    float scale_x = 1.0f;   // extra horizontal factor (Lightning's SPRITE_TIMER)
    float scale_y = 1.0f;   // extra vertical factor (the anagram tile flip)
    bool visible = true;
    Flip flip = Flip::None;
    Anchor anchor = Anchor::TopLeft;
    bool has_clip = false;
    Rect clip;              // relative to the frame's top-left
    int frame_time_ms = 0;  // > 0: loop through all frames automatically
    int frame_accum_ms = 0;
    bool additive = false;  // blendmode="additive"
    unsigned tint = 0xFFFFFF;   // color="#RRGGBB" (multiplied in)

    // Top-left corner on screen after applying the anchor.
    float left() const {
        switch (anchor) {
            case Anchor::Top:
            case Anchor::Center: return x - sheet.frame_w * 0.5f;
            case Anchor::TopRight: return x - sheet.frame_w;
            default: return x;
        }
    }
    float top() const {
        switch (anchor) {
            case Anchor::Center: return y - sheet.frame_h * 0.5f;
            case Anchor::BottomLeft: return y - sheet.frame_h;
            default: return y;
        }
    }

    // Advance the auto-frame timer (no-op unless frame_time_ms > 0).
    void tick_auto_frames(int dt_ms) {
        if (frame_time_ms <= 0 || sheet.frames <= 1) return;
        frame_accum_ms += dt_ms;
        while (frame_accum_ms >= frame_time_ms) {
            frame_accum_ms -= frame_time_ms;
            frame = (frame + 1) % sheet.frames;
        }
    }
};

}  // namespace tt2ui
