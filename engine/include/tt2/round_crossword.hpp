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

    int cell_a(int i) const noexcept { return horizontal ? a + i : a; }
    int cell_b(int i) const noexcept { return horizontal ? b : b + i; }
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
        WordPlacement p;
        p.word       = root;
        p.a          = (A_LEN - static_cast<int>(root.size())) / 2;
        p.b          = ROOT_B;
        p.horizontal = true;
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
                    commit(p, static_cast<int>(i));
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

private:
    static std::size_t index(int a, int b) {
        if (!in_storage(a, b)) throw std::out_of_range("CrosswordGrid cell");
        return static_cast<std::size_t>(a * B_LEN + b);
    }
    std::vector<CrosswordCell> cells_;
};

class CrosswordRound {
public:
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

            placements_ = r.placements;
            filled_.assign(placements_.size(), false);
            score_    = 0;
            complete_ = false;
            return r;
        }
        throw std::runtime_error("CrosswordRound: could not build a grid");
    }

    struct GuessReport {
        GuessStatus status         = GuessStatus::NotAWord;
        int         points_awarded = 0;
        bool        complete       = false;
        std::size_t placement_index = static_cast<std::size_t>(-1);
    };

    // Guess (CONFIRMED 0x41c262..0x41c293 for the full-list check,
    // 0x41a637..0x41a66b / 0x41a7e0 for the scoring): must be in the
    // full list and match an unfilled grid word; +10*len^2 once; +1000
    // when the last word is filled.
    GuessReport guess(const std::string& raw) {
        GuessReport r;
        const std::string word = GuessProcessor::lower(raw);
        if (!full_->contains(word)) return r;

        for (std::size_t i = 0; i < placements_.size(); ++i) {
            if (placements_[i].word != word) continue;
            if (filled_[i]) {
                r.status = GuessStatus::Duplicate;
                return r;
            }
            filled_[i] = true;
            r.status          = GuessStatus::Accepted;
            r.placement_index = i;
            r.points_awarded  = score_crossword_word(word.size());
            if (std::all_of(filled_.begin(), filled_.end(), [](bool f) { return f; })) {
                r.points_awarded += scoring::CROSSWORD_COMPLETE_BONUS;
                complete_ = true;
                r.complete = true;
            }
            score_ += r.points_awarded;
            return r;
        }
        r.status = GuessStatus::NoRoom;
        return r;
    }

    std::int32_t score() const noexcept { return score_; }
    bool complete() const noexcept { return complete_; }
    std::size_t filled_count() const noexcept {
        return static_cast<std::size_t>(std::count(filled_.begin(), filled_.end(), true));
    }
    const std::vector<WordPlacement>& placements() const noexcept { return placements_; }
    const Config& config() const noexcept { return cfg_; }

private:
    Config cfg_;
    const WordTree* common_;
    const WordTree* full_;
    std::vector<WordPlacement> placements_;
    std::vector<bool> filled_;
    std::int32_t score_ = 0;
    bool complete_ = false;
};

}  // namespace tt2
