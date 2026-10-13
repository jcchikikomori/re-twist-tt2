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
    // A tree containing just "a": the root lists its child letters ("a")
    // and its own flag ('0': "" is not a word); then the child node
    // follows: no letters, flag '1'. Encoded "a" "0" "1" = "a01".
    auto t = WordTree::parse(std::string_view("a01"));
    ASSERT_EQ(t.size(), std::size_t(1));
    ASSERT_TRUE(t.contains("a"));
    ASSERT_TRUE(!t.contains("aa"));
    ASSERT_EQ(t.serialize(), std::string("a01"));
    // The old (wrong) reading "letter, subtree, flag" must NOT parse
    // "a10" as {"a"}: here the root itself is flagged as a word.
    auto t2 = WordTree::parse(std::string_view("a10"));
    ASSERT_TRUE(!t2.contains("a"));
    // Unsorted / duplicate child letters are rejected.
    ASSERT_THROWS(WordTree::parse(std::string_view("ba011")));
    ASSERT_THROWS(WordTree::parse(std::string_view("aa011")));
}

TEST(tree_parse_siblings_come_after_the_flag) {
    // {"ab", "ac"}: root "a" + '0'; node a: "bc" + '0'; then b: '1', c: '1'.
    auto t = WordTree::parse(std::string_view("a0bc011"));
    ASSERT_EQ(t.size(), std::size_t(2));
    ASSERT_TRUE(t.contains("ab"));
    ASSERT_TRUE(t.contains("ac"));
    ASSERT_TRUE(!t.contains("a"));
    ASSERT_TRUE(!t.contains("abc"));
    ASSERT_EQ(t.serialize(), std::string("a0bc011"));
    auto w = t.words();
    ASSERT_EQ(w.size(), std::size_t(2));
    ASSERT_EQ(w[0], std::string("ab"));
    ASSERT_EQ(w[1], std::string("ac"));
    ASSERT_EQ(WordTree::from_words({"ac", "ab"}).serialize(), std::string("a0bc011"));
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
    // A valid tree containing exactly one word, "aardvark". Every node
    // on the path lists its single child letter and the flag '0', and
    // the last node is just '1':
    //
    //   root : "a0"   a : "a0"   a : "r0"   r : "d0"   d : "v0"
    //   v    : "a0"   a : "r0"   r : "k0"   k : "1"
    //
    // Flattened: "a0a0r0d0v0a0r0k01" (17 bytes).
    const std::string snippet = "a0a0r0d0v0a0r0k01";
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
