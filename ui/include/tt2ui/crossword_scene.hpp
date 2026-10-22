// The Crossword game scene: `CrisscrossScene` / SCENE_CRISSCROSS in the
// original. The rules live in the engine (tt2/round_crossword.hpp); this is
// the layout, the input and the round flow around them, from the
// decompilation in docs/reverse-engineering/crossword.md (addresses and
// evidence ids are repeated next to the numbers they justify). Free of the
// renderer like GameScene and BlitzScene: `ui/src/main.cpp` only draws the
// state exposed here, through the same member names (`with_play`).
//
// Built so far: the scene shell (plan T6b.1: buttons, sprites and the UI
// bar at their dialogs.xml places, the button refresh, the pause menu and
// the popup queues), the round (T6b.3: the grid, the racks, typing into the
// selected row, ENTER, CLEAR, LAST WORD, the take-back, the 700 ms advance,
// the solved puzzle, GIVE UP and the game over) and the controls (T6b.4:
// TWIST, ZOOM, the word arrows, row and rack clicks, the grid view's pan /
// turn / zoom as model state, popups 4 / 7 / 9 and the highlight, trophies
// 8 / 9, saving and resuming, the tutorial, the definition links) and the
// view (T6b.5: the grid's, the rack's and the highlight's sprites, the
// sheen, the art list for the preload); draw_crossword in main.cpp draws
// them.
//
// Deviation from the scene shape (D-11, crossword.md §9): the CRT `rand()`
// state is process-wide in the exe, seeded once from the clock, so the
// front end owns one `tt2::CrtRandom` and passes it in by reference. It must
// outlive the scene, like the word trees.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/profile.hpp"
#include "tt2/random.hpp"
#include "tt2/round_crossword.hpp"
#include "tt2/saved_game.hpp"
#include "tt2/trophies.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/game_scene.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/highscore_dialog.hpp"
#include "tt2ui/modifier.hpp"
#include "tt2ui/profile_dialogs.hpp"
#include "tt2ui/scene_popups.hpp"
#include "tt2ui/settings_dialog.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

// ---------------------------------------------------------------------------
// Layout (dialogs.xml SCENE_CRISSCROSS lines 135-167, crossword.md §1.1)
// ---------------------------------------------------------------------------
constexpr float CROSSWORD_POPUP_X = 400.0f;    // 0x419d40: +0x34c (CW5)
constexpr float CROSSWORD_POPUP_Y = 70.0f;     // 0x419d40: +0x350; Classic uses 150 (CW5)
constexpr float CROSSWORD_UI_BAR_Y = 547.0f;   // dialogs.xml:138 UI_BAR
constexpr float CROSSWORD_UI_TEXT_Y = 574.0f;  // dialogs.xml:139-145, every UI bar label

// ---------------------------------------------------------------------------
// Grid view and round flow (crossword.md §2.2, §3.3, §3.7, §5)
// ---------------------------------------------------------------------------
// One grid cell at scale 1.0: the highlight frame is 572 = 8 * 69 + 20 px
// wide (0x41de10, §3.3), and the user's zoomed-in screenshot measures a
// 69 px pitch (CW73).
constexpr float CROSSWORD_CELL_PX = 69.0f;
// 0x41d920 sets the grid view's scale to 0.5 at once on every round setup
// and every load (@0x41ddf8, CW57; the constant at 0x59b858, CW61), then
// selects the root. ZOOM out and the reveal scale to it too.
constexpr float CROSSWORD_OVERVIEW_SCALE = 0.5f;
// The scale the grid view is built at (0x44a320, CW65) and that ZOOM and the
// zoom back in after a select go to (fld1 @0x41b1c6, @0x41d7db, CW47, CW50).
constexpr float CROSSWORD_ZOOMED_SCALE = 1.0f;
// 0x41d160 pans the selected word's centre to (400, 189) (the doubles
// 0x59b958 = 400.0, 0x59b950 = 189.0, file read; screenshot CW73).
constexpr float CROSSWORD_PAN_X = 400.0f;
constexpr float CROSSWORD_PAN_Y = 189.0f;
// 0x41d160: ModifierLocationLine with smoothstep (0x4513d0), 500 ms
// (mov edx,0x1f4 @0x41d304, CW49; INFERRED duration, crossword.md §11).
// 0x41cab0 pans the same way after a zoom-out (@0x41cc2f, CW53).
constexpr int CROSSWORD_PAN_MS = 500;
// 0x41d390 -> 0x41c910 when the new word's direction differs (file read):
// ModifierDelay 300 ms (vtable 0x5943bc, +0x44 = 0x12c @0x41c966; the
// 700 ms advance uses the same vtable, @0x41ea56), then a rotation (vtable
// 0x59714c, +0x50 = 0x320 @0x41c9dc: INFERRED 800 ms, no easing) on the grid
// view and, the other way, on every letter, so letters stay upright.
constexpr int CROSSWORD_TURN_WAIT_MS = 300;
constexpr int CROSSWORD_TURN_MS = 800;
// The turn's targets (0x41d390, file read): 0 for a horizontal word (fldz
// @0x41d50a), -pi/2 for a vertical one (0x59b894), so the selected word
// reads left to right on screen.
constexpr float CROSSWORD_TURN_VERTICAL = -1.5707963f;
// The zoom back in after a select: ModifierScale to 1.0 with smoothstep,
// 1500 ms (mov esi,0x5dc @0x41d7b9, CW50; INFERRED duration).
constexpr int CROSSWORD_ZOOM_IN_MS = 1500;
// ZOOM out and the reveal's zoom-out: ModifierScale to 0.5 with smoothstep,
// 1000 ms (mov esi,0x3e8 @0x41b27d, CW47, and @0x41ffa5, file read;
// INFERRED duration).
constexpr int CROSSWORD_ZOOM_OUT_MS = 1000;
// 0x421e50 (CW42): a click in the grid counts only inside this box.
constexpr float CROSSWORD_CLICK_LEFT = 85.0f, CROSSWORD_CLICK_RIGHT = 715.0f;
constexpr float CROSSWORD_CLICK_TOP = 20.0f, CROSSWORD_CLICK_BOTTOM = 360.0f;
// The highlight (+0x3a8, built in code by 0x41de10, crossword.md §3.3):
// IMAGE_CROSSWORDHIGHLIGHT, 12 frames of 572 x 90, at the entry row's
// (x - 10, y - 11); frame len - 3, or len + 3 for a dead end (0x41df10).
constexpr float CROSSWORD_HIGHLIGHT_DX = 10.0f;
constexpr float CROSSWORD_HIGHLIGHT_DY = 11.0f;
constexpr int CROSSWORD_DEAD_END_FRAMES = 6;   // len + 3 = (len - 3) + 6
// A word's rack (word+0xb4, ctor 0x434f40, CW36): one ball per root letter,
// its row's top-left at x = 400 - n * 69 / 2 (row_left), y = 375.
constexpr float CROSSWORD_RACK_Y = 375.0f;
// 0x41e930, a slot solved and others left: ModifierDelay 700 ms, then
// 0x41e140(1) selects the next unsolved word (CW11, CW20).
constexpr int CROSSWORD_ADVANCE_MS = 700;
// 0x41fed0 with +0x264 == 1: SOUND_letter_sheen and the sheen, 2500 ms
// (0x9c4 @0x42017b), then 0x428a60 (CW20, CW52).
constexpr int CROSSWORD_SHEEN_MS = 2500;
// 0x41fed0 with +0x264 == 2 (GIVE UP): every answer shown, 2750 ms, then
// 0x41f7f0 -> 0x41f800, the game over (CW22).
constexpr int CROSSWORD_ANSWERS_MS = 2750;

// ---------------------------------------------------------------------------
// Grid and rack art (file read of 0x434f40 -> 0x43ab90; the user's
// play-test screenshots CW72..CW75)
// ---------------------------------------------------------------------------
// The word object builds its entry row with 0x43ab90(.., len, 1, 0)
// (@0x4351af..0x4351b5) and its rack with 0x43ab90(.., 1, 1)
// (@0x435163..0x435177). The fourth argument picks XwordBox over Classic's
// LetterBox (@0x43aeea; Classic passes 0, @0x43998c / 0x4399cc), the fifth
// is the cell frame, and with frame 0 the cell's IMAGE_XWORD_OVERLAY, the
// round hole, shows (@0x43b505..0x43b526). So a grid cell is XwordBox frame
// 0 under the hole, and a rack slot is XwordBox frame 1, the gold dot a
// ball covers until it leaves (CW74: a 27 px dot, centred on the slot).
constexpr int CROSSWORD_CELL_FRAME = 0;
constexpr int CROSSWORD_RACK_FRAME = 1;
inline SheetSpec xword_box_sheet() { return sheet("XwordBox", 69, 138, 1, 2, 2); }
inline SheetSpec xword_hole_sheet() { return sheet("IMAGE_XWORD_OVERLAY", 69, 69); }
inline SheetSpec crossword_highlight_sheet() { return sheet("IMAGE_CROSSWORDHIGHLIGHT", 572, 1080, 1, 12, 12); }
// Letters are LetterBalls (IMAGE_LETTERBALL_ANIM, 17 frames per letter):
// the round pearl (frame 11) in the selected row, typed or locked (CW74,
// CW75), and the square tile (frame 16) for a solved word, the frame the
// solved flip 0x4355d0 ends on, as Lightning's 0x43a310 (CW72, CW73).
inline SheetSpec crossword_ball_sheet() { return sheet("IMAGE_LETTERBALL_ANIM", 1020, 1560, 17, 26, 442); }
constexpr int CROSSWORD_TILE_FRAME = BALL_IDLE_FRAME + 5;
// The sheen of a solved puzzle (0x41fed0 @0x4200f0..0x42013a calls 0x43d310
// on every row, file read): each of the row's balls shows its
// IMAGE_CHICKLET_SHEEN child (9 frames of 60 x 60, built hidden and centred
// on the ball by the LetterBall ctor, @0x43a1b1..0x43a286) and runs it once
// with a ModifierFrame (vtable 0x5971dc) of 100 ms a frame (+0x48 = 0x64
// @0x43d39c; first and last frame -1, INFERRED: the whole sheet). Every
// ball starts together, on the node the 2500 ms wait hangs off, and nothing
// hides the sheen again before the next round.
constexpr int CROSSWORD_SHEEN_FRAMES = 9;
constexpr int CROSSWORD_SHEEN_FRAME_MS = 100;
inline SheetSpec chicklet_sheen_sheet() { return sheet("IMAGE_CHICKLET_SHEEN", 540, 60, 9, 1, 9); }
// The highlight's own turn for a vertical word (0x41de10, crossword.md
// §3.3), on top of the grid's: pi/2 + (-pi/2) = level once the grid has
// turned to the word.
constexpr float CROSSWORD_HIGHLIGHT_VERTICAL = 1.5707963f;

// One cell of the grid, in CrosswordGrid coordinates: `a` along the 18-cell
// axis, `b` along the 11-cell one, so the root lies along `a` on b = 5
// (round_crossword.hpp). Where it is on screen is the view's
// (CrosswordView::to_screen).
struct CrosswordCellView {
    int a = 0, b = 0;
    char letter = '\0';        // what the cell shows; '\0' for nothing
    // The letter is a solved word's (so a locked crossing letter is too) or,
    // after GIVE UP, the builder's answer (0x43ce60). False for a letter typed
    // into the selected row, and for an empty cell.
    bool revealed = false;
    // No word covers the cell, so nothing is drawn there. Not the builder's
    // reservation flag (CrosswordCell::blocked), which also marks letters.
    bool blocked = false;
    bool highlighted = false;  // a cell of the selected word
    // The letter is a ball in the selected row, typed or locked, while that
    // word is unsolved: a round pearl in the hole (the play-test, CW74,
    // CW75). Every other letter is a solved word's (or an answer's) square
    // tile.
    bool ball = false;
};

// The grid view (+0x3a4) as the renderer needs it. 0x41d160 anchors the view
// on the selected word's centre (AnchorPoint type 9, CW49) and pans that
// point to (400, 189); the turn and the zoom pivot on it (INFERRED,
// crossword.md §11; screenshot CW73 shows the word there at 1.0). So the
// model keeps that point, in cells (a cell's centre is at +0.5), and the
// screen keeps it on (400, 189): the pan moves the point, the turn and the
// scale act around it. Letters never turn (0x41c910 turns them back).
struct CrosswordView {
    float focus_a = 0.0f, focus_b = 0.0f;     // the grid point on (400, 189)
    float turn = 0.0f;                        // radians: 0 or CROSSWORD_TURN_VERTICAL at rest
    float scale = CROSSWORD_OVERVIEW_SCALE;   // +0x6c / +0x70

    // Where grid point (a, b), in cells, is on screen.
    std::pair<float, float> to_screen(float a, float b) const {
        const float dx = (a - focus_a) * CROSSWORD_CELL_PX * scale;
        const float dy = (b - focus_b) * CROSSWORD_CELL_PX * scale;
        const float c = std::cos(turn), s = std::sin(turn);
        return {CROSSWORD_PAN_X + dx * c - dy * s, CROSSWORD_PAN_Y + dx * s + dy * c};
    }
};

// One step of the view's animation chains: ModifierLocationLine (two
// floats), ModifierScale or the rotation of 0x41c910 (one float). It starts
// from where the values stand when the step begins, so a step after a turn
// or a chain started over a running one goes on from there.
class CrosswordViewTween : public Modifier {
public:
    CrosswordViewTween(float* first, float first_to, float* second, float second_to, int ms, Easing easing)
        : first_(first), second_(second), first_to_(first_to), second_to_(second_to), ms_(ms), easing_(easing) {}
    int update(int dt) override {
        if (!started_) {
            started_ = true;
            first_from_ = *first_;
            if (second_) second_from_ = *second_;
        }
        elapsed_ += dt;
        const bool done = ms_ <= 0 || elapsed_ >= ms_;
        const float t = done ? 1.0f : ease(easing_, static_cast<float>(elapsed_) / static_cast<float>(ms_));
        *first_ = done ? first_to_ : first_from_ + (first_to_ - first_from_) * t;
        if (second_) *second_ = done ? second_to_ : second_from_ + (second_to_ - second_from_) * t;
        if (!done) return -1;
        return ms_ <= 0 ? dt : elapsed_ - ms_;
    }

private:
    float* first_;
    float* second_;
    float first_to_, second_to_;
    float first_from_ = 0.0f, second_from_ = 0.0f;
    int ms_;
    Easing easing_;
    int elapsed_ = 0;
    bool started_ = false;
};

class CrosswordScene {
public:
    using Services = GameScene::Services;

    // `crt`: the process-wide CRT generator (D-11); the round's word-list,
    // crossing and rack shuffles draw from it, so a second game continues
    // its sequence. `resume`: a saved CROSSWORD game (0x41a110 -> load
    // 0x420fa0); nullptr starts round 1. A save that cannot be restored
    // starts round 1 too, and resume_failed() tells the front end to log it.
    CrosswordScene(const tt2::WordTree& common, const tt2::WordTree& full, tt2::PlayerProfile& profile,
                   tt2::HighScoreManager* highscores, Services services, std::int64_t rng_seed,
                   tt2::CrtRandom& crt, const tt2::SavedGame* resume = nullptr)
        : profile_(profile), highscores_(highscores), services_(std::move(services)), rng_(rng_seed), crt_(crt),
          bag_(common, profile.bag), round_(common, full, round_config(profile)),
          popups_(CROSSWORD_POPUP_X, CROSSWORD_POPUP_Y) {
        particle_rng_.set_seed(rng_seed + 1);
        build_buttons();
        build_sprites();
        resume_failed_ = resume && !resume_game(*resume);
        if (!resume || resume_failed_) start_round(1);   // 0x41a110 -> 0x41ba30 = 0x41ba40(1) (CW6, CW7)
    }
    CrosswordScene(const CrosswordScene&) = delete;
    CrosswordScene& operator=(const CrosswordScene&) = delete;

    // ---- state for the renderer -------------------------------------
    const tt2::CrosswordRound& round() const { return round_; }
    int round_number() const { return round_.round(); }
    std::int32_t score() const { return round_.score(); }
    GamePhase phase() const { return phase_; }
    bool finished() const { return finished_; }
    // +0x2f9: an animation runs (the select's pan / turn / zoom, ZOOM, the
    // reveal); keys, grid clicks and every button are off meanwhile.
    bool busy() const { return busy_; }
    // A save was given but could not be restored (a corrupt CROSSWORD save,
    // a grid that does not fit, another mode's save): a new game started
    // instead and the save was left where it was. The front end logs it.
    bool resume_failed() const { return resume_failed_; }
    // The grid view (+0x3a4): pan, turn and scale (crossword.md §3.3, §3.7).
    const CrosswordView& view() const { return view_; }
    float view_scale() const { return view_.scale; }
    float view_turn() const { return view_.turn; }
    // Where cell (a, b)'s centre is on screen now.
    std::pair<float, float> cell_center(int a, int b) const {
        return view_.to_screen(static_cast<float>(a) + 0.5f, static_cast<float>(b) + 0.5f);
    }
    // +0x3c9, the zoom-in flag (crossword.md §3.7): set by a ZOOM press that
    // zooms in, cleared by one that zooms out, never saved, and not reset by
    // a new round or a new game. The exe keeps the scene object for the whole
    // run (init 0x419d40 on the first enter only, CW58), so the front end
    // carries the flag from one CrosswordScene to the next. Set it before the
    // first update: the opening select reads it after its 500 ms pan.
    bool zoom_in_flag() const { return zoom_in_; }
    void set_zoom_in_flag(bool on) { zoom_in_ = on; }
    // The scene's own BACKGROUND (crosswordscene), not gamescene's: the id is
    // in both sections (crossword.md §1.3), so it carries the section.
    const char* background_id() const { return "crosswordscene/BACKGROUND"; }
    const char* save_mode() const { return tt2::save_mode_name(tt2::GameMode::Crossword); }

    std::string score_text() const { return std::to_string(round_.score()); }
    std::string round_text() const { return std::to_string(round_.round()); }

    // The playable 18 x 11 grid, one entry per cell at index a * 11 + b
    // (CrosswordGrid::A_LEN x B_LEN; every letter of every word lies inside
    // it, 0x435f90). A word's cells show its played word once it is solved,
    // and every answer after GIVE UP (CrosswordRound::shown_word). While the
    // selected word is unsolved its row shows its own balls on top: the
    // letters typed into it and its locked crossing letters (CW74, CW75).
    std::vector<CrosswordCellView> cells() const {
        using Grid = tt2::CrosswordGrid;
        std::vector<CrosswordCellView> out(static_cast<std::size_t>(Grid::A_LEN * Grid::B_LEN));
        for (int a = 0; a < Grid::A_LEN; ++a) {
            for (int b = 0; b < Grid::B_LEN; ++b) {
                CrosswordCellView& c = out[cell_index(a, b)];
                c.a = a;
                c.b = b;
                c.blocked = true;
            }
        }
        const auto& ps = round_.placements();
        const std::size_t sel = round_.selected();
        for (std::size_t k = 0; k < ps.size(); ++k) {
            const std::string& shown = round_.shown_word(k);
            for (std::size_t i = 0; i < ps[k].word.size(); ++i) {
                CrosswordCellView& c = out[word_cell(k, i)];
                c.blocked = false;
                if (k == sel) c.highlighted = true;
                if (!shown.empty()) {
                    c.letter = shown[i];
                    c.revealed = true;
                }
            }
        }
        if (!row_open()) return out;
        for (std::size_t i = 0; i < entry_.size(); ++i) {
            const char locked = round_.locked_letter(sel, i);
            CrosswordCellView& c = out[word_cell(sel, i)];
            if (entry_[i] >= 0) c.letter = rack()[static_cast<std::size_t>(entry_[i])];
            else if (locked != '\0') c.letter = locked;   // a locked letter the rack had no ball for
            if (c.letter == '\0') continue;
            c.revealed = locked != '\0';
            c.ball = true;
        }
        return out;
    }

    // The grid as the renderer draws it, under the overlay: each cell where
    // the view puts it, scaled with it and upright. The play-test shows the
    // cells level in both directions at rest (CW72..CW75); INFERRED: they
    // stay upright while the grid turns, as the letters do (0x41c910). Every
    // cell's box and hole first, then the letters, then the sheen, so no
    // letter goes under a neighbour's box mid-turn (how the exe stacks the
    // rows then was not traced). Blocked cells draw nothing.
    std::vector<SpriteState> grid_sprites() const {
        std::vector<SpriteState> out, letters, sheens;
        const int sheen = sheen_frame();
        for (const CrosswordCellView& c : cells()) {
            if (c.blocked) continue;
            const auto [x, y] = cell_center(c.a, c.b);
            auto centred = [&, x = x, y = y](const char* name, SheetSpec spec, int frame) {
                SpriteState s = sprite(name, std::move(spec), x, y);
                s.anchor = Anchor::Center;
                s.scale = view_.scale;
                s.frame = frame;
                return s;
            };
            out.push_back(centred("XWORD_BOX", xword_box_sheet(), CROSSWORD_CELL_FRAME));
            out.push_back(centred("XWORD_HOLE", xword_hole_sheet(), 0));
            if (c.letter < 'a' || c.letter > 'z') continue;
            const int first = (c.letter - 'a') * BALL_FRAMES_PER_LETTER;
            letters.push_back(
                centred("LETTER", crossword_ball_sheet(), first + (c.ball ? BALL_IDLE_FRAME : CROSSWORD_TILE_FRAME)));
            if (sheen >= 0) sheens.push_back(centred("SHEEN", chicklet_sheen_sheet(), sheen));
        }
        out.insert(out.end(), letters.begin(), letters.end());
        out.insert(out.end(), sheens.begin(), sheens.end());
        return out;
    }

    // The selected word's rack (word+0xb4) while that word is unsolved: one
    // slot per root letter, row_left(n) + i * 69 + 34.5 across and 375 +
    // 34.5 down (0x434f40, crossword.md §2.2), XwordBox frame 1, the gold
    // dot, under each, and the ball still in it on top. A ball in the row,
    // typed or locked, leaves its dot showing, and the rack never closes up
    // (CW74, CW75).
    std::vector<SpriteState> rack_sprites() const {
        if (!row_open()) return {};
        const std::string& r = rack();
        const int n = static_cast<int>(r.size());
        std::vector<SpriteState> dots, balls;
        for (int k = 0; k < n; ++k) {
            SpriteState dot = sprite("RACK_SLOT", xword_box_sheet(), ball_center_x(n, k), CROSSWORD_RACK_Y + BALL_HALF);
            dot.anchor = Anchor::Center;
            dot.frame = CROSSWORD_RACK_FRAME;
            dots.push_back(dot);
            const char c = r[static_cast<std::size_t>(k)];
            if (rack_in_row(static_cast<std::size_t>(k)) || c < 'a' || c > 'z') continue;
            SpriteState ball = sprite("RACK_BALL", crossword_ball_sheet(), dot.x, dot.y);
            ball.anchor = Anchor::Center;
            ball.frame = (c - 'a') * BALL_FRAMES_PER_LETTER + BALL_IDLE_FRAME;
            balls.push_back(ball);
        }
        dots.insert(dots.end(), balls.begin(), balls.end());
        return dots;
    }

    // 0x43d310's sheen frame on every letter, or -1 when no sheen shows:
    // it runs once, 100 ms a frame, from the start of the solved puzzle's
    // 2500 ms wait, and rests on its last frame until the next round.
    int sheen_frame() const {
        if (sheen_ms_ < 0) return -1;
        return std::min(CROSSWORD_SHEEN_FRAMES - 1, sheen_ms_ / CROSSWORD_SHEEN_FRAME_MS);
    }

    // The highlight frame (0x41de10 / 0x41df10): len - 3 for the selected
    // word, len + 3 (the red frames) when the select that ended last found
    // it a dead end, with popup 9 (crossword.md §3.3).
    int highlight_frame() const {
        const auto& ps = round_.placements();
        if (ps.empty()) return 0;
        const int len = static_cast<int>(ps[round_.selected()].word.size());
        return len - 3 + (dead_end_ ? CROSSWORD_DEAD_END_FRAMES : 0);
    }
    bool dead_end() const { return dead_end_; }
    // The highlight (+0x3a8) is a child of the grid view (0x41de10), so it
    // pans, scales and turns with the grid. It sits on the entry row at
    // (-10, -11) in the row's own frame, turned 0 for a horizontal word and
    // pi/2 for a vertical one, whose row runs down the grid from the top-right
    // corner of its first cell. On screen: rotation = the grid's turn + its
    // own, about the frame's top-left. At rest the selected word reads left
    // to right, so the frame is level, (10, 11) px (scaled) above and left of
    // the word's first cell; while the grid turns it turns along (and during
    // the pan before a turn it stands across the new word).
    SpriteState highlight_sprite() const {
        SpriteState s = sprite("CROSSWORD_HIGHLIGHT", crossword_highlight_sheet(), 0, 0);
        const auto& ps = round_.placements();
        s.visible = !ps.empty();
        if (ps.empty()) return s;
        const tt2::WordPlacement& p = ps[round_.selected()];
        const float dx = CROSSWORD_HIGHLIGHT_DX / CROSSWORD_CELL_PX, dy = CROSSWORD_HIGHLIGHT_DY / CROSSWORD_CELL_PX;
        const float a = static_cast<float>(p.a), b = static_cast<float>(p.b);
        // The frame's top-left in grid cells: (-10, -11) px from the row's
        // origin, turned with the row (rot(pi/2): (u, v) -> (-v, u)).
        const auto [x, y] = p.horizontal ? view_.to_screen(a - dx, b - dy) : view_.to_screen(a + 1.0f + dy, b - dx);
        s.x = x;
        s.y = y;
        s.scale = view_.scale;
        s.rotation = view_.turn + (p.horizontal ? 0.0f : CROSSWORD_HIGHLIGHT_VERTICAL);
        s.frame = highlight_frame();
        return s;
    }

    // The selected word's rack (word+0xb4): the whole root, in the order it
    // was shuffled at round setup or by TWIST. A ball in the row leaves a
    // gap: each typed letter, and each locked crossing letter, which takes a
    // ball of its own as the word is selected (the play-test, CW74).
    const std::string& rack() const {
        static const std::string none;
        return round_.selected() < racks_.size() ? racks_[round_.selected()] : none;
    }
    bool rack_in_row(std::size_t ball) const {
        for (int k : entry_) if (k >= 0 && static_cast<std::size_t>(k) == ball) return true;
        return false;
    }
    // The letters typed into the selected row, left to right. Locked
    // crossing letters are not typed (crossword.md §3.4), so they are not in
    // it, though their balls sit in the row; see row_text().
    std::string typed() const {
        std::string s;
        for (std::size_t i = 0; i < entry_.size(); ++i)
            if (typed_cell(i)) s.push_back(rack()[static_cast<std::size_t>(entry_[i])]);
        return s;
    }
    // The selected row as ENTER copies it (0x41a510): its letters left to
    // right, the locked crossing letters included, empty cells skipped. A
    // solved row holds its played word.
    std::string row_text() const {
        const std::size_t sel = round_.selected();
        if (selected_solved()) return round_.slots()[sel].played;
        std::string s;
        for (std::size_t i = 0; i < entry_.size(); ++i) {
            const char locked = round_.locked_letter(sel, i);
            if (locked != '\0') s.push_back(locked);
            else if (entry_[i] >= 0) s.push_back(rack()[static_cast<std::size_t>(entry_[i])]);
        }
        return s;
    }

    // The XML sprites in dialogs.xml order. Only OVERLAY and UI_BAR ever
    // show; the rest are visible="false" and no code shows them
    // (crossword.md §1.1, §11). The highlight the player sees is built in
    // code instead (highlight_sprite). COMMENTARY and GAME_OVER (alpha 0,
    // never looked up) are left out, as in GameScene (scene_popups.hpp).
    const std::vector<SpriteState>& sprites() const { return sprites_; }
    const SpriteState* find_sprite(const std::string& name) const {
        for (const auto& s : sprites_) if (s.name == name) return &s;
        return nullptr;
    }

    // Every image the scene draws, one entry per id, for the front end's
    // preload (boot_preload.hpp, PRELOAD_CROSSWORD): the background, the XML
    // sprites that show, the buttons, the grid's, the rack's and the
    // highlight's art, the sheen and the five popups Crossword pushes (kinds
    // 0, 3, 4, 7, 9; crossword.md §8). Not the hidden XML sprites, and not
    // the dialogs or the trophy popup, which every scene shares.
    std::vector<SheetSpec> art() const {
        std::vector<SheetSpec> out;
        auto add = [&out](const SheetSpec& s) {
            for (const SheetSpec& o : out) if (o.image_id == s.image_id) return;
            out.push_back(s);
        };
        add(sheet(background_id(), 800, 600));
        for (const SpriteState& s : sprites_) if (s.visible) add(s.sheet);
        for (const MenuButton& b : buttons_) if (b.sprite.visible) add(b.sprite.sheet);
        for (SheetSpec s : {xword_box_sheet(), xword_hole_sheet(), crossword_ball_sheet(), chicklet_sheen_sheet(),
                            crossword_highlight_sheet()})
            add(s);
        for (PopupKind k : {PopupKind::NotListed, PopupKind::AlreadyGuessed, PopupKind::CrosswordIntersection,
                            PopupKind::LockedLetter, PopupKind::DeadEnd})
            add(popup_sheet(k));
        return out;
    }

    // The UI bar's labels (dialogs.xml:139-145): SCORE and ROUND. Init hides
    // TIME_LABEL and both timer labels, and nothing counts a clock
    // (0x419d40 setVisible(0) on +0x234, +0x238, +0x244; crossword.md §4).
    std::vector<TextLabel> labels() const {
        auto label = [](const char* name, const char* font, std::string text, float x, TextAlign align, bool shown) {
            TextLabel l{name, font, std::move(text), x, CROSSWORD_UI_TEXT_Y, align, Anchor::Center, -1};
            l.visible = shown;
            return l;
        };
        return {
            label("SCORE_LABEL", "FONT_UI_LABELS", "SCORE:", 244, TextAlign::Right, true),        // :140
            label("LABEL_Score", "FONT_UI_NUMBERS", score_text(), 251, TextAlign::Left, true),     // :139, +0x23c
            label("LABEL_ROUND (copy)", "FONT_UI_LABELS", "ROUND:", 537, TextAlign::Right, true),  // :144
            label("LABEL_ROUND", "FONT_UI_NUMBERS", round_text(), 542, TextAlign::Left, true),     // :145, +0x240
            label("TIME_LABEL", "FONT_UI_LABELS", "TIME:", 707, TextAlign::Right, false),         // :143, +0x244
            label("LABEL_TimerMinutes", "FONT_UI_NUMBERS", "88:", 749, TextAlign::Right, false),   // :142, +0x234
            label("LABEL_TimerSeconds", "FONT_UI_NUMBERS", "88", 752, TextAlign::Left, false),     // :141, +0x238
        };
    }

    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    MenuButton* button(const std::string& name) {
        for (auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    const MenuButton* button(const std::string& name) const {
        for (const auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    const SpriteState& popup() const { return popups_.sprite(); }
    std::optional<PopupKind> popup_kind() const { return popups_.kind(); }
    std::size_t popups_waiting() const { return popups_.waiting(); }
    const SpriteState& trophy_popup() const { return trophies_.sprite(); }
    const SpriteState& trophy_label() const { return trophies_.label(); }
    int trophy_popup_id() const { return trophies_.id(); }
    std::size_t trophies_waiting() const { return trophies_.waiting(); }

    // ---- dialogs (the same stack as BlitzScene) -----------------------
    bool dialog_open() const { return dialog_kind_ != GameDialog::None; }
    GameDialog dialog_kind() const { return dialog_kind_; }
    const Dialog& dialog() const { return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_; }
    Dialog& dialog() { return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_; }
    const Dialog* under_dialog() const { return under_.empty() ? nullptr : &under_.back().second; }
    GameDialog under_kind() const { return under_.empty() ? GameDialog::None : under_.back().first; }
    // The balls and the burst of DIALOG_NEXTROUND and DIALOG_CONGRATS.
    const std::vector<LetterBall>& dialog_balls() const { return dialog_balls_; }
    const std::vector<Particle>& dialog_particles() const { return dialog_particles_; }
    const HelpPager& help() const { return help_; }
    const HighScoreDialog* highscore_dialog() const { return highscore_ ? &*highscore_ : nullptr; }
    const SettingsDialog* settings_dialog() const { return settings_ ? &*settings_ : nullptr; }
    void window_mode_changed(bool fullscreen) { if (settings_) settings_->sync_fullscreen(fullscreen); }
    // Everything stands still under these (GameScene::paused()).
    bool paused() const {
        return dialog_kind_ == GameDialog::PauseMenu || dialog_kind_ == GameDialog::ConfirmExit ||
               dialog_kind_ == GameDialog::ConfirmGiveUp || dialog_kind_ == GameDialog::HowToPlay ||
               dialog_kind_ == GameDialog::Settings;
    }

    // ---- saving (0x41f7d0 / 0x4203f0) --------------------------------
    // 0x41f7d0: the leave saves only while the round is in play
    // (+0x264 == 0), busy or not; the zoom state is never saved (§5.5).
    bool can_save() const { return phase_ == GamePhase::Playing && !round_.over() && !finished_; }
    // 0x4203f0 in the T5.2 format (saved_game.hpp): WORD the root, ROUND,
    // TIME 0 (+0x2fc is never counted), SCORE, and one xword= per placement
    // in commit order, root first, with its scored flag and played word.
    tt2::SavedGame snapshot() const {
        tt2::SavedGame g;
        g.mode = save_mode();
        g.word = round_.root();
        g.round = round_.round();
        g.time_ms = 0;
        g.score = round_.score();
        g.day = -1;
        const auto& ps = round_.placements();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            const tt2::CrosswordSlot& s = round_.slots()[i];
            g.crossword_words.push_back({ps[i].word, ps[i].a, ps[i].b, ps[i].horizontal, ps[i].crossing, s.scored,
                                         s.played});
        }
        return g;
    }
    void leave() {
        if (can_save() && services_.save_game) services_.save_game(snapshot());
    }

    // ---- input (0x41f120 keys, 0x41b910 buttons, 0x421e50 mouse) -----
    // 0x41f120: keys are ignored while busy (+0x2f9) or after the round
    // (+0x264 != 0).
    bool accepts_input() const {
        return phase_ == GamePhase::Playing && !round_.over() && !busy_ && !dialog_open();
    }
    // 0x41aa20: a letter goes to the next free cell of the selected row.
    // Locked cells are skipped, so the player never types a locked letter
    // (crossword.md §3.4, the user's play-test of the original). Nothing
    // happens without room in the row or without that letter in the rack.
    void type_letter(char c) {
        if (!accepts_input() || selected_solved()) return;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return;
        const int ball = available_ball(c);
        if (ball >= 0) place_ball(static_cast<std::size_t>(ball));
    }
    // Backspace (key 1, 0x41b390): SOUND_letter_clear and the last typed
    // letter back to the rack; on a solved word, the take-back (0x41eaf0).
    void backspace() {
        if (!accepts_input()) return;
        if (selected_solved()) { take_back(); return; }
        const int last = last_typed_cell();
        if (last < 0) return;
        entry_[static_cast<std::size_t>(last)] = -1;
        play(SoundId::LetterClear);
        refresh_buttons();
    }
    // Delete (key 4) is CLEAR (0x41a3d0).
    void key_delete() { if (accepts_input()) clear(); }
    // Space (key 0x11): 0x41f120 calls TWIST's 0x41b060 straight, and
    // 0x41b060 checks nothing (file read), so Space twists even when the
    // TWIST button is off (a solved word), setting the twisted flag.
    void key_space() { if (accepts_input()) twist(); }
    // Tab and Right (0x16, INFERRED): NEXT; Left (0x14): PREV
    // (0x41e140(0) / 0x41e230). The keys play no sound.
    void key_word(bool forward) { if (accepts_input()) step_word(forward); }
    // Up and Down (0x15 / 0x17, INFERRED): ZOOM (0x41b1c0).
    void key_zoom() { if (accepts_input()) zoom(); }
    // Key 5: with a dialog up it answers it; in play, on an unsolved word
    // only, ENTER with letters in the row, else LAST WORD (0x41f120).
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
        if (!accepts_input() || selected_solved()) return;
        if (typed().empty()) last_word_again();
        else submit();
    }
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
    // +0x20c = 2: focus lost under another dialog opens DIALOG_MENU once it
    // closes; never a second menu over the first (crossword.md §2.1, §5.4).
    void focus_lost() {
        if (pause_menu_up()) return;
        if (dialog_open()) menu_pending_ = true;
        else open_pause_menu();
    }
    bool menu_pending() const { return menu_pending_; }
    // Android: pause as on focus loss and save now (BlitzScene::enter_background).
    void enter_background() {
        focus_lost();
        leave();
    }

    // The hand: over an enabled button, a word's row in the click box or a
    // ball of the rack, never while busy (0x421e50 ignores those clicks).
    bool pointer_over_clickable() const {
        if (settings_) return settings_->pointer_hot();
        if (dialog_open()) return dialog_pointer_hot(dialog());
        for (const auto& b : buttons_)
            if (b.hover && b.enabled && b.sprite.visible) return true;
        if (busy_ || finished_) return false;
        return word_at(pointer_x_, pointer_y_) >= 0 || (accepts_input() && rack_ball_at(pointer_x_, pointer_y_) >= 0);
    }
    void mouse_move(float x, float y) {
        pointer_x_ = x;
        pointer_y_ = y;
        if (settings_) { settings_->mouse_move(x, y); return; }
        if (dialog_open()) { dialog_mouse_move(dialog(), x, y); return; }
        for (auto& b : buttons_) {
            const bool over = b.sprite.visible && b.enabled && b.contains(x, y);
            b.hover = over;
            if (!over) b.pressed = false;
        }
    }
    // The grid and the rack take the press (mouse down 0x421e50, vtable slot
    // 53; the pointer handler 0x41f2b0); a button under the cursor goes
    // first and fires on release.
    void mouse_down(float x, float y) {
        pointer_x_ = x;
        pointer_y_ = y;
        if (settings_) { settings_->mouse_down(x, y); return; }
        if (dialog_open()) { dialog_mouse_down(dialog(), x, y); return; }
        bool on_button = false;
        for (auto& b : buttons_) {
            b.pressed = b.sprite.visible && b.enabled && b.contains(x, y);
            on_button = on_button || b.pressed;
        }
        if (!on_button) grid_click(x, y);
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
                if (has_click_sound(b.sprite.name)) play(SoundId::Button);
                press(b.sprite.name);
                return;
            }
        }
    }

    // A scene button (mouse, keys, tests): 0x41b910 (CW8). MENU, NEXT ROUND
    // and GIVE UP work in any state, the rest only while +0x264 == 0.
    void press(const std::string& name) {
        if (dialog_open()) return;
        const MenuButton* b = button(name);
        if (!b || !b->enabled || !b->sprite.visible) return;
        if (name == "BUTTON_Menu") open_pause_menu();                              // +0x20c = 1
        else if (name == "BUTTON_GiveUp") open_dialog(GameDialog::ConfirmGiveUp);  // +0x20c = 4
        else if (!accepts_input()) return;
        else if (name == "BUTTON_Enter") submit();
        else if (name == "BUTTON_Clear") clear();
        else if (name == "BUTTON_LastWord") last_word_again();
        else if (name == "BUTTON_Twist") twist();
        else if (name == "BUTTON_Zoom") zoom();
        else if (name == "BUTTON_NEXTWORD") step_word(true);
        else if (name == "BUTTON_PREVWORD") step_word(false);
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
                // YES 0x41fb50; NO 0x41fc30 closes, unlocks and refreshes.
                close_dialog();
                if (name == "BUTTON_YES") give_up();
                break;
            case GameDialog::NextRound:
                // 0x41fcf0 -> 0x41b000: Well Rounded tracker 3, then
                // 0x41ba40(round + 1); the score carries over (crossword.md §5.2).
                close_dialog();
                trophies_.push(tt2::count_round(profile_, tt2::GameMode::Crossword));
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

    // ---- simulation (0x41ebc0, CW1) ------------------------------------
    // The update runs the pending dialogs, the button refresh, the trophy
    // queue and the popup queue; its `dt` counts no clock (crossword.md §4).
    // The port's two modifier queues hold the view's chains (the select's
    // pan / turn / zoom, ZOOM) and the scene's waits (the 700 ms advance, the
    // reveal).
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
        refresh_buttons();
        if (!paused()) {
            if (sheen_ms_ >= 0) sheen_ms_ = std::min(sheen_ms_ + dt_ms, CROSSWORD_SHEEN_FRAMES * CROSSWORD_SHEEN_FRAME_MS);
            view_queue_.update(dt_ms);
            queue_.update(dt_ms);
            popups_.update(dt_ms);
            if (trophies_.update(dt_ms, !dialog_open())) play(SoundId::TrophyWon);
        }
        for (auto& b : buttons_) b.sprite.frame = b.frame_for_state();
    }

private:
    static tt2::CrosswordRound::Config round_config(const tt2::PlayerProfile& p) {
        tt2::CrosswordRound::Config cfg;
        cfg.max_word_length = static_cast<std::size_t>(p.max_word_length);   // 0x41bbd0: min(5 + rand % 4, maxLen)
        return cfg;
    }

    // ---- round flow ---------------------------------------------------
    // 0x41ba40(number): a new word set and grid, one rack per word, the
    // tutorial once per player (step 2), then 0x41d920 (CW14, CW57).
    void start_round(int number) {
        round_.start(static_cast<std::size_t>(number), rng_, crt_, bag_);
        profile_.bag = bag_.state();
        changed();
        deal_racks();
        open_round_view();
        open_tutorial_once();
        refresh_buttons();
    }

    // Load 0x420fa0 (crossword.md §5.5, CW54): the grid, score, round and
    // each slot's played word and scored flag come back through
    // CrosswordRound::restore (word 0 selected), then 0x41d920 as for a new
    // round, then the save is used up (0x461840). False, with nothing
    // changed, when the save cannot be used. No tutorial: the load does not
    // run the round setup.
    bool resume_game(const tt2::SavedGame& g) {
        if (!g.crossword() || !g.valid()) return false;
        std::vector<tt2::WordPlacement> placements;
        std::vector<std::string> played;
        std::vector<bool> scored;
        for (const auto& w : g.crossword_words) {
            placements.push_back({w.answer, w.a, w.b, w.horizontal, w.crossing});
            played.push_back(w.played);
            scored.push_back(w.scored);
        }
        if (!round_.restore(placements, played, scored, g.score, g.round)) return false;
        // INFERRED: the racks are shuffled again as at setup (the save keeps
        // only SOURCE, the root, per word; 0x434f40 was not traced on load).
        deal_racks();
        open_round_view();
        if (services_.discard_save) services_.discard_save();
        refresh_buttons();
        return true;
    }

    // 0x41ba40 step 6: one word object per placement, root first; every rack
    // holds the whole root, shuffled on its own with the CRT (0x43c370 ->
    // 0x439370, std::random_shuffle).
    void deal_racks() {
        racks_.clear();
        const std::string& root = round_.root();
        for (std::size_t i = 0; i < round_.placements().size(); ++i) {
            std::vector<char> letters(root.begin(), root.end());
            crt_.shuffle(letters);
            racks_.emplace_back(letters.begin(), letters.end());
        }
    }

    // 0x41d920 (round setup @0x41c6f3, load @0x421dc3): +0x3c8 = 1 and the
    // grid's rotation 0 just before it (@0x41c6e1, @0x421da6), the scale 0.5
    // at once (@0x41ddf8), then word 0 selected: its pan, and the zoom in
    // only with the zoom-in flag set (crossword.md §3.2, §3.7).
    void open_round_view() {
        phase_ = GamePhase::Playing;
        busy_ = false;
        queue_.clear();
        popups_.drop_waiting();   // 0x41ba40 step 1: the popup queue is cleared
        sheen_ms_ = -1;           // new word objects: their sheen children start hidden
        view_.turn = 0.0f;
        view_horizontal_ = true;
        view_.scale = CROSSWORD_OVERVIEW_SCALE;
        if (!view_placed_) {
            // INFERRED: where the view stands before its first pan was not
            // traced; it starts on the root, so the first pan holds still.
            const auto [a, b] = word_centre(round_.selected());
            view_.focus_a = a;
            view_.focus_b = b;
            view_placed_ = true;
        }
        select_changed();
    }

    // 0x41a510 ENTER on the selected slot (crossword.md §3.5). A row of
    // another length is ignored with no sound and no popup (@0x41a5c7); a
    // rejected row is cleared; an accepted word fills the slot, even when
    // it is not the builder's answer.
    void submit() {
        if (selected_solved()) return;
        const auto rep = round_.guess(row_text());
        refresh_buttons();   // the slot's LAST WORD (+0x60) changed
        using Guess = tt2::CrosswordRound::Guess;
        if (rep.status == Guess::Ignored) return;
        if (rep.status != Guess::Accepted) {
            // 0x436a00: 0 (not a full-list sub-anagram of the root) -> popup 0;
            // -1 (already in the grid) -> popup 3; SOUND_word_reject for both.
            play(SoundId::WordReject);
            reset_row();
            popups_.push(rep.status == Guess::NotAWord ? PopupKind::NotListed : PopupKind::AlreadyGuessed);
            refresh_buttons();
            return;
        }
        // Word Nerd (0x40f1e0), then Human Dictionary for an uncommon word
        // (0x40f3a0); a bingo plays its own sound (§3.5 step 3).
        trophies_.push(tt2::count_word(profile_));
        if (rep.uncommon) trophies_.push(tt2::count_uncommon(profile_));
        changed();
        play(rep.bingo ? SoundId::WordBingo : SoundId::WordCorrect);
        reset_row();   // the typed letters now show as the slot's played word
        if (rep.complete) {
            puzzle_solved();
        } else {
            // A second solve inside the wait starts it again: one advance.
            queue_.clear();
            queue_.push_delay(CROSSWORD_ADVANCE_MS);
            queue_.push_method([this] { advance_selection(); });
        }
        refresh_buttons();
    }

    // 0x41e140(1) after the 700 ms: the next unsolved word, wrapping, then
    // the select. The wait is not busy, so NEXT / PREV can run inside it;
    // the advance then goes on from the word they picked.
    void advance_selection() {
        if (round_.advance()) select_changed();
    }

    // NEXT / PREV, Tab, Left / Right (0x41e140(0) / 0x41e230): index +- 1,
    // wrapping, solved words included; nothing with a single word.
    void step_word(bool forward) {
        if (forward ? round_.select_next() : round_.select_previous()) select_changed();
    }

    // The select 0x41e7f0 once the engine has moved the selection (CW51): the
    // old row's typed letters go back, the new word's rack shows, the
    // highlight moves at once (0x41de10), then the pan chain. Also run on the
    // solved puzzle and on GIVE UP, for the word that stays selected.
    void select_changed() {
        reset_row();
        dead_end_ = false;
        pan_to_selected();
    }

    // 0x41d160 (CW48, CW49): busy (@0x41d274), then the selected word's
    // centre pans to (400, 189), with no scale test; then 0x41d390.
    void pan_to_selected() {
        busy_ = true;
        view_queue_.clear();
        const auto [a, b] = word_centre(round_.selected());
        view_queue_.push(std::make_unique<CrosswordViewTween>(&view_.focus_a, a, &view_.focus_b, b, CROSSWORD_PAN_MS,
                                                              Easing::Smoothstep));
        view_queue_.push_method([this] { turn_and_zoom_in(); });
        refresh_buttons();
    }

    // 0x41d390 (CW50, file read), chained after the pan: a word of the other
    // direction plays SOUND_crossword_rotate_word (its only xref, CW18), waits
    // 300 ms and turns the grid to read it left to right, letters kept
    // upright; +0x3c8 takes the word's direction (@0x41d764). Then the zoom
    // back in, only with the zoom-in flag set and the scale exactly 0.5
    // (@0x41d776..0x41d797). Then 0x41cd00.
    void turn_and_zoom_in() {
        const bool horizontal = round_.placements()[round_.selected()].horizontal;
        if (horizontal != view_horizontal_) {
            play(SoundId::CrosswordRotateWord);
            view_queue_.push_delay(CROSSWORD_TURN_WAIT_MS);
            view_queue_.push(std::make_unique<CrosswordViewTween>(
                &view_.turn, horizontal ? 0.0f : CROSSWORD_TURN_VERTICAL, nullptr, 0.0f, CROSSWORD_TURN_MS,
                Easing::Linear));
        }
        view_horizontal_ = horizontal;
        if (zoom_in_ && view_.scale == CROSSWORD_OVERVIEW_SCALE) {
            view_queue_.push(std::make_unique<CrosswordViewTween>(&view_.scale, CROSSWORD_ZOOMED_SCALE, nullptr, 0.0f,
                                                                  CROSSWORD_ZOOM_IN_MS, Easing::Smoothstep));
        }
        view_queue_.push_method([this] { select_done(); });
    }

    // 0x41cd00, the end of the select's chain (file read @0x41d117..0x41d145):
    // the crossings lock (locked_letter), 0x41df10 places the highlight and
    // tests the dead end (popup 9 and the len + 3 frame); then, with the
    // round over, the reveal; else busy ends (@0x41d130).
    void select_done() {
        dead_end_ = !selected_solved() && round_.dead_end(round_.selected());
        if (dead_end_) popups_.push(PopupKind::DeadEnd);
        if (round_.over()) {
            reveal();
            return;
        }
        busy_ = false;
        refresh_buttons();
    }

    // ZOOM, Up, Down (0x41b1c0, CW46, CW47).
    void zoom() {
        if (view_.scale == CROSSWORD_ZOOMED_SCALE) {
            // fld1; fcomp [+0x6c]: +0x3c9 = 0 (@0x41b1e3), busy (@0x41b1ea),
            // then the zoom-out chain.
            zoom_in_ = false;
            busy_ = true;
            zoom_out_to_root();
            refresh_buttons();
        } else if (view_.scale == CROSSWORD_OVERVIEW_SCALE) {
            // +0x3c9 = 1 (@0x41b301), then the pan 0x41d160 (@0x41b308): the
            // select's chain on the selected word, typed letters kept.
            zoom_in_ = true;
            pan_to_selected();
        }
    }

    // The zoom-out of ZOOM and of the reveal: 0.5 over 1000 ms, then
    // 0x41cab0 anchors the view on the root's row (word 0, @0x41cb18) and pans
    // it to (400, 189), keeping the turn; it ends in 0x41cce0: busy off
    // (@0x41cce3) and the button refresh.
    void zoom_out_to_root() {
        view_queue_.push(std::make_unique<CrosswordViewTween>(&view_.scale, CROSSWORD_OVERVIEW_SCALE, nullptr, 0.0f,
                                                              CROSSWORD_ZOOM_OUT_MS, Easing::Smoothstep));
        const auto [a, b] = word_centre(0);
        view_queue_.push(std::make_unique<CrosswordViewTween>(&view_.focus_a, a, &view_.focus_b, b, CROSSWORD_PAN_MS,
                                                              Easing::Smoothstep));
        view_queue_.push_method([this] {
            busy_ = false;
            refresh_buttons();
        });
    }

    // 0x41e930 with every slot solved: +0x264 = 1 (the engine), Puzzle Pro
    // when no TWIST and Perfect Puzzler when no take-back this round
    // (award_crossword_round, crossword.md §6, CW11), and the select, whose
    // chain ends in the reveal.
    void puzzle_solved() {
        phase_ = GamePhase::Reveal;
        for (int id : tt2::award_crossword_round(profile_, tt2::CrosswordRoundFlags{round_.twisted(), round_.mistake()}))
            trophies_.push(id);
        changed();
        queue_.clear();   // nothing is left to advance to
        select_changed();
    }

    // 0x41fb50, GIVE UP answered YES: +0x264 = 2, the save deleted
    // (0x461840), and the select, whose chain ends in the reveal of every
    // answer (0x43ce60).
    void give_up() {
        if (!round_.give_up()) return;
        if (services_.discard_save) services_.discard_save();
        phase_ = GamePhase::Reveal;
        queue_.clear();   // a pending 700 ms advance has nothing left to select
        select_changed();
    }

    // The reveal 0x41fed0 (crossword.md §5.1, §5.3; file read): busy
    // (@0x41ff07); at 1.0 the zoom-out chain, +0x3c9 untouched; for a solved
    // puzzle SOUND_letter_sheen (@0x420047) and the sheen; then the wait,
    // 2500 ms (solved) or 2750 ms (given up), and 0x428a60 (busy off,
    // +0x20c = 3: the next update plays SOUND_game_next_round_acheived and
    // opens DIALOG_NEXTROUND) or the game over 0x41f7f0. At 1.0 the wait
    // hangs off 0x41cab0's node (@0x42000b, @0x42019d), so it starts when the
    // zoom-out ends, and that chain's 0x41cce0 clears busy 1500 ms in: from
    // there until the wait ends, rows answer clicks (the definition links,
    // §3.9). At 0.5 the scene stays busy until the wait ends.
    void reveal() {
        phase_ = GamePhase::Reveal;
        busy_ = true;
        const bool solved = round_.complete();
        queue_.clear();
        if (view_.scale == CROSSWORD_ZOOMED_SCALE) {
            zoom_out_to_root();
            queue_.push_delay(CROSSWORD_ZOOM_OUT_MS);
        }
        if (solved) {
            play(SoundId::LetterSheen);
            // 0x43d310 on every row, on the same node as the wait below
            // (@0x42012a..0x420132, file read): the sheen starts with it.
            queue_.push_method([this] { sheen_ms_ = 0; });
        }
        queue_.push_delay(solved ? CROSSWORD_SHEEN_MS : CROSSWORD_ANSWERS_MS);
        queue_.push_method([this, solved] {
            busy_ = false;
            if (!solved) {
                game_over();
                return;
            }
            phase_ = GamePhase::AwaitNext;
            play(SoundId::NextRoundAchieved);
            open_dialog(GameDialog::NextRound);
        });
        refresh_buttons();
    }

    // 0x41f800: the `crossword` table (0x5919d0, CW30). A score above 0 is
    // entered with the round number as `extra` (0x449c90, INFERRED as in
    // Blitz); a rank plays the sound and opens CONGRATS, else ENDGAME.
    void game_over() {
        phase_ = GamePhase::GameOver;
        bool high = false;
        if (highscores_ && round_.score() > 0) {
            const int rank = highscores_->submit(tt2::GameMode::Crossword, profile_.name, round_.score(),
                                                 std::to_string(round_.round()));
            if (rank >= 0) { play(SoundId::HighScoreAchieved); high = true; }
            changed();
        }
        open_dialog(high ? GameDialog::Congrats : GameDialog::EndGame);
    }

    // 0x41ba40 step 2: the player's tutorial flag for mode 3 (profile
    // +0xc0 + 3) is tested and set; a clear flag opens DIALOG_TUTORIAL
    // ("Crossword Mode", +0x20c = 5). Once per player.
    void open_tutorial_once() {
        bool& seen = profile_.mode_flags[static_cast<std::size_t>(tt2::GameMode::Crossword)];
        if (seen) return;
        seen = true;
        changed();
        open_dialog(GameDialog::Tutorial);
    }

    // ---- the selected row ---------------------------------------------
    bool selected_solved() const {
        const std::size_t sel = round_.selected();
        return sel < round_.slots().size() && round_.slots()[sel].solved();
    }
    // A cell index in cells() (and in CrosswordGrid's storage).
    static std::size_t cell_index(int a, int b) {
        return static_cast<std::size_t>(a * tt2::CrosswordGrid::B_LEN + b);
    }
    std::size_t word_cell(std::size_t slot, std::size_t i) const {
        const tt2::WordPlacement& p = round_.placements()[slot];
        return cell_index(p.cell_a(static_cast<int>(i)), p.cell_b(static_cast<int>(i)));
    }
    // A word's centre in cells: the point 0x41d160 anchors the view on.
    std::pair<float, float> word_centre(std::size_t slot) const {
        const tt2::WordPlacement& p = round_.placements()[slot];
        const float half = static_cast<float>(p.word.size()) * 0.5f;
        return {static_cast<float>(p.a) + (p.horizontal ? half : 0.5f),
                static_cast<float>(p.b) + (p.horizontal ? 0.5f : half)};
    }
    // The selected word takes letters: the round is in play and the word is
    // unsolved, so its rack shows and its row holds balls.
    bool row_open() const { return !round_.placements().empty() && !round_.over() && !selected_solved(); }
    bool locked_cell(std::size_t i) const { return round_.locked_letter(round_.selected(), i) != '\0'; }
    // Cell i holds a ball the player typed (not a locked letter's).
    bool typed_cell(std::size_t i) const { return entry_[i] >= 0 && !locked_cell(i); }
    // One cell per letter of the selected word: every typed letter back in
    // the rack, and each locked crossing letter on its cell with a ball of
    // its own taken from the rack, so that ball's slot shows its dot and
    // the letter can only be typed again from another copy (the play-test,
    // CW74). Which ball goes is UNCONFIRMED (CW74 shows slot 0 emptied); the
    // port takes the leftmost free ball with that letter. A locked letter
    // the rack has no ball left for keeps its cell without one.
    void reset_row() {
        const auto& ps = round_.placements();
        const std::size_t sel = round_.selected();
        entry_.assign(sel < ps.size() ? ps[sel].word.size() : 0, -1);
        if (!row_open()) return;
        for (std::size_t i = 0; i < entry_.size(); ++i) {
            const char locked = round_.locked_letter(sel, i);
            if (locked != '\0') entry_[i] = available_ball(locked);
        }
    }
    // The first cell neither typed nor locked (0x41cd00 locks the crossings
    // with solved words), or -1 when the row is full.
    int next_free_cell() const {
        for (std::size_t i = 0; i < entry_.size(); ++i)
            if (entry_[i] < 0 && round_.locked_letter(round_.selected(), i) == '\0') return static_cast<int>(i);
        return -1;
    }
    // The cell of the last typed letter, or -1 when nothing is typed. A
    // locked letter's ball is never taken back.
    int last_typed_cell() const {
        for (std::size_t i = entry_.size(); i-- > 0;)
            if (typed_cell(i)) return static_cast<int>(i);
        return -1;
    }
    // The leftmost rack ball with this letter that is not in the row, or -1.
    int available_ball(char c) const {
        const std::string& r = rack();
        for (std::size_t k = 0; k < r.size(); ++k)
            if (r[k] == c && !rack_in_row(k)) return static_cast<int>(k);
        return -1;
    }

    // 0x41aa20 for one rack ball (a key or a click on the ball, 0x41f2b0):
    // SOUND_letter_place and the ball to the next free cell. Popup 7
    // (0x43c060 / 0x40b230, INFERRED: the letter differs from a locked
    // letter) stops it first; with locked cells skipped it never lands on
    // one, so the check cannot fire in typing (open for T7.1, §3.4). Then
    // 0x41e270: a letter that leaves an unsolved crossing word needing more
    // of a letter than the root has shows popup 4 and goes back through
    // 0x41b390(0), which plays SOUND_letter_clear whatever its argument
    // (@0x41b3df..0x41b43a, file read).
    void place_ball(std::size_t ball) {
        if (!accepts_input() || selected_solved() || ball >= rack().size() || rack_in_row(ball)) return;
        const int cell = next_free_cell();
        if (cell < 0) return;
        const auto pos = static_cast<std::size_t>(cell);
        const std::size_t sel = round_.selected();
        const char c = rack()[ball];
        if (round_.clashes_with_lock(sel, pos, c)) {
            popups_.push(PopupKind::LockedLetter);
            return;
        }
        entry_[pos] = static_cast<int>(ball);
        play(SoundId::LetterPlace);
        if (round_.breaks_crossing(sel, pos, c)) {
            popups_.push(PopupKind::CrosswordIntersection);
            entry_[pos] = -1;
            play(SoundId::LetterClear);
        }
        refresh_buttons();
    }

    // CLEAR / Delete (0x41a3d0): SOUND_letter_clear and every typed letter
    // back to the rack. It takes each one back through 0x41b390, so on a
    // solved word it is the take-back (INFERRED, crossword.md §3.6).
    void clear() {
        if (selected_solved()) { take_back(); return; }
        if (typed().empty()) return;
        reset_row();
        play(SoundId::LetterClear);
        refresh_buttons();
    }

    // 0x41eaf0 through 0x41b390 (crossword.md §3.6): the selected solved
    // word is taken back; its points stay (+0x85) and +0x371 is set
    // (CrosswordRound::take_back); the row is empty and the rack shows.
    void take_back() {
        if (!round_.take_back()) return;
        play(SoundId::LetterClear);
        reset_row();
        refresh_buttons();
    }

    // TWIST (0x41b060, CW16): SOUND_word_twist, the selected word's rack
    // shuffled with the CRT (0x43c370), +0x370 = 1 (the Puzzle Pro blocker)
    // and the twist counter (0x40f170). Only the balls still in the rack
    // move: a locked letter's slot stays empty (the play-test, CW74), and
    // INFERRED, the typed ones stay in the row too.
    void twist() {
        if (!round_.twist()) return;
        play(SoundId::WordTwist);
        std::string& r = racks_[round_.selected()];
        std::vector<std::size_t> free_balls;
        std::vector<char> letters;
        for (std::size_t k = 0; k < r.size(); ++k) {
            if (rack_in_row(k)) continue;
            free_balls.push_back(k);
            letters.push_back(r[k]);
        }
        crt_.shuffle(letters);
        for (std::size_t i = 0; i < free_balls.size(); ++i) r[free_balls[i]] = letters[i];
        trophies_.push(tt2::count_twist(profile_));
        changed();
        refresh_buttons();
    }

    // LAST WORD (0x41ae20): SOUND_word_last, then the slot's last row
    // (+0x60) fills the unlocked cells when it fits the row. INFERRED: "fits"
    // is the slot's length, as a shorter row cannot be matched to its cells
    // once locked letters sit between them.
    void last_word_again() {
        play(SoundId::WordLast);
        const std::size_t sel = round_.selected();
        const std::string& last = round_.slots()[sel].last_word;
        if (last.size() != entry_.size()) return;
        reset_row();
        for (std::size_t i = 0; i < last.size(); ++i) {
            if (round_.locked_letter(sel, i) != '\0') continue;
            const int ball = available_ball(last[i]);
            if (ball < 0) break;
            entry_[i] = ball;
        }
        refresh_buttons();
    }

    // ---- the grid under the pointer (0x421e50, CW42; 0x41f2b0, CW37) ---
    static bool in_click_box(float x, float y) {
        return x >= CROSSWORD_CLICK_LEFT && x <= CROSSWORD_CLICK_RIGHT && y >= CROSSWORD_CLICK_TOP &&
               y <= CROSSWORD_CLICK_BOTTOM;
    }
    // The letter of word `slot` under (x, y), or -1. At rest the view is
    // turned by a multiple of 90 degrees, so every cell is a level square.
    int letter_at(std::size_t slot, float x, float y) const {
        const tt2::WordPlacement& p = round_.placements()[slot];
        const float half = CROSSWORD_CELL_PX * view_.scale * 0.5f;
        for (std::size_t i = 0; i < p.word.size(); ++i) {
            const auto [cx, cy] = cell_center(p.cell_a(static_cast<int>(i)), p.cell_b(static_cast<int>(i)));
            if (std::fabs(x - cx) <= half && std::fabs(y - cy) <= half) return static_cast<int>(i);
        }
        return -1;
    }
    // The word whose row is under (x, y) inside the click box, or -1. A
    // crossing cell belongs to two rows: the selected word's comes first,
    // then placement order (INFERRED: 0x421e50's loop order was not traced).
    int word_at(float x, float y) const {
        if (!in_click_box(x, y) || round_.placements().empty()) return -1;
        const std::size_t sel = round_.selected();
        if (letter_at(sel, x, y) >= 0) return static_cast<int>(sel);
        for (std::size_t k = 0; k < round_.placements().size(); ++k)
            if (k != sel && letter_at(k, x, y) >= 0) return static_cast<int>(k);
        return -1;
    }
    // The ball of the selected word's rack under (x, y), or -1: the rack
    // shows only while the word is unsolved, and a typed ball has left it.
    int rack_ball_at(float x, float y) const {
        if (round_.over() || selected_solved()) return -1;
        const std::string& r = rack();
        const int n = static_cast<int>(r.size());
        for (int k = 0; k < n; ++k) {
            if (rack_in_row(static_cast<std::size_t>(k))) continue;
            if (std::fabs(x - ball_center_x(n, k)) <= BALL_HALF &&
                std::fabs(y - (CROSSWORD_RACK_Y + BALL_HALF)) <= BALL_HALF)
                return k;
        }
        return -1;
    }
    // 0x421e50: nothing while busy; a row in play selects its word, or, on
    // the selected row, goes to the pointer handler 0x41f2b0; after the
    // round a row opens its definition (§3.9): the played word when solved
    // (word+0x3c), the answer after GIVE UP (word+0x90), never popup 10. The
    // rack's balls go to 0x41f2b0 too.
    void grid_click(float x, float y) {
        if (busy_ || finished_) return;
        const int k = word_at(x, y);
        if (k >= 0) {
            const auto slot = static_cast<std::size_t>(k);
            if (round_.over()) {
                if (services_.open_url) services_.open_url(definition_url(round_.shown_word(slot)));
                return;
            }
            if (!accepts_input()) return;
            if (slot == round_.selected()) row_click(letter_at(slot, x, y));
            else if (round_.select(slot)) select_changed();
            return;
        }
        const int ball = rack_ball_at(x, y);
        if (ball >= 0) place_ball(static_cast<std::size_t>(ball));
    }
    // 0x41f2b0 on the selected row: a click on the last typed letter takes
    // it back (0x41b390); on a solved row, a click on its last letter takes
    // the word back (0x41eaf0). INFERRED: which cell of a solved row is "the
    // last letter" (its last one).
    void row_click(int cell) {
        if (cell < 0) return;
        const int last = selected_solved() ? static_cast<int>(entry_.size()) - 1 : last_typed_cell();
        if (cell == last) backspace();
    }

    // ---- dialogs ----------------------------------------------------
    bool pause_menu_up() const {
        if (dialog_kind_ == GameDialog::PauseMenu) return true;
        for (const auto& u : under_)
            if (u.first == GameDialog::PauseMenu) return true;
        return false;
    }
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
            case GameDialog::ConfirmGiveUp:   // TEXT_DIALOG_GIVEUP_HEADER / TEXT_DIALOG_GIVEUP (0x40b310)
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
            case GameDialog::Tutorial: dialog_ = build_tutorial(static_cast<int>(tt2::GameMode::Crossword)); break;
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
                    highscore_.emplace(*highscores_, HighScoreDialog::Opener::Game, tt2::GameMode::Crossword,
                                       services_.play_sound);
                break;
            default: dialog_ = Dialog{}; break;
        }
        for (auto& b : buttons_) b.hover = b.pressed = false;
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
    // DIALOG_HIGHSCORES from CONGRATS: OK and MAIN MENU both leave the game.
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

    // ---- layout (dialogs.xml SCENE_CRISSCROSS, crossword.md §1.1) ------
    void build_buttons() {
        // Sheets: 4 rows (normal, hover, pressed, disabled); sizes from the
        // PNG headers (crossword.md §1.2).
        auto add = [&](const char* name, const char* image, int w, int h, float x, float y) {
            MenuButton b;
            b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
            buttons_.push_back(b);
        };
        add("BUTTON_Menu", "BUTTON_MENU", 119, 200, 10, 554);               // :147
        add("BUTTON_Twist", "BUTTON_Twist", 119, 200, 154, 447);            // :148
        add("BUTTON_Enter", "BUTTON_Enter", 119, 200, 526, 447);            // :149
        add("BUTTON_LastWord", "BUTTON_LastWord", 119, 200, 402, 447);      // :150
        add("BUTTON_Clear", "BUTTON_Clear", 119, 200, 278, 447);            // :151
        add("BUTTON_NextRound", "BUTTON_NextRound", 157, 208, 321, 497);    // :152, never shown (0x41b510)
        add("BUTTON_Zoom", "BUTTON_ZOOM", 68, 296, 706, 244);               // :153, 4 frames of 68x74
        add("BUTTON_GiveUp", "BUTTON_GiveUp", 157, 208, 321, 497);          // :154, shares NEXT ROUND's place
        add("BUTTON_NEXTWORD", "IMAGE_BTN_ARROW_NEXT", 52, 336, 722, 125);  // :155, 4 frames of 52x84
        add("BUTTON_PREVWORD", "IMAGE_BTN_ARROW_PREV", 52, 336, 26, 125);   // :165, 4 frames of 52x84
        button("BUTTON_NextRound")->sprite.visible = false;
    }
    // dialogs.xml:165: BUTTON_PREVWORD is the one button without
    // clicksound="SOUND_BUTTON", and 0x41e230 plays nothing (crossword.md §3.3).
    static bool has_click_sound(const std::string& name) { return name != "BUTTON_PREVWORD"; }

    void build_sprites() {
        auto add = [&](const char* name, SheetSpec spec, float x, float y, bool shown) {
            SpriteState s = sprite(name, std::move(spec), x, y);
            s.visible = shown;
            sprites_.push_back(std::move(s));
        };
        // :137, no x / y; 0x44b370 re-adds it above the grid (T6b.5).
        add("OVERLAY", sheet("IMAGE_OVERLAY", 800, 600), 0, 0, true);
        add("UI_BAR", sheet("IMAGE_UI_BAR", 800, 54), 0, CROSSWORD_UI_BAR_Y, true);   // :138
        // :158: 12 frames of 572x90; unused, the code builds its own (0x41de10).
        add("SPRITE_HIGHLIGHT", crossword_highlight_sheet(), 101, 105, false);
        // :159-164, gamescene's panel; never shown in this scene (crossword.md §11).
        SpriteState bg = sprite("PANEL_BG", sheet("IMAGE_PANEL_BG", 800, 195), 400, 0);
        bg.anchor = Anchor::Top;   // anchorpoint="top", pivotpoint="top"
        bg.visible = false;
        sprites_.push_back(bg);
        add("PANEL_BOTTOM", sheet("IMAGE_PANEL_BOTTOM", 1, 10), 190, 120, false);
        add("PANEL_LEFTCORNER", sheet("IMAGE_PANEL_CORNERL", 10, 10), 180, 120, false);
        add("PANEL_LEFT", sheet("IMAGE_PANEL_SIDEL", 10, 1), 180, 0, false);
        add("PANEL_RIGHTCORNER", sheet("IMAGE_PANEL_CORNERR", 10, 10), 630, 120, false);
        add("PANEL_RIGHT", sheet("IMAGE_PANEL_SIDER", 10, 1), 630, 0, false);
    }

    // 0x41b510 (CW23, crossword.md §3.8). Under a dialog (+0x260) or while
    // busy (+0x2f9) all ten buttons are off and keep their visibility.
    // Puzzle solved: only MENU on, GIVE UP shown but off. In play: MENU, the
    // arrows, ZOOM and GIVE UP on; TWIST with 2+ rack letters on an unsolved
    // word; ENTER on an unsolved word with letters in the row; LAST WORD with
    // an empty row and a last word; CLEAR with letters in the row (a solved
    // row holds its played word). NEXT ROUND is hidden and off in every state.
    // "Letters in the row" counts the typed letters, not the locked ones:
    // the play-test shows a row with only its locked letter with TWIST on
    // and CLEAR, LAST WORD and ENTER off, and one typed letter turning CLEAR
    // and ENTER on (CW74, CW75). TWIST counts the balls left in the rack,
    // which every ball in the row has left. Port choice: after GIVE UP the
    // buttons are set as for a solved puzzle. The exe's refresh tests only
    // "all solved" (0x41b8c0), so in the reveal's busy-free stretch it would
    // turn GIVE UP back on and a second YES would reveal again.
    void refresh_buttons() {
        const bool locked = dialog_open() || busy_;
        const bool ended = round_.over();
        const auto& slots = round_.slots();
        const std::size_t sel = round_.selected();
        const bool solved = selected_solved();
        const bool has_last = sel < slots.size() && !slots[sel].last_word.empty();
        const std::size_t typed_count = typed().size();
        const bool row_letters = solved || typed_count > 0;
        std::size_t rack_left = 0;
        for (std::size_t k = 0; k < rack().size(); ++k) if (!rack_in_row(k)) ++rack_left;
        for (auto& b : buttons_) {
            const std::string& n = b.sprite.name;
            if (n == "BUTTON_NextRound") {
                b.sprite.visible = false;
                b.enabled = false;
                continue;
            }
            if (locked) { b.enabled = false; continue; }
            if (n == "BUTTON_Menu") b.enabled = true;
            else if (n == "BUTTON_GiveUp") {
                b.sprite.visible = true;
                b.enabled = !ended;
            }
            else if (ended) b.enabled = false;
            else if (n == "BUTTON_Twist") b.enabled = rack_left > 1 && !solved;
            else if (n == "BUTTON_Enter") b.enabled = !solved && typed_count > 0;
            else if (n == "BUTTON_LastWord") b.enabled = !row_letters && has_last;
            else if (n == "BUTTON_Clear") b.enabled = row_letters;
            else b.enabled = true;   // BUTTON_NEXTWORD, BUTTON_PREVWORD, BUTTON_Zoom
        }
    }

    void play(SoundId id) { if (services_.play_sound) services_.play_sound(id); }
    void changed() { if (services_.on_profile_change) services_.on_profile_change(); }

    tt2::PlayerProfile& profile_;
    tt2::HighScoreManager* highscores_;
    Services services_;
    tt2::Random rng_;          // the game's Java LCG: root length, word count, crossings
    tt2::CrtRandom& crt_;      // the CRT rand(): word list, crossing, rack and TWIST shuffles (D-11)
    tt2::Bag bag_;
    tt2::CrosswordRound round_;
    std::vector<std::string> racks_;   // one per word, placement order (word+0xb4)
    // The selected row, one entry per letter: the rack ball typed into that
    // cell, or -1. Locked cells stay -1: their letter is the crossing's.
    std::vector<int> entry_;
    GamePhase phase_ = GamePhase::Playing;
    CrosswordView view_;
    bool view_placed_ = false;     // the view has stood on a word once
    bool view_horizontal_ = true;  // +0x3c8: the direction the view is turned to
    bool zoom_in_ = false;         // +0x3c9 (zoom_in_flag)
    bool dead_end_ = false;        // 0x41df10's result for the selected word
    int sheen_ms_ = -1;            // since the solved puzzle's sheen began (0x43d310); -1: none
    bool resume_failed_ = false;
    float pointer_x_ = -1.0f, pointer_y_ = -1.0f;
    bool finished_ = false;
    bool busy_ = false;        // +0x2f9: the view's chains and the reveal
    bool menu_pending_ = false;   // +0x20c = 2: focus lost under a dialog
    std::vector<MenuButton> buttons_;
    std::vector<SpriteState> sprites_;
    MessagePopups popups_;
    TrophyPopups trophies_;
    ModifierQueue view_queue_;  // the grid view's chains: pan, turn, zoom
    ModifierQueue queue_;       // the 700 ms advance and the reveal's wait
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
