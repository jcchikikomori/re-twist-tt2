#pragma once

// "Bag" of rack words — the persistent shuffled deck that the original
// stores in the player's profile (HKCU\...\seed and ...\index). Two
// players using the same dictionary and the same bag state always see
// the same racks in the same order. That is the part of the game that
// is "save-game".
//
// The bag is rebuilt from `seed` every time the index walks past the
// pool. Reseeding pulls `seed = timeGetTime()` (WinMM) in the original;
// we use `tt2::millis()` from `random.hpp`, which is the equivalent.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tt2/random.hpp"
#include "tt2/word_tree.hpp"
#include "tt2/wotd_day.hpp"

namespace tt2 {

// Minimum sub-anagram count a rack word must have to be "playable".
// 15 is the value hard-coded in the original (CONFIRMED at 0x43d650).
inline constexpr std::size_t MIN_SUB_ANAGRAMS = 15;

// Pool length bounds. 5..8 mirrors the original (CONFIRMED at 0x43d58e).
inline constexpr std::size_t POOL_MIN_LENGTH = 5;
inline constexpr std::size_t POOL_MAX_LENGTH = 8;

// Maximum number of shorter-than-target candidates the classic target
// loop is willing to try before settling (CONFIRMED at 0x426008).
inline constexpr int CLASSIC_ATTEMPTS = 5;

// Word of the Day bag seed (CONFIRMED at 0x43d6c8, `xor ecx, 0x539`).
// The day-0 epoch (WOTD_EPOCH_DAY) lives in wotd_day.hpp.
inline constexpr std::int64_t WOTD_SEED = 1337;

class NoRackError : public std::runtime_error {
public:
    NoRackError() : std::runtime_error("no word in the pool qualifies as a rack") {}
};

// Persistent bag state. `seed` is what `timeGetTime()` returned when
// the bag was last reseeded; `index` is how many entries have been
// consumed. The original persists these two ints per player.
struct BagState {
    std::uint32_t seed  = 0;
    std::uint32_t index = 0;
};

// Rack (root-word) picker. Mirrors the bag + sub-anagram filter loop
// at 0x43d550 in the original.
class Bag {
public:
    Bag(const WordTree& common, BagState state = BagState{0, 0})
        : common_(&common), pool_(make_pool(common)), state_(state) {
        if (pool_.empty()) {
            throw NoRackError();
        }
    }

    BagState state() const noexcept { return state_; }

    bool is_playable(const std::string& word, std::size_t min_count = MIN_SUB_ANAGRAMS) {
        auto it = memo_.find(word);
        if (it == memo_.end()) {
            it = memo_.emplace(word,
                common_->sub_anagrams(word).size() >= min_count).first;
        }
        return it->second;
    }

    // Next playable word from the bag, advancing the index. Throws
    // `NoRackError` when no pool entry qualifies.
    const std::string& next_word() {
        if (state_.index >= pool_.size()) {
            state_.seed  = millis();
            state_.index = 0;
            shuffled_.reset();
        }
        ensure_shuffled();
        // 0x43d607: word = shuffled[INDEX % size].
        const std::vector<std::string>& deck = shuffled_->second;
        for (std::size_t tries = 0; tries < deck.size(); ++tries) {
            const std::string& word = deck[state_.index % deck.size()];
            state_.index += 1;
            if (is_playable(word)) return word;
        }
        throw NoRackError();
    }

    // Classic target-length loop (CONFIRMED at 0x426008..0x426104):
    // skip longer-than-target words for free, accept an exact match,
    // otherwise keep the closest shorter one over at most
    // CLASSIC_ATTEMPTS tries.
    // A longer word does not use up an attempt (0x426008: "free skip").
    // Port guard: after a whole deck of longer words (a dictionary with
    // no word that short) the next word is taken as it is.
    std::string pick_classic(std::size_t target_length, Random& /*rng*/) {
        std::string best;
        bool have_best = false;
        int attempts = 0;
        for (std::size_t skips = 0; attempts < CLASSIC_ATTEMPTS && skips <= pool_.size();) {
            const std::string& word = next_word();
            if (word.size() > target_length) { ++skips; continue; }   // free skip
            if (word.size() == target_length) return word;           // exact hit
            ++attempts;
            if (!have_best || word.size() > best.size()) {
                best = word;
                have_best = true;
            }
        }
        if (have_best) return best;
        return next_word();
    }

    // Word of the Day for a word day (wotd_day.hpp): the 5..8 letter pool
    // shuffled with seed 1337, read at `day` (0x43d690, game-scene.md
    // §8.2). There is no playability check: the exe builds the
    // sub-anagram list but never compares its size, so a word with fewer
    // than MIN_SUB_ANAGRAMS sub-anagrams is handed out as it is.
    // Throws `NoRackError` when the pool is empty.
    static std::string word_of_the_day_for_day(const WordTree& common, std::int64_t day) {
        // DEVIATION (D1): the pool is whatever common list the game loaded.
        // With the shipped open-common.tree (19,181 words of 5-8 letters)
        // the word for a date differs from the 2008 game's; with the
        // player's own common.tree (22,048) it is the same word every day
        // (game-scene.md §8.2, W5 frame dump: 2026-10-22 -> "airport").
        auto pool = make_pool(common);
        if (pool.empty()) throw NoRackError();
        Random rng(WOTD_SEED);
        rng.shuffle(pool);
        // DEVIATION (D4): the exe has no modulo and no bounds check at
        // 0x43d690 (`base + day * 0x1c`), so it reads past the pool from
        // day n (2069-01-14 with the original list) and before day 0. The
        // port wraps into the pool instead of reading out of range.
        const auto n = static_cast<std::int64_t>(pool.size());
        return pool[static_cast<std::size_t>(((day % n) + n) % n)];
    }

    // Word of the Day at a UTC instant: the word day is
    // `floor(time_ms / 86400000) - WOTD_EPOCH_DAY` (the exe's `time()`
    // clock, game-scene.md §8.1).
    static std::string word_of_the_day(const WordTree& common, std::int64_t time_ms) {
        return word_of_the_day_for_day(common, wotd_days(WallClock{time_ms, 0, 0}).word_day);
    }

    // Classic target length L = min(rand(5, 9), max_word_length).
    static std::size_t classic_target_length(Random& rng,
                                             std::size_t max_word_length) {
        const std::size_t L = static_cast<std::size_t>(
            rng.rand(static_cast<std::int32_t>(POOL_MIN_LENGTH),
                     static_cast<std::int32_t>(POOL_MAX_LENGTH + 1)));
        return std::min(L, max_word_length);
    }

    static std::vector<std::string> make_pool(const WordTree& common) {
        return common.words_by_length(POOL_MIN_LENGTH, POOL_MAX_LENGTH);
    }

    const std::vector<std::string>& pool() const noexcept { return pool_; }

private:
    void ensure_shuffled() {
        if (!shuffled_ || shuffled_->first != state_.seed) {
            shuffled_.emplace(state_.seed, pool_);
            Random rng(static_cast<std::int64_t>(state_.seed));
            rng.shuffle(shuffled_->second);
        }
    }

    const WordTree* common_;
    std::vector<std::string> pool_;
    BagState state_;
    std::optional<std::pair<std::uint32_t, std::vector<std::string>>> shuffled_;
    std::unordered_map<std::string, bool> memo_;
};

}  // namespace tt2
