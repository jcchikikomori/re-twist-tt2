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
inline constexpr std::int64_t WOTD_SEED = 1337;

// Day 0 of Word of the Day: 2008-09-03 (unix day 14125, CONFIRMED
// at 0x43d71e: `sub edx, 0x372d` = -14125).
inline constexpr std::int64_t WOTD_EPOCH_DAY = 14125;

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
        for (std::size_t tries = 0; tries < pool_.size(); ++tries) {
            const std::string& word = pool_[state_.index % pool_.size()];
            state_.index += 1;
            if (is_playable(word)) return word;
        }
        throw NoRackError();
    }

    // Classic target-length loop (CONFIRMED at 0x426008..0x426104):
    // skip longer-than-target words for free, accept an exact match,
    // otherwise keep the closest shorter one over at most
    // CLASSIC_ATTEMPTS tries.
    std::string pick_classic(std::size_t target_length, Random& /*rng*/) {
        std::string best;
        bool have_best = false;
        for (int i = 0; i < CLASSIC_ATTEMPTS; ++i) {
            const std::string& word = next_word();
            if (word.size() > target_length) continue;        // free skip
            if (word.size() == target_length) return word;    // exact hit
            if (!have_best || word.size() > best.size()) {
                best = word;
                have_best = true;
            }
        }
        if (have_best) return best;
        return next_word();
    }

    // Word of the Day: bag shuffled with fixed seed 1337, indexed by
    // the day count. CONFIRMED at 0x43d690..0x43d71e.
    static std::string word_of_the_day(const WordTree& common,
                                       std::int64_t time_ms) {
        auto pool = make_pool(common);
        if (pool.empty()) throw NoRackError();
        const std::int64_t day_idx = (time_ms / 86'400'000LL) - WOTD_EPOCH_DAY;
        const std::size_t start = static_cast<std::size_t>(
            ((day_idx % static_cast<std::int64_t>(pool.size()))
              + static_cast<std::int64_t>(pool.size()))
            % static_cast<std::int64_t>(pool.size()));
        Random rng(WOTD_SEED);
        rng.shuffle(pool);
        Bag tmp(common, BagState{static_cast<std::uint32_t>(WOTD_SEED), 0});
        for (std::size_t i = 0; i < pool.size(); ++i) {
            const std::string& w = pool[(start + i) % pool.size()];
            if (tmp.is_playable(w)) return w;
        }
        throw NoRackError();
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
