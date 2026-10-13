// Tests for the crossword round (round_crossword.hpp) and the MSVC CRT
// generator it depends on (random.hpp).

#include <set>

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/round_crossword.hpp"
#include "tt2/word_tree.hpp"

using tt2::Bag;
using tt2::BagState;
using tt2::CrosswordGrid;
using tt2::CrosswordRound;
using tt2::CrtRandom;
using tt2::GuessStatus;
using tt2::Random;
using tt2::WordPlacement;
using tt2::WordTree;

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
WordTree full() {
    auto words = common().words();
    words.push_back("zebra");
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
    ASSERT_EQ(round.score(), 0);

    // Not in the full list.
    ASSERT_EQ(static_cast<int>(round.guess("qqq").status), static_cast<int>(GuessStatus::NotAWord));
    // In the full list, not on the grid.
    ASSERT_EQ(static_cast<int>(round.guess("zebra").status), static_cast<int>(GuessStatus::NoRoom));

    // Fill every word; the last one carries the +1000 bonus.
    int expected = 0;
    for (std::size_t i = 0; i < r.placements.size(); ++i) {
        const std::string& w = r.placements[i].word;
        auto g = round.guess(w);
        ASSERT_EQ(static_cast<int>(g.status), static_cast<int>(GuessStatus::Accepted));
        ASSERT_EQ(g.placement_index, i);
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
    ASSERT_EQ(round.filled_count(), r.placements.size());

    // A repeated word is a duplicate and scores nothing more.
    auto d = round.guess(r.placements[1].word);
    ASSERT_EQ(static_cast<int>(d.status), static_cast<int>(GuessStatus::Duplicate));
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
    ASSERT_EQ(static_cast<int>(round.guess(upper).status), static_cast<int>(GuessStatus::Accepted));
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
