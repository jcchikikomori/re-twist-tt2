#pragma once

// Lightning / Blitz round. Same bag and dictionary as Classic; the
// rules differ (CONFIRMED at 0x4117dd..0x411bc0):
//
//   - words per round:  n = min(10, floor(5 + (round - 1) * 0.2))
//   - per-word length:  L = min(maxLen, max(5, floor(7 + (round-1) * 0.2 - U[0, 2])))
//   - score:            10 * L * (L - min(L, hints))
//   - timer:            80 300 ms base, +8 000 ms per solve, cap 80 300 ms
//   - only bingos count (guess length must equal rack length)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/random.hpp"
#include "tt2/scoring.hpp"
#include "tt2/word_tree.hpp"

namespace tt2 {

class LightningRound {
public:
    struct Config {
        std::size_t max_word_length;
        std::uint32_t base_time_ms;
        std::uint32_t bonus_time_ms;
        std::size_t max_words;

        static Config defaults() noexcept {
            return Config{8, 80'300, 8'000, 10};
        }
    };

    explicit LightningRound(const WordTree& common, const WordTree& full,
                            Config cfg = Config::defaults())
        : cfg_(cfg), common_(&common), full_(&full) {}

    struct WordSpec {
        std::string rack;
        std::size_t target_length;
    };

    WordSpec pick_spec(std::size_t round, Random& rng, Bag& bag) {
        const double inc   = 0.2;
        const double n_raw = 5.0 + (static_cast<double>(round) - 1.0) * inc;
        words_per_round_ = std::min<std::size_t>(
            cfg_.max_words, static_cast<std::size_t>(std::floor(n_raw)));
        (void)n_raw;

        const double len_raw = 7.0 + (static_cast<double>(round) - 1.0) * inc
                              - static_cast<double>(rng.rand(0, 3));
        const std::size_t len = std::min(
            cfg_.max_word_length,
            std::max<std::size_t>(5, static_cast<std::size_t>(std::floor(len_raw))));

        return {bag.pick_classic(len, rng), len};
    }

    struct SolveReport {
        int  points = 0;
        bool round_complete_bonus = false;
    };

    SolveReport solve(const std::string& rack, std::size_t hints) {
        SolveReport r;
        r.points = scoring::lightning_per_word(rack.size(), hints);
        score_  += r.points;
        solved_ += 1;
        time_left_ms_ = std::min<std::uint32_t>(cfg_.base_time_ms,
                                                time_left_ms_ + cfg_.bonus_time_ms);
        return r;
    }

    SolveReport end_round() {
        SolveReport r;
        if (round_complete_) return r;
        round_complete_ = true;
        r.round_complete_bonus = true;
        score_ += scoring::LIGHTNING_ROUND_BONUS;
        return r;
    }

    std::int32_t  score() const noexcept { return score_; }
    std::size_t   solved() const noexcept { return solved_; }
    std::uint32_t time_left_ms() const noexcept { return time_left_ms_; }

    void start_round(std::uint32_t /*now_ms*/) {
        round_complete_ = false;
        time_left_ms_   = cfg_.base_time_ms;
    }

    void tick(std::uint32_t elapsed_ms) {
        if (elapsed_ms >= time_left_ms_) time_left_ms_ = 0;
        else time_left_ms_ -= elapsed_ms;
    }

private:
    Config cfg_;
    const WordTree* common_;
    const WordTree* full_;
    std::int32_t  score_          = 0;
    std::size_t   solved_         = 0;
    std::size_t   words_per_round_ = 0;
    bool          round_complete_  = false;
    std::uint32_t time_left_ms_    = 0;
};

}  // namespace tt2
