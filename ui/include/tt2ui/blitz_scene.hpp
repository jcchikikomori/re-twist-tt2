// The Lightning game scene: `BlitzScene` / SCENE_BLITZ in the original.
// The rules live in the engine (tt2/round_lightning.hpp); this is the
// layout, the input and the round flow around them, from the
// decompilation in docs/reverse-engineering/lightning.md (addresses are
// repeated next to the numbers they justify). SDL-free like GameScene:
// `ui/src/main.cpp` only draws the state exposed here.
//
// Shared with Classic: the letter balls and their cells, the dialogs
// (pause menu, give-up confirm, help, settings, tutorial, next round, end
// game, congrats, high scores) and the two popups (scene_popups.hpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/profile.hpp"
#include "tt2/random.hpp"
#include "tt2/round_lightning.hpp"
#include "tt2/saved_game.hpp"
#include "tt2/trophies.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/game_scene.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/highscore_dialog.hpp"
#include "tt2ui/modifier.hpp"
#include "tt2ui/scene_popups.hpp"
#include "tt2ui/settings_dialog.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

// ---------------------------------------------------------------------------
// Layout (dialogs.xml SCENE_BLITZ, 0x410f00, 0x414340, 0x414780)
// ---------------------------------------------------------------------------
constexpr float BLITZ_POPUP_X = 400.0f;        // 0x410f00: +0x34c
constexpr float BLITZ_POPUP_Y = 55.0f;         // 0x410f00: +0x350
// The word list: every rack at x = 10, 40 px apart, scale 0.33, centred
// on y = 285 (0x413d19: base = 285 - n * 40 * 0.5). Worked example: n = 5
// -> base 185, racks at 185, 225, 265, 305, 345.
constexpr float BLITZ_LIST_X = 10.0f;          // 0x414780: 0x41200000
constexpr float BLITZ_LIST_PITCH = 40.0f;
constexpr float BLITZ_LIST_MID_Y = 285.0f;
constexpr float BLITZ_LIST_SCALE = 0.33f;      // 0x414780: 0x3ea8f5c3
// The current word: its rack at x = 493 - L * 69 / 2, y = 281, scale 1;
// its entry row at the same x, y = 212 (0x414340 / 0x414780).
// Example L = 6: x = 286.
constexpr float BLITZ_CURRENT_MID_X = 493.0f;
constexpr float BLITZ_RACK_Y = 281.0f;         // 0x414780: 0x438c8000
constexpr float BLITZ_ENTRY_Y = 212.0f;
// IMAGE_BLITZ_MARK (x.png 22x44, two 22x22 frames): 0x414340 puts it at
// (207, rack y), then 0x414c70 moves it to x = panel width + 10 (see
// blitz_panel_w). Round end: frame 1 solved, 0 not (0x411ef0).
constexpr int BLITZ_MARK_SOLVED_FRAME = 1;
// SPRITE_TIMER (timerBar.PNG 321x92, two 321x46 frames) at (343, 110).
// Never scaled: 0x415300 clips it to (0, 0, 321 * t / 80300, 46), so the
// art shows from the left and its right end is cut off. Frame 1 only
// while time is being added.
constexpr float BLITZ_TIMER_X = 343.0f, BLITZ_TIMER_Y = 110.0f;
constexpr float BLITZ_TIMER_W = 321.0f, BLITZ_TIMER_H = 46.0f;
// The list look (0x43d280(0)): each rack ball shows its 24 px child from
// IMAGE_LETTERBALL_40 (letterball_anim_40.png 408x624, 17 x 26 frames,
// the same frame index as the 60 px sheet) and each rack cell its
// IMAGE_BLITZDOT (16x16), both centred at scale 2.5 (0x439d00, 0x43ab90).
constexpr float BLITZ_SMALL_SCALE = 2.5f;
// IMAGE_HINT_LETTERS (hint_letters.png 621x207, 9 columns, 27 frames of
// 69x69): frame = letter - 0x60. 0x43c990 fades one in, 0 -> 0.392 in 500 ms.
constexpr int BLITZ_HINT_FADE_MS = 500;
constexpr float BLITZ_HINT_ALPHA = 0.392f;
// 0x43a310(1): a solved word's balls run 6 frames at 60 ms from the
// resting pearl (11) to the square tile (16).
constexpr int BLITZ_SOLVED_FRAME = BALL_IDLE_FRAME + 5;
constexpr int BLITZ_SOLVED_FRAME_MS = 60;
// 0x414780: a switch with letters in the old row clears them first and
// waits 500 ms.
constexpr int BLITZ_SWITCH_CLEAR_MS = 500;
// 0x414780: the rack slides (linear) and scales (smoothstep) in 400 ms;
// the entry row fades (linear) in the same 400 ms.
constexpr int BLITZ_SWITCH_MS = 400;
// 0x4116e0 @0x411af4: the +8 s clip grows for (L + 4) * 100 ms, L = the
// solved word's length. Worked example: L = 6 -> 1000 ms. Linear, CONFIRMED:
// ModifierClipRect 0x478390 zeroes EDX before the base ctor 0x47ece0, which
// then stores the linear 0x4307e0 (game-scene.md §9.1, AN1, AN30).
inline int blitz_time_added_ms(std::size_t length) { return (static_cast<int>(length) + 4) * 100; }
// 0x411ef0: per word, first at 400 ms then +100 ms; the solved letters
// fade in over 400 ms; the mark fades in over 150 ms while it grows
// sideways, scale-x 0 -> 1.2 (300 ms, smoothstep) then 1.2 -> 1 (100 ms,
// smoothstep). The 2000 ms wait starts when the last word's reveal starts.
constexpr int BLITZ_REVEAL_FIRST_MS = 400;
constexpr int BLITZ_REVEAL_STEP_MS = 100;
constexpr int BLITZ_REVEAL_FADE_MS = 400;
constexpr int BLITZ_MARK_FADE_MS = 150;
constexpr int BLITZ_MARK_GROW_MS = 300;
constexpr int BLITZ_MARK_SETTLE_MS = 100;
constexpr float BLITZ_MARK_OVERSHOOT = 1.2f;
constexpr int BLITZ_REVEAL_HOLD_MS = 2000;
constexpr int BLITZ_LOSE_CLEAR_MS = 300;       // 0x411e10: a typed row is cleared first

inline float blitz_list_top(std::size_t n) {
    return BLITZ_LIST_MID_Y - static_cast<float>(n) * BLITZ_LIST_PITCH * 0.5f;
}
inline float blitz_list_y(std::size_t n, std::size_t i) {
    return blitz_list_top(n) + static_cast<float>(i) * BLITZ_LIST_PITCH;
}
inline float blitz_current_x(std::size_t length) {
    return BLITZ_CURRENT_MID_X - static_cast<float>(length) * BALL_PITCH * 0.5f;
}
// Total reveal: worked example n = 5 -> 400 + 4 * 100 + 2000 = 2800 ms.
inline int blitz_reveal_ms(std::size_t n) {
    return BLITZ_REVEAL_FIRST_MS + BLITZ_REVEAL_STEP_MS * static_cast<int>(n == 0 ? 0 : n - 1) + BLITZ_REVEAL_HOLD_MS;
}

inline SheetSpec blitz_mark_sheet() { return sheet("IMAGE_BLITZ_MARK", 22, 44, 1, 2, 2); }
inline SheetSpec blitz_timer_sheet() { return sheet("IMAGE_TIMERBAR", 321, 92, 1, 2, 2); }
inline SheetSpec hint_letter_sheet() { return sheet("IMAGE_HINT_LETTERS", 621, 207, 9, 3, 27); }

// The word-list panel around the racks (0x414c70; the ids carry the
// section, gamescene has its own IMAGE_PANEL_* files). Its size follows
// the round (0x415280 = the longest word): w = 26 * maxL - 10,
// top = 285 - 20n - 10, h = 40n. The 10 px strips and corners frame a
// background scaled to (w, h - 20). Worked example n = 5, maxL = 6:
// top 175, w 146, the right edge 185..365, the bottom strip at 365.
constexpr float BLITZ_PANEL_EDGE = 10.0f;
inline float blitz_panel_w(std::size_t max_len) { return 26.0f * static_cast<float>(max_len) - 10.0f; }
inline float blitz_panel_top(std::size_t n) { return blitz_list_top(n) - BLITZ_PANEL_EDGE; }
inline float blitz_panel_h(std::size_t n) { return static_cast<float>(n) * BLITZ_LIST_PITCH; }
inline std::vector<SpriteState> blitz_panel(std::size_t n, std::size_t max_len) {
    const float w = blitz_panel_w(max_len);
    const float top = blitz_panel_top(n);
    const float h = blitz_panel_h(n);
    const float inner = h - 2.0f * BLITZ_PANEL_EDGE;
    std::vector<SpriteState> out;
    SpriteState bg = sprite("PANEL_BG", sheet("blitzscene/IMAGE_PANEL_BG", 195, 600), 0, top + BLITZ_PANEL_EDGE);
    bg.scale_x = w / 195.0f;
    bg.scale_y = inner / 600.0f;
    out.push_back(bg);
    SpriteState t = sprite("PANEL_LEFT", sheet("blitzscene/IMAGE_PANEL_TOP", 1, 10), 0, top);
    t.scale_x = w;
    out.push_back(t);
    SpriteState b = sprite("PANEL_BOTTOM", sheet("blitzscene/IMAGE_PANEL_BOTTOM", 1, 10), 0, top + h - BLITZ_PANEL_EDGE);
    b.scale_x = w;
    out.push_back(b);
    SpriteState r = sprite("PANEL_RIGHT", sheet("blitzscene/IMAGE_PANEL_SIDER", 10, 1), w, top + BLITZ_PANEL_EDGE);
    r.scale_y = inner;
    out.push_back(r);
    out.push_back(sprite("PANEL_LEFTCORNER", sheet("blitzscene/IMAGE_PANEL_CORNERTOPR", 10, 10), w, top));
    out.push_back(sprite("PANEL_RIGHTCORNER", sheet("blitzscene/IMAGE_PANEL_CORNERR", 10, 10), w, top + h - BLITZ_PANEL_EDGE));
    return out;
}
inline SheetSpec blitz_small_ball_sheet() { return sheet("IMAGE_LETTERBALL_40", 408, 624, 17, 26, 442); }
inline SheetSpec blitz_dot_sheet() { return sheet("IMAGE_BLITZDOT", 16, 16); }

// One ball to draw: screen centre, scale and alpha. `small`: the list
// look's 24 px child draws over it at full alpha (0x411ef0 shows a solved
// word's tiles at once while only the 60 px ball fades in).
struct BlitzBallDraw {
    LetterBall ball;   // x / y on screen
    float scale = 1.0f;
    float alpha = 1.0f;
    bool small = false;
};

// One word of the round on screen (0x414340's word object).
struct BlitzWordView {
    std::string answer;             // word+0x1c: the rack word
    // Ball positions are local to the rack row: x = slot * 69 + 34.5,
    // y = 34.5 in the rack, 34.5 - 69 in the entry row (212 vs 281).
    std::vector<LetterBall> balls;
    // The rack row's top-left and scale, sliding between the list and
    // the current spot.
    float x = 0, y = 0, scale = BLITZ_LIST_SCALE;
    float from_x = 0, from_y = 0, from_scale = BLITZ_LIST_SCALE;
    float to_x = 0, to_y = 0, to_scale = BLITZ_LIST_SCALE;
    float entry_alpha = 0, entry_from = 0, entry_to = 0;
    int move_delay_ms = 0, move_ms = 0, move_elapsed_ms = 0;
    // Hint ghost letters shown in the entry row, each with its fade clock.
    std::vector<int> hint_fade_ms;
    // The round-end reveal: wait, then mark and letters.
    int reveal_delay_ms = -1;       // -1: not revealing
    int reveal_elapsed_ms = -1;
    float letters_alpha = 1.0f;     // the 60 px balls fade in at the reveal
    SpriteState mark;
    // 0x43d280: the list look (small balls and blitz dots). On for a rack
    // in the list; off as a word's move up starts, on again when its move
    // back down ends.
    bool list_look = true;

    bool moving() const { return move_ms > 0 && move_elapsed_ms < move_ms; }
    float screen_x(float local) const { return x + local * scale; }
    float screen_y(float local) const { return y + local * scale; }
    float entry_x() const { return blitz_current_x(answer.size()); }

    void slide_to(float tx, float ty, float tscale, float talpha, int delay_ms, int ms) {
        from_x = x;
        from_y = y;
        from_scale = scale;
        entry_from = entry_alpha;
        to_x = tx;
        to_y = ty;
        to_scale = tscale;
        entry_to = talpha;
        move_delay_ms = delay_ms;
        move_ms = std::max(1, ms);
        move_elapsed_ms = 0;
    }
    void tick_move(int dt) {
        if (!moving()) return;
        int left = dt;
        if (move_delay_ms > 0) {
            const int used = std::min(move_delay_ms, left);
            move_delay_ms -= used;
            left -= used;
            if (move_delay_ms > 0) return;
        }
        if (to_scale >= 1.0f) list_look = false;   // 0x414780: 0x43d280(1) as the move starts
        move_elapsed_ms = std::min(move_ms, move_elapsed_ms + left);
        const float t = static_cast<float>(move_elapsed_ms) / static_cast<float>(move_ms);   // linear, 0x4307e0
        x = from_x + (to_x - from_x) * t;
        y = from_y + (to_y - from_y) * t;
        scale = from_scale + (to_scale - from_scale) * ease(Easing::Smoothstep, t);   // 0x4513d0
        entry_alpha = entry_from + (entry_to - entry_from) * t;
        if (!moving() && to_scale < 1.0f) list_look = true;   // 0x43d280(0) once the scale ends
    }
};

class BlitzScene {
public:
    using Services = GameScene::Services;

    // `resume`: a saved BLITZ game (0x4110f0 -> 0x4174c0); nullptr starts round 1.
    BlitzScene(const tt2::WordTree& common, const tt2::WordTree& full, tt2::PlayerProfile& profile,
               tt2::HighScoreManager* highscores, Services services, std::int64_t rng_seed,
               const tt2::SavedGame* resume = nullptr)
        : profile_(profile), highscores_(highscores), services_(std::move(services)), rng_(rng_seed),
          bag_(common, profile.bag), round_(common, full), popups_(BLITZ_POPUP_X, BLITZ_POPUP_Y) {
        particle_rng_.set_seed(rng_seed + 1);
        build_buttons();
        if (!(resume && resume_game(*resume))) start_round(1);
    }
    BlitzScene(const BlitzScene&) = delete;
    BlitzScene& operator=(const BlitzScene&) = delete;

    // ---- state for the renderer -------------------------------------
    GamePhase phase() const { return phase_; }
    const tt2::LightningRound& round() const { return round_; }
    int round_number() const { return round_.round(); }
    std::int32_t score() const { return round_.score(); }
    int time_left_ms() const { return round_.time_ms(); }
    bool finished() const { return finished_; }
    bool busy() const { return busy_; }
    const std::vector<BlitzWordView>& words() const { return views_; }
    std::size_t current() const { return round_.current(); }
    const char* background_id() const { return "blitzscene/BACKGROUND_BLITZ"; }
    const char* save_mode() const { return tt2::save_mode_name(tt2::GameMode::Blitz); }

    // UI bar (0x414c70): minutes "%d" (anchored right), seconds ":%02d".
    std::string score_text() const { return std::to_string(round_.score()); }
    std::string round_text() const { return std::to_string(round_.round()); }
    std::string timer_minutes_text() const { return std::to_string(std::max(0, round_.time_ms() / 60000)); }
    std::string timer_seconds_text() const {
        char buf[8];
        std::snprintf(buf, sizeof buf, ":%02d", std::max(0, (round_.time_ms() / 1000) % 60));
        return buf;
    }

    SpriteState timer_bar() const {
        SpriteState s = sprite("SPRITE_TIMER", blitz_timer_sheet(), BLITZ_TIMER_X, BLITZ_TIMER_Y);
        s.has_clip = true;
        s.clip = {0.0f, 0.0f, BLITZ_TIMER_W * timer_fraction_, BLITZ_TIMER_H};
        s.frame = time_added_ms_ > 0 ? 1 : 0;
        return s;
    }
    // 0x415280: the longest word of the round sizes the panel.
    std::size_t max_word_length() const {
        std::size_t n = 0;
        for (const auto& w : views_) n = std::max(n, w.answer.size());
        return n;
    }
    std::vector<SpriteState> panel() const { return blitz_panel(views_.size(), max_word_length()); }
    // How long the current +8 s grow runs (0 when none is running).
    int time_added_total_ms() const { return time_added_ms_ > 0 ? time_added_total_ms_ : 0; }
    // PAUSE_VEIL (pauseshield.png 220x545 at 0, 0) covers the word list
    // while DIALOG_MENU is up (0x4135e0 / 0x4169d0), the dialogs it opens
    // included.
    bool pause_veil() const {
        if (dialog_kind_ == GameDialog::PauseMenu) return true;
        for (const auto& u : under_)
            if (u.first == GameDialog::PauseMenu) return true;
        return false;
    }

    // Cells under the balls: each word's rack (frame 1, at its scale) and
    // the entry row (frame 0) wherever it is visible.
    std::vector<SpriteState> letter_boxes() const {
        std::vector<SpriteState> out;
        for (const auto& w : views_) {
            const int n = static_cast<int>(w.answer.size());
            for (int i = 0; i < n; ++i) {
                const float lx = static_cast<float>(i) * BALL_PITCH + BALL_HALF;
                SpriteState rack = sprite("LETTERBOX", letter_box_sheet(), w.screen_x(lx), w.screen_y(BALL_HALF));
                rack.anchor = Anchor::Center;
                rack.frame = LETTERBOX_AVAILABLE_FRAME;
                rack.scale = w.scale;
                out.push_back(rack);
                if (w.entry_alpha <= 0.0f) continue;
                SpriteState entry = sprite("LETTERBOX", letter_box_sheet(), w.entry_x() + lx, BLITZ_ENTRY_Y + BALL_HALF);
                entry.anchor = Anchor::Center;
                entry.frame = LETTERBOX_PLAYED_FRAME;
                entry.alpha = w.entry_alpha;
                out.push_back(entry);
            }
        }
        return out;
    }
    // 0x43ab90 / 0x43d280(0): a blitz dot in each cell of a rack in the
    // list look. The balls cover them, so they show on solved racks.
    std::vector<SpriteState> blitz_dots() const {
        std::vector<SpriteState> out;
        for (const auto& w : views_) {
            if (!w.list_look) continue;
            for (std::size_t i = 0; i < w.answer.size(); ++i) {
                const float lx = static_cast<float>(i) * BALL_PITCH + BALL_HALF;
                SpriteState s = sprite("BLITZDOT", blitz_dot_sheet(), w.screen_x(lx), w.screen_y(BALL_HALF));
                s.anchor = Anchor::Center;
                s.scale = BLITZ_SMALL_SCALE * w.scale;
                out.push_back(s);
            }
        }
        return out;
    }
    // 0x43c990: the hint letters, left to right in the entry row.
    std::vector<SpriteState> hint_letters() const {
        std::vector<SpriteState> out;
        for (const auto& w : views_) {
            if (w.entry_alpha <= 0.0f) continue;
            for (std::size_t i = 0; i < w.hint_fade_ms.size() && i < w.answer.size(); ++i) {
                const float lx = static_cast<float>(i) * BALL_PITCH + BALL_HALF;
                SpriteState s = sprite("HINT_LETTER", hint_letter_sheet(), w.entry_x() + lx, BLITZ_ENTRY_Y + BALL_HALF);
                s.anchor = Anchor::Center;
                s.frame = w.answer[i] - 0x60;
                // 0x430440 caps a HINT_LETTER at its own alpha when the
                // row fades: min, not a product.
                const float t = std::min(1.0f, static_cast<float>(w.hint_fade_ms[i]) / BLITZ_HINT_FADE_MS);
                s.alpha = std::min(BLITZ_HINT_ALPHA * t, w.entry_alpha);
                out.push_back(s);
            }
        }
        return out;
    }
    std::vector<SpriteState> marks() const {
        std::vector<SpriteState> out;
        for (const auto& w : views_) if (w.mark.visible) out.push_back(w.mark);
        return out;
    }
    std::vector<BlitzBallDraw> balls() const {
        std::vector<BlitzBallDraw> out;
        for (const auto& w : views_) {
            for (const auto& b : w.balls) {
                BlitzBallDraw d;
                d.ball = b;
                d.scale = w.scale;
                d.ball.x = w.screen_x(b.x);
                d.ball.y = w.screen_y(b.y);
                d.alpha = w.letters_alpha;
                d.small = w.list_look;
                // The entry row stays where it is; only the rack slides.
                if (b.played) {
                    d.ball.x = w.entry_x() + b.x;
                    d.ball.y = BLITZ_ENTRY_Y + (b.y + BALL_PITCH);
                    d.scale = 1.0f;
                    d.alpha = w.entry_alpha;
                    d.small = false;
                }
                if (d.alpha > 0.0f || d.small) out.push_back(d);
            }
        }
        return out;
    }

    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    MenuButton* button(const std::string& name) {
        for (auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    const SpriteState& popup() const { return popups_.sprite(); }
    std::optional<PopupKind> popup_kind() const { return popups_.kind(); }
    std::size_t popups_waiting() const { return popups_.waiting(); }
    const SpriteState& trophy_popup() const { return trophies_.sprite(); }
    const SpriteState& trophy_label() const { return trophies_.label(); }
    int trophy_popup_id() const { return trophies_.id(); }
    std::size_t trophies_waiting() const { return trophies_.waiting(); }

    // ---- dialogs (the same stack as GameScene) ----------------------
    bool dialog_open() const { return dialog_kind_ != GameDialog::None; }
    GameDialog dialog_kind() const { return dialog_kind_; }
    const Dialog& dialog() const { return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_; }
    Dialog& dialog() { return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_; }
    const Dialog* under_dialog() const { return under_.empty() ? nullptr : &under_.back().second; }
    GameDialog under_kind() const { return under_.empty() ? GameDialog::None : under_.back().first; }
    const std::vector<LetterBall>& dialog_balls() const { return dialog_balls_; }
    const std::vector<Particle>& dialog_particles() const { return dialog_particles_; }
    const HelpPager& help() const { return help_; }
    const HighScoreDialog* highscore_dialog() const { return highscore_ ? &*highscore_ : nullptr; }
    const SettingsDialog* settings_dialog() const { return settings_ ? &*settings_ : nullptr; }
    void window_mode_changed(bool fullscreen) { if (settings_) settings_->sync_fullscreen(fullscreen); }
    // Everything stands still under these (GameScene::paused()); the
    // round clock also stops under any other dialog (0x415300: +0x260).
    bool paused() const {
        return dialog_kind_ == GameDialog::PauseMenu || dialog_kind_ == GameDialog::ConfirmExit ||
               dialog_kind_ == GameDialog::ConfirmGiveUp || dialog_kind_ == GameDialog::HowToPlay ||
               dialog_kind_ == GameDialog::Settings || dialog_kind_ == GameDialog::Tutorial;
    }

    // ---- saving (0x416af0 / 0x4174c0) --------------------------------
    // 0x416420: saved on exit only while the round is in play (+0x264 == 0).
    bool can_save() const { return phase_ == GamePhase::Playing && !round_.over() && !finished_; }
    tt2::SavedGame snapshot() const {
        tt2::SavedGame g;
        g.mode = save_mode();
        g.round = round_.round();
        g.time_ms = round_.time_ms();
        g.score = round_.score();
        g.day = -1;
        for (const auto& w : round_.words()) g.blitz_words.push_back({w.word, w.solved});
        return g;
    }
    void leave() {
        if (can_save() && services_.save_game) services_.save_game(snapshot());
    }

    // The typed word (the current entry row, left to right).
    std::string typed() const {
        if (views_.empty()) return {};
        const auto& w = views_[round_.current()];
        std::vector<const LetterBall*> row;
        for (const auto& b : w.balls) if (b.played) row.push_back(&b);
        std::sort(row.begin(), row.end(), [](const LetterBall* a, const LetterBall* b) { return a->slot < b->slot; });
        std::string s;
        for (const auto* b : row) s.push_back(b->letter);
        return s;
    }

    // ---- input (0x415c50 keys, 0x4135e0 buttons, 0x417f50 racks) ----
    bool accepts_input() const { return phase_ == GamePhase::Playing && !round_.over() && !busy_ && !dialog_open(); }
    void type_letter(char c) {
        if (!accepts_input()) return;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return;
        const int i = available_ball(c);
        if (i >= 0) place(static_cast<std::size_t>(i));   // 0x412bd0: SOUND_letter_place
    }
    // 0x412d30: take back the last typed letter.
    void backspace() {
        if (!accepts_input()) return;
        auto& w = cur();
        int last = -1;
        for (std::size_t i = 0; i < w.balls.size(); ++i)
            if (w.balls[i].played && (last < 0 || w.balls[i].slot > w.balls[static_cast<std::size_t>(last)].slot))
                last = static_cast<int>(i);
        if (last < 0) return;
        move_ball(w, w.balls[static_cast<std::size_t>(last)], false, first_free_slot(w, false));
        play(SoundId::LetterClear);
        refresh_buttons();
    }
    // Key 5: ENTER with letters in the row, else LAST WORD.
    void key_return() {
        if (dialog_open()) {
            switch (dialog_kind_) {
                case GameDialog::PauseMenu: dialog_press("BUTTON_RESUME"); break;
                case GameDialog::ConfirmExit:
                case GameDialog::ConfirmGiveUp: dialog_press("BUTTON_YES"); break;
                default: dialog_press("BUTTON_OK"); break;
            }
            return;
        }
        if (!accepts_input()) return;
        if (typed().empty()) last_word_again();
        else submit();
    }
    void key_delete() { if (accepts_input()) clear(); }
    void key_space() { if (accepts_input()) twist(); }
    // Tab, Down, Right: NEXT; Up, Left: PREV. Both count as a pass.
    void key_word(bool forward) { if (accepts_input()) pass(forward); }
    void key_escape() {
        switch (dialog_kind_) {
            case GameDialog::None: open_pause_menu(); break;
            case GameDialog::PauseMenu: dialog_press("BUTTON_RESUME"); break;
            case GameDialog::ConfirmExit:
            case GameDialog::ConfirmGiveUp: dialog_press("BUTTON_NO"); break;
            case GameDialog::HowToPlay: dialog_press("BUTTON_OK"); break;
            case GameDialog::Settings: dialog_press("BUTTON_CANCEL"); break;
            default: break;
        }
    }
    // 0x4169d0, event 0x206: DIALOG_MENU, unless it is up already. Under
    // another dialog it waits (+0x20c = 2) until that dialog closes.
    void focus_lost() {
        if (pause_veil()) return;
        if (dialog_open()) menu_pending_ = true;
        else open_pause_menu();
    }
    bool menu_pending() const { return menu_pending_; }
    // Android (T2.2 of docs/plans/android-support.md): the app went to the
    // background, where Android may kill it without warning, so there is
    // no later quit to save on. Pause as on focus loss, and save the round
    // now, as closing the window would; the round goes on when resumed.
    // Resuming (resume_game) or a game over discards the save as usual.
    void enter_background() {
        focus_lost();
        leave();
    }

    bool pointer_over_clickable() const {
        if (settings_) return settings_->pointer_hot();
        if (dialog_open()) return dialog_pointer_hot(dialog());
        for (const auto& b : buttons_)
            if (b.hover && b.enabled && b.sprite.visible) return true;
        return hot_;
    }
    void mouse_move(float x, float y) {
        if (settings_) { settings_->mouse_move(x, y); return; }
        if (dialog_open()) { dialog_mouse_move(dialog(), x, y); return; }
        for (auto& b : buttons_) {
            const bool over = b.sprite.visible && b.enabled && b.contains(x, y);
            b.hover = over;
            if (!over) b.pressed = false;
        }
        // 0x4162b0: the hand over unsolved words, over every word after the round.
        const int r = rack_at(x, y);
        hot_ = ball_at(x, y) >= 0 || (r >= 0 && (round_.over() || !round_.words()[static_cast<std::size_t>(r)].solved));
    }
    void mouse_down(float x, float y) {
        if (settings_) { settings_->mouse_down(x, y); return; }
        if (dialog_open()) { dialog_mouse_down(dialog(), x, y); return; }
        for (auto& b : buttons_) b.pressed = b.sprite.visible && b.enabled && b.contains(x, y);
    }
    void mouse_up(float x, float y) {
        if (settings_) { settings_result(settings_->mouse_up(x, y)); return; }
        if (highscore_) { highscore_result(highscore_->mouse_up(x, y)); return; }
        if (dialog_open()) {
            const std::string name = dialog_mouse_up(dialog_, x, y);
            if (!name.empty()) dialog_press(name);
            return;
        }
        for (auto& b : buttons_) {
            const bool fire = b.pressed && b.sprite.visible && b.enabled && b.contains(x, y);
            b.pressed = false;
            if (fire) {
                play(SoundId::Button);
                press(b.sprite.name);
                return;
            }
        }
        // 0x417f50: a rack in the list. After the round it opens the
        // word's definition (the URL as in Classic, definition_url).
        const int r = rack_at(x, y);
        if (r >= 0) {
            const auto i = static_cast<std::size_t>(r);
            if (round_.over()) {
                if (services_.open_url) services_.open_url(definition_url(views_[i].answer));
            } else if (accepts_input() && !round_.words()[i].solved && i != round_.current()) {
                const std::size_t old = round_.current();
                round_.select(i);
                play(SoundId::LightningSkip);
                switch_to(old, round_.current());
            }
            return;
        }
        // A click on a ball of the current word plays it or takes it back.
        if (accepts_input()) {
            const int i = ball_at(x, y);
            if (i >= 0) {
                auto& w = cur();
                auto& b = w.balls[static_cast<std::size_t>(i)];
                if (!b.played) place(static_cast<std::size_t>(i));
                else if (is_last_played(w, b)) backspace();
            }
        }
    }

    // A scene button (mouse, keys, tests): 0x4135e0.
    void press(const std::string& name) {
        if (dialog_open()) return;
        const MenuButton* b = button(name);
        if (!b || !b->enabled || !b->sprite.visible) return;
        if (name == "BUTTON_Menu") open_pause_menu();
        else if (name == "BUTTON_GiveUp") { if (phase_ == GamePhase::Playing && !round_.over()) open_dialog(GameDialog::ConfirmGiveUp); }
        else if (!accepts_input()) return;
        else if (name == "BUTTON_Twist") twist();
        else if (name == "BUTTON_Clear") clear();
        else if (name == "BUTTON_LastWord") last_word_again();
        else if (name == "BUTTON_Enter") submit();
        else if (name == "BUTTON_NEXTWORD") pass(true);
        else if (name == "BUTTON_PREVWORD") pass(false);
    }

    void dialog_press(const std::string& name) {
        if (settings_) { settings_result(settings_->press(name)); return; }
        if (highscore_) { highscore_result(highscore_->press(name)); return; }
        const MenuButton* b = dialog_.button(name);
        if (!dialog_open() || !b || !b->enabled || !b->sprite.visible) return;
        play(SoundId::Button);
        switch (dialog_kind_) {
            case GameDialog::PauseMenu:
                if (name == "BUTTON_RESUME") close_dialog();
                else if (name == "BUTTON_STOP") push_dialog(GameDialog::ConfirmExit);
                else if (name == "BUTTON_HOWTOPLAY") push_dialog(GameDialog::HowToPlay);
                else if (name == "BUTTON_SETTINGS") push_dialog(GameDialog::Settings);
                break;
            case GameDialog::HowToPlay:
                if (name == "BUTTON_OK") pop_dialog();
                else help_.turn(dialog_, name == "BUTTON_PREV" ? -1 : 1);
                break;
            case GameDialog::Tutorial:
                if (name == "BUTTON_HELP") push_dialog(GameDialog::HowToPlay);
                else close_dialog();
                break;
            case GameDialog::ConfirmExit:
                if (name == "BUTTON_YES") { close_dialog(); leave(); exit_to_menu(); }
                else pop_dialog();
                break;
            case GameDialog::ConfirmGiveUp:
                // 0x4167a0 -> 0x411e10, the same as a time-out; NO 0x416850.
                close_dialog();
                if (name == "BUTTON_YES") lose();
                break;
            case GameDialog::NextRound:
                // 0x416910 -> 0x413030: Well Rounded (tracker 2), then round + 1.
                close_dialog();
                trophies_.push(tt2::count_round(profile_, tt2::GameMode::Blitz));
                changed();
                start_round(round_.round() + 1);
                break;
            case GameDialog::Congrats: open_dialog(highscores_ ? GameDialog::HighScores : GameDialog::EndGame); break;
            case GameDialog::EndGame:
                close_dialog();
                if (name == "BUTTON_REPLAY") start_round(1);
                else exit_to_menu();
                break;
            default: break;
        }
    }

    // ---- simulation (0x415300) ---------------------------------------
    void update(int dt_ms) {
        if (finished_) return;
        if (dialog_kind_ == GameDialog::HowToPlay) help_.tick(dialog_, dt_ms);
        if (settings_) settings_->tick(dt_ms);
        else if (highscore_) highscore_->tick(dt_ms);
        else if (dialog_open()) dialog_tick(dialog_, dt_ms);
        for (auto& b : dialog_balls_)
            if (b.tick(dt_ms)) play(SoundId::LetterLoad);
        for (auto& p : dialog_particles_) p.tick(dt_ms);
        if (menu_pending_ && !dialog_open()) {
            menu_pending_ = false;
            open_pause_menu();
        }
        if (paused()) return;
        queue_.update(dt_ms);
        popups_.update(dt_ms);
        if (trophies_.update(dt_ms, !dialog_open() && queue_.empty())) play(SoundId::TrophyWon);
        tick_views(dt_ms);
        tick_time_added(dt_ms);
        // The clock and the hint timer stop under dialogs and while busy.
        if (phase_ == GamePhase::Playing && !round_.over() && !busy_ && !dialog_open()) {
            const auto rep = round_.tick(dt_ms, !typed().empty());
            for (int i = 0; i < rep.warnings; ++i) play(SoundId::LightningTimeLow);
            if (rep.hint) show_hint();
            if (!time_added_ms_) timer_fraction_ = static_cast<float>(round_.time_ms()) / tt2::LIGHTNING_START_MS;
            if (rep.time_out) lose();
        }
        for (auto& b : buttons_) b.sprite.frame = b.frame_for_state();
    }

private:
    // ---- round flow ---------------------------------------------------
    // 0x4138e0: deal round `number` (round 1 is a new game).
    void start_round(int number) {
        round_.start(number, rng_, bag_, static_cast<std::size_t>(profile_.max_word_length));
        profile_.bag = bag_.state();
        changed();
        setup_views();
        // 0x43c940(i == 0): only the current word's balls flip in.
        auto& w = cur();
        for (std::size_t i = 0; i < w.balls.size(); ++i) {
            w.balls[i].frame = 0;
            w.balls[i].reveal_delay_ms = static_cast<int>(i) * BALL_REVEAL_STAGGER_MS;
            w.balls[i].reveal_elapsed_ms = 0;
        }
        play(SoundId::LightningStartRound);
        open_tutorial_once();
        refresh_buttons();
    }

    // 0x4174c0: the round comes back from the save, hints and the trophy
    // flags start fresh, the first unsolved word is selected, the file is
    // deleted. False (nothing changed) when the save cannot be used.
    bool resume_game(const tt2::SavedGame& g) {
        if (!g.valid() || !g.blitz()) return false;
        std::vector<tt2::LightningRound::SavedWord> words;
        for (const auto& w : g.blitz_words) words.push_back({w.word, w.completed});
        round_.restore(g.round, g.time_ms, g.score, words);
        setup_views();
        if (services_.discard_save) services_.discard_save();
        refresh_buttons();
        return true;
    }

    void setup_views() {
        phase_ = GamePhase::Playing;
        busy_ = false;
        time_added_ms_ = 0;
        timer_fraction_ = static_cast<float>(round_.time_ms()) / tt2::LIGHTNING_START_MS;
        queue_.clear();
        popups_.drop_waiting();
        solved_flip_.clear();
        views_.clear();
        const auto& words = round_.words();
        std::size_t max_len = 0;
        for (const auto& w : words) max_len = std::max(max_len, w.word.size());
        for (std::size_t i = 0; i < words.size(); ++i) {
            BlitzWordView v;
            v.answer = words[i].word;
            const std::string order = scrambled(v.answer);
            for (std::size_t k = 0; k < order.size(); ++k) {
                LetterBall b;
                b.letter = order[k];
                b.slot = static_cast<int>(k);
                b.start_x = b.target_x = b.x = static_cast<float>(k) * BALL_PITCH + BALL_HALF;
                b.start_y = b.target_y = b.y = BALL_HALF;
                v.balls.push_back(b);
            }
            if (words[i].solved) show_solved(v, words[i].word);
            v.x = BLITZ_LIST_X;
            v.y = blitz_list_y(words.size(), i);
            // No anchor call (top-left), at x = panel width + 10 (0x414c70).
            v.mark = sprite("BLITZ_MARK", blitz_mark_sheet(), blitz_panel_w(max_len) + BLITZ_PANEL_EDGE, v.y);
            v.mark.visible = false;
            views_.push_back(v);
        }
        if (!views_.empty()) place_current(views_[round_.current()]);
    }

    // A solved word: its letters spell the answer in the entry row and turn
    // into square tiles (0x43a310(1)).
    void show_solved(BlitzWordView& v, const std::string& as) {
        std::vector<bool> used(v.balls.size(), false);
        for (std::size_t k = 0; k < as.size(); ++k) {
            for (std::size_t i = 0; i < v.balls.size(); ++i) {
                if (used[i] || v.balls[i].letter != as[k]) continue;
                used[i] = true;
                LetterBall& b = v.balls[i];
                b.played = true;
                b.slot = static_cast<int>(k);
                b.start_x = b.target_x = b.x = static_cast<float>(k) * BALL_PITCH + BALL_HALF;
                b.start_y = b.target_y = b.y = BALL_HALF - BALL_PITCH;
                b.frame = BLITZ_SOLVED_FRAME;
                break;
            }
        }
    }

    void place_current(BlitzWordView& v) {
        v.x = v.to_x = blitz_current_x(v.answer.size());
        v.y = v.to_y = BLITZ_RACK_Y;
        v.scale = v.to_scale = 1.0f;
        v.entry_alpha = v.entry_to = 1.0f;
        v.move_ms = 0;
        v.list_look = false;
    }

    // 0x414660 / 0x414780: the old word goes back to the list (its typed
    // letters cleared first, then a 500 ms wait), the new one comes up.
    void switch_to(std::size_t old_i, std::size_t new_i) {
        if (old_i == new_i || old_i >= views_.size() || new_i >= views_.size()) return;
        int wait = 0;
        auto& old = views_[old_i];
        if (has_played(old) && !round_.words()[old_i].solved) {
            return_all_letters(old);
            play(SoundId::LetterClear);
            wait = BLITZ_SWITCH_CLEAR_MS;
        }
        old.slide_to(BLITZ_LIST_X, blitz_list_y(views_.size(), old_i), BLITZ_LIST_SCALE, 0.0f, wait, BLITZ_SWITCH_MS);
        auto& now = views_[new_i];
        now.slide_to(blitz_current_x(now.answer.size()), BLITZ_RACK_Y, 1.0f, 1.0f, wait, BLITZ_SWITCH_MS);
        refresh_buttons();
    }

    // NEXT / PREV: always a pass, with the sound, even with nowhere to go.
    void pass(bool forward) {
        const std::size_t old = round_.current();
        round_.pass(forward);
        play(SoundId::LightningSkip);
        switch_to(old, round_.current());
    }

    // 0x4116e0: ENTER.
    void submit() {
        const std::string word = typed();
        if (word.empty()) return;
        const std::size_t i = round_.current();
        const auto rep = round_.submit(word);
        refresh_buttons();   // the word's last word changed
        using S = tt2::LightningRound::Submit;
        if (rep.status == S::Ignored) return;   // too short: nothing at all
        if (rep.status == S::Rejected) {
            play(SoundId::WordReject);
            popups_.push(PopupKind::NotListed);
            clear();   // 0x4113b0: SOUND_letter_clear
            return;
        }
        auto& w = views_[i];
        // 0x439cb0 / 0x43c890(1): the row's balls turn into square tiles.
        for (auto& b : w.balls) {
            if (!b.played) continue;
            b.frame = BALL_IDLE_FRAME;
            solved_flip_.push_back({i, static_cast<std::size_t>(&b - w.balls.data()), 0});
        }
        trophies_.push(tt2::count_word(profile_));
        if (rep.uncommon) trophies_.push(tt2::count_uncommon(profile_));
        play(SoundId::WordBingo);
        if (rep.status == S::RoundWon) {
            // Trophies 3-6 with the round's flags and the clock as it is.
            for (int id : tt2::award_lightning_round(profile_, tt2::LightningRoundFlags{
                                                               round_.twisted(), round_.passed(), round_.hinted(),
                                                               round_.time_ms()}))
                trophies_.push(id);
            changed();
            reveal(false);
            return;
        }
        changed();
        // +8 s: popup 6, the sound, the bar's clip grows (frame 1) for
        // (L + 4) * 100 ms while input waits; then 0x411520 moves on to
        // the next unsolved word.
        popups_.push(PopupKind::ExtraTime);
        play(SoundId::LightningTimeAdded);
        busy_ = true;
        time_added_total_ms_ = blitz_time_added_ms(w.answer.size());
        time_added_ms_ = time_added_total_ms_;
        timer_from_ = timer_fraction_;
        refresh_buttons();
    }

    void tick_time_added(int dt) {
        if (time_added_ms_ <= 0) return;
        time_added_ms_ = std::max(0, time_added_ms_ - dt);
        const float to = static_cast<float>(round_.time_ms()) / tt2::LIGHTNING_START_MS;
        const float t = 1.0f - static_cast<float>(time_added_ms_) / static_cast<float>(time_added_total_ms_);
        timer_fraction_ = timer_from_ + (to - timer_from_) * t;
        if (time_added_ms_ > 0) return;
        busy_ = false;
        const std::size_t old = round_.current();
        round_.advance();
        switch_to(old, round_.current());
        refresh_buttons();
    }

    // 0x411e10: time out or GIVE UP.
    void lose() {
        if (phase_ != GamePhase::Playing) return;
        round_.give_up();   // no-op after a time-out (the round is already lost)
        int wait = 0;
        if (!views_.empty() && has_played(cur())) {
            return_all_letters(cur());
            play(SoundId::LetterClear);
            wait = BLITZ_LOSE_CLEAR_MS;
        }
        if (services_.discard_save) services_.discard_save();   // 0x461840 (0x411ef0, lost)
        reveal(true, wait);
    }

    // 0x411ef0: every word back in the list; per word (400 ms, +100 ms)
    // solved letters come back into the rack, unsolved racks spell the
    // answer, and the marks pop; 2000 ms later the round-end step.
    void reveal(bool lost, int wait = 0) {
        phase_ = GamePhase::Reveal;
        busy_ = true;
        time_added_ms_ = 0;
        const std::size_t cur_i = round_.current();
        if (cur_i < views_.size()) {
            auto& v = views_[cur_i];
            v.slide_to(BLITZ_LIST_X, blitz_list_y(views_.size(), cur_i), BLITZ_LIST_SCALE, 0.0f, wait, BLITZ_SWITCH_MS);
        }
        for (std::size_t i = 0; i < views_.size(); ++i) {
            views_[i].reveal_delay_ms = wait + BLITZ_REVEAL_FIRST_MS + BLITZ_REVEAL_STEP_MS * static_cast<int>(i);
            views_[i].reveal_elapsed_ms = -1;
        }
        refresh_buttons();
        queue_.push_delay(wait + blitz_reveal_ms(views_.size()));
        queue_.push_method([this, lost] {
            busy_ = false;
            if (lost) game_over();
            else {
                // 0x428a60 -> case 3: the sound and DIALOG_NEXTROUND.
                phase_ = GamePhase::AwaitNext;
                play(SoundId::NextRoundAchieved);
                open_dialog(GameDialog::NextRound);
            }
            refresh_buttons();
        });
    }

    void start_word_reveal(std::size_t i) {
        auto& v = views_[i];
        const bool solved = round_.words()[i].solved;
        v.mark.visible = true;
        v.mark.frame = solved ? BLITZ_MARK_SOLVED_FRAME : 0;
        v.mark.alpha = 0.0f;
        v.mark.scale_x = 0.0f;   // grows sideways from its left edge
        v.entry_alpha = v.entry_to = 0.0f;
        if (solved) {
            // 0x43bad0(0, j, 1.0): the entry row's tiles snap into the rack,
            // slot for slot; their small tiles show at once and only the
            // 60 px balls fade in.
            for (auto& b : v.balls) {
                if (!b.played) continue;
                b.played = false;
                b.start_x = b.target_x = b.x = static_cast<float>(b.slot) * BALL_PITCH + BALL_HALF;
                b.start_y = b.target_y = b.y = BALL_HALF;
                b.move_ms = 0;
            }
            v.letters_alpha = 0.0f;
        } else {
            // 0x43c6a0: the rack's balls in answer order.
            return_all_letters(v);
            std::vector<bool> used(v.balls.size(), false);
            for (std::size_t k = 0; k < v.answer.size(); ++k) {
                for (std::size_t j = 0; j < v.balls.size(); ++j) {
                    if (used[j] || v.balls[j].letter != v.answer[k]) continue;
                    used[j] = true;
                    move_ball(v, v.balls[j], false, static_cast<int>(k), BALL_RELAYOUT_MS);
                    break;
                }
            }
        }
    }

    // 0x416440: the blitz table, then CONGRATS or ENDGAME.
    void game_over() {
        phase_ = GamePhase::GameOver;
        bool high = false;
        if (highscores_ && round_.score() > 0) {
            const int rank = highscores_->submit(tt2::GameMode::Blitz, profile_.name, round_.score(),
                                                 std::to_string(round_.round()));
            if (rank >= 0) { play(SoundId::HighScoreAchieved); high = true; }
            changed();
        }
        open_dialog(high ? GameDialog::Congrats : GameDialog::EndGame);
    }

    // 0x4138e0: the tutorial once per player (mode flag 2).
    void open_tutorial_once() {
        bool& seen = profile_.mode_flags[static_cast<std::size_t>(tt2::GameMode::Blitz)];
        if (seen) return;
        seen = true;
        changed();
        open_dialog(GameDialog::Tutorial);
    }

    // 0x4131f0 via the update: the next answer letter appears as a ghost
    // in the entry row; the sound only when a letter was revealed.
    void show_hint() {
        auto& w = cur();
        if (w.hint_fade_ms.size() >= w.answer.size()) return;
        w.hint_fade_ms.push_back(0);
        play(SoundId::LightningHint);
    }

    void tick_views(int dt) {
        for (std::size_t i = 0; i < views_.size(); ++i) {
            auto& v = views_[i];
            v.tick_move(dt);
            for (auto& h : v.hint_fade_ms) h = std::min(BLITZ_HINT_FADE_MS, h + dt);
            for (auto& b : v.balls)
                if (b.tick(dt) && i == round_.current()) play(SoundId::LetterLoad);
            if (v.reveal_delay_ms >= 0) {
                v.reveal_delay_ms -= dt;
                if (v.reveal_delay_ms < 0) {
                    v.reveal_elapsed_ms = -v.reveal_delay_ms;
                    v.reveal_delay_ms = -1;
                    start_word_reveal(i);
                }
            } else if (v.reveal_elapsed_ms >= 0) {
                v.reveal_elapsed_ms += dt;
            }
            if (v.reveal_elapsed_ms >= 0) {
                const int e = v.reveal_elapsed_ms;
                v.mark.alpha = std::min(1.0f, static_cast<float>(e) / BLITZ_MARK_FADE_MS);
                if (e < BLITZ_MARK_GROW_MS)
                    v.mark.scale_x = BLITZ_MARK_OVERSHOOT *
                                     ease(Easing::Smoothstep, static_cast<float>(e) / BLITZ_MARK_GROW_MS);
                else if (e < BLITZ_MARK_GROW_MS + BLITZ_MARK_SETTLE_MS)
                    v.mark.scale_x = BLITZ_MARK_OVERSHOOT - (BLITZ_MARK_OVERSHOOT - 1.0f) *
                                                                ease(Easing::Smoothstep, static_cast<float>(e - BLITZ_MARK_GROW_MS) / BLITZ_MARK_SETTLE_MS);
                else
                    v.mark.scale_x = 1.0f;
                v.letters_alpha = std::min(1.0f, std::max(v.letters_alpha, static_cast<float>(e) / BLITZ_REVEAL_FADE_MS));
            }
        }
        // The solved flip: 6 frames at 60 ms per ball.
        for (auto it = solved_flip_.begin(); it != solved_flip_.end();) {
            it->elapsed += dt;
            LetterBall& b = views_[it->word].balls[it->ball];
            const int step = std::min(BLITZ_SOLVED_FRAME - BALL_IDLE_FRAME, it->elapsed / BLITZ_SOLVED_FRAME_MS);
            b.frame = BALL_IDLE_FRAME + step;
            if (b.frame == BLITZ_SOLVED_FRAME) it = solved_flip_.erase(it);
            else ++it;
        }
    }

    // ---- dialogs ----------------------------------------------------
    void open_pause_menu() {
        if (finished_ || dialog_open()) return;
        open_dialog(GameDialog::PauseMenu);
    }
    void open_dialog(GameDialog kind) {
        under_.clear();
        show_dialog(kind);
    }
    void show_dialog(GameDialog kind) {
        dialog_kind_ = kind;
        dialog_balls_.clear();
        dialog_particles_.clear();
        settings_.reset();
        highscore_.reset();
        switch (kind) {
            case GameDialog::PauseMenu: dialog_ = build_pause_menu(); break;
            case GameDialog::ConfirmExit:
                dialog_ = build_yes_no(game_strings::EXIT_HEADER, game_strings::EXIT_BODY, false, services_.wrap_text);
                break;
            case GameDialog::ConfirmGiveUp:
                dialog_ = build_yes_no(game_strings::GIVEUP_HEADER, game_strings::GIVEUP_BODY, false, services_.wrap_text);
                break;
            case GameDialog::Settings:
                dialog_ = Dialog{};
                settings_.emplace(services_.settings.current ? services_.settings.current() : tt2::GameSettings{},
                                  profile_.max_word_length, services_.settings.fullscreen_supported,
                                  SettingsDialog::Services{services_.play_sound, services_.settings.apply_sound_volume,
                                                           services_.settings.apply_music_volume});
                break;
            case GameDialog::HowToPlay:
                dialog_ = build_help();
                help_ = HelpPager{};
                break;
            case GameDialog::NextRound:
                dialog_ = build_next_round();
                dialog_balls_ = next_round_balls(NEXT_ROUND_LETTERBALLS);
                dialog_particles_ = next_round_particles(dialog_balls_, particle_rng_);
                break;
            case GameDialog::EndGame: dialog_ = build_end_game(); break;
            case GameDialog::Congrats:
                dialog_ = build_congrats();
                dialog_balls_ = congrats_balls(CONGRATS_LETTERBALLS);
                dialog_particles_ = next_round_particles(dialog_balls_, particle_rng_);
                break;
            case GameDialog::HighScores:
                dialog_ = Dialog{};
                if (highscores_)
                    highscore_.emplace(*highscores_, HighScoreDialog::Opener::Game, tt2::GameMode::Blitz, services_.play_sound);
                break;
            case GameDialog::Tutorial: dialog_ = build_tutorial(static_cast<int>(tt2::GameMode::Blitz)); break;
            default: dialog_ = Dialog{}; break;
        }
        for (auto& b : buttons_) b.hover = b.pressed = false;
        hot_ = false;
        refresh_buttons();
    }
    void close_dialog() { open_dialog(GameDialog::None); }
    void push_dialog(GameDialog kind) {
        if (dialog_kind_ != GameDialog::None) under_.emplace_back(dialog_kind_, dialog_);
        show_dialog(kind);
    }
    void pop_dialog() {
        if (under_.empty()) { close_dialog(); return; }
        auto [kind, d] = std::move(under_.back());
        under_.pop_back();
        show_dialog(GameDialog::None);
        dialog_kind_ = kind;
        dialog_ = std::move(d);
        for (auto& b : dialog_.buttons) b.hover = b.pressed = false;
        refresh_buttons();
    }
    void settings_result(SettingsDialog::Result r) {
        if (!settings_) return;
        if (r == SettingsDialog::Result::Ok) {
            profile_.max_word_length = settings_->max_word_length();
            changed();
            if (services_.settings.commit) services_.settings.commit(settings_->values());
        }
        if (r == SettingsDialog::Result::Ok || r == SettingsDialog::Result::Cancel) pop_dialog();
    }
    void highscore_result(HighScoreDialog::Result r) {
        if (r == HighScoreDialog::Result::None) return;
        close_dialog();
        exit_to_menu();
    }
    void exit_to_menu() {
        if (finished_) return;
        finished_ = true;
        if (services_.on_exit) services_.on_exit();
    }

    // ---- letters ------------------------------------------------------
    BlitzWordView& cur() { return views_[round_.current()]; }
    const BlitzWordView& cur() const { return views_[round_.current()]; }
    static bool has_played(const BlitzWordView& w) {
        for (const auto& b : w.balls) if (b.played) return true;
        return false;
    }
    static int first_free_slot(const BlitzWordView& w, bool played_row) {
        const int n = static_cast<int>(w.balls.size());
        for (int s = 0; s < n; ++s) {
            bool used = false;
            for (const auto& b : w.balls) if (b.played == played_row && b.slot == s) used = true;
            if (!used) return s;
        }
        return -1;
    }
    static bool is_last_played(const BlitzWordView& w, const LetterBall& ball) {
        for (const auto& b : w.balls) if (b.played && b.slot > ball.slot) return false;
        return ball.played;
    }
    static void move_ball(BlitzWordView&, LetterBall& b, bool played_row, int slot, int ms = BALL_MOVE_MS) {
        if (slot < 0) return;
        b.played = played_row;
        b.slot = slot;
        b.start_x = b.x;
        b.start_y = b.y;
        b.target_x = static_cast<float>(slot) * BALL_PITCH + BALL_HALF;
        b.target_y = played_row ? BALL_HALF - BALL_PITCH : BALL_HALF;
        b.move_ms = ms;
        b.move_elapsed_ms = 0;
    }
    int available_ball(char c) const {
        const auto& w = cur();
        int best = -1;
        for (std::size_t i = 0; i < w.balls.size(); ++i) {
            if (w.balls[i].played || w.balls[i].letter != c) continue;
            if (best < 0 || w.balls[i].slot < w.balls[static_cast<std::size_t>(best)].slot) best = static_cast<int>(i);
        }
        return best;
    }
    void place(std::size_t i, bool click = true) {
        auto& w = cur();
        const int slot = first_free_slot(w, true);
        if (slot < 0) return;
        move_ball(w, w.balls[i], true, slot);
        if (click) play(SoundId::LetterPlace);
        refresh_buttons();
    }
    static void return_all_letters(BlitzWordView& w) {
        std::vector<std::size_t> played;
        for (std::size_t i = 0; i < w.balls.size(); ++i) if (w.balls[i].played) played.push_back(i);
        std::sort(played.begin(), played.end(), [&](std::size_t a, std::size_t b) { return w.balls[a].slot < w.balls[b].slot; });
        for (std::size_t i : played) move_ball(w, w.balls[i], false, first_free_slot(w, false));
    }
    // 0x4113b0.
    void clear() {
        if (views_.empty() || !has_played(cur())) return;
        return_all_letters(cur());
        play(SoundId::LetterClear);
        refresh_buttons();
    }
    // 0x412e90: SOUND_word_last once, then the word's last word's letters.
    void last_word_again() {
        play(SoundId::WordLast);
        const std::string last = round_.current_word().last_word;
        if (last.empty()) return;
        return_all_letters(cur());
        for (char c : last) {
            const int i = available_ball(c);
            if (i >= 0) place(static_cast<std::size_t>(i), false);
        }
        refresh_buttons();
    }
    // 0x413090: SOUND_word_twist, the round's twist flag, the twist counter.
    // The shuffle itself is GameScene's (same caveats on the sound).
    void twist() {
        auto& w = cur();
        std::vector<std::size_t> avail;
        std::string letters;
        for (std::size_t i = 0; i < w.balls.size(); ++i) {
            if (w.balls[i].played) continue;
            if (w.balls[i].moving()) return;
            avail.push_back(i);
            letters.push_back(w.balls[i].letter);
        }
        if (letters.size() < 2 || letters.find_first_not_of(letters[0]) == std::string::npos) return;
        play(SoundId::WordTwist);
        std::vector<int> slots;
        for (std::size_t i : avail) slots.push_back(w.balls[i].slot);
        auto order = [&](const std::vector<int>& s) {
            std::vector<std::pair<int, char>> cells;
            for (std::size_t k = 0; k < avail.size(); ++k) cells.emplace_back(s[k], w.balls[avail[k]].letter);
            std::sort(cells.begin(), cells.end());
            std::string out;
            for (const auto& c : cells) out.push_back(c.second);
            return out;
        };
        const std::string before = order(slots);
        for (int tries = 0; tries < TWIST_MAX_SHUFFLES; ++tries) {
            rng_.shuffle(slots);
            const std::string now = order(slots);
            if (now != before && now != w.answer) break;
        }
        for (std::size_t k = 0; k < avail.size(); ++k) move_ball(w, w.balls[avail[k]], false, slots[k], BALL_RELAYOUT_MS);
        round_.twist();
        trophies_.push(tt2::count_twist(profile_));
        changed();
    }
    // GameScene::scrambled: shuffled with the game LCG until it no longer
    // spells the word (the deal order was not traced).
    std::string scrambled(const std::string& word) {
        std::vector<char> letters(word.begin(), word.end());
        for (int tries = 0; tries < 8 && std::string(letters.begin(), letters.end()) == word; ++tries)
            rng_.shuffle(letters);
        return std::string(letters.begin(), letters.end());
    }

    // ---- hit tests ------------------------------------------------------
    int ball_at(float x, float y) const {
        if (views_.empty() || round_.over()) return -1;
        const auto& w = cur();
        if (w.scale < 1.0f) return -1;
        for (std::size_t i = 0; i < w.balls.size(); ++i) {
            const auto& b = w.balls[i];
            const float bx = b.played ? w.entry_x() + b.target_x : w.screen_x(b.target_x);
            const float by = b.played ? BLITZ_ENTRY_Y + b.target_y + BALL_PITCH : w.screen_y(b.target_y);
            if (std::fabs(x - bx) <= BALL_HALF && std::fabs(y - by) <= BALL_HALF) return static_cast<int>(i);
        }
        return -1;
    }
    // A rack sitting in the list (the current one is not in it while playing).
    int rack_at(float x, float y) const {
        for (std::size_t i = 0; i < views_.size(); ++i) {
            const auto& w = views_[i];
            if (w.scale >= 1.0f) continue;
            const float wd = static_cast<float>(w.answer.size()) * BALL_PITCH * w.scale;
            const float ht = BALL_PITCH * w.scale;
            if (x >= w.x && x < w.x + wd && y >= w.y && y < w.y + ht) return static_cast<int>(i);
        }
        return -1;
    }

    // ---- buttons (dialogs.xml SCENE_BLITZ, refresh 0x413340) ----------
    void build_buttons() {
        auto add = [&](const char* name, const char* image, int w, int h, float x, float y) {
            MenuButton b;
            b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
            buttons_.push_back(b);
        };
        add("BUTTON_Menu", "BUTTON_MENU", 119, 200, 10, 554);
        add("BUTTON_Twist", "BUTTON_Twist", 119, 200, 253, 390);
        add("BUTTON_Clear", "BUTTON_Clear", 119, 200, 375, 390);
        add("BUTTON_LastWord", "BUTTON_LastWord", 119, 200, 498, 390);
        add("BUTTON_Enter", "BUTTON_Enter", 119, 200, 622, 390);
        add("BUTTON_GiveUp", "BUTTON_GiveUp", 157, 208, 417, 456);
        // 0x414c70 centres the arrows on the panel above and below the list.
        add("BUTTON_PREVWORD", "IMAGE_BTN_ARROW_UP", 76, 248, 60, 12);
        add("BUTTON_NEXTWORD", "IMAGE_BTN_ARROW_DOWN", 76, 248, 60, 491);
    }
    // 0x413340: everything off while a dialog is up or the scene is busy;
    // else Menu, GiveUp and the arrows on, Clear with letters in the row,
    // LastWord with an empty row and a last word, Enter with a full row
    // (INFERRED from the string-length test), Twist with 2+ rack letters.
    // BUTTON_NextRound is never shown in Lightning, so the port has none.
    void refresh_buttons() {
        const bool live = phase_ == GamePhase::Playing && !round_.over() && !busy_ && !dialog_open();
        std::size_t played = 0, total = 0, length = 0;
        bool has_last = false;
        if (!views_.empty()) {
            const auto& w = cur();
            total = w.balls.size();
            length = w.answer.size();
            for (const auto& b : w.balls) if (b.played) ++played;
            has_last = !round_.current_word().last_word.empty();
        }
        // 0x414c70: the arrows centred on the panel width (floored by
        // 0x419900, INFERRED), PREV one arrow height above the panel,
        // NEXT 10 px below it. Worked example n = 5, maxL = 6: w = 146,
        // PREV at (35, 113), NEXT at (35, 385).
        const float w = blitz_panel_w(max_word_length());
        const float top = blitz_panel_top(views_.size());
        const float bottom = top + blitz_panel_h(views_.size());
        for (auto& b : buttons_) {
            const std::string& n = b.sprite.name;
            const float arrow_x = std::floor(w * 0.5f - static_cast<float>(b.sprite.sheet.frame_w) * 0.5f);
            if (n == "BUTTON_Menu") b.enabled = !dialog_open();
            else if (n == "BUTTON_GiveUp") b.enabled = live;
            else if (n == "BUTTON_PREVWORD") {
                b.enabled = live;
                b.sprite.x = arrow_x;
                b.sprite.y = top - static_cast<float>(b.sprite.sheet.frame_h);
            } else if (n == "BUTTON_NEXTWORD") {
                b.enabled = live;
                b.sprite.x = arrow_x;
                b.sprite.y = bottom + BLITZ_PANEL_EDGE;
            }
            else if (n == "BUTTON_Clear") b.enabled = live && played > 0;
            else if (n == "BUTTON_LastWord") b.enabled = live && played == 0 && has_last;
            else if (n == "BUTTON_Enter") b.enabled = live && length > 0 && played == length;
            else if (n == "BUTTON_Twist") b.enabled = live && total - played > 1;
        }
    }

    void play(SoundId id) { if (services_.play_sound) services_.play_sound(id); }
    void changed() { if (services_.on_profile_change) services_.on_profile_change(); }

    struct SolvedFlip {
        std::size_t word, ball;
        int elapsed;
    };

    tt2::PlayerProfile& profile_;
    tt2::HighScoreManager* highscores_;
    Services services_;
    tt2::Random rng_;
    tt2::Bag bag_;
    tt2::LightningRound round_;
    std::vector<BlitzWordView> views_;
    std::vector<SolvedFlip> solved_flip_;
    GamePhase phase_ = GamePhase::Playing;
    bool finished_ = false;
    bool busy_ = false;              // +0x2f9: time added / round-end reveal
    int time_added_ms_ = 0, time_added_total_ms_ = 0;
    float timer_fraction_ = 1.0f, timer_from_ = 1.0f;
    bool hot_ = false;
    bool menu_pending_ = false;      // +0x20c = 2: focus lost under a dialog
    std::vector<MenuButton> buttons_;
    MessagePopups popups_;
    TrophyPopups trophies_;
    ModifierQueue queue_;
    HelpPager help_;
    std::vector<std::pair<GameDialog, Dialog>> under_;
    std::optional<SettingsDialog> settings_;
    std::optional<HighScoreDialog> highscore_;
    std::vector<LetterBall> dialog_balls_;
    std::vector<Particle> dialog_particles_;
    tt2::Random particle_rng_;
    GameDialog dialog_kind_ = GameDialog::None;
    Dialog dialog_;
};

}  // namespace tt2ui
