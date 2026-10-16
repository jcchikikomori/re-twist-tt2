#pragma once

// Lightning (Blitz) rules: one round of n rack words against an 80.3 s
// clock. Spec: docs/reverse-engineering/lightning.md (REA, CONFIRMED
// unless tagged).
//
//   - words per round (0x4138e0):  n = min(10, floor(5 + (round - 1) * 0.2))
//   - word length (0x413dd3):      v = 7 + (round - 1) * 0.2 - U[0, 2),
//                                  L = min(maxLen, trunc(max(5, v)))
//   - only a full-length word counts; a shorter one is ignored (0x4116e0)
//   - solve: 10 * L * (L - min(L, hints)), +8000 ms capped at 80300 ms
//     (not on the last word); the last word adds 1000 and wins the round
//   - hints come by themselves (0x415300): 9000 ms on the current word,
//     then every 4000 ms, held back while the player has typed letters
//   - warning beeps at 10, 9, ... 1 s; the mark re-arms after +8 s
//   - the clock reaching 0 ends the game (0x411e10); there are no lives
//
// The trees are non-owning pointers: keep them alive as long as the round.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/scoring.hpp"
#include "tt2/word_tree.hpp"

namespace tt2 {

inline constexpr int LIGHTNING_START_MS = 80'300;    // 0x139ac
inline constexpr int LIGHTNING_SOLVE_MS = 8'000;
inline constexpr int LIGHTNING_WARNING_MS = 10'000;  // +0x304 at round start
inline constexpr int LIGHTNING_HINT_FIRST_MS = 9'000;   // word+0xbc (0x414340)
inline constexpr int LIGHTNING_HINT_NEXT_MS = 4'000;    // 0x4131f0
inline constexpr std::size_t LIGHTNING_MAX_WORDS = 10;
inline constexpr std::size_t LIGHTNING_MIN_LENGTH = 5;
inline constexpr double LIGHTNING_WORD_NUMBER_INCREMENT = 0.2;   // default.xml BLITZ_WORD_NUMBER_INCREMENT
inline constexpr double LIGHTNING_WORD_SIZE_INCREMENT = 0.2;     // default.xml BLITZ_WORD_SIZE_INCREMENT

// 0x4138e0: words in round `round` (1-based).
inline std::size_t lightning_word_count(int round) {
    const double n = 5.0 + static_cast<double>(round - 1) * LIGHTNING_WORD_NUMBER_INCREMENT;
    return std::min(LIGHTNING_MAX_WORDS, static_cast<std::size_t>(std::floor(n)));
}

// 0x413dd3..0x413ef6: one word's length. U[0, 2) is the LCG's next 24
// bits scaled to [0, 1), times 2.
inline std::size_t lightning_word_length(int round, Random& rng, std::size_t max_word_length) {
    rng.next_int();
    const double u = static_cast<double>(rng.state() >> 24) / 16777216.0 * 2.0;
    const double v = 7.0 + static_cast<double>(round - 1) * LIGHTNING_WORD_SIZE_INCREMENT - u;
    const auto len = static_cast<std::size_t>(v <= 5.0 ? 5.0 : v);
    return std::min(len, max_word_length);
}

struct LightningWord {
    std::string word;           // the bag word, also the hint answer (word+0x1c)
    std::size_t length = 0;
    bool solved = false;        // word+0x88
    int hints = 0;              // word+0xc0
    int hint_ms = LIGHTNING_HINT_FIRST_MS;   // word+0xbc
    std::string last_word;      // word+0x64: the last ENTER on this word
    std::string solved_as;      // the word that solved it (any full-length anagram)
};

class LightningRound {
public:
    enum class Submit { Ignored, Rejected, Solved, RoundWon };

    struct SubmitReport {
        Submit status = Submit::Ignored;
        int points = 0;              // word points (+1000 included on RoundWon)
        bool uncommon = false;       // not in the common list (Human Dictionary)
        int time_added_ms = 0;
    };

    struct TickReport {
        int warnings = 0;            // SOUND_lightning_time_low beeps this tick
        bool hint = false;           // a hint fired on the current word
        bool time_out = false;       // the clock reached 0 this tick
    };

    LightningRound(const WordTree& common, const WordTree& full) : common_(&common), full_(&full) {}

    // 0x4138e0: deal round `round`. Round 1 starts a new game (score 0).
    void start(int round, Random& rng, Bag& bag, std::size_t max_word_length) {
        std::vector<std::string> words;
        const std::size_t n = lightning_word_count(round);
        for (std::size_t i = 0; i < n; ++i)
            words.push_back(bag.pick_classic(lightning_word_length(round, rng, max_word_length), rng));
        begin(round, words);
        if (round == 1) score_ = 0;
    }

    // 0x4174c0: a saved game. Hints and the trophy flags start fresh.
    struct SavedWord {
        std::string word;
        bool completed = false;
    };
    void restore(int round, int time_ms, std::int32_t score, const std::vector<SavedWord>& saved) {
        std::vector<std::string> words;
        for (const auto& w : saved) words.push_back(w.word);
        begin(round, words);
        for (std::size_t i = 0; i < saved.size(); ++i) words_[i].solved = saved[i].completed;
        time_ms_ = std::clamp(time_ms, 0, LIGHTNING_START_MS);
        warning_mark_ = rearmed_mark(time_ms_);
        score_ = score;
        current_ = first_unsolved_from(0, true);
    }

    // ---- state -------------------------------------------------------
    int round() const noexcept { return round_; }
    std::int32_t score() const noexcept { return score_; }
    int time_ms() const noexcept { return time_ms_; }
    const std::vector<LightningWord>& words() const noexcept { return words_; }
    std::size_t current() const noexcept { return current_; }
    const LightningWord& current_word() const { return words_[current_]; }
    bool won() const noexcept { return won_; }
    bool lost() const noexcept { return lost_; }
    bool over() const noexcept { return won_ || lost_; }
    bool twisted() const noexcept { return twisted_; }   // +0x3bc
    bool passed() const noexcept { return passed_; }     // +0x3bd
    bool hinted() const noexcept { return hinted_; }     // +0x3be
    std::size_t solved_count() const {
        std::size_t n = 0;
        for (const auto& w : words_) n += w.solved ? 1 : 0;
        return n;
    }

    // ---- actions -----------------------------------------------------
    // 0x4116e0: ENTER on the current word.
    SubmitReport submit(const std::string& typed) {
        SubmitReport r;
        if (over() || words_.empty()) return r;
        LightningWord& w = words_[current_];
        if (w.solved) return r;
        const std::string guess = lower(typed);
        w.last_word = guess;
        if (guess.size() != w.length) return r;   // only full-length words count
        if (!full_->contains(guess) || !is_anagram(guess, w.word)) {
            r.status = Submit::Rejected;
            return r;
        }
        w.solved = true;
        w.solved_as = guess;
        r.uncommon = !common_->contains(guess);
        r.points = scoring::lightning_per_word(w.length, static_cast<std::size_t>(w.hints));
        if (solved_count() == words_.size()) {
            r.status = Submit::RoundWon;
            r.points += scoring::LIGHTNING_ROUND_BONUS;
            won_ = true;
        } else {
            r.status = Submit::Solved;
            const int before = time_ms_;
            time_ms_ = std::min(LIGHTNING_START_MS, time_ms_ + LIGHTNING_SOLVE_MS);
            r.time_added_ms = time_ms_ - before;
            warning_mark_ = rearmed_mark(time_ms_);
        }
        score_ += r.points;
        return r;
    }

    // 0x411520 after a solve: the next unsolved word, not a pass.
    void advance() { if (!over()) current_ = first_unsolved_from(current_ + 1, true); }

    // NEXT / PREV / Tab / arrows (0x4135e0, 0x415c50): always a pass,
    // even when there is no other unsolved word to go to.
    void pass(bool forward) {
        if (over()) return;
        passed_ = true;
        current_ = forward ? first_unsolved_from(current_ + 1, true)
                           : first_unsolved_from(current_ + words_.size() - 1, false);
    }
    // 0x417f50: clicking another unsolved word's rack is a pass too.
    bool select(std::size_t i) {
        if (over() || i >= words_.size() || words_[i].solved || i == current_) return false;
        passed_ = true;
        current_ = i;
        return true;
    }
    // 0x413090: a twist marks the round (Brainstorm).
    void twist() { if (!over()) twisted_ = true; }

    // 0x415300: the clock, the warning beeps and the current word's hint
    // timer. `typing`: the entry row holds letters the player typed.
    TickReport tick(int dt_ms, bool typing) {
        TickReport r;
        if (over()) return r;
        time_ms_ -= dt_ms;
        while (time_ms_ < warning_mark_ && warning_mark_ > 0) {
            warning_mark_ -= 1000;
            r.warnings += 1;
        }
        if (time_ms_ <= 0) {
            time_ms_ = 0;
            lost_ = true;
            r.time_out = true;
            return r;
        }
        LightningWord& w = words_[current_];
        if (!w.solved) {
            w.hint_ms -= dt_ms;
            if (w.hint_ms <= 0 && !typing) {
                w.hint_ms = LIGHTNING_HINT_NEXT_MS;
                w.hints += 1;
                hinted_ = true;
                r.hint = true;
            }
        }
        return r;
    }

    // Give up (0x4167a0 -> 0x411e10): the same as running out of time.
    void give_up() { if (!over()) lost_ = true; }

private:
    void begin(int round, const std::vector<std::string>& words) {
        round_ = round;
        words_.clear();
        for (const auto& s : words) {
            LightningWord w;
            w.word = s;
            w.length = s.size();
            words_.push_back(w);
        }
        current_ = 0;
        time_ms_ = LIGHTNING_START_MS;
        warning_mark_ = LIGHTNING_WARNING_MS;
        twisted_ = passed_ = hinted_ = false;
        won_ = lost_ = false;
    }

    static int rearmed_mark(int t) {
        const int m = std::min(t, LIGHTNING_WARNING_MS);
        return m - m % 1000;
    }

    // The first unsolved word from `from` (mod n), walking forward or back;
    // the current word when every other one is solved.
    std::size_t first_unsolved_from(std::size_t from, bool forward) const {
        const std::size_t n = words_.size();
        if (n == 0) return 0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = forward ? (from + k) % n : (from + n - k % n) % n;
            if (!words_[i].solved) return i;
        }
        return current_;
    }

    static std::string lower(std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }
    static bool is_anagram(std::string a, std::string b) {
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    }

    const WordTree* common_;
    const WordTree* full_;
    std::vector<LightningWord> words_;
    std::size_t current_ = 0;
    int round_ = 0;
    int time_ms_ = LIGHTNING_START_MS;
    int warning_mark_ = LIGHTNING_WARNING_MS;
    std::int32_t score_ = 0;
    bool twisted_ = false, passed_ = false, hinted_ = false;
    bool won_ = false, lost_ = false;
};

}  // namespace tt2
