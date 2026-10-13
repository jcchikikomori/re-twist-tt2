#pragma once

// Classic round (Timed, Untimed, Word of the Day) — the single class
// `TextTwist2Scene` in the original. The three variants are just
// three fields of the round state:
//
//   - `timed`     : whether the round timer ticks
//   - `wotd`      : whether the rack is the Word of the Day
//   - `time_left_ms`: round timer, only meaningful when `timed`
//
// Word of the Day uses the same board-builder as Classic but caps
// the board at 5 slots per length (CONFIRMED at 0x4262b0).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/scoring.hpp"

namespace tt2 {

class ClassicRound {
public:
    struct Config {
        bool timed                 = false;
        bool wotd                  = false;
        std::size_t max_word_length = 8;
        std::uint32_t round_ms     = 0;       // 0 = untimed
        std::size_t board_per_length_cap = 0; // WOTD uses 5
    };

    struct Result {
        std::string rack;
        std::vector<std::string> sub_anagrams;
        std::vector<std::string> board_words;
        std::size_t slot_count = 0;
    };

    ClassicRound(const WordTree& common, const WordTree& full, Config cfg)
        : cfg_(cfg), common_(&common), full_(&full) {}

    Result start(Random& rng, Bag& bag, std::int64_t seed) {
        Result r;
        if (cfg_.wotd) {
            r.rack = Bag::word_of_the_day(*common_, seed);
        } else {
            const std::size_t L = Bag::classic_target_length(rng, cfg_.max_word_length);
            r.rack = bag.pick_classic(L, rng);
        }

        Board b = Board::build(r.rack, *common_, cfg_.board_per_length_cap);
        r.sub_anagrams = b.all();
        for (std::size_t l = 3; l <= 8; ++l) {
            for (const auto& w : b.slots(l)) r.board_words.push_back(w);
        }
        r.slot_count = r.board_words.size();

        filled_board_ = std::make_unique<FilledBoard>(std::move(b));

        score_          = 0;
        found_          = 0;
        qualified_      = false;
        time_left_ms_   = cfg_.round_ms;
        rack_           = r.rack;
        return r;
    }

    struct GuessReport {
        GuessStatus status        = GuessStatus::NotAWord;
        int         points_awarded = 0;
        bool        filled_last   = false;
        bool        bingo         = false;
    };

    GuessReport guess(const std::string& word_raw) {
        GuessReport r;
        if (!filled_board_) {
            r.status = GuessStatus::NotAWord;
            return r;
        }
        GuessProcessor proc(*filled_board_, *common_, *full_);
        GuessOutcome o = proc.apply(word_raw);
        r.status        = o.status;
        r.bingo         = o.bingo;
        r.filled_last   = o.filled_last;
        if (o.status == GuessStatus::Accepted || o.status == GuessStatus::Uncommon) {
            PlacementScore p = score_classic(
                word_raw.size(), o.bingo, qualified_, o.filled_last,
                filled_board_->slot_count());
            filled_board_->mark_filled(o.slot_index);
            score_      += p.total;
            found_      += 1;
            qualified_   = p.qualified;
            r.points_awarded = p.total;
        }
        return r;
    }

    std::int32_t  score() const noexcept { return score_; }
    std::size_t   found() const noexcept { return found_; }
    bool          qualified() const noexcept { return qualified_; }
    std::uint32_t time_left_ms() const noexcept { return time_left_ms_; }

    void tick(std::uint32_t elapsed_ms) {
        if (!cfg_.timed) return;
        if (elapsed_ms >= time_left_ms_) time_left_ms_ = 0;
        else time_left_ms_ -= elapsed_ms;
    }

    const std::string& rack() const noexcept { return rack_; }
    const Config&      config() const noexcept { return cfg_; }

    // The board with its per-slot "found" flags, or nullptr before
    // `start`. The UI reads it to draw the word grid and to reveal the
    // missing words at the end of a round.
    const FilledBoard* board() const noexcept { return filled_board_.get(); }
    std::size_t slot_count() const noexcept { return filled_board_ ? filled_board_->slot_count() : 0; }
    bool complete() const noexcept {
        return filled_board_ && filled_board_->filled_count() == filled_board_->slot_count();
    }

private:
    Config cfg_;
    const WordTree* common_;
    const WordTree* full_;
    std::string rack_;
    std::unique_ptr<FilledBoard> filled_board_;
    std::int32_t score_         = 0;
    std::size_t  found_         = 0;
    bool         qualified_     = false;
    std::uint32_t time_left_ms_ = 0;
};

}  // namespace tt2
