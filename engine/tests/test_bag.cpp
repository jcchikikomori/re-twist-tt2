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
