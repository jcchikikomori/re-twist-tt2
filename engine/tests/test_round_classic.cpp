// Tests for the classic round (Timed, Untimed, WOTD) — round_classic.hpp.

#include <functional>
#include <map>

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/word_tree.hpp"

using tt2::Bag;
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
    // A "full" list with everything `common` has plus a few extras
    // (used by Uncommon-flagging tests, etc.).
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
        // Full-only extras (would be flagged "uncommon").
        "quux", "zany", "zebra",
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
