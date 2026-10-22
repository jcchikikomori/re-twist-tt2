#pragma once

// Crossword ("Crisscross") round — `CrisscrossScene` + `CrosswordTable`
// in the original.
//
// CONFIRMED 2026-10-13 against the installed TextTwist2.exe
// (SHA-256 0c6e962c…) with Ghidra 12.1.4 through REA:
//
//   0x41ba40  round setup: root pick, word set, retry when the grid fails
//   0x436090  grid builder: root centred, then 10,000 placement attempts
//   0x435f90  fit check for one candidate placement
//   0x435d30  commit: write letters, block the two end cells, block the
//             3x3 neighbourhood of the crossing cell
//   0x4228f0  MSVC std::random_shuffle (CRT rand) over the word list
//   0x439370  MSVC std::random_shuffle (CRT rand) over the crossing list
//
// Two random sources are in play and must not be swapped:
//   - the game's Java-style LCG (`Random`) picks the root length, the
//     word count, and which already-placed word to cross on every
//     attempt;
//   - the CRT `rand()` (`CrtRandom`) shuffles the word list and the
//     candidate crossing list.
//
// Grid coordinates follow the binary: cell index = a * 11 + b, where
// `a` runs along the 18-long axis and `b` along the 11-long axis.
// A horizontal word advances `a`; a vertical word advances `b`.
//
// Play rules, CONFIRMED 2026-10-22 (docs/reverse-engineering/crossword.md
// §3-§7, the gaps G1-G10 of §10) unless tagged:
//
//   0x41e140 / 0x41e230  NEXT / PREV: one selected slot, index +-1 with
//                        wrap, solved slots included; 0x41e140(1) after a
//                        solve: the next unsolved slot
//   0x41a510 / 0x436a00  ENTER fills the selected slot only: a row of
//                        another length is ignored; not a full-list
//                        sub-anagram of the root -> not a word; already
//                        played in a slot -> duplicate; any other word
//                        fills the slot and may differ from the answer
//   0x41a665 / 0x41a7e0  10 * len^2 once per slot (word+0x85), +1000 when
//                        the last slot is solved; the score runs across
//                        rounds, round 1 zeroes it (0x41ba40)
//   0x41cd00             every crossing cell is locked to the letter of
//                        the solved slot crossing it
//   0x41aa20 / 0x41e270 / 0x41e060
//                        popups 7 / 4 / 9: lock clash, a crossing slot
//                        left unspellable, no word fits (meanings INFERRED)
//   0x41eaf0             take back: the played word goes, word+0x85
//                        stays, scene+0x371 is set
//   0x41b060             TWIST sets scene+0x370
//
// The non-owning WordTree pointers must outlive the round.
//
// Deliberate deviation: the original retries the whole setup forever
// when the grid cannot be built. `CrosswordRound::start` gives up after
// `Config::max_setup_retries` and throws, so a bad dictionary cannot
// hang the caller.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/random.hpp"
#include "tt2/scoring.hpp"
#include "tt2/word_tree.hpp"

namespace tt2 {

struct CrosswordCell {
    int  letter  = 0;      // 0 = empty, otherwise the char value
    bool blocked = false;  // no later word may put a letter here
};

struct WordPlacement {
    std::string word;
    int  a          = 0;
    int  b          = 0;
    bool horizontal = true;
    // Index in `word` of the letter laid on the word it was placed
    // against: the `crossing` that `CrosswordGrid::commit` takes. -1 for
    // the root, which crosses nothing.
    int  crossing   = -1;

    int cell_a(int i) const noexcept { return horizontal ? a + i : a; }
    int cell_b(int i) const noexcept { return horizontal ? b : b + i; }

    // Index in `word` of the letter on cell (ca, cb), or -1 when the word
    // does not cover that cell.
    int index_of_cell(int ca, int cb) const noexcept {
        const int i = horizontal ? ca - a : cb - b;
        const bool on_line = horizontal ? cb == b : ca == a;
        return on_line && i >= 0 && i < static_cast<int>(word.size()) ? i : -1;
    }
};

class CrosswordGrid {
public:
    // Playable extent (CONFIRMED 0x436180: `18 - len`, and the bounds
    // checks in 0x435f90). The original table reserves more cells than
    // 18x11 because the fit check reads one cell past each end.
    static constexpr int A_LEN   = 18;
    static constexpr int B_LEN   = 11;
    static constexpr int A_CELLS = 20;
    static constexpr int ROOT_B  = 5;
    static constexpr int MAX_PLACEMENT_ATTEMPTS = 10'000;  // `cmp ..., 0x2710`

    CrosswordGrid() : cells_(static_cast<std::size_t>(A_CELLS * B_LEN)) {}

    const CrosswordCell& at(int a, int b) const { return cells_[index(a, b)]; }
    CrosswordCell&       at(int a, int b)       { return cells_[index(a, b)]; }

    static bool in_storage(int a, int b) noexcept {
        return a >= 0 && a < A_CELLS && b >= 0 && b < B_LEN;
    }

    // Fit check (CONFIRMED 0x435f90). `crossing` is the index in
    // `p.word` that already sits on the grid; it is skipped.
    //
    //   - start must satisfy 1 <= a <= 18 and 1 <= b <= 9;
    //   - the cell after the word must satisfy the same bounds;
    //   - the cell before and the cell after the word must be empty
    //     (blocked is fine there);
    //   - every other letter cell must be empty AND not blocked.
    //
    // There is no explicit side-neighbour test: the original relies on
    // the 3x3 block written at every crossing by `commit`.
    bool fits(const WordPlacement& p, int crossing) const {
        const int len = static_cast<int>(p.word.size());
        if (p.a < 1 || p.a > A_LEN || p.b < 1 || p.b > B_LEN - 2) return false;
        const int end_a = p.cell_a(len);
        const int end_b = p.cell_b(len);
        if (end_a < 1 || end_a > A_LEN || end_b < 1 || end_b > B_LEN - 2) return false;

        for (int i = -1; i <= len; ++i) {
            if (crossing >= 0 && i == crossing) continue;
            const CrosswordCell& c = at(p.cell_a(i), p.cell_b(i));
            if (i == -1 || i == len) {
                if (c.letter != 0) return false;
            } else if (c.letter != 0 || c.blocked) {
                return false;
            }
        }
        return true;
    }

    // Commit (CONFIRMED 0x435d30). Writes the letters, blocks the two
    // end cells, and blocks the 3x3 neighbourhood of the crossing cell.
    // A negative `crossing` (the root) writes no 3x3 block
    // (CONFIRMED 0x436180..0x4361ed: the root path has no such loop).
    void commit(const WordPlacement& p, int crossing) {
        const int len = static_cast<int>(p.word.size());
        for (int i = -1; i <= len; ++i) {
            const int a = p.cell_a(i);
            const int b = p.cell_b(i);
            if (i == -1 || i == len) {
                at(a, b).blocked = true;
            } else {
                at(a, b).letter = static_cast<unsigned char>(p.word[static_cast<std::size_t>(i)]);
            }
            if (crossing >= 0 && i == crossing) {
                for (int da = -1; da <= 1; ++da) {
                    for (int db = -1; db <= 1; ++db) {
                        at(a + da, b + db).blocked = true;
                    }
                }
            }
        }
    }

    // Root placement (CONFIRMED 0x436180..0x4361ed): horizontal,
    // a = (18 - len) / 2, b = 5, end cells blocked.
    WordPlacement place_root(const std::string& root) {
        WordPlacement p = root_placement(root);
        commit(p, -1);
        return p;
    }

    // One placement attempt against the word `placed` (CONFIRMED
    // 0x4364xx..0x4366xx, the body of the 10,000-attempt loop):
    //
    //   1. list every (i, j) with word[i] == placed.word[j] whose placed
    //      cell is not blocked; only `j` is recorded, in (i outer,
    //      j inner) order;
    //   2. shuffle that list with the CRT generator;
    //   3. pop from the back: for each `j`, scan `i` in order and try the
    //      perpendicular placement; the first fit is committed.
    std::optional<WordPlacement> try_place(const std::string& word,
                                           const WordPlacement& placed,
                                           CrtRandom& crt) {
        std::vector<int> candidates;
        for (std::size_t i = 0; i < word.size(); ++i) {
            for (std::size_t j = 0; j < placed.word.size(); ++j) {
                if (word[i] != placed.word[j]) continue;
                const int ja = placed.cell_a(static_cast<int>(j));
                const int jb = placed.cell_b(static_cast<int>(j));
                if (at(ja, jb).blocked) continue;
                candidates.push_back(static_cast<int>(j));
            }
        }
        if (candidates.empty()) return std::nullopt;
        crt.shuffle(candidates);

        while (!candidates.empty()) {
            const int j = candidates.back();
            candidates.pop_back();
            for (std::size_t i = 0; i < word.size(); ++i) {
                if (word[i] != placed.word[static_cast<std::size_t>(j)]) continue;
                WordPlacement p;
                p.word = word;
                if (placed.horizontal) {
                    p.a          = placed.a + j;
                    p.b          = placed.b - static_cast<int>(i);
                    p.horizontal = false;
                } else {
                    p.a          = placed.a - static_cast<int>(i);
                    p.b          = placed.b + j;
                    p.horizontal = true;
                }
                if (fits(p, static_cast<int>(i))) {
                    p.crossing = static_cast<int>(i);
                    commit(p, p.crossing);
                    return p;
                }
            }
        }
        return std::nullopt;
    }

    struct BuildResult {
        std::vector<WordPlacement> placements;  // root first
        int attempts_used = 0;                  // total across all words
    };

    // Grid builder (CONFIRMED 0x436090). `words` must end with the root;
    // words are consumed from the back (the original pops the vector).
    // Each non-root word gets up to MAX_PLACEMENT_ATTEMPTS tries, each
    // against one already-placed word chosen with the Java LCG.
    static std::optional<BuildResult> build(std::vector<std::string> words,
                                            Random& lcg, CrtRandom& crt,
                                            CrosswordGrid* out_grid = nullptr) {
        if (words.empty()) return std::nullopt;
        CrosswordGrid g;
        BuildResult r;
        r.placements.push_back(g.place_root(words.back()));
        words.pop_back();

        while (!words.empty()) {
            const std::string word = words.back();
            words.pop_back();
            bool placed = false;
            for (int attempt = 0; attempt < MAX_PLACEMENT_ATTEMPTS; ++attempt) {
                ++r.attempts_used;
                const std::size_t k = static_cast<std::size_t>(
                    lcg.rand(0, static_cast<std::int32_t>(r.placements.size())));
                if (auto p = g.try_place(word, r.placements[k], crt)) {
                    r.placements.push_back(*p);
                    placed = true;
                    break;
                }
            }
            if (!placed) return std::nullopt;
        }
        if (out_grid) *out_grid = g;
        return r;
    }

    // Rebuilds a grid from its placements in commit order, root first,
    // through `commit` (the port of 0x435d30), so the blocked cells come
    // back exactly. The builder is never re-run: the exe keeps the table
    // in its save and does not rebuild it (crossword.md §5.5). Each
    // placement must be one the builder could have made at that point:
    // the root centred on ROOT_B with no crossing (0x436180); every later
    // word laid on an earlier word's letter that is not blocked (the
    // candidate rule in try_place) and passing `fits`. Returns nullopt on
    // the first placement that does not fit.
    static std::optional<CrosswordGrid> replay(const std::vector<WordPlacement>& placements) {
        if (placements.empty()) return std::nullopt;
        CrosswordGrid g;
        const WordPlacement& root = placements.front();
        const WordPlacement centred = root_placement(root.word);
        if (root.word.empty() || root.crossing != -1 || !root.horizontal ||
            root.a != centred.a || root.b != centred.b || !g.fits(root, -1)) {
            return std::nullopt;
        }
        g.commit(root, -1);
        for (std::size_t k = 1; k < placements.size(); ++k) {
            if (!g.can_cross(placements[k])) return std::nullopt;
            g.commit(placements[k], placements[k].crossing);
        }
        return g;
    }

private:
    static WordPlacement root_placement(const std::string& root) {
        WordPlacement p;
        p.word       = root;
        p.a          = (A_LEN - static_cast<int>(root.size())) / 2;
        p.b          = ROOT_B;
        p.horizontal = true;
        return p;
    }

    // A non-root placement the builder could commit on this grid: its
    // crossing letter sits on an unblocked cell holding the same letter,
    // and every other cell fits. `fits` skips the crossing cell, so the
    // letter is checked here (try_place gets it right by construction).
    bool can_cross(const WordPlacement& p) const {
        const int len = static_cast<int>(p.word.size());
        if (p.crossing < 0 || p.crossing >= len || !fits(p, p.crossing)) return false;
        const CrosswordCell& c = at(p.cell_a(p.crossing), p.cell_b(p.crossing));
        const int letter = static_cast<unsigned char>(p.word[static_cast<std::size_t>(p.crossing)]);
        return c.letter != 0 && c.letter == letter && !c.blocked;
    }

    static std::size_t index(int a, int b) {
        if (!in_storage(a, b)) throw std::out_of_range("CrosswordGrid cell");
        return static_cast<std::size_t>(a * B_LEN + b);
    }
    std::vector<CrosswordCell> cells_;
};

// A cell one slot shares with another (G4, crossword.md §3.3). The
// builder's `WordPlacement::crossing` names only the cell where a word met
// the word it was placed against; later words can cross it too, and the
// exe locks every one of them (0x41cd00).
struct CrosswordCrossing {
    std::size_t position       = 0;  // letter index in this slot's word
    std::size_t other          = 0;  // the other slot (placement index)
    std::size_t other_position = 0;  // letter index in the other slot's word
};

// One word object's play state (ctor 0x434f40, crossword.md §2.2).
struct CrosswordSlot {
    std::string played;     // +0x3c: the player's word, empty while unsolved; may differ from the answer
    std::string last_word;  // +0x60: the last row ENTERed on this slot (LAST WORD)
    bool scored = false;    // +0x85: points paid; a take-back keeps it
    std::vector<CrosswordCrossing> crossings;  // every shared cell, by position

    bool solved() const noexcept { return !played.empty(); }   // +0x84
};

class CrosswordRound {
public:
    static constexpr std::size_t NO_SLOT = static_cast<std::size_t>(-1);

    // scene+0x264: 0 playing, 1 puzzle solved, 2 given up.
    enum class State { Playing, Solved, GivenUp };

    // ENTER's outcome (0x41a510 / 0x436a00).
    enum class Guess {
        Ignored,    // not playing, slot solved, or the row's length differs (@0x41a5c7)
        NotAWord,   // not a full-list sub-anagram of the root: popup 0
        Duplicate,  // already played in a slot: popup 3
        Accepted,   // written into the selected slot (0x436ff0)
    };

    struct Config {
        std::size_t max_word_length       = 8;    // player setting (6/7/8)
        double      word_number_increment = 0.2;  // CROSSWORD uses BLITZ_WORD_NUMBER_INCREMENT
        std::size_t max_words             = 10;
        int         max_setup_retries     = 1000; // deviation, see header comment
    };

    struct Result {
        std::string root;
        std::vector<std::string> words;          // as given to the builder, root last
        std::vector<WordPlacement> placements;   // root first
        CrosswordGrid grid;
        int setup_retries = 0;
    };

    CrosswordRound(const WordTree& common, const WordTree& full)
        : CrosswordRound(common, full, Config{}) {}

    CrosswordRound(const WordTree& common, const WordTree& full, Config cfg)
        : cfg_(cfg), common_(&common), full_(&full) {}

    // Root length (CONFIRMED 0x41bbd0..0x41bc10): L = min(5 + rand % 4, maxLen).
    static std::size_t root_target_length(Random& lcg, std::size_t max_word_length) {
        const std::size_t L = 5 + static_cast<std::size_t>(lcg.rand(0, 4));
        return std::min(L, max_word_length);
    }

    // Word count (CONFIRMED 0x41bf23..0x41c030):
    //   N = min(10, 4 + rand % floor(4 + (round - 1) * increment))
    static std::size_t word_count(std::size_t round, Random& lcg, double increment,
                                  std::size_t max_words) {
        const double span_d = std::floor(4.0 + (static_cast<double>(round) - 1.0) * increment);
        const std::int32_t span = static_cast<std::int32_t>(span_d);
        const std::size_t n = 4 + static_cast<std::size_t>(lcg.rand(0, std::max(span, 1)));
        return std::min(n, max_words);
    }

    // Word set (CONFIRMED 0x41be00..0x41c1bd). Returns nullopt when the
    // root does not have exactly one full-length anagram in the common
    // list (itself). Otherwise: the common sub-anagrams, shuffled with
    // the CRT generator; the first N entries excluding the root; then
    // the root appended last.
    static std::optional<std::vector<std::string>> choose_word_set(
        const std::string& root, const WordTree& common, std::size_t round,
        Random& lcg, CrtRandom& crt, const Config& cfg) {
        std::vector<std::string> subs = common.sub_anagrams(root);
        crt.shuffle(subs);

        std::size_t full_length = 0;
        for (const auto& w : subs) if (w.size() == root.size()) ++full_length;
        if (full_length != 1) return std::nullopt;

        std::size_t n = word_count(round, lcg, cfg.word_number_increment, cfg.max_words);
        std::vector<std::string> out;
        for (const auto& w : subs) {
            if (n == 0) break;
            if (w != root) out.push_back(w);
            --n;  // decremented for every entry, root included
        }
        out.push_back(root);
        return out;
    }

    Result start(std::size_t round, Random& lcg, CrtRandom& crt, Bag& bag) {
        for (int retry = 0; retry < cfg_.max_setup_retries; ++retry) {
            const std::size_t L = root_target_length(lcg, cfg_.max_word_length);
            const std::string root = bag.pick_classic(L, lcg);
            auto words = choose_word_set(root, *common_, round, lcg, crt, cfg_);
            if (!words) continue;
            CrosswordGrid grid;
            auto built = CrosswordGrid::build(*words, lcg, crt, &grid);
            if (!built) continue;

            Result r;
            r.root          = root;
            r.words         = std::move(*words);
            r.placements    = std::move(built->placements);
            r.grid          = std::move(grid);
            r.setup_retries = retry;

            begin(r.placements, r.grid, make_slots(r.placements), static_cast<int>(round));
            if (round == 1) score_ = 0;   // 0x41ba40: round 1 zeroes the score
            return r;
        }
        throw std::runtime_error("CrosswordRound: could not build a grid");
    }

    // A saved round (the exe's load 0x420fa0; crossword.md §5.5, G9):
    // the placements in commit order, root first, and per slot the played
    // word (empty while unsolved) and the scored flag; the running score
    // and the round number. The grid is replayed through
    // `CrosswordGrid::replay`, never rebuilt. The twisted and mistake
    // flags start clear (the exe saves neither) and word 0, the root, is
    // selected even when it is solved: the load makes word 0 the selected
    // word and 0x41d920 selects word 0 (crossword.md §5.5, CW54, CW57).
    //
    // Returns false, leaving the round untouched, on a placement that
    // does not fit or a state the game cannot write: sizes that differ,
    // round < 1, a negative score, an answer or played word the root
    // cannot spell, a played word of the wrong length, played but not
    // scored, a word played twice, crossing slots that disagree, or every
    // slot solved (the exe saves only while playing, 0x41f7d0). Played
    // words are not looked up in the dictionary, so a swapped word list
    // does not void a save.
    bool restore(const std::vector<WordPlacement>& placements,
                 const std::vector<std::string>& played,
                 const std::vector<bool>& scored,
                 std::int32_t score, int round) {
        if (round < 1 || score < 0) return false;
        if (played.size() != placements.size() || scored.size() != placements.size()) return false;
        auto grid = CrosswordGrid::replay(placements);
        if (!grid) return false;
        for (const auto& p : placements) {
            if (!spellable_from(p.word, placements.front().word)) return false;
        }
        std::vector<CrosswordSlot> slots = make_slots(placements);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            slots[i].played = played[i];
            slots[i].scored = scored[i];
        }
        if (!possible_state(slots, placements)) return false;

        begin(placements, std::move(*grid), std::move(slots), round);   // selects word 0
        score_ = score;
        return true;
    }

    // ---- state -------------------------------------------------------
    int round() const noexcept { return round_; }               // +0x300
    std::int32_t score() const noexcept { return score_; }      // +0x308, runs across rounds
    State state() const noexcept { return state_; }             // +0x264
    bool complete() const noexcept { return state_ == State::Solved; }
    bool over() const noexcept { return state_ != State::Playing; }
    bool twisted() const noexcept { return twisted_; }          // +0x370: Puzzle Pro blocker
    bool mistake() const noexcept { return mistake_; }          // +0x371: Perfect Puzzler blocker
    std::size_t selected() const noexcept { return selected_; } // +0x378
    const std::string& root() const noexcept { return root_; }
    const std::vector<WordPlacement>& placements() const noexcept { return placements_; }
    const std::vector<CrosswordSlot>& slots() const noexcept { return slots_; }
    const CrosswordGrid& grid() const noexcept { return grid_; }
    const Config& config() const noexcept { return cfg_; }
    std::size_t solved_count() const noexcept {
        return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(),
                                                       [](const CrosswordSlot& s) { return s.solved(); }));
    }

    // The word a slot's row shows: the played word, or every answer after
    // GIVE UP (0x41fb50 -> 0x43ce60), solved rows included. Also the word
    // a row click looks up after the round (0x421e50, crossword.md §3.9).
    const std::string& shown_word(std::size_t slot) const {
        return state_ == State::GivenUp ? placements_.at(slot).word : slots_.at(slot).played;
    }

    // 0x41cd00: a crossing cell of `slot` is locked to the letter of the
    // solved slot crossing it. '\0' when the cell is not locked.
    char locked_letter(std::size_t slot, std::size_t position) const {
        check_cell(slot, position);
        for (const auto& c : slots_[slot].crossings) {
            const CrosswordSlot& other = slots_[c.other];
            if (c.position == position && other.solved()) return other.played[c.other_position];
        }
        return '\0';
    }

    // Popup 7 (0x41aa20 -> 0x43c060 / 0x40b230, INFERRED meaning): the
    // letter differs from the locked letter of that cell.
    bool clashes_with_lock(std::size_t slot, std::size_t position, char letter) const {
        const char locked = locked_letter(slot, position);
        return locked != '\0' && locked != lower(letter);
    }

    // Popup 4 (0x41e270, INFERRED meaning): `letter` on a cell shared with
    // an unsolved slot would leave that slot's fixed letters (this one
    // plus its letters locked by solved slots) needing more of a letter
    // than the root has. A crossing with a solved slot is a locked cell
    // (popup 7), not this check.
    bool breaks_crossing(std::size_t slot, std::size_t position, char letter) const {
        check_cell(slot, position);
        for (const auto& c : slots_[slot].crossings) {
            if (c.position != position || slots_[c.other].solved()) continue;
            std::string fixed(1, lower(letter));
            for (std::size_t k = 0; k < placements_[c.other].word.size(); ++k) {
                const char locked = k == c.other_position ? '\0' : locked_letter(c.other, k);
                if (locked != '\0') fixed += locked;
            }
            if (!spellable_from(fixed, root_)) return true;
        }
        return false;
    }

    // Popup 9 and the red highlight (0x41df10 -> 0x41e060, INFERRED
    // meaning): an unsolved slot no full-list sub-anagram of the root of
    // its length can fill any more, because each one is already played or
    // disagrees with a locked letter.
    bool dead_end(std::size_t slot) const {
        if (slots_.at(slot).solved()) return false;
        const std::size_t len = placements_[slot].word.size();
        for (const auto& w : full_subs_) {
            if (w.size() == len && !in_grid(w) && agrees_with_locks(slots_, slot, w)) return false;
        }
        return true;
    }

    // ---- actions (all ignored once the round is over) ----------------
    // A click on another word's row (0x421e50, state 0 only).
    bool select(std::size_t slot) {
        if (over() || slot >= slots_.size() || slot == selected_) return false;
        selected_ = slot;
        return true;
    }
    // NEXT / Tab / Right (0x41e140(0)): index + 1, wrapping, solved words
    // included; nothing happens with a single word.
    bool select_next() {
        if (over() || slots_.size() < 2) return false;
        selected_ = (selected_ + 1) % slots_.size();
        return true;
    }
    // PREV / Left (0x41e230): index - 1, wrapping.
    bool select_previous() {
        if (over() || slots_.size() < 2) return false;
        selected_ = (selected_ + slots_.size() - 1) % slots_.size();
        return true;
    }
    // 700 ms after a solve (0x41e930 -> 0x41e140(1)): the next unsolved
    // word, wrapping. False when no other word is unsolved.
    bool advance() {
        if (over() || slots_.empty()) return false;
        const std::size_t next = first_unsolved_from(selected_ + 1);
        if (next == NO_SLOT || next == selected_) return false;
        selected_ = next;
        return true;
    }

    struct GuessReport {
        Guess       status         = Guess::Ignored;
        int         points_awarded = 0;        // 10*len^2 on the slot's first solve, +1000 on the last slot
        bool        bingo          = false;    // every root letter: SOUND_word_bingo, not word_correct
        bool        uncommon       = false;    // Human Dictionary counter (0x40f3a0)
        bool        complete       = false;    // the puzzle is solved
        std::size_t slot           = NO_SLOT;  // the filled slot (the selected one)
    };

    // ENTER on the selected slot (0x41a510, crossword.md §3.5):
    //   1. the row is kept as the slot's LAST WORD (+0x60);
    //   2. a length other than the slot's is ignored (@0x41a5c7);
    //   3. 0x436a00: not in the root's full-list sub-anagrams
    //      (table+0x198) -> NotAWord; equal to a word played in any slot
    //      -> Duplicate; else the typed word fills this slot (0x436ff0),
    //      even when it is not the answer, and is uncommon when it is not
    //      in the root's common sub-anagrams (table+0x188);
    //   4. 10*len^2 when the slot was never scored (@0x41a665, +0x85),
    //      +1000 when every slot is solved (@0x41a7e0).
    // Port guard: a row that disagrees with a locked letter is ignored.
    // The exe's row keeps its locked letters (0x41aa20), so it never
    // reaches ENTER with one.
    GuessReport guess(const std::string& row) {
        GuessReport r;
        if (over() || slots_.empty() || slots_[selected_].solved()) return r;
        CrosswordSlot& slot = slots_[selected_];
        const std::string word = GuessProcessor::lower(row);
        slot.last_word = word;
        if (word.size() != placements_[selected_].word.size()) return r;
        if (!agrees_with_locks(slots_, selected_, word)) return r;
        if (std::find(full_subs_.begin(), full_subs_.end(), word) == full_subs_.end()) {
            r.status = Guess::NotAWord;
            return r;
        }
        if (in_grid(word)) {
            r.status = Guess::Duplicate;
            return r;
        }
        slot.played = word;
        r.status   = Guess::Accepted;
        r.slot     = selected_;
        r.bingo    = word.size() == root_.size();
        // The word is spellable from the root here, so "not in the root's
        // common sub-anagrams" is "not in the common list".
        r.uncommon = !common_->contains(word);
        if (!slot.scored) {
            r.points_awarded = score_crossword_word(word.size());
            slot.scored = true;
        }
        if (solved_count() == slots_.size()) {
            r.points_awarded += scoring::CROSSWORD_COMPLETE_BONUS;
            r.complete = true;
            state_ = State::Solved;
        }
        score_ += r.points_awarded;
        return r;
    }

    // Take the selected solved word back (0x41eaf0, crossword.md §3.6):
    // the played word leaves the slot and the grid, the scored flag stays
    // (solving it again pays nothing), and +0x371 is set.
    bool take_back() {
        if (over() || slots_.empty() || !slots_[selected_].solved()) return false;
        slots_[selected_].played.clear();
        mistake_ = true;
        return true;
    }

    // TWIST / Space (0x41b060): sets +0x370. The rack shuffle itself is
    // the scene's (CRT, 0x43c370), as are the button's enable rules.
    bool twist() {
        if (over() || slots_.empty()) return false;
        twisted_ = true;
        return true;
    }

    // GIVE UP, YES (0x41fb50): +0x264 = 2; the rows show their answers.
    bool give_up() {
        if (over() || slots_.empty()) return false;
        state_ = State::GivenUp;
        return true;
    }

private:
    // 0x41ba40 steps 3 and 6, or a load: a fresh round on these
    // placements, the root selected, the flags clear.
    void begin(std::vector<WordPlacement> placements, CrosswordGrid grid,
               std::vector<CrosswordSlot> slots, int round) {
        root_       = placements.front().word;
        full_subs_  = full_->sub_anagrams(root_);   // table+0x198
        placements_ = std::move(placements);
        grid_       = std::move(grid);
        slots_      = std::move(slots);
        round_      = round;
        selected_   = 0;
        state_      = State::Playing;
        twisted_    = false;
        mistake_    = false;
    }

    static std::vector<CrosswordSlot> make_slots(const std::vector<WordPlacement>& ps) {
        std::vector<CrosswordSlot> slots(ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i) slots[i].crossings = crossings_of(ps, i);
        return slots;
    }

    // Every cell placement `i` shares with another, by position.
    static std::vector<CrosswordCrossing> crossings_of(const std::vector<WordPlacement>& ps, std::size_t i) {
        std::vector<CrosswordCrossing> out;
        for (std::size_t p = 0; p < ps[i].word.size(); ++p) {
            const int a = ps[i].cell_a(static_cast<int>(p));
            const int b = ps[i].cell_b(static_cast<int>(p));
            for (std::size_t j = 0; j < ps.size(); ++j) {
                const int q = j == i ? -1 : ps[j].index_of_cell(a, b);
                if (q >= 0) out.push_back({p, j, static_cast<std::size_t>(q)});
            }
        }
        return out;
    }

    // `word` (as long as the slot) keeps every letter locked by a solved
    // crossing slot.
    static bool agrees_with_locks(const std::vector<CrosswordSlot>& slots, std::size_t slot,
                                  const std::string& word) {
        for (const auto& c : slots[slot].crossings) {
            const CrosswordSlot& other = slots[c.other];
            if (other.solved() && word[c.position] != other.played[c.other_position]) return false;
        }
        return true;
    }

    // A slot state the game can save (see `restore`).
    static bool possible_state(const std::vector<CrosswordSlot>& slots,
                               const std::vector<WordPlacement>& ps) {
        std::size_t solved = 0;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const CrosswordSlot& s = slots[i];
            if (!s.solved()) continue;
            ++solved;
            if (!s.scored || s.played.size() != ps[i].word.size()) return false;
            if (!spellable_from(s.played, ps.front().word)) return false;
            if (!agrees_with_locks(slots, i, s.played)) return false;
            for (std::size_t j = 0; j < i; ++j) {
                if (slots[j].played == s.played) return false;
            }
        }
        return solved < slots.size();
    }

    // The first unsolved slot at `from` or after it, wrapping; NO_SLOT
    // when every slot is solved.
    std::size_t first_unsolved_from(std::size_t from) const {
        const std::size_t n = slots_.size();
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = (from + k) % n;
            if (!slots_[i].solved()) return i;
        }
        return NO_SLOT;
    }

    // 0x436a00's duplicate test: the word is played in some slot.
    bool in_grid(const std::string& word) const {
        return std::any_of(slots_.begin(), slots_.end(),
                           [&word](const CrosswordSlot& s) { return s.played == word; });
    }

    void check_cell(std::size_t slot, std::size_t position) const {
        if (position >= placements_.at(slot).word.size()) {
            throw std::out_of_range("CrosswordRound cell");
        }
    }

    static char lower(char c) noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    Config cfg_;
    const WordTree* common_;
    const WordTree* full_;
    std::string root_;
    std::vector<WordPlacement> placements_;   // root first
    std::vector<CrosswordSlot> slots_;        // parallel to placements_
    CrosswordGrid grid_;
    std::vector<std::string> full_subs_;      // table+0x198
    std::size_t selected_ = 0;
    int round_ = 0;
    std::int32_t score_ = 0;
    State state_ = State::Playing;
    bool twisted_ = false;
    bool mistake_ = false;
};

}  // namespace tt2
