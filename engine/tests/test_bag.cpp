// Tests for the bag / rack-picker (bag.hpp).

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/word_tree.hpp"

using tt2::Bag;
using tt2::BagState;
using tt2::Random;
using tt2::WordTree;

namespace {
WordTree small_dict() {
    // Every 5..8 letter word made from {a, b, c, d, e}.
    std::vector<std::string> words;
    auto expand = [&](auto self, const std::string& prefix,
                      int depth) -> void {
        if (depth == 0) {
            if (prefix.size() >= 5) words.push_back(prefix);
            return;
        }
        for (char c : {'a', 'b', 'c', 'd', 'e'}) {
            // Avoid immediate repeats for variety.
            if (!prefix.empty() && prefix.back() == c) continue;
            self(self, prefix + c, depth - 1);
        }
    };
    expand(expand, "", 8);
    return WordTree::from_words(words);
}
}  // namespace

TEST(bag_pool_is_5_to_8_letter_words) {
    auto t = small_dict();
    Bag b(t);
    for (const auto& w : b.pool()) {
        ASSERT_TRUE(w.size() >= 5 && w.size() <= 8);
    }
    ASSERT_TRUE(!b.pool().empty());
}

TEST(bag_state_round_trip) {
    auto t = small_dict();
    Bag b(t, BagState{42, 7});
    ASSERT_EQ(b.state().seed,  std::uint32_t(42));
    ASSERT_EQ(b.state().index, std::uint32_t(7));
}

TEST(classic_target_length_in_range) {
    Random r(0);
    for (int i = 0; i < 100; ++i) {
        const std::size_t L = Bag::classic_target_length(r, 8);
        ASSERT_TRUE(L >= 5 && L <= 8);
    }
}

TEST(word_of_the_day_is_deterministic) {
    auto t = small_dict();
    const auto a = Bag::word_of_the_day(t, 1'700'000'000'000LL);
    const auto b = Bag::word_of_the_day(t, 1'700'000'000'000LL);
    ASSERT_EQ(a, b);
    // Different day → may differ.
    const auto c = Bag::word_of_the_day(t, 1'700'000'000'000LL + 86'400'000LL);
    (void)c;  // No assertion; just shouldn't crash.
}

namespace {
// Word of the Day fixture: eight pool words, three of them ("quack",
// "fjord", "nymph") with no sub-anagram but themselves. Shuffled with
// seed 1337 the pool reads
//   0 crate, 1 caret, 2 quack, 3 nymph, 4 fjord, 5 cater, 6 react, 7 trace.
WordTree wotd_dict() {
    return WordTree::from_words({
        "caret", "crate", "trace", "react", "cater", "quack", "fjord", "nymph",
        "ace", "act", "arc", "are", "art", "ate", "car", "cat", "ear", "eat",
        "era", "eta", "rat", "tar", "tea", "tae",
        "acre", "care", "race", "rate", "tare", "tear", "cart",
    });
}
}  // namespace

// AC-W3 (D-14): pool[day mod n] of the 1337-shuffled pool, no walk.
TEST(word_of_the_day_for_day_reads_the_shuffled_pool) {
    auto t = wotd_dict();
    std::vector<std::string> deck = Bag::make_pool(t);
    Random(tt2::WOTD_SEED).shuffle(deck);
    ASSERT_TRUE(deck == (std::vector<std::string>{"crate", "caret", "quack", "nymph",
                                                  "fjord", "cater", "react", "trace"}));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 0), std::string("crate"));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 1), std::string("caret"));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 7), std::string("trace"));
}

TEST(word_of_the_day_for_day_next_day_is_the_next_entry) {
    auto t = wotd_dict();
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 5), std::string("cater"));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 6), std::string("react"));
    ASSERT_NE(Bag::word_of_the_day_for_day(t, 5), Bag::word_of_the_day_for_day(t, 6));
}

TEST(word_of_the_day_for_day_does_not_skip_unplayable_words) {
    // Day 2 is "quack", which has 1 sub-anagram (< 15). The old
    // walk-forward would have skipped quack, nymph and fjord and
    // returned "cater"; the exe (0x43d690) takes pool[day] as it is.
    auto t = wotd_dict();
    Bag b(t);
    ASSERT_TRUE(!b.is_playable("quack"));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 2), std::string("quack"));
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 3), std::string("nymph"));
}

TEST(word_of_the_day_for_day_wraps_out_of_range_days) {
    // Deviation D4: the port wraps where the exe would read out of range.
    auto t = wotd_dict();
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 8), std::string("crate"));       // day == n
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, 6623), std::string("trace"));    // 6623 % 8 = 7
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, -1), std::string("trace"));      // before 2008-09-03
    ASSERT_EQ(Bag::word_of_the_day_for_day(t, -14125), std::string("nymph"));  // 1970-01-01: -14125 mod 8 = 3
}

TEST(word_of_the_day_for_day_empty_pool_throws) {
    // No 5..8 letter word at all.
    auto t = WordTree::from_words({"ace", "act", "care", "race"});
    bool threw = false;
    try {
        (void)Bag::word_of_the_day_for_day(t, 0);
    } catch (const tt2::NoRackError&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

TEST(word_of_the_day_uses_the_utc_word_day) {
    // 2026-10-22 23:30 UTC is word day 6623; one ms before midnight
    // UTC is still 6623, midnight is 6624.
    auto t = wotd_dict();
    ASSERT_EQ(Bag::word_of_the_day(t, 1792711800LL * 1000), std::string("trace"));
    ASSERT_EQ(Bag::word_of_the_day(t, 1792713600LL * 1000 - 1), std::string("trace"));
    ASSERT_EQ(Bag::word_of_the_day(t, 1792713600LL * 1000), std::string("crate"));
}

TEST(pick_classic_skips_longer_words) {
    // A pool of 5-letter words whose sub-anagrams live in the same
    // list, so the bag's "≥15 sub-anagrams" filter never disqualifies
    // them. The original game's dictionary is 25 004 words; for tests
    // we just need a small slice where every pool entry is "playable".
    auto t = WordTree::from_words({
        // The "caret" family: 3-letter a/e/r/c/t combos and their
        // longer variants.
        "caret", "crate", "trace", "react", "carte", "cater", "irate",
        "alert", "alter", "later", "ratel", "artel", "taler",
        "spare", "spear", "parse", "reaps", "spire", "prate",
        // 3-letter sub-anagrams shared by all of the above.
        "ace", "act", "arc", "are", "art", "ate", "car", "cat",
        "ear", "eat", "era", "rat", "sat", "sea", "set", "tar",
        "tea", "ape", "asp", "pas", "pea", "per", "pet", "pie",
        "psi", "rap", "ras", "rep", "rip", "sap", "sip", "spa",
        "alp", "alt", "ape", "are", "art", "ate", "eat", "era",
        "eta", "lar", "lat", "lea", "let", "pal", "pea", "pet",
        "rat", "tae", "tar", "tea", "tel",
    });
    Bag b(t, BagState{1, 0});
    Random r(0);
    for (int i = 0; i < 20; ++i) {
        const std::string w = b.pick_classic(5, r);
        ASSERT_EQ(w.size(), std::size_t(5));
    }
}

TEST(next_word_walks_the_shuffled_deck) {
    // 0x43d550: the pool shuffled with the seed, read at INDEX.
    auto t = small_dict();
    Bag b(t, BagState{7, 0});
    std::vector<std::string> deck = b.pool();
    Random(7).shuffle(deck);
    std::vector<std::string> expected;
    for (const auto& w : deck) {
        if (b.is_playable(w)) expected.push_back(w);
        if (expected.size() == 10) break;
    }
    for (const auto& w : expected) ASSERT_EQ(b.next_word(), w);
    // Not the alphabetical pool order (the bug), and another seed differs.
    Bag sorted(t, BagState{7, 0});
    std::vector<std::string> in_pool_order;
    for (const auto& w : b.pool()) {
        if (sorted.is_playable(w)) in_pool_order.push_back(w);
        if (in_pool_order.size() == 10) break;
    }
    ASSERT_TRUE(in_pool_order != expected);
    Bag other(t, BagState{8, 0});
    std::vector<std::string> seq;
    for (int i = 0; i < 10; ++i) seq.push_back(other.next_word());
    ASSERT_TRUE(seq != expected);
}

TEST(pick_classic_longer_words_do_not_use_up_attempts) {
    // ~98% of this deck is 8 letters, so a 5 sits behind long runs of
    // longer words; the free skip still reaches it every time.
    auto words = small_dict().words();
    std::vector<std::string> fives;
    auto expand = [&](auto self, const std::string& prefix, int depth) -> void {
        if (depth == 0) { fives.push_back(prefix); return; }
        for (char c : {'a', 'b', 'c', 'd', 'e'})
            if (prefix.empty() || prefix.back() != c) self(self, prefix + c, depth - 1);
    };
    expand(expand, "", 5);
    words.insert(words.end(), fives.begin(), fives.end());
    auto t = WordTree::from_words(words);
    Bag b(t, BagState{3, 0});
    Random r(0);
    for (int i = 0; i < 20; ++i) ASSERT_EQ(b.pick_classic(5, r).size(), std::size_t(5));
}

TEST(pick_classic_without_a_short_enough_word_still_returns) {
    // Port guard: small_dict() has only 8-letter words, so after a whole
    // deck of skips the next word is taken as it is.
    auto t = small_dict();
    Bag b(t, BagState{1, 0});
    Random r(0);
    ASSERT_EQ(b.pick_classic(5, r).size(), std::size_t(8));
}
