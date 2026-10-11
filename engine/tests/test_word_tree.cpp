// Tests for the .tree parser/serializer (word_tree.hpp).

#include "test_harness.hpp"
#include "tt2/word_tree.hpp"

using tt2::WordTree;
using tt2::TreeFormatError;

TEST(tree_parse_empty_file) {
    // The empty root is `0` (no children, not a word).
    ASSERT_THROWS(WordTree::parse(std::string_view("")));
}

TEST(tree_parse_single_word) {
    // A tree containing just "a": root has child 'a' (end), then
    // root's flag is '0' (root itself is not a word). Encoded as
    // "a" + (subtree: empty, end '1') + "0" = "a10".
    auto t = WordTree::parse(std::string_view("a10"));
    ASSERT_EQ(t.size(), std::size_t(1));
    ASSERT_TRUE(t.contains("a"));
    ASSERT_TRUE(!t.contains("aa"));
}

TEST(tree_round_trip_canonical) {
    // Build from words, encode, decode, encode — should be byte-identical.
    auto t1 = WordTree::from_words({"apple", "banana", "cherry", "date"});
    const auto s1 = t1.serialize();
    auto t2 = WordTree::parse(s1);
    const auto s2 = t2.serialize();
    ASSERT_EQ(s1, s2);
    ASSERT_EQ(t1.size(), t2.size());
    for (const auto& w : {"apple", "banana", "cherry", "date"}) {
        ASSERT_TRUE(t1.contains(w));
        ASSERT_TRUE(t2.contains(w));
    }
}

TEST(tree_round_trip_matches_original_format) {
    // A valid tree containing exactly one word, "aardvark". The
    // encoding is a recursive pre-order serialized trie:
    //
    //   root   : "a" + (a-subtree) + "0" (root is not a word)
    //   a-sub  : "a" + (a-sub)    + "0"
    //   a-sub  : "r" + (r-sub)    + "0"
    //   r-sub  : "d" + (d-sub)    + "0"
    //   d-sub  : "v" + (v-sub)    + "0"
    //   v-sub  : "a" + (a-sub)    + "0"
    //   a-sub  : "r" + (r-sub)    + "0"
    //   r-sub  : "k" + (k-sub)    + "0"
    //   k-sub  : "1" (end, no kids)
    //
    // Flattened: "aardvark100000000" (17 bytes).
    const std::string snippet = "aardvark100000000";
    auto t = WordTree::parse(std::string_view(snippet));
    ASSERT_EQ(t.size(), std::size_t(1));
    ASSERT_TRUE(t.contains("aardvark"));
    ASSERT_TRUE(!t.contains("aardva"));
    ASSERT_EQ(t.serialize(), snippet);
}

TEST(tree_sub_anagrams_skips_duplicate_letters) {
    // "aabc" — should yield "a", "aa", "aab", "aabc", "ab", "abc", "ac", "b", "bc", "c".
    // "aab" should appear once, not three times.
    auto t = WordTree::from_words({"a", "aa", "aab", "aabc", "ab",
                                   "abc", "ac", "b", "bc", "c"});
    auto subs = t.sub_anagrams("aabc");
    int count_aab = 0;
    for (const auto& w : subs) if (w == "aab") ++count_aab;
    ASSERT_EQ(count_aab, 1);
    // The DFS order has "aab" before "ab" (a-b comes before a-c... no,
    // we iterate remaining in order). The point is no duplicates.
    ASSERT_TRUE(!subs.empty());
}

TEST(tree_is_tree_word_validation) {
    ASSERT_TRUE(WordTree::is_tree_word("hello"));
    ASSERT_TRUE(!WordTree::is_tree_word(""));
    ASSERT_TRUE(!WordTree::is_tree_word("Hello"));   // uppercase
    ASSERT_TRUE(!WordTree::is_tree_word("he llo"));  // space
    ASSERT_TRUE(!WordTree::is_tree_word("héllo"));   // accent
}

TEST(tree_from_words_rejects_invalid) {
    std::vector<std::string> bad = {"hello", "World"};
    ASSERT_THROWS(WordTree::from_words(bad));
}
