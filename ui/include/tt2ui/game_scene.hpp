// The Classic game scene (Untimed / Timed): `TextTwist2Scene` in the
// original. Layout and flow come from the decompilation listed in
// docs/reverse-engineering/game-scene.md; the addresses are repeated
// next to the numbers they justify. Everything here is SDL-free so the
// rules can be unit tested headless; `ui/src/main.cpp` only draws the
// state this class exposes.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/profile.hpp"
#include "tt2/random.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/word_tree.hpp"
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
constexpr int BALL_IDLE_FRAME = 16;          // last frame of the row: the square "chicklet" (INFERRED)

// x of the left edge of a row holding `n` letters (0x4396e0: 400 - n*69/2).
inline float row_left(int n) { return 400.0f - static_cast<float>(n) * BALL_PITCH * 0.5f; }
inline float ball_center_x(int n, int slot) { return row_left(n) + static_cast<float>(slot) * BALL_PITCH + BALL_HALF; }

struct LetterBall {
    char letter = 0;
    bool played = false;    // which row it sits in
    int slot = 0;           // cell index inside that row
    float x = 0, y = 0;     // current centre on screen
    float target_x = 0, target_y = 0;
};

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

constexpr int ROUND_MS = 120000;          // 0x425c50: +0x2fc
constexpr int WARNING_MS = 10000;         // 0x425c50: +0x304 (countdown starts at 10 s)
constexpr int LIVES = 3;                  // 0x425420: game over when the miss counter reaches 3
constexpr int REVEAL_MS = 3000;           // 0x425420: ModifierDelay(3000) after the reveal
constexpr int COMMENT_SHOW_MS = 2000;     // INFERRED: commentary fade timing
constexpr int COMMENT_FADE_MS = 250;
constexpr int POPUP_FRAME_MS = 60;        // INFERRED: bingo / all-words popup frame time
constexpr float BALL_MOVE_SPEED = 900.0f; // INFERRED: px/s for the ball slide

// Frames of images/gamescene/tt_commentary.png (189x648, 9 rows).
enum class Comment {
    NotListed = 0,       // "SORRY, THAT'S NOT ONE OF THE LISTED WORDS"
    AllWords = 1,        // "CONGRATULATIONS! YOU GOT ALL THE WORDS!"
    Bingo = 2,           // "YOU GOT THE LONGEST WORD! YOU QUALIFY FOR THE NEXT ROUND!"
    AlreadyGuessed = 3,  // "YOU'VE ALREADY GUESSED THAT WORD!"
    EnoughOfLength = 5,  // "YOU ALREADY HAVE ENOUGH WORDS OF THAT LENGTH!"
    NoneOfLength = 8,    // "YOU DONT NEED ANY WORDS OF THAT LENGTH"
};

enum class GamePhase {
    Playing,
    Reveal,      // missing words shown, waiting REVEAL_MS (time up / give up / all words)
    AwaitNext,   // round over, NEXT ROUND button (or any click) starts the next one
    GameOver,    // GAME_OVER sprite shown, click returns to the menu
};

enum class RoundEnd { None, AllWords, TimeUpQualified, TimeUpMissed, NextRound, GaveUp };

struct SlotView {
    std::string word;
    bool found = false;
    bool revealed = false;   // shown at the end of a lost round
    int x = 0, y = 0;        // screen position of the first tile
};

class GameScene {
public:
    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void()> on_profile_change;   // bag / counters changed: save
        std::function<void()> on_exit;             // back to the main menu
        std::function<std::uint32_t()> seed_millis = [] { return 0u; };   // timeGetTime() for the bag
    };

    GameScene(const tt2::WordTree& common, const tt2::WordTree& full, tt2::PlayerProfile& profile,
              tt2::HighScoreManager* highscores, bool timed, Services services, std::int64_t rng_seed)
        : common_(common), full_(full), profile_(profile), highscores_(highscores), timed_(timed),
          services_(std::move(services)), rng_(rng_seed), bag_(common, profile.bag) {
        build_buttons();
        commentary_ = sprite("COMMENTARY", sheet("COMMENTS", 189, 648, 1, 9, 9), 316, 106);
        commentary_.alpha = 0.0f;
        game_over_ = sprite("GAME_OVER", sheet("GAME_OVER", 234, 116, 1, 2, 2), 291, 264);
        game_over_.alpha = 0.0f;
        popup_bingo_ = sprite("POPUP_BINGO", sheet("IMAGE_POPUP_BINGO", 1900, 420, 5, 3, 15), 400, 150);
        popup_bingo_.anchor = Anchor::Center;
        popup_bingo_.visible = false;
        popup_all_ = sprite("POPUP_ALLWORDS", sheet("IMAGE_POPUP_ALLWORDS", 2280, 420, 6, 3, 18), 400, 150);
        popup_all_.anchor = Anchor::Center;
        popup_all_.visible = false;
        start_round(1);
    }

    // ---- state for the renderer -------------------------------------
    bool timed() const { return timed_; }
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
    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    MenuButton* button(const std::string& name) {
        for (auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    const SpriteState& commentary() const { return commentary_; }
    const SpriteState& game_over_sprite() const { return game_over_; }
    const SpriteState& popup_bingo() const { return popup_bingo_; }
    const SpriteState& popup_all_words() const { return popup_all_; }
    const char* background_id() const { return timed_ ? "gamescene/BACKGROUND_TIMED" : "gamescene/BACKGROUND"; }

    // UI bar texts (0x426360: minutes label "<m>:" and seconds "%02d").
    std::string score_text() const { return std::to_string(score_); }
    std::string round_text() const { return std::to_string(round_number_); }
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
    void type_letter(char c) {
        if (phase_ != GamePhase::Playing) return;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return;
        // First available ball with that letter, left to right (0x427680 scans the row in order).
        int best = -1;
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            if (balls_[i].played || balls_[i].letter != c) continue;
            if (best < 0 || balls_[i].slot < balls_[static_cast<std::size_t>(best)].slot) best = static_cast<int>(i);
        }
        if (best >= 0) place(static_cast<std::size_t>(best));
    }
    void backspace() {
        if (phase_ != GamePhase::Playing) return;
        int last = -1;
        for (std::size_t i = 0; i < balls_.size(); ++i) {
            if (!balls_[i].played) continue;
            if (last < 0 || balls_[i].slot > balls_[static_cast<std::size_t>(last)].slot) last = static_cast<int>(i);
        }
        if (last >= 0) unplace(static_cast<std::size_t>(last));
    }
    void key_return() {
        switch (phase_) {
            case GamePhase::Playing: submit(); break;
            case GamePhase::AwaitNext: next_round(); break;
            case GamePhase::GameOver: exit_to_menu(); break;
            default: break;
        }
    }
    void key_space() { if (phase_ == GamePhase::Playing) twist(); }
    void key_escape() {
        // INFERRED: Esc behaves like the in-game Menu button (leave the game).
        if (phase_ == GamePhase::GameOver || phase_ == GamePhase::AwaitNext) exit_to_menu();
        else give_up();
    }

    void mouse_move(float x, float y) {
        for (auto& b : buttons_) {
            const bool over = b.sprite.visible && b.enabled && b.contains(x, y);
            if (over && !b.hover) play(SoundId::ButtonRollover);
            b.hover = over;
            if (!over) b.pressed = false;
        }
    }
    void mouse_down(float x, float y) {
        for (auto& b : buttons_) b.pressed = b.sprite.visible && b.enabled && b.contains(x, y);
    }
    void mouse_up(float x, float y) {
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
        // 0x427680: a click on an available ball plays it; a click on the
        // last played ball takes it back.
        if (phase_ == GamePhase::Playing) {
            for (std::size_t i = 0; i < balls_.size(); ++i) {
                const auto& b = balls_[i];
                if (std::fabs(x - b.target_x) <= BALL_HALF && std::fabs(y - b.target_y) <= BALL_HALF) {
                    if (!b.played) { place(i); return; }
                    if (is_last_played(i)) { unplace(i); return; }
                }
            }
            return;
        }
        if (phase_ == GamePhase::AwaitNext) next_round();
        else if (phase_ == GamePhase::GameOver) exit_to_menu();
    }

    void press(const std::string& name) {
        if (name == "BUTTON_Twist") twist();
        else if (name == "BUTTON_Clear") clear();
        else if (name == "BUTTON_LastWord") last_word_again();
        else if (name == "BUTTON_Enter") submit();
        else if (name == "BUTTON_NextRound") { if (phase_ == GamePhase::AwaitNext || qualified()) next_round(); }
        else if (name == "BUTTON_GiveUp") give_up();
        else if (name == "BUTTON_Menu") exit_to_menu();
    }

    // ---- simulation -------------------------------------------------
    void update(int dt_ms) {
        if (finished_) return;
        queue_.update(dt_ms);
        comment_queue_.update(dt_ms);
        popup_queue_.update(dt_ms);
        // Ball slide (INFERRED easing: constant speed).
        const float step = BALL_MOVE_SPEED * static_cast<float>(dt_ms) / 1000.0f;
        for (auto& b : balls_) {
            const float dx = b.target_x - b.x, dy = b.target_y - b.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d <= step) { b.x = b.target_x; b.y = b.target_y; }
            else { b.x += dx / d * step; b.y += dy / d * step; }
        }
        // Timer (0x426d59: only CLASSIC_TIMED / WOTD tick).
        if (phase_ == GamePhase::Playing && timed_) {
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
    void start_round(int number) {
        round_number_ = number;
        round_end_ = RoundEnd::None;
        phase_ = GamePhase::Playing;
        time_left_ms_ = ROUND_MS;
        tt2::ClassicRound::Config cfg;
        cfg.timed = timed_;
        cfg.max_word_length = static_cast<std::size_t>(profile_.max_word_length);
        cfg.round_ms = timed_ ? ROUND_MS : 0;
        round_ = std::make_unique<tt2::ClassicRound>(common_, full_, cfg);
        const auto res = round_->start(rng_, bag_, 0);
        rack_ = res.rack;
        profile_.bag = bag_.state();
        if (services_.on_profile_change) services_.on_profile_change();
        // Board.
        slots_.clear();
        table_ = layout_anagram_table(res.board_words);
        for (std::size_t i = 0; i < res.board_words.size(); ++i) {
            SlotView v;
            v.word = res.board_words[i];
            v.x = static_cast<int>(table_x()) + table_.slots[i].x;
            v.y = static_cast<int>(table_y()) + table_.slots[i].y;
            slots_.push_back(v);
        }
        // Rack: every ball starts in the available row (0x4396e0 loop).
        balls_.clear();
        const int n = static_cast<int>(rack_.size());
        for (int i = 0; i < n; ++i) {
            LetterBall b;
            b.letter = rack_[static_cast<std::size_t>(i)];
            b.played = false;
            b.slot = i;
            b.target_x = b.x = ball_center_x(n, i);
            b.target_y = b.y = AVAILABLE_ROW_Y + BALL_HALF;
            balls_.push_back(b);
        }
        play(SoundId::LetterLoad);
        game_over_.alpha = 0.0f;
        refresh_buttons();
    }

    void submit() {
        if (phase_ != GamePhase::Playing) return;
        const std::string word = typed();
        if (word.empty()) return;
        last_word_ = word;
        return_all_letters();
        const auto rep = round_->guess(word);
        using tt2::GuessStatus;
        switch (rep.status) {
            case GuessStatus::NotAWord:
                play(SoundId::WordReject);
                show_comment(Comment::NotListed);
                return;
            case GuessStatus::Duplicate:
                play(SoundId::WordReject);
                show_comment(Comment::AlreadyGuessed);
                return;
            case GuessStatus::NoRoom: {
                play(SoundId::WordReject);
                bool any = false;
                for (const auto& s : slots_) if (s.word.size() == word.size()) any = true;
                show_comment(any ? Comment::EnoughOfLength : Comment::NoneOfLength);
                return;
            }
            case GuessStatus::Accepted:
            case GuessStatus::Uncommon:
                break;
        }
        score_ += rep.points_awarded;
        profile_.solved += 1;
        if (rep.status == GuessStatus::Uncommon) profile_.hard_solved += 1;
        // The slot that received the guess shows the guessed word —
        // it may not be the board answer (0x432b20 / 0x431880).
        if (rep.slot_index < slots_.size()) {
            slots_[rep.slot_index].found = true;
            slots_[rep.slot_index].word  = rep.placed_word;
        }
        if (rep.filled_last) {
            play(SoundId::AllWords);
            show_popup(popup_all_);
            show_comment(Comment::AllWords);
            end_round(RoundEnd::AllWords);
        } else if (rep.bingo) {
            play(SoundId::WordBingo);
            show_popup(popup_bingo_);
            show_comment(Comment::Bingo);
        } else {
            play(SoundId::WordCorrect);
        }
        if (services_.on_profile_change) services_.on_profile_change();
        refresh_buttons();
    }

    void time_up() {
        play(SoundId::ClockTimeUp);
        if (round_->qualified()) end_round(RoundEnd::TimeUpQualified);
        else end_round(RoundEnd::TimeUpMissed);
    }

    void give_up() {
        if (phase_ != GamePhase::Playing) return;
        end_round(RoundEnd::GaveUp);
    }

    // 0x428840 (all words) / 0x425420 (missed): reveal the board, wait,
    // then either carry on or finish the game.
    void end_round(RoundEnd why) {
        round_end_ = why;
        return_all_letters();
        bool game_over = false;
        switch (why) {
            case RoundEnd::AllWords:
            case RoundEnd::TimeUpQualified:
            case RoundEnd::NextRound:
                profile_.rounds_completed[static_cast<std::size_t>(mode())] += 1;
                play(SoundId::NextRoundAchieved);
                break;
            case RoundEnd::TimeUpMissed:
                misses_ += 1;
                play(SoundId::LoseLife);
                game_over = misses_ >= LIVES;
                break;
            case RoundEnd::GaveUp:
                game_over = true;
                break;
            case RoundEnd::None: break;
        }
        if (services_.on_profile_change) services_.on_profile_change();
        const bool need_reveal = !round_->complete();
        if (need_reveal) {
            for (auto& s : slots_) if (!s.found) s.revealed = true;
            play(SoundId::WordPassReveal);
        }
        phase_ = GamePhase::Reveal;
        refresh_buttons();
        const GamePhase after = game_over ? GamePhase::GameOver : GamePhase::AwaitNext;
        queue_.push_delay(need_reveal ? REVEAL_MS : 0);
        queue_.push_method([this, after] {
            phase_ = after;
            if (after == GamePhase::GameOver) finish_game();
            refresh_buttons();
        });
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

    void finish_game() {
        game_over_.frame = 0;
        game_over_.alpha = 0.0f;
        queue_.push_alpha(&game_over_, 0.0f, 1.0f, 500);
        if (highscores_ && score_ > 0) {
            const int rank = highscores_->submit(mode(), profile_.name, score_, std::to_string(round_number_));
            if (rank >= 0) play(SoundId::HighScoreAchieved);
            if (services_.on_profile_change) services_.on_profile_change();
        }
    }

    void exit_to_menu() {
        if (finished_) return;
        finished_ = true;
        if (services_.on_exit) services_.on_exit();
    }

    tt2::GameMode mode() const { return timed_ ? tt2::GameMode::Timed : tt2::GameMode::Untimed; }

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
    void move_to(LetterBall& b, bool played_row, int slot) {
        b.played = played_row;
        b.slot = slot;
        b.target_x = ball_center_x(static_cast<int>(balls_.size()), slot);
        b.target_y = (played_row ? PLAYED_ROW_Y : AVAILABLE_ROW_Y) + BALL_HALF;
    }
    // 0x43bad0(index = -1): the ball goes to the first empty cell of the row.
    void place(std::size_t i) {
        const int slot = first_free_slot(true);
        if (slot < 0) return;
        move_to(balls_[i], true, slot);
        play(SoundId::LetterPlace);
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
    void last_word_again() {
        if (phase_ != GamePhase::Playing || last_word_.empty()) return;
        return_all_letters();
        for (char c : last_word_) type_letter(c);
    }
    // INFERRED: the shipped twist shuffles the available balls with the
    // game LCG (std::random_shuffle semantics via 0x43d950).
    void twist() {
        if (phase_ != GamePhase::Playing) return;
        std::vector<std::size_t> avail;
        for (std::size_t i = 0; i < balls_.size(); ++i) if (!balls_[i].played) avail.push_back(i);
        if (avail.size() < 2) return;
        std::vector<int> slots;
        for (std::size_t i : avail) slots.push_back(balls_[i].slot);
        rng_.shuffle(slots);
        for (std::size_t k = 0; k < avail.size(); ++k) move_to(balls_[avail[k]], false, slots[k]);
        profile_.twists += 1;
        play(SoundId::WordTwist);
    }

    // ---- presentation helpers --------------------------------------
    void show_comment(Comment c) {
        commentary_.frame = static_cast<int>(c);
        comment_queue_.clear();
        commentary_.alpha = 0.0f;
        comment_queue_.push_alpha(&commentary_, 0.0f, 1.0f, COMMENT_FADE_MS);
        comment_queue_.push_delay(COMMENT_SHOW_MS);
        comment_queue_.push_alpha(&commentary_, 1.0f, 0.0f, COMMENT_FADE_MS);
    }
    // One pass through the sheet, then hidden again.
    void show_popup(SpriteState& p) {
        popup_queue_.clear();
        popup_bingo_.visible = popup_all_.visible = false;
        p.visible = true;
        p.frame = 0;
        popup_queue_.push_frames(&p, 0, -1, POPUP_FRAME_MS);
        popup_queue_.push_method([&p] { p.visible = false; });
    }
    void play(SoundId id) { if (services_.play_sound) services_.play_sound(id); }

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
        add("BUTTON_GiveUp", "BUTTON_GiveUp", 157, 208, 317, 473);
    }
    // NEXT ROUND and GIVE UP share one spot; which one shows depends on the
    // qualified flag (INFERRED wiring, see gameplay-notes §4).
    void refresh_buttons() {
        const bool playing = phase_ == GamePhase::Playing;
        const bool q = round_ && round_->qualified();
        for (auto& b : buttons_) {
            const std::string& n = b.sprite.name;
            if (n == "BUTTON_NextRound") { b.sprite.visible = (playing && q) || phase_ == GamePhase::AwaitNext; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_GiveUp") { b.sprite.visible = playing && !q; b.enabled = b.sprite.visible; }
            else if (n == "BUTTON_Menu") { b.enabled = true; }
            else { b.enabled = playing; }
        }
    }

    const tt2::WordTree& common_;
    const tt2::WordTree& full_;
    tt2::PlayerProfile& profile_;
    tt2::HighScoreManager* highscores_;
    bool timed_;
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
    std::vector<LetterBall> balls_;
    std::vector<MenuButton> buttons_;
    SpriteState commentary_, game_over_, popup_bingo_, popup_all_;
    ModifierQueue queue_, comment_queue_, popup_queue_;
};

}  // namespace tt2ui
