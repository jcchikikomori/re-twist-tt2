// The Classic game scene (Untimed / Timed) and Word of the Day:
// `TextTwist2Scene` in the original, which runs all three modes in one
// class (game-scene.md §8: the mode string at +0x30c). Layout and flow
// come from the decompilation listed in
// docs/reverse-engineering/game-scene.md; the addresses are repeated
// next to the numbers they justify. Everything here is SDL-free so the
// rules can be unit tested headless; `ui/src/main.cpp` only draws the
// state this class exposes.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/profile.hpp"
#include "tt2/random.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/saved_game.hpp"
#include "tt2/trophies.hpp"
#include "tt2/word_tree.hpp"
#include "tt2/wotd_day.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/scene_popups.hpp"
#include "tt2ui/highscore_dialog.hpp"
#include "tt2ui/settings_dialog.hpp"
#include "tt2ui/trophy_room.hpp"
#include "tt2ui/modifier.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

// ---------------------------------------------------------------------------
// Word grid ("AnagramTable", layout at 0x431e50, word node at 0x430800)
// ---------------------------------------------------------------------------

constexpr int ANAGRAM_TILE_W = 20;        // ANAGRAM_TILE frame (anagramTile.png 20x40, 2 frames)
constexpr int ANAGRAM_TILE_H = 20;
constexpr int ANAGRAM_COLUMN_GAP = 3;     // default.xml ANAGRAM_ColumnGap
constexpr int ANAGRAM_COLUMN_PAD = 20;    // 0x431e50: "+ 20.0" after the longest word of a column
constexpr int ANAGRAM_MIN_COLUMNS = 4;    // 0x431e50: columns = max(4, ceil(n / 8))

// Word-appear flip (0x4310f0, game-scene.md §2.1). Per letter i of a word:
// wait i * 100 ms (no wait when every letter flips together), squash the
// tile's height 1 -> 0 in 150 ms, set the letter frame (and the yellow
// tile frame for a revealed word), then grow tile and letter 0 -> 1 in
// 150 ms with the letter shown. All scales are linear (0x4307e0).
constexpr int ANAGRAM_FLIP_STAGGER_MS = 100;  // 0x4316c4: delay += 0x64 per letter
constexpr int ANAGRAM_FLIP_HALF_MS = 150;     // ModifierScale 0x96, both halves

struct AnagramTile {
    float tile_scale_y = 1.0f;
    float letter_scale_y = 1.0f;
    bool letter_visible = false;
    int tile_frame = 0;   // 1 = yellow, only for words revealed at round end
};

// Time from the flip's start until the last letter has settled.
inline int anagram_flip_ms(int letters, bool together) {
    if (letters <= 0) return 0;
    return (together ? 0 : (letters - 1) * ANAGRAM_FLIP_STAGGER_MS) + 2 * ANAGRAM_FLIP_HALF_MS;
}

// Tile and letter `i` at `elapsed` ms into the flip. `revealed` marks a
// word the player missed (slot +0x174, set only by 0x4333a0).
inline AnagramTile anagram_tile_at(int elapsed, int i, bool revealed, bool together) {
    AnagramTile t;
    const int start = together ? 0 : i * ANAGRAM_FLIP_STAGGER_MS;
    const int local = elapsed - start;
    if (local < ANAGRAM_FLIP_HALF_MS) {
        if (local > 0) t.tile_scale_y = 1.0f - static_cast<float>(local) / ANAGRAM_FLIP_HALF_MS;
        return t;
    }
    t.tile_frame = revealed ? 1 : 0;
    t.letter_visible = true;
    const float grow = std::min(1.0f, static_cast<float>(local - ANAGRAM_FLIP_HALF_MS) / ANAGRAM_FLIP_HALF_MS);
    t.tile_scale_y = grow;
    t.letter_scale_y = grow;
    return t;
}
constexpr int ANAGRAM_WORDS_PER_COLUMN = 8;
constexpr float TABLE_Y = 15.0f;          // 0x4396e0: table at (400 - width/2, 15)

struct SlotLayout {
    int x = 0, y = 0;        // top-left of the first tile, relative to the table
    int column = 0;
};

struct TableLayout {
    std::vector<SlotLayout> slots;   // one per board word, in board order
    int width = 0;                   // right edge of the widest column (+0x1c4)
    int height = 0;                  // bottom of the tallest column (+0x1c8)
};

// 0x431e50. Words are laid out in board order (length 3..8, DFS order
// within a length) top to bottom, `rows_per_column` rows per column with
// the fractional remainder spread over the columns; a new column starts
// `longest * 20 + 20 + ANAGRAM_ColumnGap` to the right of the previous
// one, where `longest` is the longest word placed so far (the running
// maximum starts at 3).
inline TableLayout layout_anagram_table(const std::vector<std::string>& words) {
    TableLayout out;
    const int n = static_cast<int>(words.size());
    if (n == 0) return out;
    int columns = static_cast<int>(std::ceil(n / static_cast<double>(ANAGRAM_WORDS_PER_COLUMN)));
    if (columns < ANAGRAM_MIN_COLUMNS + 1) columns = ANAGRAM_MIN_COLUMNS;   // "< 5 -> 4"
    const double per_column = n / static_cast<double>(columns);
    const int base_rows = static_cast<int>(std::floor(per_column));
    const double frac_step = per_column - base_rows;
    double frac = frac_step;
    int rows_left = base_rows;
    int longest = 3;
    float x = 0.0f, y = 0.0f;
    int column = 0;
    for (int i = 0; i < n; ++i) {
        if (rows_left < 1) {
            frac += frac_step;
            if (frac >= 1.0 || frac < 0.999) {
                rows_left = base_rows;
                if (frac >= 1.0) { frac -= 1.0; rows_left = base_rows + 1; }
            } else {
                frac = 0.0;
                rows_left = base_rows + 1;
            }
            x += static_cast<float>(ANAGRAM_COLUMN_GAP + longest * ANAGRAM_TILE_W + ANAGRAM_COLUMN_PAD);
            y = 0.0f;
            ++column;
        }
        SlotLayout s;
        s.x = static_cast<int>(x);
        s.y = static_cast<int>(y);
        s.column = column;
        out.slots.push_back(s);
        const int len = static_cast<int>(words[static_cast<std::size_t>(i)].size());
        longest = std::max(longest, len);
        y += ANAGRAM_TILE_H;
        out.width = std::max(out.width, static_cast<int>(x) + longest * ANAGRAM_TILE_W);
        out.height = std::max(out.height, static_cast<int>(y));
        --rows_left;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Letter rows ("LetterballContainer", 0x4396e0 / 0x43bad0 / 0x43c370)
// ---------------------------------------------------------------------------

constexpr float BALL_PITCH = 69.0f;       // 0x43bad0: x = i * 69 + 34.5
constexpr float BALL_HALF = 34.5f;
constexpr float PLAYED_ROW_Y = 213.0f;    // 0x4396e0: container y of the typed letters
constexpr float AVAILABLE_ROW_Y = 295.0f; // 0x4396e0: container y of the rack
constexpr int BALL_W = 60;                // IMAGE_LETTERBALL_ANIM 1020x1560, 17 columns, 26 rows
constexpr int BALL_H = 60;
constexpr int BALL_FRAMES_PER_LETTER = 17;   // 0x439d00: frame = (letter - 'a') * 17
// The resting ball is the round "pearl": the round-start flip (0x43a820)
// runs a ModifierFrame from the letter's first frame to first + 11 and
// stops there. Frames 0-2 are the blank ball turning edge-on, 3 shows
// the letter edge-on, 4-11 the letter ball; 12-16 morph into the square
// tile used elsewhere (CONFIRMED range, frame contents read off the sheet).
constexpr int BALL_IDLE_FRAME = 11;
constexpr int BALL_REVEAL_FRAME_MS = 60;     // 0x43a820: ModifierFrame(60 ms)
constexpr int BALL_REVEAL_STAGGER_MS = 120;  // 0x43c940: delay += 0x78 per ball
constexpr int BALL_MOVE_MS = 300;            // 0x43a4c0: ModifierLocationLine(0x12c) for place / take back
constexpr int BALL_RELAYOUT_MS = 400;        // 0x43a4c0 (no container): 0x190, used by the twist relayout
constexpr int TWIST_MAX_SHUFFLES = 16;       // port guard on 0x43c370's reshuffle-until-different loop

// x of the left edge of a row holding `n` letters (0x4396e0: 400 - n*69/2).
inline float row_left(int n) { return 400.0f - static_cast<float>(n) * BALL_PITCH * 0.5f; }
inline float ball_center_x(int n, int slot) { return row_left(n) + static_cast<float>(slot) * BALL_PITCH + BALL_HALF; }

// The cell art under each ball. Both rows are LetterBox containers
// (0x43ab90, vtable LetterBox): for every cell the ctor adds a "LetterBox"
// sprite (letterBox.png 69x138, two 69x69 frames stacked) centred at
// (i * 69 + 34.5, 34.5) and sets its frame to the ctor's last argument.
// 0x4396e0 passes 0 for the played row (frame 0, the blue socket) and 1
// for the available row (frame 1, a small gold dot that the resting ball
// covers). The container's children are drawn before the balls it holds.
constexpr int LETTERBOX_SIZE = 69;
constexpr int LETTERBOX_PLAYED_FRAME = 0;
constexpr int LETTERBOX_AVAILABLE_FRAME = 1;

inline SheetSpec letter_box_sheet() { return sheet("LetterBox", 69, 138, 1, 2, 2); }

// The played row's n cells, then the available row's n cells.
inline std::vector<SpriteState> letter_boxes(int n) {
    std::vector<SpriteState> out;
    for (int row = 0; row < 2 && n > 0; ++row) {
        const bool played = row == 0;
        const float y = (played ? PLAYED_ROW_Y : AVAILABLE_ROW_Y) + BALL_HALF;
        for (int i = 0; i < n; ++i) {
            SpriteState s = sprite("LETTERBOX", letter_box_sheet(), ball_center_x(n, i), y);
            s.anchor = Anchor::Center;
            s.frame = played ? LETTERBOX_PLAYED_FRAME : LETTERBOX_AVAILABLE_FRAME;
            out.push_back(s);
        }
    }
    return out;
}

struct LetterBall {
    char letter = 0;
    bool played = false;    // which row it sits in
    int slot = 0;           // cell index inside that row
    float x = 0, y = 0;     // current centre on screen
    float target_x = 0, target_y = 0;
    // Slide: a straight line from (start_x, start_y) to the target over
    // move_ms (ModifierLocationLine, easing 0x4513d0 = smoothstep, set at
    // 0x43a601 / 0x43a797 in 0x43a4c0).
    float start_x = 0, start_y = 0;
    int move_ms = 0, move_elapsed_ms = 0;
    // Round-start flip: wait reveal_delay_ms, then frames 0..11.
    int frame = BALL_IDLE_FRAME;   // offset inside the letter's 17-frame row
    int reveal_delay_ms = 0;
    int reveal_elapsed_ms = -1;    // -1: not flipping
    bool reveal_started = false;   // the delay ran out and the frames began
    bool revealing() const { return reveal_elapsed_ms >= 0; }
    // Still sliding to its cell (0x43a4c0 keeps the move in +0x184 until
    // the method 0x43a2f0 clears it at the end of the slide).
    bool moving() const { return move_ms > 0 && move_elapsed_ms < move_ms; }
    // Frame index in IMAGE_LETTERBALL_ANIM.
    int sheet_frame() const { return (letter - 'a') * BALL_FRAMES_PER_LETTER + frame; }
    // Advance the slide and the flip; true on the tick the flip starts.
    // 0x43a820 adds the ModifierFrame and the SOUND_letter_load method
    // (0x43a940) as siblings under the same ModifierDelay, so the sound
    // plays as the frames begin, not after the 12th frame.
    bool tick(int dt) {
        if (move_ms > 0 && move_elapsed_ms < move_ms) {
            move_elapsed_ms = std::min(move_ms, move_elapsed_ms + dt);
            const float t = ease(Easing::Smoothstep, static_cast<float>(move_elapsed_ms) / static_cast<float>(move_ms));
            x = start_x + (target_x - start_x) * t;
            y = start_y + (target_y - start_y) * t;
        } else {
            x = target_x;
            y = target_y;
        }
        if (!revealing()) return false;
        int left = dt;
        if (reveal_delay_ms > 0) {
            const int used = std::min(reveal_delay_ms, left);
            reveal_delay_ms -= used;
            left -= used;
            if (reveal_delay_ms > 0) return false;
        }
        const bool started = !reveal_started;
        reveal_started = true;
        reveal_elapsed_ms += left;
        const int last = BALL_IDLE_FRAME;
        if (reveal_elapsed_ms >= (last + 1) * BALL_REVEAL_FRAME_MS) {
            frame = last;
            reveal_elapsed_ms = -1;
        } else {
            frame = reveal_elapsed_ms / BALL_REVEAL_FRAME_MS;
        }
        return started;
    }
};

// DIALOG_NEXTROUND letterballs (0x409100, game-scene.md §6): every
// non-space character of TEXT_NEXT_ROUND_LETTERBALLS becomes a LetterBall
// on the next sprite slot "1".."8" (dialog-local centres). Character i
// (spaces count) waits 500 + 200 * i ms, then runs the round-start flip
// 0x43a820 with its sound flag set. Upper case is folded to the sheet's
// a-z rows; a character without a row, or one past slot 8, gets no ball
// (the original finds no sprite "9"). Each ball also bursts 8 particles
// (next_round_particles below).
inline constexpr const char* NEXT_ROUND_LETTERBALLS = "Great Job";   // TEXT_NEXT_ROUND_LETTERBALLS
constexpr int NEXT_ROUND_BALL_SLOTS = 8;
constexpr float NEXT_ROUND_BALL_XY[NEXT_ROUND_BALL_SLOTS][2] = {
    {60, 34}, {124, 3}, {192, -8}, {259, 1}, {321, 34}, {126, 75}, {192, 61}, {257, 74}};
constexpr int NEXT_ROUND_BALL_DELAY_MS = 500;   // 0x409100: local_d0 = 500
constexpr int NEXT_ROUND_BALL_STEP_MS = 200;    // += 200 per character

// DIALOG_CONGRATS runs the same routine (0x401290) on TEXT_CONGRATS_LETTERBALLS
// with its own seven sprites "1".."7".
inline constexpr const char* CONGRATS_LETTERBALLS = "Awesome";   // TEXT_CONGRATS_LETTERBALLS
constexpr int CONGRATS_BALL_SLOTS = 7;
constexpr float CONGRATS_BALL_XY[CONGRATS_BALL_SLOTS][2] = {
    {17, 99}, {62, 52}, {124, 18}, {193, 1}, {262, 18}, {319, 52}, {365, 104}};

inline std::vector<LetterBall> dialog_letter_balls(const std::string& text, const float (*xy)[2], int slots) {
    std::vector<LetterBall> out;
    int slot = 0;
    for (std::size_t i = 0; i < text.size() && slot < slots; ++i) {
        if (text[i] == ' ') continue;
        const int ix = slot++;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        if (c < 'a' || c > 'z') continue;
        LetterBall b;
        b.letter = c;
        b.slot = ix;
        b.start_x = b.target_x = b.x = xy[ix][0];
        b.start_y = b.target_y = b.y = xy[ix][1];
        b.frame = 0;
        b.reveal_delay_ms = NEXT_ROUND_BALL_DELAY_MS + NEXT_ROUND_BALL_STEP_MS * static_cast<int>(i);
        b.reveal_elapsed_ms = 0;
        out.push_back(b);
    }
    return out;
}
inline std::vector<LetterBall> next_round_balls(const std::string& text) {
    return dialog_letter_balls(text, NEXT_ROUND_BALL_XY, NEXT_ROUND_BALL_SLOTS);
}
inline std::vector<LetterBall> congrats_balls(const std::string& text) {
    return dialog_letter_balls(text, CONGRATS_BALL_XY, CONGRATS_BALL_SLOTS);
}

// DIALOG_NEXTROUND particle burst (0x409100, CONFIRMED from the listing):
// 8 IMAGE_PARTICLEBALL sprites (particleball.png, 18x18) per ball, linked
// in just before their ball so they draw under it. Each one waits its
// ball's delay (ModifierDelay, 0x4096d9 copies the 500 + 200 * i delay),
// then two modifiers chained after that delay run together:
//   - ModifierLocationLine, 700 ms (0x4098bf: EDX = 0x2bc), linear
//     (0x4307e0), from the ball centre to centre + (75 + rx) * cos(a),
//     (75 + ry) * sin(a);
//   - ModifierAlpha 1 -> 0 over 600 + rand % 90 ms, linear.
// Random draws per particle, in this order: the angle a = next(24) / 2^24
// * 2pi (0x4095a1, CL = 0x18), rx = rand % 50, ry = rand % 50, then the
// fade rand % 90. The fade always ends first (at most 689 ms), so a
// particle is gone before it reaches the end of its line.
constexpr int NEXT_ROUND_PARTICLES_PER_BALL = 8;   // 0x40935b: [esp+0x3c] = 8
constexpr int PARTICLE_TRAVEL_MS = 700;
constexpr float PARTICLE_DISTANCE = 75.0f;         // [0x59ba08]
constexpr int PARTICLE_DISTANCE_SPAN = 50;         // rand % 0x32
constexpr int PARTICLE_FADE_MS = 600;              // + 0x258
constexpr int PARTICLE_FADE_SPAN_MS = 90;          // rand % 0x5a

struct Particle {
    float start_x = 0, start_y = 0;   // dialog-local, the ball's centre
    float end_x = 0, end_y = 0;
    int delay_ms = 0;
    int fade_ms = PARTICLE_FADE_MS;
    int elapsed_ms = 0;

    void tick(int dt) { elapsed_ms += dt; }
    int moving_ms() const { return std::max(0, elapsed_ms - delay_ms); }
    float x() const {
        const float t = std::min(1.0f, static_cast<float>(moving_ms()) / PARTICLE_TRAVEL_MS);
        return start_x + (end_x - start_x) * t;
    }
    float y() const {
        const float t = std::min(1.0f, static_cast<float>(moving_ms()) / PARTICLE_TRAVEL_MS);
        return start_y + (end_y - start_y) * t;
    }
    float alpha() const {
        return 1.0f - std::min(1.0f, static_cast<float>(moving_ms()) / static_cast<float>(fade_ms));
    }
};

inline std::vector<Particle> next_round_particles(const std::vector<LetterBall>& balls, tt2::Random& rng) {
    constexpr double TWO_PI = 6.283185307179586;   // 0x401921fb60000000
    std::vector<Particle> out;
    for (const auto& b : balls) {
        for (int k = 0; k < NEXT_ROUND_PARTICLES_PER_BALL; ++k) {
            rng.next_int();   // advance, then take the top 24 of the 48 state bits
            const double a = static_cast<double>(rng.state() >> 24) / 16777216.0 * TWO_PI;
            const int rx = rng.rand(0, PARTICLE_DISTANCE_SPAN);
            const int ry = rng.rand(0, PARTICLE_DISTANCE_SPAN);
            Particle p;
            p.start_x = b.x;
            p.start_y = b.y;
            p.end_x = b.x + (PARTICLE_DISTANCE + static_cast<float>(rx)) * static_cast<float>(std::cos(a));
            p.end_y = b.y + (PARTICLE_DISTANCE + static_cast<float>(ry)) * static_cast<float>(std::sin(a));
            p.delay_ms = b.reveal_delay_ms;
            p.fade_ms = PARTICLE_FADE_MS + rng.rand(0, PARTICLE_FADE_SPAN_MS);
            out.push_back(p);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

constexpr int ROUND_MS = 120000;          // 0x425c50: +0x2fc
// Deliberate deviation: the original enables ENTER from one played letter
// (0x425807: size > 0). No board word is shorter than 3 letters, so the
// port keeps the button off until then. Return on the keyboard still
// submits any non-empty row, as in the original (0x427560).
constexpr std::size_t ENTER_MIN_LETTERS = 3;
constexpr int WARNING_MS = 10000;         // 0x425c50: +0x304 (countdown starts at 10 s)
constexpr int LIVES = 3;                  // 0x425420: game over when the miss counter reaches 3
constexpr int REVEAL_MS = 3000;           // 0x428840 / 0x425420 / 0x428c80: ModifierDelay(3000) after the reveal
// Word of the Day (game-scene.md §8).
constexpr std::size_t WOTD_BOARD_PER_LENGTH = 5;   // 0x4262b0: counters [0, 0, 0, 5, 5, 5, 5, 5, 5]
constexpr int WOTD_ALL_WORDS_REVEAL_MS = 1000;     // 0x424730 -> 0x428840(1000)
constexpr int WOTD_GAME_OVER_DELAY_MS = 3000;      // state 3 -> 0x428c80(.., 0): no second reveal, 3000 ms

enum class GamePhase {
    Playing,
    Reveal,      // round over: missing words shown, waiting REVEAL_MS
    AwaitNext,   // DIALOG_NEXTROUND is up; OK starts the next round
    GameOver,    // DIALOG_ENDGAME (or DIALOG_CONGRATS) is up
};

// Passed: PASS or GIVE UP answered YES (0x4253a0 -> 0x425420(0)).
enum class RoundEnd { None, AllWords, TimeUpQualified, TimeUpMissed, NextRound, Passed };

// 0x433500: a clicked word goes to a dictionary page with the word
// appended as is; the slot words are already lowercase a-z. Deliberate
// deviation: the original's http://dictionary.reference.com/browse/
// (0x58feec) now only redirects, so the port goes straight to the
// HTTPS dictionary.com page it lands on.
inline constexpr const char* DEFINITION_URL_PREFIX = "https://www.dictionary.com/browse/";
inline std::string definition_url(const std::string& word) { return DEFINITION_URL_PREFIX + word; }

// 0x42a460 (Word of the Day only): the load recomputes today's scene day
// and, when the save's DAY differs, deletes the save and starts a new game
// with no prompt (game-scene.md §8.6). `rule` is IMPROVEMENTS -> WORD OF
// THE DAY, so switching it can turn a same-day save into another day's
// (AC-W11). Pure, so the front end can run the same check before it asks
// "Continue the game?".
inline bool wotd_save_is_stale(const tt2::SavedGame& g, tt2::WallClock now,
                               tt2::WotdDayRule rule = tt2::WotdDayRule::Original) {
    return static_cast<std::int64_t>(g.day) != tt2::wotd_days(now, rule).scene_day;
}

// The main menu runs the same check before "Continue the game?": a Word of
// the Day save from another scene day is deleted and a new game starts with
// no prompt (AC-W7), so only a same-day save reaches the prompt. The other
// modes keep no day (DAY = -1), so their saves always reach it.
inline bool menu_discards_saved_game(tt2::GameMode mode, const tt2::SavedGame& g, tt2::WallClock now,
                                     tt2::WotdDayRule rule = tt2::WotdDayRule::Original) {
    return mode == tt2::GameMode::WordOfTheDay && wotd_save_is_stale(g, now, rule);
}

struct SlotView {
    std::string word;
    bool found = false;
    bool revealed = false;   // shown at the end of a lost round
    // The original tests the length of the shown word (+0x1b0, the size of
    // the std::string at +0x19c), so guessed and revealed words both count.
    bool shown() const { return found || revealed; }
    // 0x431740: a hit on any of the word's 20x20 tiles.
    bool contains(float px, float py) const {
        return px >= static_cast<float>(x) && py >= static_cast<float>(y) &&
               px < static_cast<float>(x + static_cast<int>(word.size()) * ANAGRAM_TILE_W) &&
               py < static_cast<float>(y + ANAGRAM_TILE_H);
    }
    int x = 0, y = 0;        // screen position of the first tile
    int flip_ms = -1;        // time into the word-appear flip; -1 = not started
    bool flip_together = false;   // restored words flip every letter at once (0x433090)

    AnagramTile tile(int i) const {
        if (flip_ms < 0) return AnagramTile{};
        return anagram_tile_at(flip_ms, i, revealed, flip_together);
    }
    bool flipping() const {
        return flip_ms >= 0 && flip_ms < anagram_flip_ms(static_cast<int>(word.size()), flip_together);
    }
};

// A key press as a round letter, or 0. SDL3's letter keycodes are the
// lower-case ASCII letters ('a' == SDLK_A), already following the keyboard
// layout, so the original's WM_CHAR letters come through the same. Ctrl,
// Alt and GUI chords (shortcuts) type nothing.
inline char letter_for_key(std::uint32_t keycode, bool command_modifier) {
    if (command_modifier || keycode < 'a' || keycode > 'z') return 0;
    return static_cast<char>(keycode);
}

class GameScene {
public:
    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void()> on_profile_change;   // bag / counters changed: save
        std::function<void()> on_exit;             // back to the main menu
        std::function<std::uint32_t()> seed_millis = [] { return 0u; };   // timeGetTime() for the bag
        // Saved games (0x427b20 save on leave, 0x461840 delete).
        std::function<void(const tt2::SavedGame&)> save_game;
        std::function<void()> discard_save;
        // Word wrap for the yes/no MESSAGE (wrapstyle="wordwrap"); empty
        // keeps the strings' own line breaks.
        LineBreaker wrap_text;
        // DIALOG_SETTINGS: live volumes, OK commit, fullscreen support.
        SettingsHost settings;
        // 0x433500: clicking a shown word opens its definition in the
        // browser (ShellExecuteA "open"); the front end owns the browser.
        std::function<void(const std::string&)> open_url;
        // Word of the Day (D-3): the wall clock, read at round start (word
        // day, scene day), on resume (the stale-day check) and at game over
        // (the high-score date). The front end fills it from the OS; the
        // default is a fixed instant, so the model never reads a clock.
        std::function<tt2::WallClock()> now = [] { return tt2::WallClock{}; };
        // IMPROVEMENTS -> WORD OF THE DAY (D-12, deviation UX-12).
        tt2::WotdDayRule wotd_rule = tt2::WotdDayRule::Original;
        // No pause hook: the pause menu leaves the music playing (DialogMenu
        // show 0x40aa30 has no stream call; game-scene.md §6, REA batch A).
    };

    // `mode`: Timed, Untimed or WordOfTheDay (D-1); Lightning and Crossword
    // have scenes of their own, so they throw std::invalid_argument.
    // `resume`: a saved game to continue (0x42a460); nullptr starts round 1.
    // A Word of the Day start throws tt2::NoRackError when the common list
    // has no 5-8 letter word.
    GameScene(const tt2::WordTree& common, const tt2::WordTree& full, tt2::PlayerProfile& profile,
              tt2::HighScoreManager* highscores, tt2::GameMode mode, Services services, std::int64_t rng_seed,
              const tt2::SavedGame* resume = nullptr)
        : common_(common), full_(full), profile_(profile), highscores_(highscores), mode_(scene_mode(mode)),
          services_(std::move(services)), rng_(rng_seed), bag_(common, profile.bag) {
        particle_rng_.set_seed(rng_seed + 1);
        build_buttons();
        if (!(resume && resume_game(*resume))) {
            start_round(1);
            open_tutorial_once();
        }
    }

    // ---- state for the renderer -------------------------------------
    tt2::GameMode mode() const { return mode_; }
    // The clock runs in Timed and Word of the Day (0x426d59 / 0x426d67).
    bool timed() const { return mode_ == tt2::GameMode::Timed || wotd(); }
    bool wotd() const { return mode_ == tt2::GameMode::WordOfTheDay; }
    // Word of the Day: the scene day taken at round start (+0x1ec, local
    // standard time by default), which the save writes as DAY and game end
    // writes into player.WOD; -1 in the other modes.
    std::int32_t scene_day() const { return scene_day_; }
    GamePhase phase() const { return phase_; }
    RoundEnd last_round_end() const { return round_end_; }
    int round_number() const { return round_number_; }
    std::int32_t score() const { return score_; }
    int time_left_ms() const { return time_left_ms_; }
    bool qualified() const { return round_ && round_->qualified(); }
    int misses() const { return misses_; }
    bool finished() const { return finished_; }
    const std::string& rack() const { return rack_; }
    const std::string& last_word() const { return last_word_; }
    const TableLayout& table() const { return table_; }
    float table_x() const { return 400.0f - static_cast<float>(table_.width) * 0.5f; }   // 0x4396e0
    float table_y() const { return TABLE_Y; }
    const std::vector<SlotView>& slots() const { return slots_; }
    const std::vector<LetterBall>& balls() const { return balls_; }
    // USER-OBSERVED (not traced in the binary): an available cell's gold
    // dot stays hidden while its ball deals in, and shows once it rests.
    std::vector<SpriteState> letter_boxes() const {
        const int n = static_cast<int>(balls_.size());
        std::vector<SpriteState> out = tt2ui::letter_boxes(n);
        for (const auto& b : balls_)
            if (!b.played && b.revealing() && b.slot >= 0 && b.slot < n) out[n + b.slot].visible = false;
        return out;
    }
    // DIALOG_NEXTROUND letterballs, dialog-local; empty for other dialogs.
    const std::vector<LetterBall>& dialog_balls() const { return dialog_balls_; }
    // DIALOG_NEXTROUND particle burst, dialog-local; drawn under the balls.
    const std::vector<Particle>& dialog_particles() const { return dialog_particles_; }
    // DIALOG_HELP page state; meaningful while dialog_kind() == HowToPlay.
    const HelpPager& help() const { return help_; }
    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    MenuButton* button(const std::string& name) {
        for (auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    // The popup on screen (hidden when none), its kind, and how many wait.
    const SpriteState& popup() const { return popups_.sprite(); }
    std::optional<PopupKind> popup_kind() const { return popups_.kind(); }
    std::size_t popups_waiting() const { return popups_.waiting(); }
    // The trophy popup (0x429510): the trophy art and its FONT_TROPHYNAMES
    // title, both hidden when none is up; the id on show (-1 = none) and
    // how many wait.
    const SpriteState& trophy_popup() const { return trophies_.sprite(); }
    const SpriteState& trophy_label() const { return trophies_.label(); }
    int trophy_popup_id() const { return trophies_.id(); }
    std::size_t trophies_waiting() const { return trophies_.waiting(); }
    const char* background_id() const {
        if (wotd()) return "gamescene/BACKGROUND_WOTD";
        return timed() ? "gamescene/BACKGROUND_TIMED" : "gamescene/BACKGROUND";
    }
    bool dialog_open() const { return dialog_kind_ != GameDialog::None; }
    GameDialog dialog_kind() const { return dialog_kind_; }
    const Dialog& dialog() const {
        return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_;
    }
    // The dialog still open under the top one (the pause menu under its
    // "Exit Game?", SETTINGS or HOW TO PLAY; the tutorial under its help),
    // or nullptr. The original stacks them: 0x408320 / 0x408250 / 0x408180
    // show the new dialog without hiding the menu, and NO / OK (0x408c00)
    // close only the dialog on top.
    const Dialog* under_dialog() const { return under_.empty() ? nullptr : &under_.back().second; }
    GameDialog under_kind() const { return under_.empty() ? GameDialog::None : under_.back().first; }
    Dialog& dialog() { return settings_ ? settings_->dialog() : highscore_ ? highscore_->dialog() : dialog_; }
    // DIALOG_HIGHSCORES while dialog_kind() == HighScores, else nullptr.
    const HighScoreDialog* highscore_dialog() const { return highscore_ ? &*highscore_ : nullptr; }
    // DIALOG_SETTINGS while dialog_kind() == Settings, else nullptr.
    const SettingsDialog* settings_dialog() const { return settings_ ? &*settings_ : nullptr; }
    // Alt+Enter changed the window mode: an open Settings dialog follows it.
    void window_mode_changed(bool fullscreen) { if (settings_) settings_->sync_fullscreen(fullscreen); }
    // The clock and the animations stand still while one of these is up.
    bool paused() const {
        return dialog_kind_ == GameDialog::PauseMenu || dialog_kind_ == GameDialog::ConfirmExit ||
               dialog_kind_ == GameDialog::ConfirmPass || dialog_kind_ == GameDialog::ConfirmGiveUp ||
               dialog_kind_ == GameDialog::HowToPlay ||
               dialog_kind_ == GameDialog::Settings || dialog_kind_ == GameDialog::Tutorial;
    }
    const char* save_mode() const { return tt2::save_mode_name(mode()); }

    // 0x427b20: a round still being played is saved when the scene is left.
    bool can_save() const { return phase_ == GamePhase::Playing && !finished_ && round_ != nullptr; }
    // 0x429dd0: WORD, ROUND, TIME, LONGESTWORD, SCORE, DAY, FAILCOUNT and
    // the found words in board order.
    tt2::SavedGame snapshot() const {
        tt2::SavedGame g;
        g.mode = save_mode();
        g.word = rack_;
        g.round = round_number_;
        g.time_ms = time_left_ms_;
        g.qualified = qualified();
        g.score = score_;
        g.day = wotd() ? scene_day_ : -1;   // 0x429dd0: DAY = +0x1ec
        g.fail_count = misses_;
        for (const auto& v : slots_) if (v.found) g.found_words.push_back(v.word);
        return g;
    }
    // Leaving the scene (menu STOP, closing the window): save a running round.
    void leave() {
        if (can_save() && services_.save_game) services_.save_game(snapshot());
    }

    // UI bar texts (0x426360: minutes label "<m>:" and seconds "%02d").
    std::string score_text() const { return std::to_string(score_); }
    std::string round_text() const { return std::to_string(round_number_); }
    // Whether the UI bar draws "ROUND:" and round_text(). DEVIATION (D6,
    // user decision): hidden in Word of the Day, where the round is always
    // 1. The exe shows "ROUND: 1" there: 0x426360 sets LABEL_ROUND from
    // +0x300 with no mode check (game-scene.md §8.10).
    bool round_visible() const { return !wotd(); }
    std::string timer_minutes_text() const { return std::to_string(time_left_ms_ / 60000) + ":"; }
    std::string timer_seconds_text() const {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02d", (time_left_ms_ / 1000) % 60);
        return buf;
    }

    // The typed word (played row, left to right).
    std::string typed() const {
        std::string s;
        std::vector<const LetterBall*> row;
        for (const auto& b : balls_) if (b.played) row.push_back(&b);
        std::sort(row.begin(), row.end(), [](const LetterBall* a, const LetterBall* b) { return a->slot < b->slot; });
        for (const auto* b : row) s.push_back(b->letter);
        return s;
    }

    // ---- input ------------------------------------------------------
    // Rounds read letters from key presses (letter_for_key), not SDL text
    // input: turning text input on brings up the on-screen keyboard on
    // Android and on Steam Deck, and on Android its hidden text field also
    // swallowed the first Back. Only the name dialog turns text input on.
    void type_letter(char c) {
        if (phase_ != GamePhase::Playing || dialog_open()) return;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return;
        const int best = available_ball(c);
        if (best >= 0) place(static_cast<std::size_t>(best));
    }
    void backspace() {
        if (phase_ != GamePhase::Playing || dialog_open()) return;
        int last = -1;
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            if (!balls_[i].played) continue;
            if (last < 0 || balls_[i].slot > balls_[static_cast<std::size_t>(last)].slot) last = static_cast<int>(i);
        }
        if (last >= 0) unplace(static_cast<std::size_t>(last));
    }
    void key_return() {
        if (dialog_open()) {
            switch (dialog_kind_) {
                case GameDialog::PauseMenu: dialog_press("BUTTON_RESUME"); break;
                case GameDialog::ConfirmExit:
                case GameDialog::ConfirmPass:
                case GameDialog::ConfirmGiveUp: dialog_press("BUTTON_YES"); break;
                default: dialog_press("BUTTON_OK"); break;
            }
            return;
        }
        // 0x427560, engine key 5 (Return): with letters in the played row it
        // is ENTER (0x424730); with the row empty it is LAST WORD (0x425000),
        // so Return twice re-types the word just submitted.
        if (phase_ != GamePhase::Playing) return;
        if (typed().empty()) last_word_again();
        else submit();
    }
    // 0x427560, engine key 4 (Delete, table 0x5d3228: 0x7f): CLEAR (0x4245c0).
    void key_delete() { if (!dialog_open()) clear(); }
    void key_space() { if (phase_ == GamePhase::Playing && !dialog_open()) twist(); }
    // INFERRED: Esc opens the pause menu, and backs out of it.
    void key_escape() {
        switch (dialog_kind_) {
            case GameDialog::None: open_pause_menu(); break;
            case GameDialog::PauseMenu: dialog_press("BUTTON_RESUME"); break;
            case GameDialog::ConfirmExit:
            case GameDialog::ConfirmPass:
            case GameDialog::ConfirmGiveUp: dialog_press("BUTTON_NO"); break;
            case GameDialog::HowToPlay: dialog_press("BUTTON_OK"); break;
            case GameDialog::Settings: dialog_press("BUTTON_CANCEL"); break;
            default: break;   // round-end dialogs need a choice
        }
    }
    // 0x428b60, event 0x206 (INFERRED: the window lost focus): show the menu.
    void focus_lost() { if (!dialog_open()) open_pause_menu(); }
    // Android (T2.2 of docs/plans/android-support.md): the app went to the
    // background, where Android may kill it without warning, so there is
    // no later quit to save on. Pause as on focus loss, and save the round
    // now, as closing the window would; the round goes on when resumed.
    // Resuming (resume_game) or a game over discards the save as usual.
    void enter_background() {
        focus_lost();
        leave();
    }

    // True while the pointer sits on a clickable button — the SDL
    // front end switches to the hand cursor for this.
    bool pointer_over_clickable() const {
        if (settings_) return settings_->pointer_hot();
        if (dialog_open()) return dialog_pointer_hot(dialog());
        for (const auto& b : buttons_)
            if (b.hover && b.enabled && b.sprite.visible) return true;
        return ball_hot_ || word_hot_;
    }

    void mouse_move(float x, float y) {
        if (settings_) { settings_->mouse_move(x, y); return; }
        if (dialog_open()) {
            dialog_mouse_move(dialog(), x, y);   // no hover sound (0x48d390)
            return;
        }
        for (auto& b : buttons_) {
            const bool over = b.sprite.visible && b.enabled && b.contains(x, y);
            b.hover = over;
            if (!over) b.pressed = false;
        }
        // 0x427a60: the pointer over any letter ball (either row, any
        // phase) only switches to the hand cursor; the ball itself does
        // not change.
        ball_hot_ = ball_at(x, y) >= 0;
        word_hot_ = shown_slot_at(x, y) >= 0;   // 0x433500(0): hover only reports the hit
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
            const bool over = b.sprite.visible && b.enabled && b.contains(x, y);
            const bool fire = b.pressed && over;
            b.pressed = false;
            if (fire) {
                play(SoundId::Button);
                press(b.sprite.name);
                return;
            }
        }
        // 0x427680 asks 0x433500(1) first: a click on a shown word looks
        // it up (the original acts on mouse down; the port's grid and balls
        // both act on release).
        const int w = shown_slot_at(x, y);
        if (w >= 0) {
            if (services_.open_url) services_.open_url(definition_url(slots_[static_cast<std::size_t>(w)].word));
            return;
        }
        // 0x427680: a click on an available ball plays it; a click on the
        // last played ball takes it back.
        if (phase_ == GamePhase::Playing) {
            const int i = ball_at(x, y);
            if (i >= 0) {
                const auto k = static_cast<std::size_t>(i);
                if (!balls_[k].played) { place(k); return; }
                if (is_last_played(k)) { unplace(k); return; }
            }
        }
    }

    // The slot under (x, y) that shows a word, or -1 (0x433500 / 0x431740).
    int shown_slot_at(float x, float y) const {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].shown() && slots_[i].contains(x, y)) return static_cast<int>(i);
        return -1;
    }

    // The ball under (x, y), or -1: the 69x69 cell it is heading to.
    int ball_at(float x, float y) const {
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            const auto& b = balls_[i];
            if (std::fabs(x - b.target_x) <= BALL_HALF && std::fabs(y - b.target_y) <= BALL_HALF)
                return static_cast<int>(i);
        }
        return -1;
    }

    // A scene button (also used by the keyboard shortcuts and tests).
    void press(const std::string& name) {
        if (dialog_open()) return;
        // A disabled letter button ignores presses, as a click on it would.
        if (name == "BUTTON_Twist" || name == "BUTTON_Clear" || name == "BUTTON_LastWord" || name == "BUTTON_Enter") {
            const MenuButton* b = button(name);
            if (b && !b->enabled) return;
        }
        if (name == "BUTTON_Twist") twist();
        else if (name == "BUTTON_Clear") clear();
        else if (name == "BUTTON_LastWord") last_word_again();
        else if (name == "BUTTON_Enter") submit();
        // BUTTON_WOTD_DONE's command is BUTTON_NextRound (dialogs.xml), and
        // the dispatcher 0x425b60 matches commands, so DONE runs the NEXT
        // ROUND path 0x428840(3000) (game-scene.md §8.4).
        else if (name == "BUTTON_NextRound" || name == "BUTTON_WOTD_DONE") {
            if (phase_ == GamePhase::Playing && qualified()) next_round();
        }
        else if (name == "BUTTON_Pass" || name == "BUTTON_Pass_2" || name == "BUTTON_GiveUp") ask_pass(name);
        else if (name == "BUTTON_Menu") open_pause_menu();
    }

    // A button of the open dialog (mouse, keyboard and tests).
    void dialog_press(const std::string& name) {
        if (settings_) { settings_result(settings_->press(name)); return; }   // plays its own sounds
        if (highscore_) { highscore_result(highscore_->press(name)); return; }   // so does this one
        const MenuButton* b = dialog_.button(name);
        if (!dialog_open() || !b || !b->enabled || !b->sprite.visible) return;
        play(SoundId::Button);
        switch (dialog_kind_) {
            case GameDialog::PauseMenu:
                if (name == "BUTTON_RESUME") close_dialog();
                else if (name == "BUTTON_STOP") push_dialog(GameDialog::ConfirmExit);    // 0x408320
                else if (name == "BUTTON_HOWTOPLAY") push_dialog(GameDialog::HowToPlay); // 0x408180
                else if (name == "BUTTON_SETTINGS") push_dialog(GameDialog::Settings);   // 0x408250
                break;
            case GameDialog::HowToPlay:
                // OK (0x408c00) closes the help; the dialog that opened it
                // (pause menu or tutorial) is still under it.
                if (name == "BUTTON_OK") pop_dialog();
                else help_.turn(dialog_, name == "BUTTON_PREV" ? -1 : 1);
                break;
            case GameDialog::Tutorial:
                // READ MORE 0x40e1f0 opens DIALOG_HELP over the tutorial;
                // PLAY 0x40e1c0 -> GameScene 0x427b10 clears the block
                // flag +0x260 and refreshes the buttons.
                if (name == "BUTTON_HELP") push_dialog(GameDialog::HowToPlay);
                else close_dialog();
                break;
            case GameDialog::ConfirmExit:
                // 0x408320: YES (0x408430) hands the answer to the scene,
                // which saves the round and leaves (0x427b20); NO (0x408c00)
                // closes only this dialog, uncovering the pause menu.
                if (name == "BUTTON_YES") { close_dialog(); leave(); exit_to_menu(); }
                else pop_dialog();
                break;
            case GameDialog::ConfirmPass:
            case GameDialog::ConfirmGiveUp:
                // YES 0x428610 -> 0x4253a0 -> 0x425420(0); NO 0x428720 resumes.
                close_dialog();
                if (name == "BUTTON_YES") pass_round();
                break;
            case GameDialog::NextRound:
                // Word of the Day: 0x409a70 calls owner slot +0xdc, 0x427b20
                // (leave; no save once the game is over), not +0xd8 (next
                // round) (game-scene.md §8.5, W32-W34).
                close_dialog();
                if (wotd()) exit_to_menu();
                else next_round();
                break;
            case GameDialog::PassRound:   // OK 0x409d10 -> GameScene 0x428aa0 -> 0x425190
                close_dialog();
                next_round();
                break;
            // 0x401b50: OK opens DIALOG_HIGHSCORES (0x428540) on the mode just
            // played, the new entry blinking, MAIN MENU instead of OK.
            case GameDialog::Congrats: open_dialog(highscores_ ? GameDialog::HighScores : GameDialog::EndGame); break;
            case GameDialog::EndGame:
                close_dialog();
                if (name == "BUTTON_REPLAY") new_game();
                else exit_to_menu();
                break;
            case GameDialog::Settings:
            case GameDialog::HighScores:
            case GameDialog::None: break;
        }
    }

    // ---- simulation -------------------------------------------------
    void update(int dt_ms) {
        if (finished_) return;
        if (dialog_kind_ == GameDialog::HowToPlay) help_.tick(dialog_, dt_ms);
        if (settings_) settings_->tick(dt_ms);
        else if (highscore_) highscore_->tick(dt_ms);
        else if (dialog_open()) dialog_tick(dialog_, dt_ms);
        for (auto& b : dialog_balls_)
            if (b.tick(dt_ms)) play(SoundId::LetterLoad);   // 0x43a820 flag 1 -> 0x43a940 as the flip starts
        for (auto& p : dialog_particles_) p.tick(dt_ms);
        if (paused()) return;
        const int round_before = round_number_;
        queue_.update(dt_ms);
        popups_.update(dt_ms);
        // 0x426860 @0x426ce6: the next trophy shows when no dialog or
        // round-end step is pending and none is up. It does not stop the clock.
        if (trophies_.update(dt_ms, !dialog_open() && queue_.empty())) play(SoundId::TrophyWon);
        if (phase_ == GamePhase::Playing && round_number_ == round_before) round_elapsed_ms_ += dt_ms;
        for (auto& v : slots_)
            if (v.flipping()) v.flip_ms += dt_ms;
        for (auto& b : balls_)
            if (b.tick(dt_ms)) play(SoundId::LetterLoad);   // 0x43c940(1): 0x43a940 as each flip starts
        // Timer (0x426d59: only CLASSIC_TIMED / WOTD tick).
        // A round started by the queue this frame begins at a full 2:00.
        if (phase_ == GamePhase::Playing && timed() && round_number_ == round_before) {
            const int before = time_left_ms_;
            time_left_ms_ = std::max(0, time_left_ms_ - dt_ms);
            // 10 s warning: one SOUND_game_clock_countdown per second (0x425f47).
            if (time_left_ms_ <= WARNING_MS && before > 0) {
                const int sec_before = (before + 999) / 1000, sec_now = (time_left_ms_ + 999) / 1000;
                if (sec_now < sec_before && time_left_ms_ > 0) play(SoundId::ClockCountdown);
            }
            if (time_left_ms_ == 0 && before > 0) time_up();
        }
        for (auto& b : buttons_) b.sprite.frame = b.frame_for_state();
    }

private:
    // ---- round flow -------------------------------------------------
    tt2::ClassicRound::Config round_config() const {
        tt2::ClassicRound::Config cfg;
        cfg.timed = timed();
        // Word of the Day skips the target-length loop that reads it
        // (0x426008-0x426104), so MAX WORD LENGTH does not apply there.
        cfg.max_word_length = static_cast<std::size_t>(profile_.max_word_length);
        cfg.round_ms = timed() ? ROUND_MS : 0;
        if (wotd()) {
            cfg.wotd = true;                                 // 0x425fd0 -> 0x4261d4: 0x43d690 picks the rack
            cfg.wotd_day = word_day_;
            cfg.board_per_length_cap = WOTD_BOARD_PER_LENGTH;   // 0x4262b0 @0x426193
        }
        return cfg;
    }

    void start_round(int number) {
        if (wotd()) {
            // 0x425c50 @0x425cb9-0x425cec: the scene day (+0x1ec) is taken
            // once per round start; 0x43d690 picks the word from the UTC
            // word day (game-scene.md §8.1). A game that crosses midnight
            // keeps both.
            const tt2::WotdDays days = tt2::wotd_days(services_.now(), services_.wotd_rule);
            word_day_ = days.word_day;
            scene_day_ = static_cast<std::int32_t>(days.scene_day);
        }
        round_ = std::make_unique<tt2::ClassicRound>(common_, full_, round_config());
        const auto res = round_->start(rng_, bag_, 0);   // the third argument is unused (WOTD: Config::wotd_day)
        profile_.bag = bag_.state();
        if (services_.on_profile_change) services_.on_profile_change();
        setup_round(number, res);
    }

    // 0x42a460: round 1 is rebuilt from WORD, the counters come back from
    // the file, every found word is placed again, then the file is
    // deleted. False (and nothing changed) when the save cannot be used.
    bool resume_game(const tt2::SavedGame& g) {
        if (!g.valid()) return false;
        if (wotd()) {
            // 0x42a460: a save from another scene day is deleted and a new
            // game starts, with no prompt; a same-day save restores its
            // word (no new pick) and its day (game-scene.md §8.6, W36).
            if (wotd_save_is_stale(g, services_.now(), services_.wotd_rule)) {
                if (services_.discard_save) services_.discard_save();
                return false;
            }
            scene_day_ = g.day;
        }
        auto round = std::make_unique<tt2::ClassicRound>(common_, full_, round_config());
        const auto res = round->start_with_rack(g.word);
        if (res.board_words.empty()) return false;
        round_ = std::move(round);
        setup_round(g.round, res);
        for (const auto& w : g.found_words) {
            const auto rep = round_->restore_guess(w);
            if (rep.slot_index < slots_.size()) {
                SlotView& v = slots_[rep.slot_index];
                v.found = true;
                v.word = rep.placed_word;
                v.flip_ms = 0;              // 0x433090 -> 0x432b20 with flag 1:
                v.flip_together = true;     // every letter flips at once
            }
        }
        score_ = g.score;
        misses_ = g.fail_count;
        if (timed()) time_left_ms_ = std::min(ROUND_MS, g.time_ms);
        round_->restore(0, g.qualified, static_cast<std::uint32_t>(time_left_ms_));
        if (services_.discard_save) services_.discard_save();
        refresh_buttons();
        return true;
    }

    void setup_round(int number, const tt2::ClassicRound::Result& res) {
        round_number_ = number;
        // The last word lives in the round object (+0x1f4, string +0x64),
        // which 0x4396e0 builds empty every round: LAST starts off.
        last_word_.clear();
        round_end_ = RoundEnd::None;
        phase_ = GamePhase::Playing;
        time_left_ms_ = ROUND_MS;
        rack_ = res.rack;
        // Board.
        slots_.clear();
        board_words_ = res.board_words;
        table_ = layout_anagram_table(res.board_words);
        for (std::size_t i = 0; i < res.board_words.size(); ++i) {
            SlotView v;
            v.word = res.board_words[i];
            v.x = static_cast<int>(table_x()) + table_.slots[i].x;
            v.y = static_cast<int>(table_y()) + table_.slots[i].y;
            slots_.push_back(v);
        }
        // Rack: every ball starts in the available row (0x4396e0 loop).
        const std::string order = scrambled(rack_);
        balls_.clear();
        const int n = static_cast<int>(rack_.size());
        for (int i = 0; i < n; ++i) {
            LetterBall b;
            b.letter = order[static_cast<std::size_t>(i)];
            b.played = false;
            b.slot = i;
            b.start_x = b.target_x = b.x = ball_center_x(n, i);
            b.start_y = b.target_y = b.y = AVAILABLE_ROW_Y + BALL_HALF;
            // 0x43c940: each ball flips in 120 ms after the previous one.
            b.frame = 0;
            b.reveal_delay_ms = i * BALL_REVEAL_STAGGER_MS;
            b.reveal_elapsed_ms = 0;
            balls_.push_back(b);
        }
        popups_.drop_waiting();   // 0x425c50 -> 0x42ace0: queued popups are dropped
        // 0x425c50: +0x370 (twisted this round) clears every round; the
        // streaks (+0x36c, +0x374) only on round 1 of a new game.
        twisted_this_round_ = false;
        round_elapsed_ms_ = 0;
        if (number == 1) {
            no_twist_streak_ = 0;
            bingo_streak_ = 0;
        }
        refresh_buttons();
    }

    // The rack letters in the order the balls are laid out. DEVIATION /
    // INFERRED: the order the shipped game deals the balls in was not
    // traced. Laying them out in word order handed the player the bingo
    // (ROADMAP "Identified bugs" #1), so they are shuffled with the game
    // LCG like a twist, and shuffled again (up to 8 tries) while they still
    // spell the rack word.
    std::string scrambled(const std::string& word) {
        std::vector<char> letters(word.begin(), word.end());
        for (int tries = 0; tries < 8 && std::string(letters.begin(), letters.end()) == word; ++tries)
            rng_.shuffle(letters);
        return std::string(letters.begin(), letters.end());
    }

    // DIALOG_ENDGAME "PLAY AGAIN": a fresh game in the same mode.
    void new_game() {
        score_ = 0;
        misses_ = 0;
        last_word_.clear();
        start_round(1);
        open_tutorial_once();
    }

    // 0x425c50 (a fresh game, not a resumed one): the player's TRACKER flag
    // for this mode (profile +0xc0 + mode) is read and set; a clear flag
    // sends the scene to state 5, DIALOG_TUTORIAL.
    void open_tutorial_once() {
        bool& seen = profile_.mode_flags[static_cast<std::size_t>(mode())];
        if (seen) return;
        seen = true;
        if (services_.on_profile_change) services_.on_profile_change();
        open_dialog(GameDialog::Tutorial);
    }


    void submit() {
        if (phase_ != GamePhase::Playing) return;
        const std::string word = typed();
        if (word.empty()) return;
        last_word_ = word;
        return_all_letters();
        const bool was_qualified = qualified();
        const auto rep = round_->guess(word);
        using tt2::GuessStatus;
        switch (rep.status) {
            // 0x424730 switch on 0x432850: 0 and -1 play SOUND_word_reject;
            // the no-room case (2) plays no sound, only the popup.
            case GuessStatus::NotAWord:
                play(SoundId::WordReject);
                queue_popup(PopupKind::NotListed);
                return;
            case GuessStatus::Duplicate:
                play(SoundId::WordReject);
                queue_popup(PopupKind::AlreadyGuessed);
                return;
            case GuessStatus::NoRoom: {
                bool any = false;
                for (const auto& s : slots_) if (s.word.size() == word.size()) any = true;
                queue_popup(any ? PopupKind::EnoughOfLength : PopupKind::NoneOfLength);
                return;
            }
            case GuessStatus::Accepted:
            case GuessStatus::Uncommon:
                break;
        }
        score_ += rep.points_awarded;
        // 0x424730, in its order: the no-twist streak, the word counters,
        // the bingo streak (any other word resets it), then Look Ma.
        no_twist_streak_ += 1;
        queue_trophy(tt2::award_no_twist_streak(profile_, no_twist_streak_));
        queue_trophy(tt2::count_word(profile_));
        if (rep.status == GuessStatus::Uncommon) queue_trophy(tt2::count_uncommon(profile_));
        if (rep.bingo) {
            bingo_streak_ += 1;
            queue_trophy(tt2::award_bingo_streak(profile_, bingo_streak_));
        } else {
            bingo_streak_ = 0;
        }
        if (rep.filled_last) queue_trophy(tt2::award_look_ma(profile_, twisted_this_round_));
        // The slot that received the guess shows the guessed word —
        // it may not be the board answer (0x432b20 / 0x431880).
        if (rep.slot_index < slots_.size()) {
            slots_[rep.slot_index].found = true;
            slots_[rep.slot_index].word  = rep.placed_word;
            slots_[rep.slot_index].flip_ms = 0;   // 0x432b20 -> 0x4310f0, staggered
        }
        if (rep.filled_last) {
            play(SoundId::AllWords);
            queue_popup(PopupKind::AllWords);
            end_round(RoundEnd::AllWords);
        } else if (rep.bingo) {
            // 0x424730: every bingo plays the sound; the banner only marks
            // the first one, the one that qualifies the round.
            play(SoundId::WordBingo);
            // Word of the Day queues popup 11 instead of the BINGO banner
            // (0x424730 @0x424aa1 -> 0x428e20(0xb), game-scene.md §8.5).
            if (!was_qualified) queue_popup(wotd() ? PopupKind::WordOfTheDay : PopupKind::Bingo);
            else show_definitions_hint_once();
        } else {
            play(SoundId::WordCorrect);
            show_definitions_hint_once();
        }
        if (services_.on_profile_change) services_.on_profile_change();
        refresh_buttons();
    }

    void time_up() {
        play(SoundId::ClockTimeUp);
        if (round_->qualified()) end_round(RoundEnd::TimeUpQualified);
        else end_round(RoundEnd::TimeUpMissed);
    }

    // State 4 (0x426860): the shown skip button asks "PASS?" while a pass is
    // left (misses + 1 < 3), else "GIVE UP?". Word of the Day has no lives
    // and always asks "Give Up?" (0x426bac -> 0x426c3e, game-scene.md §8.5).
    // Hidden buttons do nothing.
    void ask_pass(const std::string& name) {
        const MenuButton* b = button(name);
        if (phase_ != GamePhase::Playing || !b || !b->enabled || !b->sprite.visible) return;
        open_dialog(!wotd() && misses_ + 1 < LIVES ? GameDialog::ConfirmPass : GameDialog::ConfirmGiveUp);
    }

    // 0x425420(0): a pass is a miss. With misses left the words are revealed
    // and the next round starts by itself; the third miss (GIVE UP) reveals,
    // then ends the game (0x428c80(.., 1) -> 0x427d10).
    void pass_round() {
        if (phase_ != GamePhase::Playing) return;
        end_round(RoundEnd::Passed);
    }

    // Round over. Every path reveals the missing words
    // (SOUND_word_pass_reveal) and waits 3000 ms, then:
    //   qualified (0x428840)      -> DIALOG_NEXTROUND, OK starts the next round
    //   passed, lives left (0x425420(0) -> 0x425190) -> the next round starts by itself
    //   time up unqualified, lives left (0x425420(1) -> 0x428a80, state 6)
    //                             -> DIALOG_PASSROUND, OK starts the next round
    //   third miss / give up (0x425420 else, 0x428c80 -> 0x427d10)
    //                             -> high score, DIALOG_CONGRATS or DIALOG_ENDGAME
    void end_round(RoundEnd why) {
        if (wotd()) {
            end_wotd_round(why);
            return;
        }
        round_end_ = why;
        return_all_letters();
        enum class After { NextDialog, PassDialog, AutoNext, GameOver } after = After::NextDialog;
        switch (why) {
            case RoundEnd::AllWords:
            case RoundEnd::TimeUpQualified:
            case RoundEnd::NextRound:
                // 0x428840: a round finished within 5 s wins Speed Speller.
                if (why != RoundEnd::NextRound) queue_trophy(tt2::award_speed_speller(profile_, round_elapsed_ms_));
                // 0x40f2b0 (the original counts it on NEXT ROUND, 0x425190;
                // the popup waits for the dialog either way).
                queue_trophy(tt2::count_round(profile_, mode()));
                break;
            case RoundEnd::TimeUpMissed:
            case RoundEnd::Passed:
                misses_ += 1;
                play(SoundId::LoseLife);
                if (misses_ >= LIVES) after = After::GameOver;
                else after = why == RoundEnd::TimeUpMissed ? After::PassDialog : After::AutoNext;
                break;
            case RoundEnd::None: break;
        }
        if (services_.on_profile_change) services_.on_profile_change();
        if (after == After::GameOver && services_.discard_save) services_.discard_save();   // 0x461840
        reveal_missing_words();
        queue_.push_delay(REVEAL_MS);
        queue_.push_method([this, after] {
            switch (after) {
                case After::NextDialog:
                    phase_ = GamePhase::AwaitNext;
                    play(SoundId::NextRoundAchieved);   // 0x426860 case 3
                    open_dialog(GameDialog::NextRound);
                    break;
                case After::PassDialog:
                    phase_ = GamePhase::AwaitNext;   // 0x426860 case 6: no sound
                    open_dialog(GameDialog::PassRound);
                    break;
                case After::AutoNext: start_round(round_number_ + 1); break;
                case After::GameOver: finish_game(); break;
            }
            refresh_buttons();
        });
    }

    // Shows the missing words (SOUND_word_pass_reveal) and enters the
    // reveal phase; the caller queues what follows.
    void reveal_missing_words() {
        if (!round_->complete()) {
            // 0x4333a0: each empty slot shows the first word of its length
            // not on show yet (tt2::reveal_words), never a found word again.
            std::vector<std::string> shown;
            for (const auto& s : slots_) shown.push_back(s.found ? s.word : std::string());
            shown = tt2::reveal_words(board_words_, std::move(shown));
            for (std::size_t i = 0; i < slots_.size(); ++i) {
                SlotView& s = slots_[i];
                if (s.found) continue;
                s.word = shown[i];
                s.revealed = true;
                s.flip_ms = 0;   // 0x4333a0: +0x174 = 1, then 0x4310f0 staggered
            }
        }
        play(SoundId::WordPassReveal);
        phase_ = GamePhase::Reveal;
        refresh_buttons();
    }

    // Word of the Day is exactly one round (game-scene.md §8.5); nothing
    // reaches the next-round starter 0x425190.
    //   DONE (0x425b60 -> 0x428840(3000)), all words (0x424730 ->
    //   0x428840(1000)), time up after a bingo (0x426860 @0x42701b ->
    //   0x428840(3000)): reveal, wait, then state 3 (0x426a27 -> 0x426b56):
    //   count_round(player, 4) with its trophy popup, 0x427cc0 (delete the
    //   save, WOD), and 0x428c80(.., 0): 3000 ms with no second reveal, then
    //   game over 0x427d10.
    //   Time up without a bingo (@0x426ff4-0x427014: 0x427cc0) and GIVE UP
    //   -> YES (0x4253a0): delete the save and write WOD at once, then
    //   0x428c80(.., 1): reveal, 3000 ms, game over. No round is counted and
    //   no life is lost: WOTD never calls the miss path 0x425420.
    // Leaving during the reveal, before state 3, writes neither a save nor
    // WOD (INFERRED edge, §8.6): can_save() is false outside Playing.
    void end_wotd_round(RoundEnd why) {
        round_end_ = why;
        return_all_letters();
        const bool round_won =
            why == RoundEnd::AllWords || why == RoundEnd::TimeUpQualified || why == RoundEnd::NextRound;
        // 0x428840: as in Classic, a round finished within 5 s wins Speed Speller.
        if (why == RoundEnd::AllWords || why == RoundEnd::TimeUpQualified)
            queue_trophy(tt2::award_speed_speller(profile_, round_elapsed_ms_));
        if (!round_won) close_wotd_day();
        reveal_missing_words();
        if (round_won) {
            queue_.push_delay(why == RoundEnd::AllWords ? WOTD_ALL_WORDS_REVEAL_MS : REVEAL_MS);
            queue_.push_method([this] {
                // State 3 sets the block flag +0x260 first, so the trophy
                // waits like every popup behind a pending step (trophies.md §1).
                queue_trophy(tt2::count_round(profile_, mode_));   // mov eax, 4 @0x426b74; call 0x40f2b0
                close_wotd_day();
            });
            queue_.push_delay(WOTD_GAME_OVER_DELAY_MS);
        } else {
            queue_.push_delay(REVEAL_MS);
        }
        queue_.push_method([this] {
            finish_game();
            refresh_buttons();
        });
    }

    // 0x427cc0 / 0x4253a0: delete the save (0x461840), then player.WOD = the
    // scene day taken at round start (+0x1ec), so the menu gate closes
    // until the next scene day (game-scene.md §8.6).
    void close_wotd_day() {
        if (services_.discard_save) services_.discard_save();
        profile_.wod_day = scene_day_;
        if (services_.on_profile_change) services_.on_profile_change();
    }

    void next_round() {
        if (phase_ == GamePhase::Playing) {
            // NEXT ROUND pressed while qualified (untimed): ends this round early.
            if (!round_->qualified()) return;
            end_round(RoundEnd::NextRound);
            return;
        }
        if (phase_ != GamePhase::AwaitNext) return;
        start_round(round_number_ + 1);
    }

    // 0x427d10: a positive score goes to the high-score table; a new
    // entry shows DIALOG_CONGRATS, anything else DIALOG_ENDGAME. Word of
    // the Day (game-scene.md §8.5, §8.7) stores the date at game over
    // (0x43ff60: GetSystemTime, so the UTC date by default) instead of the
    // round, and a qualified game that did not rank gets DIALOG_NEXTROUND
    // as a "back to the menu" card.
    void finish_game() {
        phase_ = GamePhase::GameOver;
        bool high = false;
        if (highscores_ && score_ > 0) {
            const std::string extra = wotd() ? tt2::wotd_days(services_.now(), services_.wotd_rule).date
                                             : std::to_string(round_number_);
            const int rank = highscores_->submit(mode(), profile_.name, score_, extra);
            if (rank >= 0) { play(SoundId::HighScoreAchieved); high = true; }
            if (services_.on_profile_change) services_.on_profile_change();
        }
        if (high) {
            open_dialog(GameDialog::Congrats);
        } else if (wotd() && qualified()) {
            play(SoundId::NextRoundAchieved);
            open_dialog(GameDialog::NextRound);
        } else {
            open_dialog(GameDialog::EndGame);
        }
    }

    // ---- dialogs ----------------------------------------------------
    void open_pause_menu() {
        if (finished_ || dialog_open()) return;
        open_dialog(GameDialog::PauseMenu);
    }
    // Replaces every open dialog with `kind` (None closes them all).
    void open_dialog(GameDialog kind) {
        under_.clear();
        show_dialog(kind);
    }
    // Builds `kind` as the top dialog; the ones under it are left alone.
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
            case GameDialog::ConfirmPass:
                dialog_ = build_yes_no(game_strings::PASS_HEADER, game_strings::PASS_BODY, false, services_.wrap_text);
                break;
            case GameDialog::ConfirmGiveUp:
                dialog_ = build_yes_no(game_strings::GIVEUP_HEADER, game_strings::GIVEUP_BODY, false, services_.wrap_text);
                break;
            case GameDialog::Settings:
                dialog_ = Dialog{};
                settings_.emplace(services_.settings.current ? services_.settings.current() : tt2::GameSettings{},
                                  profile_.max_word_length, services_.settings.fullscreen_supported,
                                  SettingsDialog::Services{services_.play_sound,
                                                           services_.settings.apply_sound_volume,
                                                           services_.settings.apply_music_volume});
                break;
            case GameDialog::HowToPlay:
                dialog_ = build_help();
                help_ = HelpPager{};
                break;
            case GameDialog::NextRound:
                dialog_ = build_next_round(wotd());
                dialog_balls_ = next_round_balls(NEXT_ROUND_LETTERBALLS);
                dialog_particles_ = next_round_particles(dialog_balls_, particle_rng_);
                break;
            case GameDialog::EndGame: dialog_ = build_end_game(wotd()); break;
            case GameDialog::Congrats:
                dialog_ = build_congrats();
                dialog_balls_ = congrats_balls(CONGRATS_LETTERBALLS);   // 0x401270 -> 0x401290
                dialog_particles_ = next_round_particles(dialog_balls_, particle_rng_);
                break;
            case GameDialog::HighScores:
                dialog_ = Dialog{};
                if (highscores_)
                    highscore_.emplace(*highscores_, HighScoreDialog::Opener::Game, mode(), services_.play_sound);
                break;
            case GameDialog::PassRound: dialog_ = build_pass_round(); break;
            case GameDialog::Tutorial: dialog_ = build_tutorial(static_cast<int>(mode())); break;
            case GameDialog::None: dialog_ = Dialog{}; break;
        }
        for (auto& b : buttons_) b.hover = b.pressed = false;
        ball_hot_ = word_hot_ = false;
    }
    void close_dialog() { open_dialog(GameDialog::None); }
    // Opens `kind` over the open dialog, which stays as it is underneath.
    void push_dialog(GameDialog kind) {
        if (dialog_kind_ != GameDialog::None) under_.emplace_back(dialog_kind_, dialog_);
        show_dialog(kind);
    }
    // Closes the top dialog only: the one under it comes back unchanged
    // (its page, its values), with no hover left over from before.
    void pop_dialog() {
        if (under_.empty()) { close_dialog(); return; }
        auto [kind, d] = std::move(under_.back());
        under_.pop_back();
        show_dialog(GameDialog::None);
        dialog_kind_ = kind;
        dialog_ = std::move(d);
        for (auto& b : dialog_.buttons) b.hover = b.pressed = false;
    }

    // OK (0x40c620) stores the player's max word length and saves the
    // players (0x462080); it applies from the next rack (F13). Both OK and
    // CANCEL go back to the pause menu.
    void settings_result(SettingsDialog::Result r) {
        if (!settings_) return;
        if (r == SettingsDialog::Result::Ok) {
            profile_.max_word_length = settings_->max_word_length();
            if (services_.on_profile_change) services_.on_profile_change();
            if (services_.settings.commit) services_.settings.commit(settings_->values());
        }
        if (r == SettingsDialog::Result::Ok || r == SettingsDialog::Result::Cancel) pop_dialog();   // the menu is under it
    }

    // DIALOG_HIGHSCORES after a game: MAIN MENU leaves the scene (the art is
    // BUTTON_PAUSE_END; INFERRED, the scene's handler was not traced).
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

    // D-1: the modes this scene plays; the others have their own scenes.
    static tt2::GameMode scene_mode(tt2::GameMode m) {
        if (m == tt2::GameMode::Timed || m == tt2::GameMode::Untimed || m == tt2::GameMode::WordOfTheDay) return m;
        throw std::invalid_argument(std::string("GameScene cannot play mode ") + tt2::mode_key(m));
    }

    // ---- letters ----------------------------------------------------
    int first_free_slot(bool played_row) const {
        const int n = static_cast<int>(balls_.size());
        for (int s = 0; s < n; ++s) {
            bool used = false;
            for (const auto& b : balls_) if (b.played == played_row && b.slot == s) used = true;
            if (!used) return s;
        }
        return -1;
    }
    bool is_last_played(std::size_t i) const {
        for (const auto& b : balls_) if (b.played && b.slot > balls_[i].slot) return false;
        return balls_[i].played;
    }
    void move_to(LetterBall& b, bool played_row, int slot, int ms = BALL_MOVE_MS) {
        b.played = played_row;
        b.slot = slot;
        b.start_x = b.x;
        b.start_y = b.y;
        b.target_x = ball_center_x(static_cast<int>(balls_.size()), slot);
        b.target_y = (played_row ? PLAYED_ROW_Y : AVAILABLE_ROW_Y) + BALL_HALF;
        b.move_ms = ms;
        b.move_elapsed_ms = 0;
        refresh_buttons();   // every letter move changes TWIST / CLEAR / ENTER / LAST
    }
    // The first available ball with that letter, left to right (0x427680
    // scans the row in order), or -1.
    int available_ball(char c) const {
        int best = -1;
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            if (balls_[i].played || balls_[i].letter != c) continue;
            if (best < 0 || balls_[i].slot < balls_[static_cast<std::size_t>(best)].slot) best = static_cast<int>(i);
        }
        return best;
    }
    // 0x43bad0(index = -1): the ball goes to the first empty cell of the
    // row. The move itself is silent; a typed letter clicks.
    void place(std::size_t i, bool click = true) {
        const int slot = first_free_slot(true);
        if (slot < 0) return;
        move_to(balls_[i], true, slot);
        if (click) play(SoundId::LetterPlace);
    }
    void unplace(std::size_t i) {
        const int slot = first_free_slot(false);
        if (slot < 0) return;
        move_to(balls_[i], false, slot);
        play(SoundId::LetterClear);
    }
    void return_all_letters() {
        // Left to right so the rack reads the same way it was typed.
        std::vector<std::size_t> played;
        for (std::size_t i = 0; i < balls_.size(); ++i) if (balls_[i].played) played.push_back(i);
        std::sort(played.begin(), played.end(), [&](std::size_t a, std::size_t b) { return balls_[a].slot < balls_[b].slot; });
        for (std::size_t i : played) {
            const int slot = first_free_slot(false);
            if (slot >= 0) move_to(balls_[i], false, slot);
        }
    }
    void clear() {
        if (phase_ != GamePhase::Playing) return;
        if (typed().empty()) return;
        return_all_letters();
        play(SoundId::LetterClear);
    }
    // 0x425000 plays SOUND_word_last once, at full volume, then moves each
    // letter of the last word with 0x43bad0, which plays nothing: one
    // whoosh, no letter_place per ball. It plays even with no last word,
    // because Return on an empty row (0x427560 case 5) calls it without
    // checking; the LAST button is disabled then (0x425710), so only the
    // key reaches that case.
    void last_word_again() {
        if (phase_ != GamePhase::Playing || dialog_open()) return;
        play(SoundId::WordLast);
        if (last_word_.empty()) return;
        return_all_letters();
        for (char c : last_word_) {
            const int i = available_ball(c);
            if (i >= 0) place(static_cast<std::size_t>(i), false);
        }
    }
    // 0x425240 plays SOUND_word_twist on every press, then 0x43c370 asks
    // 0x43c280 whether the rack may move. DEVIATION: the port plays the
    // sound only when the rack moves, so a held key doesn't stack a click
    // per key repeat while the balls are still sliding. It refuses while fewer than two
    // letters are available, while every available letter is the same, or
    // while any ball in the available row is still sliding (its +0x184
    // move is set until 0x43a2f0 clears it), so a held key twists once per
    // finished slide instead of once per key repeat. Otherwise it shuffles
    // again until the order differs from the one before and from the rack
    // word (0x43c370's two string compares; the second string is INFERRED
    // to be the rack word). INFERRED: the shuffle itself uses the game LCG
    // (std::random_shuffle semantics via 0x439370).
    void twist() {
        if (phase_ != GamePhase::Playing) return;
        if (!can_twist()) return;
        play(SoundId::WordTwist);
        std::vector<std::size_t> avail;
        for (std::size_t i = 0; i < balls_.size(); ++i) if (!balls_[i].played) avail.push_back(i);
        std::vector<int> slots;
        for (std::size_t i : avail) slots.push_back(balls_[i].slot);
        const std::string before = available_order(avail, slots);
        // The original loops until the order changes; the cap only guards
        // against a rack the RNG cannot reorder.
        for (int tries = 0; tries < TWIST_MAX_SHUFFLES; ++tries) {
            rng_.shuffle(slots);
            const std::string now = available_order(avail, slots);
            if (now != before && now != rack_) break;
        }
        // 0x43c370: the relayout slides every ball to its new cell in 400 ms.
        for (std::size_t k = 0; k < avail.size(); ++k) move_to(balls_[avail[k]], false, slots[k], BALL_RELAYOUT_MS);
        // 0x425240: a twist ends the no-twist streak and marks the round.
        no_twist_streak_ = 0;
        twisted_this_round_ = true;
        queue_trophy(tt2::count_twist(profile_));
    }
    // 0x43c280: may the available row be twisted now?
    bool can_twist() const {
        std::string letters;
        for (const auto& b : balls_) {
            if (b.played) continue;
            if (b.moving()) return false;
            letters.push_back(b.letter);
        }
        if (letters.size() < 2) return false;
        return letters.find_first_not_of(letters[0]) != std::string::npos;
    }
    // The available letters read left to right if ball avail[k] sat in slots[k].
    // The row can have empty cells, so this sorts by cell, not by index.
    std::string available_order(const std::vector<std::size_t>& avail, const std::vector<int>& slots) const {
        std::vector<std::pair<int, char>> cells;
        for (std::size_t k = 0; k < avail.size(); ++k) cells.emplace_back(slots[k], balls_[avail[k]].letter);
        std::sort(cells.begin(), cells.end());
        std::string out;
        for (const auto& c : cells) out.push_back(c.second);
        return out;
    }

    // ---- presentation helpers --------------------------------------
    void queue_popup(PopupKind k) { popups_.push(k); }

    // 0x424730: the first accepted word that is neither the first bingo
    // nor the round's last word queues popup 10, once. The flag (+0x1f0)
    // is not cleared at round start (0x425c50); where the original zeroes
    // it is UNCONFIRMED, so here it lasts as long as this scene.
    void show_definitions_hint_once() {
        if (definitions_hint_shown_) return;
        definitions_hint_shown_ = true;
        queue_popup(PopupKind::Definitions);
    }
    void play(SoundId id) { if (services_.play_sound) services_.play_sound(id); }

    void queue_trophy(int id) { trophies_.push(id); }

    // dialogs.xml SCENE_GAME: 119x200 sheets (4 rows of 50) and 157x208
    // sheets (4 rows of 52) for the big round-end buttons.
    void build_buttons() {
        auto add = [&](const char* name, const char* image, int w, int h, float x, float y) {
            MenuButton b;
            b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
            buttons_.push_back(b);
        };
        add("BUTTON_Menu", "BUTTON_MENU", 119, 200, 10, 554);
        add("BUTTON_Twist", "BUTTON_Twist", 119, 200, 147, 407);
        add("BUTTON_Clear", "BUTTON_Clear", 119, 200, 273, 407);
        add("BUTTON_LastWord", "BUTTON_LastWord", 119, 200, 399, 407);
        add("BUTTON_Enter", "BUTTON_Enter", 119, 200, 525, 407);
        add("BUTTON_NextRound", "BUTTON_NextRound", 157, 208, 317, 473);
        add("BUTTON_WOTD_DONE", "BUTTON_Done", 157, 208, 317, 473);   // button_done.png, rowcount 4
        add("BUTTON_GiveUp", "BUTTON_GiveUp", 157, 208, 317, 473);
        add("BUTTON_Pass", "BUTTON_Pass", 157, 208, 317, 473);
        add("BUTTON_Pass_2", "BUTTON_PASS2", 157, 208, 317, 473);
    }
    // NEXT ROUND, PASS x2, PASS and GIVE UP share one spot (317, 473); the
    // refresh 0x425710 shows one: qualified -> NEXT ROUND, else by misses
    // 0 -> BUTTON_Pass_2, 1 -> BUTTON_Pass, 2 -> GIVE UP (game-scene.md §5).
    // Word of the Day (§8.4, W6, W7): round start points the "next round"
    // slot +0x224 at BUTTON_WOTD_DONE, and the refresh hides both PASS
    // buttons, so the spot shows GIVE UP until the first bingo, then DONE.
    // The same refresh gates the letter buttons on the two rows:
    //   TWIST  more than one ball left in the available row (0x4257c3);
    //   CLEAR  at least one letter played (0x425858);
    //   LAST   nothing played and a last word to recall (0x42580f);
    //   ENTER  at least one letter played (0x425807) in the original;
    //          the port waits for ENTER_MIN_LETTERS (an enhancement).
    void refresh_buttons() {
        const bool playing = phase_ == GamePhase::Playing;
        const bool q = round_ && round_->qualified();
        std::size_t played = 0;
        for (const auto& b : balls_) if (b.played) ++played;
        const std::size_t available = balls_.size() - played;
        for (auto& b : buttons_) {
            const std::string& n = b.sprite.name;
            // 0x425710: NEXT ROUND only for a qualified round, so not behind
            // DIALOG_PASSROUND (AwaitNext after a time-up miss).
            const bool next_shown = (playing || phase_ == GamePhase::AwaitNext) && q;
            if (n == "BUTTON_NextRound") { b.sprite.visible = !wotd() && next_shown; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_WOTD_DONE") { b.sprite.visible = wotd() && next_shown; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_Pass_2") { b.sprite.visible = !wotd() && playing && !q && misses_ == 0; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_Pass") { b.sprite.visible = !wotd() && playing && !q && misses_ == 1; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_GiveUp") { b.sprite.visible = playing && !q && (wotd() || misses_ >= 2); b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_Menu") { b.enabled = true; }
            else if (n == "BUTTON_Twist") { b.enabled = playing && available > 1; }
            else if (n == "BUTTON_Clear") { b.enabled = playing && played > 0; }
            else if (n == "BUTTON_LastWord") { b.enabled = playing && played == 0 && !last_word_.empty(); }
            else if (n == "BUTTON_Enter") { b.enabled = playing && played >= ENTER_MIN_LETTERS; }
            else { b.enabled = playing; }
        }
    }

    const tt2::WordTree& common_;
    const tt2::WordTree& full_;
    tt2::PlayerProfile& profile_;
    tt2::HighScoreManager* highscores_;
    tt2::GameMode mode_;
    Services services_;
    tt2::Random rng_;
    tt2::Bag bag_;
    std::unique_ptr<tt2::ClassicRound> round_;
    int round_number_ = 0;
    std::int32_t score_ = 0;
    int time_left_ms_ = ROUND_MS;
    int misses_ = 0;
    bool finished_ = false;
    GamePhase phase_ = GamePhase::Playing;
    RoundEnd round_end_ = RoundEnd::None;
    std::string rack_, last_word_;
    TableLayout table_;
    std::vector<SlotView> slots_;
    std::vector<std::string> board_words_;   // the flat slot answers (0x433320's list)
    std::vector<LetterBall> balls_;
    HelpPager help_;                         // DIALOG_HELP page state
    std::vector<std::pair<GameDialog, Dialog>> under_;   // open dialogs under the top one, bottom first
    std::optional<SettingsDialog> settings_; // DIALOG_SETTINGS while open
    std::optional<HighScoreDialog> highscore_;   // DIALOG_HIGHSCORES while open
    std::vector<LetterBall> dialog_balls_;   // DIALOG_NEXTROUND letterballs
    std::vector<Particle> dialog_particles_;
    // The original draws the burst from its global LCG (0x5fde90); a
    // separate stream keeps the cosmetic burst from shifting the deal and
    // twist randomness.
    tt2::Random particle_rng_;
    std::vector<MenuButton> buttons_;
    bool ball_hot_ = false;   // pointer over a letter ball (hand cursor)
    bool word_hot_ = false;   // pointer over a shown word (hand cursor)
    MessagePopups popups_;                   // +0x334 queue, +0x348 the one up
    bool definitions_hint_shown_ = false;    // +0x1f0: popup 10 already queued
    ModifierQueue queue_;
    // Trophies (docs/reverse-engineering/trophies.md).
    TrophyPopups trophies_;
    std::int32_t no_twist_streak_ = 0;       // +0x36c
    std::int32_t bingo_streak_ = 0;          // +0x374
    bool twisted_this_round_ = false;        // +0x370
    int round_elapsed_ms_ = 0;               // 120000 - (+0x2fc), counted in every mode
    // Word of the Day, both taken at round start (game-scene.md §8.1).
    std::int64_t word_day_ = 0;              // picks the rack (0x43d690)
    std::int32_t scene_day_ = -1;            // +0x1ec: save DAY and player.WOD
    GameDialog dialog_kind_ = GameDialog::None;
    Dialog dialog_;
};

}  // namespace tt2ui
