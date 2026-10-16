// DIALOG_HIGHSCORES (DialogHighScore), headless.
//
// CONFIRMED (REA, docs/reverse-engineering/profiles.md §4.4):
//   - Two openers: the main menu's HIGH SCORES (0x42f0d0, flag +0x1dc = 1:
//     OK shows, MAIN MENU is hidden) and DIALOG_CONGRATS' OK (0x428540,
//     flag 0: MAIN MENU shows, OK is hidden). Both sit at (302, 409).
//   - Show 0x4063f0 picks the tab of the game mode last chosen (global
//     0x5fdea8, Timed at launch), checks its switch, sets TAB_PAGE01's
//     frame and fills the table (0x4068d0).
//   - 0x4068d0: ten rows from the "panel" template at (216, 107), each one
//     30 px under the last (INFERRED: the panel height); name, score
//     ("%d", no separators) and the stored extra (round, or the date for
//     Word of the Day), "-" in all three for a missing entry. The extra
//     column's header is "Round:", or "Date:" on the wotd tab.
//   - The newest entry (the table's lastscore) blinks (ModifierBlink 0.5,
//     1.0, 300, 200) the first time its table is shown by this dialog;
//     switching tabs stops it for good.
//   - LABEL_NR / SPRITE_NR are never set: no rank numbers are drawn.
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

#include "tt2/profile.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/profile_dialogs.hpp"
#include "tt2ui/settings_dialog.hpp"

namespace tt2ui {

namespace highscore_strings {
inline constexpr const char* HEADER = "HIGH SCORES";   // CTEXT_HIGHSCORE_HEADER
inline constexpr const char* NAME = "Name:";           // CTEXT_HIGHSCORE_NAME
inline constexpr const char* SCORE = "Score:";         // CTEXT_HIGHSCORE_SCORE
inline constexpr const char* ROUND = "Round:";         // TEXT_HIGHSCORE_EXTRA
inline constexpr const char* DATE = "Date:";           // TEXT_HIGHSCORE_DATE
inline constexpr const char* EMPTY = "-";              // 0x58e024
}  // namespace highscore_strings

class HighScoreDialog {
public:
    enum class Opener { Menu, Game };
    enum class Result { None, Close, MainMenu };

    static constexpr int W = 614, H = 495;
    static constexpr int ROWS = 10;
    static constexpr float ROW_X = 216, ROW_Y = 107, ROW_H = 30;
    static constexpr float NAME_X = 68;      // LABEL_NAME: width 136, centred
    static constexpr float SCORE_X = 159;
    static constexpr float EXTRA_X = 287;
    // ModifierBlink(0.5, 1.0, 300, 200): INFERRED 300 ms dim, 200 ms full.
    static constexpr float BLINK_DIM = 0.5f;
    static constexpr int BLINK_DIM_MS = 300, BLINK_FULL_MS = 200;
    // TABS (tabs.png 153x304): 8 rows of 38, rows 4..7 = checked.
    static constexpr int TAB_W = 153, TAB_H = 304;

    struct Tab {
        const char* switch_name;
        tt2::GameMode mode;
        int page_frame;   // TAB_PAGE01 column
        float y;          // switch top
    };
    // The order of the tab strip, top to bottom (dialogs.xml).
    static constexpr std::array<Tab, 5> TABS = {{
        {"SWITCH_UNTIMED", tt2::GameMode::Untimed, 0, 95},
        {"SWITCH_TIMED", tt2::GameMode::Timed, 1, 147},
        {"SWITCH_WOD", tt2::GameMode::WordOfTheDay, 2, 199},
        {"SWITCH_BLITZ", tt2::GameMode::Blitz, 3, 250},
        {"SWITCH_CROSSWORD", tt2::GameMode::Crossword, 4, 302},
    }};

    HighScoreDialog(tt2::HighScoreManager& scores, Opener opener, tt2::GameMode mode,
                    std::function<void(SoundId)> play_sound = {})
        : scores_(scores), opener_(opener), play_sound_(std::move(play_sound)) {
        build();
        select(mode);
    }

    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    tt2::GameMode mode() const { return mode_; }
    Opener opener() const { return opener_; }
    // The row that blinks (0..9), or -1.
    int blinking_row() const { return blink_row_; }

    // A dialog button by name (mouse, keyboard, tests). A tab click plays
    // SOUND_BUTTON (its onsound) and refills; OK / MAIN MENU close.
    Result press(const std::string& name) {
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return Result::None;
        for (const Tab& t : TABS) {
            if (name != t.switch_name) continue;
            sound(SoundId::Button);
            if (t.mode != mode_) select(t.mode);
            return Result::None;
        }
        sound(SoundId::Button);
        if (name == "BUTTON_OK") return Result::Close;
        if (name == "BUTTON_MAINMENU") return Result::MainMenu;
        return Result::None;
    }
    Result mouse_up(float x, float y) {
        const std::string name = dialog_mouse_up(dialog_, x, y);
        return name.empty() ? Result::None : press(name);
    }

    void tick(int dt_ms) {
        for (auto& b : dialog_.buttons) {
            b.sprite.frame = b.frame_for_state();
            if (b.sprite.name.rfind("SWITCH_", 0) == 0 && b.sprite.name == current_tab().switch_name)
                b.sprite.frame += SWITCH_ON_ROW_OFFSET;
        }
        if (blink_row_ < 0) return;
        blink_ms_ = (blink_ms_ + dt_ms) % (BLINK_DIM_MS + BLINK_FULL_MS);
        set_row_alpha(blink_row_, blink_ms_ < BLINK_DIM_MS ? BLINK_DIM : 1.0f);
    }

private:
    const Tab& current_tab() const {
        for (const Tab& t : TABS) if (t.mode == mode_) return t;
        return TABS[0];
    }

    void sound(SoundId id) { if (play_sound_) play_sound_(id); }

    void build() {
        dialog_.w = W;
        dialog_.h = H;
        SpriteState page2 = sprite("TAB_PAGE02", sheet("IMAGE_TAB_PAGE02", 383, 333), 191, 77);
        SpriteState page1 = sprite("TAB_PAGE01", sheet("IMAGE_TAB_PAGE01", 800, 333, 5, 1, 5), 31, 77);
        dialog_.sprites.push_back(page2);
        dialog_.sprites.push_back(page1);
        auto label = [&](const char* name, const char* font, const std::string& text, float x, float y, Anchor a,
                         int spacing = -1) {
            dialog_.labels.push_back(TextLabel{name, font, text, x, y, TextAlign::Center, a, spacing});
            return &dialog_.labels.back();
        };
        label("LABEL_HEADER", "FONT_DIALOG_HEADERS", highscore_strings::HEADER, 302, 8, Anchor::Top);
        // The column headers have no anchor (top left) but are centred text;
        // the row labels under them centre on the same points.
        label("HEADER_NAME", "FONT_HIGHSCOREBODY", highscore_strings::NAME, ROW_X + NAME_X, 84, Anchor::Top);
        label("HEADER_SCORE", "FONT_HIGHSCOREBODY", highscore_strings::SCORE, 375, 84, Anchor::Top);
        label("HEADER_EXTRA", "FONT_HIGHSCOREBODY", highscore_strings::ROUND, 505, 84, Anchor::Top);
        // Tab captions (FONT_HIGHSCORELABEL).
        label("LABEL_UNTIMED", "FONT_HIGHSCORELABEL", "UNTIMED", 105, 106, Anchor::Top, -1);
        label("LABEL_TIMED", "FONT_HIGHSCORELABEL", "TIMED", 109, 165, Anchor::Center);
        TextLabel* wod = label("LABEL_WOD", "FONT_HIGHSCORELABEL", "WORD of\nthe DAY", 107, 217, Anchor::Center);
        wod->line_height = 16;
        label("LABEL_BLITZ", "FONT_HIGHSCORELABEL", "LIGHTNING", 108, 269, Anchor::Center, -1);
        label("LABEL_CROSSWORD", "FONT_HIGHSCORELABEL", "CROSSWORD", 105, 320, Anchor::Center, -3);
        for (int i = 0; i < ROWS; ++i) {
            const float y = ROW_Y + ROW_H * static_cast<float>(i);
            const std::string n = std::to_string(i);
            label(("ROW_NAME_" + n).c_str(), "FONT_FONT_HIGHSCORE_TEXT", "", ROW_X + NAME_X, y, Anchor::Top)
                ->max_width = 136;
            label(("ROW_SCORE_" + n).c_str(), "FONT_FONT_HIGHSCORE_TEXT", "", ROW_X + SCORE_X, y, Anchor::Top);
            label(("ROW_EXTRA_" + n).c_str(), "FONT_FONT_HIGHSCORE_TEXT", "", ROW_X + EXTRA_X, y, Anchor::Top);
        }
        for (const Tab& t : TABS) {
            MenuButton s;
            s.sprite = sprite(t.switch_name, sheet("TABS", TAB_W, TAB_H, 1, 8, 8), 39, t.y);
            dialog_.buttons.push_back(s);
        }
        dialog_.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 302, 409, Anchor::Top));
        dialog_.buttons.push_back(make_dialog_button("BUTTON_MAINMENU", "BUTTON_PAUSE_END", 302, 409, Anchor::Top, 181, 208));
        // 0x4063f0: the menu's copy hides MAIN MENU, the game's hides OK.
        MenuButton* hidden = dialog_.button(opener_ == Opener::Menu ? "BUTTON_MAINMENU" : "BUTTON_OK");
        hidden->sprite.visible = false;
        hidden->enabled = false;
    }

    // 0x4068d0.
    void select(tt2::GameMode mode) {
        mode_ = mode;
        const Tab& tab = current_tab();
        if (SpriteState* page = dialog_.sprite_named("TAB_PAGE01")) page->frame = tab.page_frame;
        dialog_.label("HEADER_EXTRA")->text =
            mode == tt2::GameMode::WordOfTheDay ? highscore_strings::DATE : highscore_strings::ROUND;
        const tt2::HighScoreTable& table = scores_.table(mode);
        if (blink_row_ >= 0) set_row_alpha(blink_row_, 1.0f);
        blink_row_ = -1;
        for (int i = 0; i < ROWS; ++i) {
            // The "-" rows only show for a short table; table() refills any
            // table that is not 10 long (0x406e90), so the menu never sees one.
            const bool has = i < table.size();
            const std::string n = std::to_string(i);
            const tt2::HighScoreEntry* e = has ? &table.entries[static_cast<std::size_t>(i)] : nullptr;
            dialog_.label("ROW_NAME_" + n)->text = e ? e->name : highscore_strings::EMPTY;
            dialog_.label("ROW_SCORE_" + n)->text = e ? std::to_string(e->score) : highscore_strings::EMPTY;
            dialog_.label("ROW_EXTRA_" + n)->text = e ? e->extra : highscore_strings::EMPTY;
        }
        // +0x268: only the first fill of this dialog may blink. DEVIATION: the
        // default fill inserts too, leaving lastscore on a made-up entry (row
        // 0 of a fresh table); only a real score blinks here.
        if (blink_pending_ && table.last_index >= 0 && table.last_index < ROWS &&
            !table.entries[static_cast<std::size_t>(table.last_index)].prefilled) {
            blink_row_ = table.last_index;
            blink_ms_ = 0;
            set_row_alpha(blink_row_, BLINK_DIM);
        }
        blink_pending_ = false;
        tick(0);
    }

    void set_row_alpha(int row, float a) {
        const std::string n = std::to_string(row);
        for (const char* prefix : {"ROW_NAME_", "ROW_SCORE_", "ROW_EXTRA_"})
            if (TextLabel* l = dialog_.label(prefix + n)) l->alpha = a;
    }

    tt2::HighScoreManager& scores_;
    Opener opener_;
    std::function<void(SoundId)> play_sound_;
    Dialog dialog_;
    tt2::GameMode mode_ = tt2::GameMode::Timed;
    int blink_row_ = -1;
    int blink_ms_ = 0;
    bool blink_pending_ = true;
};

}  // namespace tt2ui
