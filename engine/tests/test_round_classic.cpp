// Tests for the classic round (Timed, Untimed, WOTD) — round_classic.hpp.

#include <functional>
#include <map>

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/random.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/word_tree.hpp"

using tt2::Bag;
using tt2::Board;
using tt2::ClassicRound;
using tt2::GuessStatus;
using tt2::Random;
using tt2::WordTree;

namespace {
// A tiny dictionary where every pool entry (5..8 letters) has at
// least 15 sub-anagrams in the same list. Built around the
// "caret"/"alert"/"spare" word families — each rack word is a real
// English word and every sub-anagram is too.
WordTree common() {
    return WordTree::from_words({
        // 5-letter pool entries.
        "caret", "crate", "trace", "react", "carte", "cater", "irate",
        "alert", "alter", "later", "ratel", "artel", "taler",
        "spare", "spear", "parse", "reaps", "spire", "prate", "taper",
        // 6-letter entries.
        "traces", "crates", "reacts", "carpet", "apters", "spirea",
        "spiral", "trails", "artels", "alerts", "alters",
        // 7-8 letter entries.
        "spiracle", "particle", "articles", "sterical", "realists",
        // 3-letter sub-anagrams of the above racks.
        "ace", "act", "arc", "are", "art", "ate", "car", "cat",
        "ear", "eat", "era", "eta", "rat", "sat", "sea", "set",
        "tar", "tea", "ape", "asp", "pas", "pea", "per", "pet",
        "pie", "psi", "rap", "ras", "rep", "rip", "sap", "sip",
        "spa", "alp", "alt", "lar", "lat", "lea", "let", "pal",
        "tae", "tel", "tap", "lit", "rat", "pit", "tip", "tri",
        // 4-letter sub-anagrams.
        "acre", "acts", "arcs", "ares", "arts", "ates", "care", "cats",
        "ears", "ease", "east", "eats", "eras", "race", "rate", "rats",
        "rest", "sear", "seat", "tare", "tars", "tear", "teas", "tsar",
        "alps", "apes", "apse", "leap", "pale", "palp", "pare", "part",
        "pase", "past", "pate", "peal", "pear", "peas", "peat", "pert",
        "pets", "pies", "pipe", "pips", "pire", "pita", "pith", "prat",
        "psis", "rape", "raps", "rase", "rasp", "reap", "reps", "ripe",
        "rips", "spar", "spit", "tape", "taps", "trap", "trip",
    });
}
WordTree full() {
    // A "full" list with everything `common` has plus full-only
    // extras: uncommon sub-anagrams for every rack family in the
    // pool, and "quux" (not spellable from any rack) for the gate
    // tests.
    return WordTree::from_words({
        "caret", "crate", "trace", "react", "carte", "cater", "irate",
        "alert", "alter", "later", "ratel", "artel", "taler",
        "spare", "spear", "parse", "reaps", "spire", "prate", "taper",
        "traces", "crates", "reacts", "carpet", "apters", "spirea",
        "spiral", "trails", "artels", "alerts", "alters",
        "spiracle", "particle", "articles", "sterical", "realists",
        "ace", "act", "arc", "are", "art", "ate", "car", "cat",
        "ear", "eat", "era", "eta", "rat", "sat", "sea", "set",
        "tar", "tea", "ape", "asp", "pas", "pea", "per", "pet",
        "pie", "psi", "rap", "ras", "rep", "rip", "sap", "sip",
        "spa", "alp", "alt", "lar", "lat", "lea", "let", "pal",
        "tae", "tel", "tap", "lit", "rat", "pit", "tip", "tri",
        "acre", "acts", "arcs", "ares", "arts", "ates", "care", "cats",
        "ears", "ease", "east", "eats", "eras", "race", "rate", "rats",
        "rest", "sear", "seat", "tare", "tars", "tear", "teas", "tsar",
        "alps", "apes", "apse", "leap", "pale", "palp", "pare", "part",
        "pase", "past", "pate", "peal", "pear", "peas", "peat", "pert",
        "pets", "pies", "pipe", "pips", "pire", "pita", "pith", "prat",
        "psis", "rape", "raps", "rase", "rasp", "reap", "reps", "ripe",
        "rips", "spar", "spit", "tape", "taps", "trap", "trip",
        // Full-only extras (flagged "uncommon" when they fill a slot).
        "recat",                                    // caret/crate/carpet/...
        "tale", "real",                             // alert/alter/...
        "pares", "pears", "stare",                  // spare/apters/...
        "caper",                                    // carpet/particle/...
        "praise",                                   // spirea
        "pails", "rails", "lair", "stair",          // spiral/trails/...
        "quux",                                     // not spellable from any rack
    });
}
}  // namespace

TEST(classic_round_starts_with_rack) {
    // `Bag` and `ClassicRound` store non-owning pointers into the
    // trees they were given — the trees must outlive the round, so
    // we keep them in local variables (not temporaries).
    auto com = common();
    auto ful = full();
    Random rng(0);
    Bag bag(com);
    ClassicRound r(com, ful, ClassicRound::Config{});
    const auto res = r.start(rng, bag, 0);
    ASSERT_TRUE(!res.rack.empty());
    ASSERT_TRUE(!res.board_words.empty());
}

TEST(classic_round_bingo_qualifies) {
    auto com = common();
    auto ful = full();
    Random rng(0);
    Bag bag(com);
    ClassicRound r(com, ful, ClassicRound::Config{});
    const auto res = r.start(rng, bag, 0);
    const auto rep = r.guess(res.rack);
    ASSERT_EQ(static_cast<int>(rep.status),
              static_cast<int>(GuessStatus::Accepted));
    ASSERT_TRUE(rep.bingo);
    ASSERT_TRUE(r.qualified());
}

TEST(classic_round_exposes_board_and_completion) {
    auto com = common();
    auto ful = full();
    Random rng(0);
    Bag bag(com);
    ClassicRound r(com, ful, ClassicRound::Config{});
    ASSERT_TRUE(r.board() == nullptr);          // nothing before start
    ASSERT_EQ(r.slot_count(), std::size_t{0});
    ASSERT_TRUE(!r.complete());
    const auto res = r.start(rng, bag, 0);
    ASSERT_TRUE(r.board() != nullptr);
    ASSERT_EQ(r.slot_count(), res.slot_count);
    ASSERT_EQ(r.board()->flat_slots().size(), res.slot_count);
    ASSERT_TRUE(!r.complete());
    (void)r.guess(res.board_words[0]);
    ASSERT_TRUE(r.board()->is_filled(0));
    for (std::size_t i = 1; i < res.board_words.size(); ++i) (void)r.guess(res.board_words[i]);
    ASSERT_TRUE(r.complete());
}

TEST(classic_round_duplicate_after_accept) {
    auto com = common();
    auto ful = full();
    Random rng(0);
    Bag bag(com);
    ClassicRound r(com, ful, ClassicRound::Config{});
    const auto res = r.start(rng, bag, 0);
    (void)r.guess(res.rack);
    const auto rep = r.guess(res.rack);
    ASSERT_EQ(static_cast<int>(rep.status),
              static_cast<int>(GuessStatus::Duplicate));
}

TEST(classic_round_uncommon_guess_fills_a_slot) {
    auto com = common();
    auto ful = full();
    ClassicRound r(com, ful, ClassicRound::Config{});
    // A fixed rack: the bag's order depends on its seed. "alert" has the
    // full-only "tale" / "real" and 4-letter slots for them.
    const auto res = r.start_with_rack("alert");
    // Pick a full-only word spellable from the rack whose length has
    // slots on the board.
    std::string extra;
    // Name the board: `Board::build(...).all()` in the range-for would
    // reference a temporary that dies before the loop body runs (C++17).
    const Board board = Board::build(res.rack, ful);
    for (const auto& w : board.all()) {
        if (w.size() < 3 || w.size() > 8 || com.contains(w)) continue;
        for (const auto& b : res.board_words) {
            if (b.size() == w.size()) { extra = w; break; }
        }
        if (!extra.empty()) break;
    }
    ASSERT_TRUE(!extra.empty());
    const auto rep = r.guess(extra);
    ASSERT_EQ(static_cast<int>(rep.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_TRUE(rep.slot_index != static_cast<std::size_t>(-1));
    ASSERT_EQ(rep.placed_word, extra);
    ASSERT_EQ(r.found(), std::size_t(1));
    ASSERT_EQ(r.score(),
              10 * static_cast<int>(extra.size()) * static_cast<int>(extra.size()));
    // Re-guessing the placed word is a duplicate and scores nothing.
    const auto again = r.guess(extra);
    ASSERT_EQ(static_cast<int>(again.status),
              static_cast<int>(GuessStatus::Duplicate));
    ASSERT_EQ(r.found(), std::size_t(1));
    ASSERT_EQ(r.score(),
              10 * static_cast<int>(extra.size()) * static_cast<int>(extra.size()));
}

TEST(classic_round_guess_gate_rejects_foreign_words) {
    auto com = common();
    auto ful = full();
    Random rng(0);
    Bag bag(com);
    ClassicRound r(com, ful, ClassicRound::Config{});
    (void)r.start(rng, bag, 0);
    // "quux" is in the full list but no rack in the pool can spell it.
    ASSERT_EQ(static_cast<int>(r.guess("quux").status),
              static_cast<int>(GuessStatus::NotAWord));
    ASSERT_EQ(r.found(), std::size_t(0));
    ASSERT_EQ(r.score(), 0);
}

TEST(classic_wotd_caps_per_length_at_5) {
    // Build a common list with > 5 sub-anagrams per length for some
    // rack, and confirm WOTD caps each bucket at 5.
    std::vector<std::string> words;
    for (int l = 3; l <= 8; ++l) {
        std::function<void(std::string&, int, int)> go =
            [&](std::string& w, int depth, int start) {
                if (depth == l) {
                    words.push_back(w);
                    return;
                }
                for (int k = start; k < 10; ++k) {
                    w.push_back("abcdefghij"[k]);
                    go(w, depth + 1, k);
                    w.pop_back();
                }
            };
        std::string w;
        go(w, 0, 0);
    }
    auto com = WordTree::from_words(words);
    ClassicRound r(com, com, ClassicRound::Config{false, true, 8, 0, 5});
    Random rng(0);
    Bag bag(com);
    const auto res = r.start(rng, bag, 0);
    std::map<std::size_t, std::size_t> counts;
    for (const auto& w : res.board_words) ++counts[w.size()];
    for (const auto& [len, c] : counts) {
        ASSERT_TRUE(c <= 5);
    }
}

// ---- saved-game restore (0x42a460) ---------------------------------------

TEST(classic_round_starts_from_a_given_rack) {
    auto com = common();
    auto ful = full();
    ClassicRound r(com, ful, ClassicRound::Config{});
    const auto res = r.start_with_rack("caret");
    ASSERT_EQ(res.rack, std::string("caret"));
    ASSERT_TRUE(!res.board_words.empty());
    ASSERT_EQ(r.rack(), std::string("caret"));
    ASSERT_EQ(r.score(), 0);
    ASSERT_TRUE(!r.qualified());
    // The board is the same one a bag pick of the same word would give.
    Board b = Board::build("caret", com, 0);
    ASSERT_EQ(res.slot_count, b.all().size());
}

TEST(classic_round_restore_places_words_without_scoring) {
    auto com = common();
    auto ful = full();
    ClassicRound r(com, ful, ClassicRound::Config{});
    r.start_with_rack("caret");
    const auto a = r.restore_guess("care");
    ASSERT_TRUE(a.status == GuessStatus::Accepted);
    ASSERT_EQ(a.points_awarded, 0);
    ASSERT_TRUE(!a.bingo);
    ASSERT_EQ(a.placed_word, std::string("care"));
    ASSERT_EQ(r.found(), std::size_t{1});
    ASSERT_EQ(r.score(), 0);
    ASSERT_TRUE(!r.qualified());                        // only `restore` sets it
    // A restored bingo does not qualify by itself either.
    const auto b = r.restore_guess("caret");
    ASSERT_TRUE(b.status == GuessStatus::Accepted);
    ASSERT_TRUE(!r.qualified());
    // Duplicates and foreign words are reported, not placed.
    ASSERT_TRUE(r.restore_guess("care").status == GuessStatus::Duplicate);
    ASSERT_TRUE(r.restore_guess("quux").status == GuessStatus::NotAWord);
    ASSERT_EQ(r.found(), std::size_t{2});
    // The counters come back from the file.
    ClassicRound::Config timed;
    timed.timed = true;
    timed.round_ms = 120000;
    ClassicRound t(com, ful, timed);
    t.start_with_rack("caret");
    t.restore(1250, true, 85000);
    ASSERT_EQ(t.score(), 1250);
    ASSERT_TRUE(t.qualified());
    ASSERT_EQ(t.time_left_ms(), std::uint32_t{85000});
    r.restore(40, false, 85000);
    ASSERT_EQ(r.time_left_ms(), std::uint32_t{0});       // untimed: the clock stays at 0
    ASSERT_TRUE(r.restore_guess("x").status == GuessStatus::NotAWord);
    ClassicRound fresh(com, ful, ClassicRound::Config{});
    ASSERT_TRUE(fresh.restore_guess("care").status == GuessStatus::NotAWord);   // before start
}
