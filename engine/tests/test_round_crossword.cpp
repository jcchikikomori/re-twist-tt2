// Tests for the crossword round (round_crossword.hpp) and the MSVC CRT
// generator it depends on (random.hpp).

#include <set>
#include <string>
#include <vector>

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/round_crossword.hpp"
#include "tt2/saved_game.hpp"
#include "tt2/trophies.hpp"
#include "tt2/word_tree.hpp"

using tt2::Bag;
using tt2::BagState;
using tt2::CrosswordGrid;
using tt2::CrosswordRound;
using tt2::CrtRandom;
using tt2::Random;
using tt2::WordPlacement;
using tt2::WordTree;
using Guess = tt2::CrosswordRound::Guess;
using State = tt2::CrosswordRound::State;

namespace {
// One pool word ("planet") with plenty of 3-4 letter sub-anagrams and
// no other full-length anagram. Nothing else is 5..8 letters, so the
// bag always hands out "planet".
WordTree common() {
    return WordTree::from_words({
        "planet",
        "plan", "plat", "lane", "lean", "pane", "peal", "plea", "pale",
        "leap", "neat", "lent", "pant", "pelt", "tale", "teal", "late",
        "ant", "ape", "apt", "eat", "tan", "tap", "ten", "net", "pen",
        "pet", "pat", "nap", "lap", "alp", "let", "ale", "tea",
        // A root with a second full-length anagram, for the rejection test.
        "caret", "crate",
    });
}
// The common words plus "zebra", "nape" (full list only and spellable
// from "planet": an uncommon guess) and "tent" (full list, but "planet"
// has one 't').
WordTree full() {
    auto words = common().words();
    words.push_back("zebra");
    words.push_back("nape");
    words.push_back("tent");
    return WordTree::from_words(words);
}

bool consistent(const CrosswordGrid& g, const std::vector<WordPlacement>& ps) {
    for (const auto& p : ps) {
        for (std::size_t i = 0; i < p.word.size(); ++i) {
            const auto& c = g.at(p.cell_a(static_cast<int>(i)), p.cell_b(static_cast<int>(i)));
            if (c.letter != static_cast<unsigned char>(p.word[i])) return false;
        }
    }
    return true;
}

// Every one of the 20x11 stored cells: letter and blocked flag.
bool same_cells(const CrosswordGrid& x, const CrosswordGrid& y) {
    for (int a = 0; a < CrosswordGrid::A_CELLS; ++a) {
        for (int b = 0; b < CrosswordGrid::B_LEN; ++b) {
            if (x.at(a, b).letter != y.at(a, b).letter) return false;
            if (x.at(a, b).blocked != y.at(a, b).blocked) return false;
        }
    }
    return true;
}

std::string layout(const std::vector<WordPlacement>& ps) {
    std::string out;
    for (const auto& p : ps) {
        if (!out.empty()) out += ' ';
        out += p.word + ":" + std::to_string(p.a) + "," + std::to_string(p.b) +
               (p.horizontal ? "h" : "v") + "," + std::to_string(p.crossing);
    }
    return out;
}

// A hand-made grid the builder could have made, in commit order:
//
//   0 planet  a 6..11, b 5   horizontal, the root
//   1 pane    a 8, b 4..7    vertical, its 'a' (index 1) on planet's 'a' (8,5)
//   2 tale    a 5..8, b 7    horizontal, its 'e' (index 3) on pane's 'e' (8,7)
//   3 ten     a 11, b 5..7   vertical, its 't' (index 0) on planet's 't' (11,5)
//
// "pane" crosses two words, "planet" and "tale"; its builder crossing
// index names only the first.
std::vector<WordPlacement> fixed_grid() {
    return {
        {"planet", 6, 5, true, -1},
        {"pane", 8, 4, false, 1},
        {"tale", 5, 7, true, 3},
        {"ten", 11, 5, false, 0},
    };
}
const std::vector<std::string> NONE_PLAYED{"", "", "", ""};
const std::vector<bool> NONE_SCORED{false, false, false, false};

int code(Guess g) { return static_cast<int>(g); }
int code(State s) { return static_cast<int>(s); }

// What the Crossword scene writes on leave: WORD is the root, one xword
// entry per placement in commit order, root first, TIME 0.
tt2::SavedGame save_of(const CrosswordRound& r) {
    tt2::SavedGame g;
    g.mode  = tt2::save_mode_name(tt2::GameMode::Crossword);
    g.word  = r.root();
    g.round = r.round();
    g.score = r.score();
    for (std::size_t i = 0; i < r.placements().size(); ++i) {
        const WordPlacement& p = r.placements()[i];
        const tt2::CrosswordSlot& s = r.slots()[i];
        g.crossword_words.push_back({p.word, p.a, p.b, p.horizontal, p.crossing, s.scored, s.played});
    }
    return g;
}

// And what its resume does with a parsed save.
bool restore_from(CrosswordRound& r, const tt2::SavedGame& g) {
    std::vector<WordPlacement> placements;
    std::vector<std::string> played;
    std::vector<bool> scored;
    for (const auto& w : g.crossword_words) {
        placements.push_back({w.answer, w.a, w.b, w.horizontal, w.crossing});
        played.push_back(w.played);
        scored.push_back(w.scored);
    }
    return r.restore(placements, played, scored, g.score, g.round);
}

// Fills slots 1..3 of the fixed grid with their answers; true when the
// last one solved the puzzle.
bool solve_pane_tale_ten(CrosswordRound& round) {
    round.select(1);
    round.guess("pane");
    round.select(2);
    round.guess("tale");
    round.select(3);
    return round.guess("ten").complete;
}

tt2::CrosswordRoundFlags flags_of(const CrosswordRound& r) {
    return tt2::CrosswordRoundFlags{r.twisted(), r.mistake()};
}
}  // namespace

// --- CRT rand -------------------------------------------------------------

TEST(crt_rand_msvc_reference) {
    // Well-known msvcrt sequence for srand(1).
    CrtRandom r;
    ASSERT_EQ(r.rand(), 41);
    ASSERT_EQ(r.rand(), 18467);
    ASSERT_EQ(r.rand(), 6334);
    ASSERT_EQ(r.rand(), 26500);
    ASSERT_EQ(r.rand(), 19169);
}

TEST(crt_rand_srand_resets) {
    CrtRandom a(7), b(7);
    a.rand(); a.rand();
    a.srand(7);
    ASSERT_EQ(a.rand(), b.rand());
    ASSERT_EQ(a.rand(), b.rand());
}

TEST(crt_shuffle_matches_hand_computation) {
    // rand(): 41, 18467, 6334, 26500 →
    //   i=1: 41 % 2 = 1      swap(1,1)
    //   i=2: 18467 % 3 = 2   swap(2,2)
    //   i=3: 6334 % 4 = 2    swap(3,2)  → 0 1 3 2 4
    //   i=4: 26500 % 5 = 0   swap(4,0)  → 4 1 3 2 0
    CrtRandom r;
    std::vector<int> v{0, 1, 2, 3, 4};
    r.shuffle(v);
    ASSERT_TRUE(v == (std::vector<int>{4, 1, 3, 2, 0}));
}

TEST(crt_below_never_reaches_bound) {
    CrtRandom r(123);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(r.below(7) < 7);
    }
    ASSERT_EQ(r.below(1), 0u);
}

// --- Grid ----------------------------------------------------------------

TEST(crossword_root_is_centred_on_middle_line) {
    CrosswordGrid g;
    WordPlacement p = g.place_root("planet");
    ASSERT_EQ(p.a, 6);   // (18 - 6) / 2
    ASSERT_EQ(p.b, 5);
    ASSERT_TRUE(p.horizontal);
    for (int i = 0; i < 6; ++i) {
        ASSERT_EQ(g.at(6 + i, 5).letter, static_cast<int>("planet"[i]));
        ASSERT_TRUE(!g.at(6 + i, 5).blocked);
    }
    ASSERT_TRUE(g.at(5, 5).blocked);
    ASSERT_TRUE(g.at(12, 5).blocked);
    ASSERT_EQ(g.at(5, 5).letter, 0);
}

TEST(crossword_fits_rejects_out_of_bounds) {
    CrosswordGrid g;
    WordPlacement p;
    p.word = "plan";
    p.horizontal = true;
    p.a = 0; p.b = 5;             ASSERT_TRUE(!g.fits(p, -1));  // a must be >= 1
    p.a = 15; p.b = 5;            ASSERT_TRUE(!g.fits(p, -1));  // a + len must be <= 18
    p.a = 14; p.b = 5;            ASSERT_TRUE(g.fits(p, -1));
    p.a = 5; p.b = 0;             ASSERT_TRUE(!g.fits(p, -1));  // b must be >= 1
    p.a = 5; p.b = 10;            ASSERT_TRUE(!g.fits(p, -1));  // b must be <= 9
    p.horizontal = false;
    p.a = 5; p.b = 6;             ASSERT_TRUE(!g.fits(p, -1));  // b + len must be <= 9
    p.a = 5; p.b = 5;             ASSERT_TRUE(g.fits(p, -1));
}

TEST(crossword_fits_rejects_overlap_and_blocked_cells) {
    CrosswordGrid g;
    g.place_root("planet");  // cells (6..11, 5); (5,5) and (12,5) blocked

    // Vertical "lane" crossing the root's 'l' at (7,5): fits with crossing=0.
    WordPlacement v;
    v.word = "lane"; v.horizontal = false; v.a = 7; v.b = 5;
    ASSERT_TRUE(g.fits(v, 0));

    // Same placement but claiming a different crossing index must fail,
    // because (7,5) is occupied by the root's letter.
    ASSERT_TRUE(!g.fits(v, 1));

    // A vertical word whose interior crosses a blocked end cell fails.
    WordPlacement w;
    w.word = "tan"; w.horizontal = false; w.a = 12; w.b = 4;  // (12,5) blocked
    ASSERT_TRUE(!g.fits(w, -1));

    // The cell before/after the word may be blocked but not lettered.
    WordPlacement u;
    u.word = "ant"; u.horizontal = false; u.a = 5; u.b = 6;  // (5,5) blocked above
    ASSERT_TRUE(g.fits(u, -1));
    WordPlacement x;
    x.word = "ant"; x.horizontal = false; x.a = 6; x.b = 6;  // (6,5) has 'p'
    ASSERT_TRUE(!g.fits(x, -1));
}

TEST(crossword_commit_blocks_ends_and_3x3_around_crossing) {
    CrosswordGrid g;
    g.place_root("planet");
    WordPlacement v;
    v.word = "lane"; v.horizontal = false; v.a = 7; v.b = 5;
    g.commit(v, 0);

    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(g.at(7, 5 + i).letter, static_cast<int>("lane"[i]));
    }
    ASSERT_TRUE(g.at(7, 4).blocked);  // cell before
    ASSERT_TRUE(g.at(7, 9).blocked);  // cell after
    for (int da = -1; da <= 1; ++da) {
        for (int db = -1; db <= 1; ++db) {
            ASSERT_TRUE(g.at(7 + da, 5 + db).blocked);
        }
    }
    ASSERT_TRUE(!g.at(9, 5).blocked);  // root letter away from the crossing
}

TEST(crossword_try_place_is_perpendicular_and_consistent) {
    CrosswordGrid g;
    WordPlacement root = g.place_root("planet");
    CrtRandom crt;
    auto p = g.try_place("plan", root, crt);
    ASSERT_TRUE(p.has_value());
    ASSERT_TRUE(!p->horizontal);
    ASSERT_TRUE(consistent(g, {root, *p}));
    // Crosses the root somewhere on the middle line.
    bool crosses = false;
    for (int i = 0; i < 4; ++i) {
        if (p->cell_b(i) == 5 && p->a >= root.a && p->a < root.a + 6) crosses = true;
    }
    ASSERT_TRUE(crosses);
}

TEST(crossword_try_place_fails_without_shared_letters) {
    CrosswordGrid g;
    WordPlacement root = g.place_root("planet");
    CrtRandom crt;
    auto p = g.try_place("zzz", root, crt);
    ASSERT_TRUE(!p.has_value());
}

TEST(crossword_build_gives_up_after_10000_attempts) {
    Random lcg(1);
    CrtRandom crt;
    auto r = CrosswordGrid::build({"zzz", "planet"}, lcg, crt);
    ASSERT_TRUE(!r.has_value());
}

TEST(crossword_build_places_all_words_root_first) {
    Random lcg(1);
    CrtRandom crt;
    CrosswordGrid g;
    auto r = CrosswordGrid::build({"tale", "pane", "plan", "planet"}, lcg, crt, &g);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->placements.size(), 4u);
    ASSERT_EQ(r->placements[0].word, std::string("planet"));
    ASSERT_EQ(r->placements[1].word, std::string("plan"));   // consumed from the back
    ASSERT_TRUE(consistent(g, r->placements));
    ASSERT_TRUE(r->attempts_used >= 3);
    ASSERT_TRUE(r->attempts_used < 3 * CrosswordGrid::MAX_PLACEMENT_ATTEMPTS);
    // Every non-root word is perpendicular to at least one earlier word.
    for (std::size_t i = 1; i < r->placements.size(); ++i) {
        bool perpendicular = false;
        for (std::size_t k = 0; k < i; ++k) {
            if (r->placements[k].horizontal != r->placements[i].horizontal) perpendicular = true;
        }
        ASSERT_TRUE(perpendicular);
    }
}

TEST(crossword_build_fails_when_no_legal_crossing_remains) {
    // "plan" and "lane" only use the root's first four letters, and every
    // crossing blocks its 3x3 neighbourhood, so "tan" is always boxed in.
    // The original game hits the 10,000-attempt cap and restarts the
    // round; the builder reports failure instead.
    for (std::int64_t seed = 1; seed <= 5; ++seed) {
        Random lcg(seed);
        CrtRandom crt;
        auto r = CrosswordGrid::build({"tan", "lane", "plan", "planet"}, lcg, crt);
        ASSERT_TRUE(!r.has_value());
    }
}

TEST(crossword_build_is_deterministic_for_same_seeds) {
    auto run = [] {
        Random lcg(99);
        CrtRandom crt(5);
        auto r = CrosswordGrid::build({"tan", "lane", "plan", "pelt", "planet"}, lcg, crt);
        std::vector<std::string> out;
        if (r) for (const auto& p : r->placements) {
            out.push_back(p.word + ":" + std::to_string(p.a) + "," + std::to_string(p.b) +
                          (p.horizontal ? "h" : "v"));
        }
        return out;
    };
    ASSERT_TRUE(run() == run());
}

// --- Crossing index and replay --------------------------------------------

TEST(crossword_place_root_has_no_crossing) {
    CrosswordGrid g;
    ASSERT_EQ(g.place_root("planet").crossing, -1);
}

TEST(crossword_try_place_records_the_crossing_index) {
    CrosswordGrid g;
    WordPlacement root = g.place_root("planet");
    CrtRandom crt;
    auto p = g.try_place("plan", root, crt);
    ASSERT_TRUE(p.has_value());
    ASSERT_TRUE(p->crossing >= 0 && p->crossing < 4);
    // The crossing letter is the one sitting on the root's row.
    const auto i = static_cast<std::size_t>(p->crossing);
    ASSERT_EQ(p->cell_b(p->crossing), 5);
    ASSERT_EQ(g.at(p->cell_a(p->crossing), 5).letter, static_cast<int>(p->word[i]));
    ASSERT_EQ(root.word[static_cast<std::size_t>(p->a - root.a)], p->word[i]);
}

TEST(crossword_build_layout_is_unchanged_for_fixed_seeds) {
    // Pinned from the builder before WordPlacement gained `crossing`: same
    // cells, same number of attempts, same CRT draws. The crossing indexes
    // were worked out by hand from the cells (e.g. "pelt" at (11, 2..5)
    // meets planet's 't' at (11,5) with its index 3).
    Random lcg1(1);
    CrtRandom crt1(5);
    auto r1 = CrosswordGrid::build({"tale", "pane", "plan", "pelt", "planet"}, lcg1, crt1);
    ASSERT_TRUE(r1.has_value());
    ASSERT_EQ(layout(r1->placements),
              std::string("planet:6,5h,-1 pelt:11,2v,3 plan:6,5v,0 pane:9,3v,2 tale:5,7h,1"));
    ASSERT_EQ(r1->attempts_used, 4);
    ASSERT_EQ(crt1.state(), 1568135154u);

    Random lcg3(3);
    CrtRandom crt3(5);
    auto r3 = CrosswordGrid::build({"tale", "pane", "plan", "pelt", "planet"}, lcg3, crt3);
    ASSERT_TRUE(r3.has_value());
    ASSERT_EQ(layout(r3->placements),
              std::string("planet:6,5h,-1 pelt:11,2v,3 plan:6,5v,0 pane:8,3h,3 tale:5,7h,1"));
    ASSERT_EQ(r3->attempts_used, 4);
}

TEST(crossword_replaying_the_commits_rebuilds_the_built_grid) {
    auto com = common();
    auto ful = full();
    for (std::int64_t seed = 1; seed <= 20; ++seed) {
        Bag bag(com, BagState{1, 0});
        Random lcg(seed);
        CrtRandom crt(static_cast<std::uint32_t>(seed));
        CrosswordRound round(com, ful);
        auto r = round.start(1, lcg, crt, bag);

        CrosswordGrid replayed;
        for (const auto& p : r.placements) replayed.commit(p, p.crossing);
        ASSERT_TRUE(same_cells(replayed, r.grid));

        auto checked = CrosswordGrid::replay(r.placements);
        ASSERT_TRUE(checked.has_value());
        ASSERT_TRUE(same_cells(*checked, r.grid));
    }
}

TEST(crossword_replay_accepts_the_fixed_grid) {
    auto g = CrosswordGrid::replay(fixed_grid());
    ASSERT_TRUE(g.has_value());
    ASSERT_TRUE(consistent(*g, fixed_grid()));
    ASSERT_TRUE(g->at(8, 6).blocked);   // 3x3 around pane x planet
    ASSERT_TRUE(g->at(8, 8).blocked);   // 3x3 around tale x pane
    ASSERT_TRUE(g->at(11, 8).blocked);  // the cell after "ten"
}

TEST(crossword_replay_rejects_a_placement_the_builder_could_not_make) {
    ASSERT_TRUE(!CrosswordGrid::replay({}).has_value());

    auto off_centre = fixed_grid();
    off_centre[0].a = 5;
    ASSERT_TRUE(!CrosswordGrid::replay(off_centre).has_value());

    auto vertical_root = fixed_grid();
    vertical_root[0].horizontal = false;
    ASSERT_TRUE(!CrosswordGrid::replay(vertical_root).has_value());

    auto root_crossing = fixed_grid();
    root_crossing[0].crossing = 0;
    ASSERT_TRUE(!CrosswordGrid::replay(root_crossing).has_value());

    auto no_crossing = fixed_grid();
    no_crossing[3].crossing = -1;
    ASSERT_TRUE(!CrosswordGrid::replay(no_crossing).has_value());

    auto past_the_end = fixed_grid();
    past_the_end[1].crossing = 4;
    ASSERT_TRUE(!CrosswordGrid::replay(past_the_end).has_value());

    // "ten" over planet's 'e' at (10,5): every cell fits, the letter differs.
    auto wrong_letter = fixed_grid();
    wrong_letter[3].a = 10;
    ASSERT_TRUE(!CrosswordGrid::replay(wrong_letter).has_value());

    // "tale" one row up runs into the 3x3 block around pane x planet.
    auto blocked_cells = fixed_grid();
    blocked_cells[2].b = 6;
    ASSERT_TRUE(!CrosswordGrid::replay(blocked_cells).has_value());

    // "ten" past the bottom edge (b + len must stay <= 9).
    auto off_grid = fixed_grid();
    off_grid[3].b = 7;
    ASSERT_TRUE(!CrosswordGrid::replay(off_grid).has_value());

    // A word crossing pane's 'p' at (8,4), a blocked cell (3x3 around
    // pane x planet): try_place never offers a blocked crossing.
    auto blocked_crossing = fixed_grid();
    blocked_crossing.push_back({"p", 8, 4, true, 0});
    ASSERT_TRUE(!CrosswordGrid::replay(blocked_crossing).has_value());
}

// --- Round setup ---------------------------------------------------------

TEST(crossword_root_target_length_in_5_to_8_capped) {
    Random lcg(3);
    for (int i = 0; i < 100; ++i) {
        const std::size_t L = CrosswordRound::root_target_length(lcg, 8);
        ASSERT_TRUE(L >= 5 && L <= 8);
    }
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(CrosswordRound::root_target_length(lcg, 6) <= 6);
    }
}

TEST(crossword_word_count_formula) {
    // Round 1: floor(4 + 0) = 4 → N in [4, 7].
    Random lcg(11);
    for (int i = 0; i < 50; ++i) {
        const std::size_t n = CrosswordRound::word_count(1, lcg, 0.2, 10);
        ASSERT_TRUE(n >= 4 && n <= 7);
    }
    // Round 31: floor(4 + 30 * 0.2) = 10 → N in [4, 13], capped at 10.
    bool hit_cap = false;
    for (int i = 0; i < 200; ++i) {
        const std::size_t n = CrosswordRound::word_count(31, lcg, 0.2, 10);
        ASSERT_TRUE(n >= 4 && n <= 10);
        if (n == 10) hit_cap = true;
    }
    ASSERT_TRUE(hit_cap);
}

TEST(crossword_word_set_rejects_root_with_another_anagram) {
    auto com = common();
    Random lcg(1);
    CrtRandom crt;
    CrosswordRound::Config cfg;
    auto ws = CrosswordRound::choose_word_set("caret", com, 1, lcg, crt, cfg);
    ASSERT_TRUE(!ws.has_value());
}

TEST(crossword_word_set_ends_with_root_and_excludes_it_elsewhere) {
    auto com = common();
    Random lcg(1);
    CrtRandom crt;
    CrosswordRound::Config cfg;
    auto ws = CrosswordRound::choose_word_set("planet", com, 1, lcg, crt, cfg);
    ASSERT_TRUE(ws.has_value());
    ASSERT_EQ(ws->back(), std::string("planet"));
    ASSERT_TRUE(ws->size() >= 4 && ws->size() <= 8);  // N in [4,7] (+ root, − root if drawn)
    std::set<std::string> seen;
    for (const auto& w : *ws) {
        ASSERT_TRUE(com.contains(w));
        ASSERT_TRUE(seen.insert(w).second);
    }
}

TEST(crossword_round_start_and_guess_flow) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(2024);
    CrtRandom crt;
    CrosswordRound round(com, ful);
    auto r = round.start(1, lcg, crt, bag);

    ASSERT_EQ(r.root, std::string("planet"));
    ASSERT_EQ(r.placements.front().word, std::string("planet"));
    ASSERT_EQ(r.placements.size(), r.words.size());
    ASSERT_TRUE(consistent(r.grid, r.placements));
    ASSERT_TRUE(same_cells(round.grid(), r.grid));
    ASSERT_EQ(round.root(), std::string("planet"));
    ASSERT_EQ(round.score(), 0);
    ASSERT_EQ(round.round(), 1);
    ASSERT_EQ(code(round.state()), code(State::Playing));
    ASSERT_EQ(round.selected(), 0u);   // 0x41ba40: the root is selected first
    ASSERT_EQ(round.slots().size(), r.placements.size());

    // The guess goes into the selected slot (the root, 6 letters) only.
    // Not in the full list: rejected.
    ASSERT_EQ(code(round.guess("qqqqqq").status), code(Guess::NotAWord));
    // In the full list but shorter than the slot: ignored (the old any-slot
    // guess called this NoRoom).
    ASSERT_EQ(code(round.guess("zebra").status), code(Guess::Ignored));

    // Fill every slot with its answer; the last one carries the +1000 bonus.
    int expected = 0;
    for (std::size_t i = 0; i < r.placements.size(); ++i) {
        if (i != round.selected()) ASSERT_TRUE(round.select(i));
        const std::string& w = r.placements[i].word;
        auto g = round.guess(w);
        ASSERT_EQ(code(g.status), code(Guess::Accepted));
        ASSERT_EQ(g.slot, i);
        ASSERT_EQ(round.slots()[i].played, w);
        const int len = static_cast<int>(w.size());
        expected += 10 * len * len;
        if (i + 1 == r.placements.size()) {
            ASSERT_TRUE(g.complete);
            expected += 1000;
        } else {
            ASSERT_TRUE(!g.complete);
        }
        ASSERT_EQ(g.points_awarded, (i + 1 == r.placements.size()) ? 10 * len * len + 1000
                                                                    : 10 * len * len);
    }
    ASSERT_EQ(round.score(), expected);
    ASSERT_TRUE(round.complete());
    ASSERT_EQ(round.solved_count(), r.placements.size());

    // The puzzle is solved: a repeated word scores nothing more.
    auto d = round.guess(r.placements[1].word);
    ASSERT_EQ(code(d.status), code(Guess::Ignored));
    ASSERT_EQ(d.points_awarded, 0);
    ASSERT_EQ(round.score(), expected);
}

TEST(crossword_round_guess_is_case_insensitive) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(7);
    CrtRandom crt;
    CrosswordRound round(com, ful);
    auto r = round.start(1, lcg, crt, bag);
    std::string upper = r.placements[0].word;
    for (char& c : upper) c = static_cast<char>(c - 'a' + 'A');
    ASSERT_EQ(code(round.guess(upper).status), code(Guess::Accepted));
    ASSERT_EQ(round.slots()[0].played, r.placements[0].word);   // stored in lower case
}

TEST(crossword_round_throws_when_grid_impossible) {
    // A dictionary whose only pool word has a second anagram, so every
    // setup attempt is rejected and the retry cap trips.
    auto com = WordTree::from_words({
        "caret", "crate", "ace", "act", "arc", "are", "art", "ate", "car", "cat",
        "ear", "eat", "era", "eta", "rat", "tar", "tea", "ret", "tae",
        "acre", "care", "race", "rate", "tare", "tear", "cart",
    });
    Bag bag(com, BagState{1, 0});
    Random lcg(1);
    CrtRandom crt;
    CrosswordRound::Config cfg;
    cfg.max_setup_retries = 10;
    CrosswordRound round(com, com, cfg);
    ASSERT_THROWS(round.start(1, lcg, crt, bag));
}

// --- G9 restore ----------------------------------------------------------

TEST(crossword_restore_rebuilds_the_round_cell_by_cell) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(2024);
    CrtRandom crt;
    CrosswordRound before(com, ful);
    auto r = before.start(1, lcg, crt, bag);
    ASSERT_EQ(code(before.guess("planet").status), code(Guess::Accepted));   // the root, 360

    std::vector<std::string> played;
    std::vector<bool> scored;
    for (const auto& s : before.slots()) {
        played.push_back(s.played);
        scored.push_back(s.scored);
    }
    CrosswordRound after(com, ful);
    ASSERT_TRUE(after.restore(r.placements, played, scored, before.score(), 3));

    ASSERT_TRUE(same_cells(after.grid(), r.grid));
    ASSERT_EQ(layout(after.placements()), layout(r.placements));
    ASSERT_EQ(after.root(), std::string("planet"));
    ASSERT_EQ(after.score(), 360);
    ASSERT_EQ(after.round(), 3);
    ASSERT_EQ(code(after.state()), code(State::Playing));
    for (std::size_t i = 0; i < played.size(); ++i) {
        ASSERT_EQ(after.slots()[i].played, played[i]);
        ASSERT_EQ(after.slots()[i].scored, static_cast<bool>(scored[i]));
    }
    ASSERT_EQ(after.selected(), 1u);   // the first unsolved slot
}

TEST(crossword_restore_keeps_the_scored_flag_of_a_taken_back_slot) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    // "pane" was solved, scored and taken back before the save.
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, true, false, false}, 520, 2));
    ASSERT_EQ(round.selected(), 1u);
    auto g = round.guess("pane");
    ASSERT_EQ(code(g.status), code(Guess::Accepted));
    ASSERT_EQ(g.points_awarded, 0);
    ASSERT_EQ(round.score(), 520);
}

TEST(crossword_restore_starts_with_fresh_flags) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.twist());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(round.take_back());
    ASSERT_TRUE(round.twisted() && round.mistake());

    // The exe saves neither flag (crossword.md §5.5).
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 1));
    ASSERT_TRUE(!round.twisted());
    ASSERT_TRUE(!round.mistake());
    ASSERT_EQ(round.selected(), 1u);
}

TEST(crossword_restore_fails_on_a_misfit_placement) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 2));

    auto misfit = fixed_grid();
    misfit[3].a = 10;   // "ten" over planet's 'e'
    ASSERT_TRUE(!round.restore(misfit, NONE_PLAYED, NONE_SCORED, 0, 1));

    // The failed restore left the round as it was.
    ASSERT_EQ(round.placements()[3].a, 11);
    ASSERT_EQ(round.slots()[0].played, std::string("planet"));
    ASSERT_EQ(round.score(), 360);
    ASSERT_EQ(round.round(), 2);
}

TEST(crossword_restore_rejects_a_slot_state_the_game_cannot_write) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    const auto g = fixed_grid();
    const std::vector<bool> all{true, true, true, true};

    // Sizes differ from the placements.
    ASSERT_TRUE(!round.restore(g, {"", "", ""}, NONE_SCORED, 0, 1));
    ASSERT_TRUE(!round.restore(g, NONE_PLAYED, {false, false, false}, 0, 1));
    // No round 0, no negative score.
    ASSERT_TRUE(!round.restore(g, NONE_PLAYED, NONE_SCORED, 0, 0));
    ASSERT_TRUE(!round.restore(g, NONE_PLAYED, NONE_SCORED, -1, 1));
    // A played word shorter than its slot.
    ASSERT_TRUE(!round.restore(g, {"plan", "", "", ""}, all, 0, 1));
    // A played word "planet" cannot spell.
    ASSERT_TRUE(!round.restore(g, {"", "tent", "", ""}, all, 0, 1));
    // A played word that was never paid for.
    ASSERT_TRUE(!round.restore(g, {"planet", "", "", ""}, NONE_SCORED, 0, 1));
    // The same word in two slots.
    ASSERT_TRUE(!round.restore(g, {"", "lane", "lane", ""}, all, 0, 1));
    // Crossing slots that disagree: pant's 't' and tale's 'e' share (8,7).
    ASSERT_TRUE(!round.restore(g, {"", "pant", "tale", ""}, all, 0, 1));
    // Every slot solved: the exe saves only while playing (0x41f7d0).
    ASSERT_TRUE(!round.restore(g, {"planet", "pane", "tale", "ten"}, all, 0, 1));
    // An answer "planet" cannot spell.
    auto bad_answer = fixed_grid();
    bad_answer[3].word = "tet";
    ASSERT_TRUE(!round.restore(bad_answer, NONE_PLAYED, NONE_SCORED, 0, 1));
    // An answer with a letter outside a..z.
    auto bad_letter = fixed_grid();
    bad_letter[2].word = "tAle";
    ASSERT_TRUE(!round.restore(bad_letter, NONE_PLAYED, NONE_SCORED, 0, 1));

    ASSERT_TRUE(round.slots().empty());   // nothing was restored
}

// --- G1 selection --------------------------------------------------------

TEST(crossword_next_and_previous_wrap_and_include_solved_words) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 1));
    ASSERT_EQ(round.selected(), 1u);

    ASSERT_TRUE(round.select_previous());
    ASSERT_EQ(round.selected(), 0u);   // the solved root is not skipped
    ASSERT_TRUE(round.select_previous());
    ASSERT_EQ(round.selected(), 3u);   // wraps to the last word
    ASSERT_TRUE(round.select_next());
    ASSERT_EQ(round.selected(), 0u);   // wraps to the first word
    ASSERT_TRUE(round.select_next());
    ASSERT_EQ(round.selected(), 1u);
}

TEST(crossword_next_and_previous_do_nothing_with_one_word) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore({fixed_grid()[0]}, {""}, {false}, 0, 1));
    ASSERT_TRUE(!round.select_next());
    ASSERT_TRUE(!round.select_previous());
    ASSERT_EQ(round.selected(), 0u);
}

TEST(crossword_round_not_started_ignores_input) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.slots().empty());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Ignored));
    ASSERT_TRUE(!round.select(0));
    ASSERT_TRUE(!round.select_next());
    ASSERT_TRUE(!round.advance());
    ASSERT_TRUE(!round.take_back());
    ASSERT_TRUE(!round.twist());
    ASSERT_TRUE(!round.give_up());
    ASSERT_TRUE(!round.twisted());
}

TEST(crossword_advance_moves_to_the_next_unsolved_word) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"", "pane", "", "ten"}, {false, true, false, true}, 250, 1));
    ASSERT_TRUE(round.select(2));
    ASSERT_EQ(code(round.guess("tale").status), code(Guess::Accepted));
    ASSERT_EQ(round.selected(), 2u);   // the solve itself does not move the selection

    ASSERT_TRUE(round.advance());      // 0x41e140(1): skips "ten", wraps to the root
    ASSERT_EQ(round.selected(), 0u);
    ASSERT_TRUE(round.select_next());  // NEXT does not skip: "pane" is solved
    ASSERT_EQ(round.selected(), 1u);
}

TEST(crossword_advance_stays_when_no_other_word_is_unsolved) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"", "pane", "tale", "ten"}, {false, true, true, true}, 410, 1));
    ASSERT_EQ(round.selected(), 0u);
    ASSERT_TRUE(!round.advance());
    ASSERT_EQ(round.selected(), 0u);
}

TEST(crossword_select_picks_another_row) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(2));
    ASSERT_EQ(round.selected(), 2u);
    ASSERT_TRUE(!round.select(2));   // the selected row: its letters get the click
    ASSERT_TRUE(!round.select(4));   // no such word
    ASSERT_EQ(round.selected(), 2u);
}

// --- G2 / G3 slot-bound guess ---------------------------------------------

TEST(crossword_guess_shorter_than_the_slot_is_ignored) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    auto g = round.guess("plan");   // the root slot has 6 letters
    ASSERT_EQ(code(g.status), code(Guess::Ignored));
    ASSERT_EQ(g.points_awarded, 0);
    ASSERT_TRUE(!round.slots()[0].solved());
    ASSERT_EQ(round.slots()[0].last_word, std::string("plan"));   // LAST WORD keeps it
    ASSERT_EQ(round.score(), 0);
}

TEST(crossword_guess_must_be_a_full_list_sub_anagram_of_the_root) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pnae").status), code(Guess::NotAWord));   // not a word
    ASSERT_EQ(code(round.guess("tent").status), code(Guess::NotAWord));   // a word, one 't' too many
    ASSERT_EQ(round.slots()[1].last_word, std::string("tent"));
    ASSERT_TRUE(!round.slots()[1].solved());
    ASSERT_EQ(round.score(), 0);
}

TEST(crossword_guess_already_in_the_grid_is_a_duplicate) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Accepted));
    ASSERT_TRUE(round.select(2));
    auto d = round.guess("pane");
    ASSERT_EQ(code(d.status), code(Guess::Duplicate));
    ASSERT_EQ(d.points_awarded, 0);
    ASSERT_TRUE(!round.slots()[2].solved());
    ASSERT_EQ(round.score(), 160);
}

TEST(crossword_guess_fills_the_selected_slot_with_another_valid_word) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    // "tale" is another slot's answer, not yet in the grid: it fills "pane"'s slot.
    auto g = round.guess("tale");
    ASSERT_EQ(code(g.status), code(Guess::Accepted));
    ASSERT_EQ(g.slot, 1u);
    ASSERT_EQ(g.points_awarded, 160);
    ASSERT_EQ(round.slots()[1].played, std::string("tale"));
    ASSERT_EQ(round.shown_word(1), std::string("tale"));
    ASSERT_EQ(round.placements()[1].word, std::string("pane"));   // the answer is kept

    // ...so the "tale" slot cannot take it any more.
    ASSERT_TRUE(round.select(2));
    ASSERT_EQ(code(round.guess("tale").status), code(Guess::Duplicate));
}

TEST(crossword_crossing_slots_take_the_played_word_letters) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pant").status), code(Guess::Accepted));
    // tale's last cell takes pant's 't', not the answer's 'e'.
    ASSERT_EQ(round.locked_letter(2, 3), 't');
    ASSERT_TRUE(round.select(2));
    // The answer no longer agrees with the locked letter: such a row cannot
    // be typed (popup 7), so ENTER with it is ignored.
    ASSERT_EQ(code(round.guess("tale").status), code(Guess::Ignored));
    ASSERT_EQ(code(round.guess("pelt").status), code(Guess::Accepted));
}

// --- G7 score ------------------------------------------------------------

TEST(crossword_scores_ten_len_squared_per_slot_and_1000_for_the_last) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));

    auto planet = round.guess("planet");
    ASSERT_EQ(planet.points_awarded, 360);
    ASSERT_TRUE(planet.bingo);
    ASSERT_TRUE(round.advance());
    auto pane = round.guess("pane");
    ASSERT_EQ(pane.points_awarded, 160);
    ASSERT_TRUE(!pane.bingo);
    ASSERT_TRUE(round.advance());
    ASSERT_EQ(round.guess("tale").points_awarded, 160);
    ASSERT_TRUE(round.advance());
    auto ten = round.guess("ten");
    ASSERT_EQ(ten.points_awarded, 1090);   // 90 + 1000
    ASSERT_TRUE(ten.complete);
    ASSERT_EQ(round.score(), 1770);
    ASSERT_EQ(code(round.state()), code(State::Solved));
}

TEST(crossword_a_slot_taken_back_and_solved_again_scores_nothing) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(round.guess("pane").points_awarded, 160);
    ASSERT_TRUE(round.take_back());
    auto again = round.guess("pane");
    ASSERT_EQ(code(again.status), code(Guess::Accepted));
    ASSERT_EQ(again.points_awarded, 0);
    ASSERT_TRUE(round.take_back());
    ASSERT_EQ(round.guess("lane").points_awarded, 0);   // another word, same slot
    ASSERT_EQ(round.score(), 160);
}

TEST(crossword_score_runs_across_rounds_until_round_1) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(2024);
    CrtRandom crt;
    CrosswordRound round(com, ful);
    round.start(1, lcg, crt, bag);
    ASSERT_EQ(round.guess("planet").points_awarded, 360);

    round.start(2, lcg, crt, bag);
    ASSERT_EQ(round.round(), 2);
    ASSERT_EQ(round.score(), 360);   // carried over
    ASSERT_EQ(round.solved_count(), 0u);
    ASSERT_EQ(round.selected(), 0u);

    round.start(1, lcg, crt, bag);   // a new game
    ASSERT_EQ(round.round(), 1);
    ASSERT_EQ(round.score(), 0);
}

// --- G4 crossings and locked letters -------------------------------------

TEST(crossword_lists_every_crossing_of_a_slot) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));

    const auto& pane = round.slots()[1].crossings;
    ASSERT_EQ(pane.size(), 2u);
    ASSERT_EQ(pane[0].position, 1u);
    ASSERT_EQ(pane[0].other, 0u);
    ASSERT_EQ(pane[0].other_position, 2u);
    ASSERT_EQ(pane[1].position, 3u);   // tale crossed pane after it was placed
    ASSERT_EQ(pane[1].other, 2u);
    ASSERT_EQ(pane[1].other_position, 3u);
    ASSERT_EQ(round.placements()[1].crossing, 1);   // the builder's index names one

    const auto& planet = round.slots()[0].crossings;
    ASSERT_EQ(planet.size(), 2u);
    ASSERT_EQ(planet[0].other, 1u);
    ASSERT_EQ(planet[1].position, 5u);
    ASSERT_EQ(planet[1].other, 3u);
    ASSERT_EQ(round.slots()[2].crossings.size(), 1u);
    ASSERT_EQ(round.slots()[3].crossings.size(), 1u);
}

TEST(crossword_locked_letters_come_from_solved_crossing_slots) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(round.locked_letter(1, 1), '\0');   // planet unsolved: nothing locked

    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_EQ(round.locked_letter(1, 1), 'a');
    ASSERT_EQ(round.locked_letter(3, 0), 't');
    ASSERT_EQ(round.locked_letter(1, 0), '\0');   // not a crossing
    ASSERT_EQ(round.locked_letter(2, 3), '\0');   // crosses pane, which is unsolved

    ASSERT_TRUE(round.take_back());
    ASSERT_EQ(round.locked_letter(1, 1), '\0');
    ASSERT_THROWS(round.locked_letter(1, 4));      // past the end of "pane"
    ASSERT_THROWS(round.locked_letter(4, 0));      // no such slot
}

// --- G5 typing checks ----------------------------------------------------

TEST(crossword_popup_7_a_letter_clashing_with_a_locked_letter) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 1));
    ASSERT_TRUE(round.clashes_with_lock(1, 1, 'e'));
    ASSERT_TRUE(!round.clashes_with_lock(1, 1, 'a'));   // the locked letter itself
    ASSERT_TRUE(!round.clashes_with_lock(1, 1, 'A'));
    ASSERT_TRUE(!round.clashes_with_lock(1, 0, 'e'));   // an unlocked cell
}

TEST(crossword_popup_4_a_letter_a_crossing_word_cannot_spell) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 1));
    ASSERT_TRUE(round.select(2));
    // tale's last cell is pane's last cell; pane already holds planet's
    // only 'a', so a second 'a' there would leave pane unspellable.
    ASSERT_TRUE(round.breaks_crossing(2, 3, 'a'));
    ASSERT_TRUE(round.breaks_crossing(2, 3, 'A'));
    ASSERT_TRUE(!round.breaks_crossing(2, 3, 'e'));
    ASSERT_TRUE(!round.breaks_crossing(2, 0, 'a'));   // not a crossing
    // A crossing with a solved word is a locked cell (popup 7), not this check.
    ASSERT_TRUE(!round.breaks_crossing(1, 1, 'a'));
}

TEST(crossword_popup_9_dead_end_when_no_unplayed_word_fits) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(!round.dead_end(2));   // "tale" fits

    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("plea").status), code(Guess::Accepted));
    // tale's last cell is now 'a', and "plea", the only 4-letter word
    // ending in 'a', is already in the grid.
    ASSERT_EQ(round.locked_letter(2, 3), 'a');
    ASSERT_TRUE(round.dead_end(2));
    ASSERT_TRUE(!round.dead_end(1));   // a solved slot is never a dead end

    ASSERT_TRUE(round.take_back());
    ASSERT_TRUE(!round.dead_end(2));
}

// --- G6 take back --------------------------------------------------------

TEST(crossword_take_back_clears_the_word_keeps_the_points_sets_the_mistake_flag) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(round.guess("planet").points_awarded, 360);
    ASSERT_TRUE(!round.mistake());

    ASSERT_TRUE(round.take_back());
    ASSERT_TRUE(!round.slots()[0].solved());
    ASSERT_EQ(round.slots()[0].played, std::string(""));
    ASSERT_TRUE(round.slots()[0].scored);
    ASSERT_EQ(round.score(), 360);
    ASSERT_TRUE(round.mistake());
    ASSERT_EQ(round.solved_count(), 0u);
    // The word left the grid, so it is no duplicate now.
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
}

TEST(crossword_take_back_needs_the_selected_slot_solved) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(!round.take_back());
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Accepted));
    ASSERT_TRUE(round.select(0));
    ASSERT_TRUE(!round.take_back());   // the root is selected and unsolved
    ASSERT_TRUE(round.slots()[1].solved());
    ASSERT_TRUE(!round.mistake());
}

// --- G8 flags ------------------------------------------------------------

TEST(crossword_twist_sets_the_twisted_flag_and_rejections_set_no_flag) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(code(round.guess("qqqqqq").status), code(Guess::NotAWord));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Accepted));
    ASSERT_TRUE(round.select(2));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Duplicate));
    ASSERT_TRUE(!round.twisted());
    ASSERT_TRUE(!round.mistake());

    ASSERT_TRUE(round.twist());
    ASSERT_TRUE(round.twisted());
    ASSERT_TRUE(!round.mistake());
}

TEST(crossword_flags_reset_when_a_round_starts) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(2024);
    CrtRandom crt;
    CrosswordRound round(com, ful);
    round.start(1, lcg, crt, bag);
    ASSERT_TRUE(round.twist());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(round.take_back());
    ASSERT_TRUE(round.twisted() && round.mistake());

    round.start(2, lcg, crt, bag);
    ASSERT_TRUE(!round.twisted());
    ASSERT_TRUE(!round.mistake());
}

// --- G10 uncommon --------------------------------------------------------

TEST(crossword_uncommon_means_not_a_common_sub_anagram_of_the_root) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "", "", ""}, {true, false, false, false}, 360, 1));
    ASSERT_EQ(round.selected(), 1u);
    auto nape = round.guess("nape");   // full list only
    ASSERT_EQ(code(nape.status), code(Guess::Accepted));
    ASSERT_TRUE(nape.uncommon);
    ASSERT_TRUE(round.select(3));
    auto ten = round.guess("ten");
    ASSERT_EQ(code(ten.status), code(Guess::Accepted));
    ASSERT_TRUE(!ten.uncommon);
}

// --- Round end -----------------------------------------------------------

TEST(crossword_give_up_shows_the_answers_and_ends_input) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pant").status), code(Guess::Accepted));
    ASSERT_EQ(round.shown_word(1), std::string("pant"));
    ASSERT_EQ(round.shown_word(2), std::string(""));

    ASSERT_TRUE(round.give_up());
    ASSERT_EQ(code(round.state()), code(State::GivenUp));
    ASSERT_TRUE(round.over());
    ASSERT_EQ(round.shown_word(0), std::string("planet"));
    ASSERT_EQ(round.shown_word(1), std::string("pane"));   // the answer, not the played word
    ASSERT_EQ(round.shown_word(2), std::string("tale"));

    ASSERT_TRUE(!round.give_up());
    ASSERT_TRUE(!round.select(2));
    ASSERT_TRUE(!round.select_next());
    ASSERT_TRUE(!round.select_previous());
    ASSERT_TRUE(!round.take_back());
    ASSERT_TRUE(!round.twist());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Ignored));
    ASSERT_EQ(round.score(), 160);
}

TEST(crossword_solving_the_last_slot_ends_the_round) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), {"planet", "pane", "tale", ""}, {true, true, true, false}, 680, 1));
    ASSERT_EQ(round.selected(), 3u);
    auto g = round.guess("ten");
    ASSERT_TRUE(g.complete);
    ASSERT_EQ(round.score(), 1770);
    ASSERT_TRUE(round.complete());
    ASSERT_EQ(round.shown_word(3), std::string("ten"));

    ASSERT_TRUE(!round.advance());
    ASSERT_TRUE(!round.select(0));
    ASSERT_TRUE(!round.take_back());
    ASSERT_TRUE(!round.give_up());
}

// --- C6 save (AC-C9) ---------------------------------------------------------

TEST(crossword_save_bounds_match_the_grid_and_the_word_count) {
    // saved_game.hpp keeps its own copy of these, like the Blitz bounds.
    ASSERT_EQ(tt2::SAVED_CROSSWORD_A_CELLS, CrosswordGrid::A_CELLS);
    ASSERT_EQ(tt2::SAVED_CROSSWORD_B_CELLS, CrosswordGrid::B_LEN);
    ASSERT_EQ(tt2::SAVED_CROSSWORD_MAX_WORDS, CrosswordRound::Config{}.max_words + 1);   // plus the root
}

TEST(crossword_save_round_trip_restores_the_round_cell_by_cell) {
    auto com = common();
    auto ful = full();
    Bag bag(com, BagState{1, 0});
    Random lcg(2024);
    CrtRandom crt;
    CrosswordRound before(com, ful);
    auto r = before.start(3, lcg, crt, bag);
    ASSERT_TRUE(r.placements.size() >= 4u);
    // The root solved; slot 1 solved and taken back; slot 2 solved.
    ASSERT_EQ(code(before.guess(r.root).status), code(Guess::Accepted));
    ASSERT_TRUE(before.select(1));
    ASSERT_EQ(code(before.guess(r.placements[1].word).status), code(Guess::Accepted));
    ASSERT_TRUE(before.take_back());
    ASSERT_TRUE(before.select(2));
    ASSERT_EQ(code(before.guess(r.placements[2].word).status), code(Guess::Accepted));

    tt2::SavedGame back;
    ASSERT_TRUE(tt2::parse(tt2::serialize(save_of(before)), back));
    ASSERT_TRUE(back.crossword());
    CrosswordRound after(com, ful);
    ASSERT_TRUE(restore_from(after, back));

    ASSERT_TRUE(same_cells(after.grid(), before.grid()));   // all 20x11 cells: letter and blocked flag
    ASSERT_EQ(layout(after.placements()), layout(before.placements()));
    ASSERT_EQ(after.round(), 3);
    ASSERT_EQ(after.score(), before.score());
    ASSERT_EQ(after.slots().size(), before.slots().size());
    for (std::size_t i = 0; i < before.slots().size(); ++i) {
        ASSERT_EQ(after.slots()[i].played, before.slots()[i].played);
        ASSERT_EQ(after.slots()[i].scored, before.slots()[i].scored);
    }
    ASSERT_TRUE(after.slots()[1].scored && !after.slots()[1].solved());   // the take-back survived
}

TEST(crossword_save_keeps_a_played_word_that_differs_from_the_answer) {
    auto com = common();
    auto ful = full();
    CrosswordRound before(com, ful);
    ASSERT_TRUE(before.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 2));
    ASSERT_EQ(code(before.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(before.select(1));
    ASSERT_EQ(code(before.guess("pant").status), code(Guess::Accepted));

    const std::string text = tt2::serialize(save_of(before));
    ASSERT_TRUE(text.find("mode=CROSSWORD\nword=planet\nround=2\ntime=0\n") != std::string::npos);
    ASSERT_TRUE(text.find("score=520\n") != std::string::npos);
    ASSERT_TRUE(text.find("xword=planet,6,5,h,-1,1,planet\nxword=pane,8,4,v,1,1,pant\n"
                          "xword=tale,5,7,h,3,0,\nxword=ten,11,5,v,0,0,\n") != std::string::npos);
    tt2::SavedGame back;
    ASSERT_TRUE(tt2::parse(text, back));
    CrosswordRound after(com, ful);
    ASSERT_TRUE(restore_from(after, back));
    ASSERT_TRUE(same_cells(after.grid(), before.grid()));
    ASSERT_EQ(after.slots()[1].played, std::string("pant"));
    ASSERT_EQ(after.locked_letter(2, 3), 't');   // tale's last cell holds pant's 't'
    ASSERT_EQ(after.score(), 520);
    ASSERT_EQ(after.round(), 2);
}

TEST(crossword_save_with_a_misfit_placement_parses_but_does_not_restore) {
    // pane one cell to the right: its 'a' would sit on planet's 'n'. The
    // bounds pass, so only restore() can tell; the scene then starts a new
    // game.
    const std::string text =
        "format=re-twist-tt2 savegame\nversion=1\nmode=CROSSWORD\nword=planet\nround=1\n"
        "xword=planet,6,5,h,-1,0,\nxword=pane,9,4,v,1,0,\n";
    tt2::SavedGame g;
    ASSERT_TRUE(tt2::parse(text, g));
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(!restore_from(round, g));
    ASSERT_TRUE(round.slots().empty());
}

// --- C7 trophies (AC-C10) ----------------------------------------------------

TEST(crossword_clean_puzzle_wins_puzzle_pro_and_perfect_puzzler) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(solve_pane_tale_ten(round));

    tt2::PlayerProfile p;
    const auto won = tt2::award_crossword_round(p, flags_of(round));
    ASSERT_EQ(won.size(), std::size_t{2});
    ASSERT_EQ(won[0], static_cast<int>(tt2::TROPHY_PUZZLE_PRO));
    ASSERT_EQ(won[1], static_cast<int>(tt2::TROPHY_PERFECT_PUZZLER));
}

TEST(crossword_rejected_guesses_block_neither_puzzle_trophy) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(code(round.guess("qqqqqq").status), code(Guess::NotAWord));    // popup 0
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(round.select(1));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Accepted));
    ASSERT_TRUE(round.select(2));
    ASSERT_EQ(code(round.guess("pane").status), code(Guess::Duplicate));     // popup 3
    ASSERT_TRUE(solve_pane_tale_ten(round));

    tt2::PlayerProfile p;
    const auto won = tt2::award_crossword_round(p, flags_of(round));
    ASSERT_EQ(won.size(), std::size_t{2});
    ASSERT_EQ(p.trophies[tt2::TROPHY_PUZZLE_PRO], tt2::TROPHY_NEW);
    ASSERT_EQ(p.trophies[tt2::TROPHY_PERFECT_PUZZLER], tt2::TROPHY_NEW);
}

TEST(crossword_one_twist_blocks_puzzle_pro) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_TRUE(round.twist());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(solve_pane_tale_ten(round));

    tt2::PlayerProfile p;
    const auto won = tt2::award_crossword_round(p, flags_of(round));
    ASSERT_EQ(won.size(), std::size_t{1});
    ASSERT_EQ(won[0], static_cast<int>(tt2::TROPHY_PERFECT_PUZZLER));
    ASSERT_EQ(p.trophies[tt2::TROPHY_PUZZLE_PRO], tt2::TROPHY_NOT_WON);
}

TEST(crossword_one_take_back_blocks_perfect_puzzler) {
    auto com = common();
    auto ful = full();
    CrosswordRound round(com, ful);
    ASSERT_TRUE(round.restore(fixed_grid(), NONE_PLAYED, NONE_SCORED, 0, 1));
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(round.take_back());
    ASSERT_EQ(code(round.guess("planet").status), code(Guess::Accepted));
    ASSERT_TRUE(solve_pane_tale_ten(round));

    tt2::PlayerProfile p;
    const auto won = tt2::award_crossword_round(p, flags_of(round));
    ASSERT_EQ(won.size(), std::size_t{1});
    ASSERT_EQ(won[0], static_cast<int>(tt2::TROPHY_PUZZLE_PRO));
    ASSERT_EQ(p.trophies[tt2::TROPHY_PERFECT_PUZZLER], tt2::TROPHY_NOT_WON);
}
