#pragma once

// Board (the on-screen word slots) and the AnagramTable that holds
// both dictionaries and the sub-anagram lists.
//
// CONFIRMED (0x43d7b0 / 0x432330 / 0x432b20): the board is the
// **sub-anagrams of the rack in the common list, length 3..8**, capped
// at ~48 slots by length-bucket quotas. Each bucket gets
//   max(1, trunc(count * 48 / total))
// slots. The two-letter and one-letter words are never used because
// the cap starts at 3.

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt2/word_tree.hpp"

namespace tt2 {

// Hard cap on the total slot count (CONFIRMED at 0x4324b6 / 0x4324c9:
// `cmp esi, 0x30` → 48; `48.0 / total`).
inline constexpr std::size_t BOARD_SLOT_CAP = 48;

class Board {
public:
    using Slots = std::array<std::vector<std::string>, 9>;  // index = length

    // Build the board for `rack` using the common-list sub-anagrams.
    // CONFIRMED: 0x43dcb0 (collect) → 0x432330 (cap) → 0x432b20 (place).
    // `per_length_cap` keeps only the first N words of each length 3..8
    // (WOTD uses 5; Classic leaves it 0). It runs **before** the 48-slot
    // quota, the way round start calls 0x4262b0 before the AnagramTable
    // ctor reaches 0x432330 (game-scene.md §8.3), so a crowded rack
    // still gets 5 words of every length it has.
    static Board build(const std::string& rack, const WordTree& common,
                       std::size_t per_length_cap = 0) {
        Board b;
        b.rack_ = rack;
        b.all_  = common.sub_anagrams(rack);
        if (per_length_cap > 0) {
            b.cap(b.slots_, first_per_length(b.all_, per_length_cap));
        } else {
            b.cap(b.slots_, b.all_);
        }
        return b;
    }

    // All sub-anagrams the original enumerator would have found, in
    // DFS order. Two-letter words are still in the list; the slot
    // cap is what hides them from the player.
    const std::vector<std::string>& all() const noexcept { return all_; }

    // The slot list for one length. Lengths 1..8 are valid; index 0
    // is empty.
    const std::vector<std::string>& slots(std::size_t length) const {
        return length < slots_.size() ? slots_[length] : empty_;
    }

    const std::string& rack() const noexcept { return rack_; }

    std::size_t filled_count() const noexcept {
        std::size_t n = 0;
        for (const auto& s : slots_) n += s.size();
        return n;
    }

    std::size_t slot_count() const noexcept {
        std::size_t n = 0;
        for (const auto& s : slots_) n += s.size();
        return n;
    }

    // True when `word` is the rack word itself (bingo: uses every
    // letter).
    bool is_bingo(const std::string& word) const noexcept {
        return word.size() == rack_.size() && is_anagram_of_rack(word);
    }

    bool is_anagram_of_rack(std::string word) const {
        if (word.size() > rack_.size()) return false;
        std::array<int, 26> need{};
        for (char c : rack_) ++need[static_cast<std::uint8_t>(c - 'a')];
        for (char c : word) {
            if (--need[static_cast<std::uint8_t>(c - 'a')] < 0) return false;
        }
        return true;
    }

private:
    std::string rack_;
    std::vector<std::string> all_;
    Slots slots_{};
    static inline const std::vector<std::string> empty_{};

    // 0x4262b0: one counter per length, [0, 0, 0, N, N, N, N, N, N] for
    // lengths 0..8; walk the list in order and drop every word whose
    // counter is already 0. Lengths 1..2 (counter 0) are dropped too.
    static std::vector<std::string> first_per_length(const std::vector<std::string>& words,
                                                     std::size_t per_length) {
        std::array<std::size_t, 9> left{};
        for (std::size_t l = 3; l <= 8; ++l) left[l] = per_length;
        std::vector<std::string> kept;
        for (const auto& w : words) {
            const std::size_t l = w.size();
            if (l >= left.size() || left[l] == 0) continue;
            --left[l];
            kept.push_back(w);
        }
        return kept;
    }

    // 0x432330: the 48-slot quota over `words` (in DFS order).
    static void cap(Slots& slots, const std::vector<std::string>& words) {
        std::array<std::size_t, 9> count{};
        for (const auto& w : words) {
            if (w.size() >= 1 && w.size() <= 8) ++count[w.size()];
        }
        std::size_t total = 0;
        for (std::size_t l = 3; l <= 8; ++l) total += count[l];
        if (total == 0) return;

        const double factor =
            static_cast<double>(BOARD_SLOT_CAP) / static_cast<double>(total);
        std::array<std::size_t, 9> quota{};
        for (std::size_t l = 3; l <= 8; ++l) {
            const std::size_t q = count[l] == 0
                ? 0
                : std::max<std::size_t>(1, static_cast<std::size_t>(count[l] * factor));
            quota[l] = q;
        }

        std::array<std::size_t, 9> placed{};
        for (const auto& w : words) {
            const std::size_t l = w.size();
            if (l < 3 || l > 8) continue;
            if (placed[l] >= quota[l]) continue;
            slots[l].push_back(w);
            ++placed[l];
        }
    }
};

// Guess-check results. Mirrors the return codes fed to the jump
// table at 0x424d4c from 0x432850 / 0x432b20.
//
//   0 = NotAWord       (not in the full list or not spellable from
//                       the rack — the find over the list at
//                       table+0x190 misses)
//   1 = Duplicate      (already sitting in a filled slot)
//   2 = NoRoom         (valid, but no free slot of that length)
//   3 = Uncommon       (placed in a free slot, but not in the common list)
//   4 = Accepted       (placed in a free slot of the same length)
enum class GuessStatus {
    NotAWord   = 0,
    Duplicate  = 1,
    NoRoom     = 2,
    Uncommon   = 3,
    Accepted   = 4,
};

struct GuessOutcome {
    GuessStatus status = GuessStatus::NotAWord;
    int  points        = 0;
    bool filled_last   = false;
    bool bingo         = false;
    bool newly_uncommon = false;
    std::size_t slot_index = static_cast<std::size_t>(-1);
    std::string placed_word;   // the lowered guess, set on success
};

// A `Board` augmented with per-slot occupancy. The original game
// tracks this with a parallel byte-array of "found" flags. Here we
// keep the flag in a parallel `std::vector<bool>`, and — like the
// original's slot word at `+0x19c` — the **placed** word per slot.
// The slot word is empty until the player fills the slot, then it
// holds the guessed word (0x431880), which may differ from the board
// answer.
class FilledBoard {
public:
    explicit FilledBoard(Board b) : board_(std::move(b)) {
        const std::size_t n = board_.slot_count();
        filled_.resize(n, false);
        placed_.resize(n);
    }

    const Board& board() const noexcept { return board_; }
    std::size_t slot_count() const noexcept { return board_.slot_count(); }
    std::size_t filled_count() const noexcept {
        std::size_t n = 0;
        for (bool f : filled_) if (f) ++n;
        return n;
    }
    bool is_filled(std::size_t i) const {
        return i < filled_.size() && filled_[i];
    }

    // Record a placement: slot `i` now shows `placed` (the guessed
    // word, not necessarily the board answer).
    void mark_filled(std::size_t i, std::string placed) {
        if (i < filled_.size()) {
            filled_[i] = true;
            placed_[i] = std::move(placed);
        }
    }

    // The word sitting in slot `i` (only meaningful once filled).
    const std::string& placed_word(std::size_t i) const {
        static const std::string none;
        return i < placed_.size() ? placed_[i] : none;
    }

    // True when `word` is already sitting in a filled slot (0x432850
    // step 2 — the comparison reads the placed word, 0x4318f0).
    bool is_placed(const std::string& word) const {
        for (std::size_t i = 0; i < placed_.size(); ++i) {
            if (filled_[i] && placed_[i] == word) return true;
        }
        return false;
    }

    // The flat slot list, parallel to `filled_`. Index 0..slot_count().
    std::vector<std::string> flat_slots() const {
        std::vector<std::string> out;
        out.reserve(board_.slot_count());
        for (std::size_t l = 3; l <= 8; ++l) {
            for (const auto& w : board_.slots(l)) out.push_back(w);
        }
        return out;
    }

private:
    Board board_;
    std::vector<bool> filled_;
    std::vector<std::string> placed_;
};

// The round-end reveal (0x4333a0, CONFIRMED). A guess fills the first
// empty slot of its length (0x432b20), not the slot of that board word,
// so an empty slot's own answer may already be on show elsewhere. For
// each empty slot in order, 0x433320 takes the first board word of the
// slot's length (+0x178) that 0x433180 finds in no slot, and 0x431880
// writes it at once, so later slots skip it too: no word shows twice.
// `board_words` is the flat slot list; `shown[i]` is slot i's word, empty
// when unfound. Returns `shown` with every empty slot filled. Should no
// word be left (0x433320 would return index 0), the slot keeps its own.
inline std::vector<std::string> reveal_words(const std::vector<std::string>& board_words,
                                             std::vector<std::string> shown) {
    shown.resize(board_words.size());
    const auto on_show = [&shown](const std::string& w) {
        for (const auto& s : shown) if (s == w) return true;
        return false;
    };
    for (std::size_t i = 0; i < shown.size(); ++i) {
        if (!shown[i].empty()) continue;
        const std::size_t len = board_words[i].size();
        std::string pick = board_words[i];
        for (const auto& w : board_words)
            if (w.size() == len && !on_show(w)) { pick = w; break; }
        shown[i] = pick;
    }
    return shown;
}

class GuessProcessor {
public:
    GuessProcessor(FilledBoard& board, const WordTree& common,
                   const WordTree& full)
        : board_(board), common_(&common), full_(&full) {}

    const FilledBoard& board() const noexcept { return board_; }

    // Apply a guess. The slot is **not** marked as filled here — the
    // caller does that once it has updated the round state.
    // `slot_index` in the returned `GuessOutcome` is the index to mark
    // (with `placed_word`).
    //
    // The rule set is 0x432850 + 0x432b20 (see
    // docs/reverse-engineering/gameplay-notes.md § "Guess check"):
    // any full-list word spellable from the rack fills the first empty
    // slot of its length and is displayed there — it need not be one
    // of the board's words.
    GuessOutcome apply(const std::string& raw_word) {
        const std::string word = lower(raw_word);
        GuessOutcome o;

        // Reject non-letters before any lookup (the dictionary and
        // the rack check below assume `a..z`).
        for (char c : word) {
            if (c < 'a' || c > 'z') {
                o.status = GuessStatus::NotAWord;
                return o;
            }
        }

        // 0x432850 entry: a linear find over the full-list copy at
        // table+0x190 (the rack's full-tree sub-anagram enumeration).
        // The guess must be in the full dictionary *and* spellable
        // from the rack letters.
        if (!full_->contains(word) ||
            !board_.board().is_anagram_of_rack(word)) {
            o.status = GuessStatus::NotAWord;
            return o;
        }

        // Already sitting in a filled slot → Duplicate. The slot
        // holds the placed word (0x431880 wrote the guess into it),
        // so a board word covered by an uncommon word is *not* a
        // duplicate — only the placed word is.
        if (board_.is_placed(word)) {
            o.status = GuessStatus::Duplicate;
            return o;
        }

        // 0x432b20: the first empty slot with the same length. When
        // there is none (including lengths with no slots at all, e.g.
        // two-letter words) the guess has nowhere to go.
        const std::size_t flat = first_empty_slot_of(word.size());
        if (flat == static_cast<std::size_t>(-1)) {
            o.status = GuessStatus::NoRoom;
            return o;
        }

        // Not in the common list → the out-flag "uncommon".
        const bool is_common = common_->contains(word);
        o.newly_uncommon = !is_common;
        o.slot_index     = flat;
        o.placed_word    = word;
        o.status         = is_common ? GuessStatus::Accepted
                                     : GuessStatus::Uncommon;
        o.points = 10 * static_cast<int>(word.size())
                      * static_cast<int>(word.size());
        o.bingo = board_.board().is_bingo(word);
        o.filled_last = (board_.filled_count() + 1) == board_.slot_count();
        return o;
    }

    static std::string lower(std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

private:
    // Flat index of the first free slot with the given word length,
    // or -1 when there is none. Slots are scanned in flat order
    // (lengths 3..8, board order within a length).
    std::size_t first_empty_slot_of(std::size_t length) const {
        std::size_t i = 0;
        for (std::size_t l = 3; l <= 8; ++l) {
            for (const auto& w : board_.board().slots(l)) {
                (void)w;
                if (l == length && !board_.is_filled(i)) return i;
                ++i;
            }
        }
        return static_cast<std::size_t>(-1);
    }

    FilledBoard& board_;
    const WordTree* common_;
    const WordTree* full_;
};

}  // namespace tt2
