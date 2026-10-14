// DIALOG_HELP, the HOW TO PLAY pages, headless.
//
// Layout and timing come from docs/reverse-engineering/help-and-settings.md
// §2 (ctor 0x404830, page turn 0x404f50). The original instructions are
// drawn inside the four IMAGE_HELP_PAGE0N pictures, which load at runtime
// from imported-assets/ only. The placeholder build shows the short pages
// in help_strings instead; they are written for this project, not copied.
#pragma once

#include <array>
#include <string>

#include "tt2ui/game_dialogs.hpp"

namespace tt2ui {

namespace help_strings {
inline constexpr const char* HEADER = "HOW TO PLAY";   // LABEL_HEADER text="HOW TO PLAY"
// Placeholder pages, one per help image.
inline constexpr std::array<const char*, 4> PLACEHOLDER_PAGES = {
    "THE GOAL\n\nSpell words with the letters on the rack. Every word of three letters or more that fits a "
    "slot on the board fills it. Find a word that uses every letter to qualify for the next round.",
    "CONTROLS\n\nType a letter or click a ball to move it into the row. ENTER submits the word, or repeats "
    "the last word when the row is empty. BACKSPACE takes back the last letter. DELETE clears the row. "
    "SPACE twists the rack.",
    "SCORING\n\nA word scores 10 x length x length points. The first word that uses every letter adds "
    "1000 points. Finding every word adds 100 points per slot and ends the round.",
    "MODES\n\nUntimed has no clock: play each rack at your own pace. Timed gives two minutes per round; "
    "qualify before the clock runs out, or the game is over.",
};
}  // namespace help_strings

constexpr int HELP_W = 798;
constexpr int HELP_H = 595;
// The page count is the number of HELP_HEADERS entries in text.xml.
constexpr int HELP_PAGE_COUNT = 4;
constexpr int HELP_PAGE_X = 36;
constexpr int HELP_PAGE_Y = 83;
constexpr int HELP_PAGE_W = 727;
constexpr int HELP_PAGE_H = 468;
// 0x404f50: both pages fade over 200 ms, easing 0x4307e0 is linear.
constexpr int HELP_FADE_MS = 200;

inline std::string help_page_name(int i) { return "HELP_PAGE0" + std::to_string(i + 1); }
inline std::string help_page_image(int i) { return "IMAGE_HELP_PAGE0" + std::to_string(i + 1); }

// DIALOG_HELP 798x595. Page 1 starts at alpha 255, the others at 0.
inline Dialog build_help() {
    Dialog d;
    d.w = HELP_W;
    d.h = HELP_H;
    for (int i = 0; i < HELP_PAGE_COUNT; ++i) {
        SpriteState s = sprite(help_page_name(i).c_str(), sheet(help_page_image(i).c_str(), HELP_PAGE_W, HELP_PAGE_H),
                               HELP_PAGE_X, HELP_PAGE_Y);
        s.alpha = i == 0 ? 1.0f : 0.0f;
        d.sprites.push_back(s);
    }
    d.labels.push_back(TextLabel{"LABEL_HEADER", "FONT_DIALOG_HEADERS", help_strings::HEADER, 394, 28,
                                 TextAlign::Center, Anchor::Center, -1});
    d.buttons.push_back(make_dialog_button("BUTTON_PREV", "BUTTON_PREV", 28, 507, Anchor::TopLeft));
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 400, 505, Anchor::Top));
    d.buttons.push_back(make_dialog_button("BUTTON_NEXT", "BUTTON_NEXT", 776, 511, Anchor::TopRight));
    d.button("BUTTON_PREV")->sprite.visible = false;   // page 0
    return d;
}

// The page state of an open DIALOG_HELP; the pictures live in the Dialog.
struct HelpPager {
    int page = 0;
    int from = -1;      // the page fading out, -1 when idle
    int fade_ms = 0;    // elapsed time of the fade

    bool fading() const { return from >= 0; }

    // PREV (0x404f10, step -1) / NEXT (0x404f30, step +1). Clamped, never
    // wrapped. INFERRED: a turn during a fade first finishes that fade.
    void turn(Dialog& d, int step) {
        const int to = page + step;
        if (to < 0 || to >= HELP_PAGE_COUNT) return;
        if (fading()) finish(d);
        from = page;
        page = to;
        fade_ms = 0;
        if (auto* b = d.button("BUTTON_PREV")) b->sprite.visible = page > 0;
        if (auto* b = d.button("BUTTON_NEXT")) b->sprite.visible = page < HELP_PAGE_COUNT - 1;
    }

    void tick(Dialog& d, int dt_ms) {
        if (!fading()) return;
        fade_ms += dt_ms;
        if (fade_ms >= HELP_FADE_MS) { finish(d); return; }
        const float t = static_cast<float>(fade_ms) / HELP_FADE_MS;
        set_alpha(d, from, 1.0f - t);
        set_alpha(d, page, t);
    }

private:
    void finish(Dialog& d) {
        set_alpha(d, from, 0.0f);
        set_alpha(d, page, 1.0f);
        from = -1;
        fade_ms = 0;
    }
    static void set_alpha(Dialog& d, int i, float a) {
        for (auto& s : d.sprites)
            if (s.name == help_page_name(i)) s.alpha = a;
    }
};

// A stand-alone help screen (the main menu's HOW TO PLAY).
class HelpDialog {
public:
    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    const HelpPager& pager() const { return pager_; }

    // Runs a button by name; true when OK closed the dialog.
    bool press(const std::string& name) {
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return false;
        if (name == "BUTTON_OK") return true;   // 0x408c00
        pager_.turn(dialog_, name == "BUTTON_PREV" ? -1 : 1);
        return false;
    }
    void tick(int dt_ms) {
        pager_.tick(dialog_, dt_ms);
        dialog_tick(dialog_, dt_ms);
    }

private:
    Dialog dialog_ = build_help();
    HelpPager pager_;
};

}  // namespace tt2ui
