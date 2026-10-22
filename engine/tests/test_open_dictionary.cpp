// The shipped open dictionaries against their sources in data-src/
// (docs/dictionary-sources.md, scripts/build-open-dictionary.sh):
// open-common.tree is common.words minus blocklist.words, so a blocked word
// is never a rack, an answer slot or a Crossword root; open-full.tree keeps
// every blocked word, so typing one still counts as a guess.
#include "test_harness.hpp"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/word_tree.hpp"

namespace {

const std::string DATA_DIR = TT2_SOURCE_DATA_DIR;
const std::string DATA_SRC_DIR = std::string(TT2_SOURCE_DATA_DIR) + "/../data-src";

std::string read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// One word per line; blank lines and `#` comment lines are skipped.
std::vector<std::string> read_word_list(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<std::string> words;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        words.push_back(line);
    }
    return words;
}

const std::vector<std::string>& blocklist() {
    static const std::vector<std::string> words = read_word_list(DATA_SRC_DIR + "/blocklist.words");
    return words;
}

const std::vector<std::string>& common_words() {
    static const std::vector<std::string> words = read_word_list(DATA_SRC_DIR + "/common.words");
    return words;
}

const tt2::WordTree& open_common() {
    static const tt2::WordTree tree = tt2::WordTree::parse(read_bytes(DATA_DIR + "/open-common.tree"));
    return tree;
}

const tt2::WordTree& open_full() {
    static const tt2::WordTree tree = tt2::WordTree::parse(read_bytes(DATA_DIR + "/open-full.tree"));
    return tree;
}

bool is_lowercase_word(const std::string& word) {
    if (word.empty()) return false;
    for (char c : word)
        if (c < 'a' || c > 'z') return false;
    return true;
}

}  // namespace

TEST(blocklist_is_sorted_unique_lowercase_words) {
    const auto& words = blocklist();
    ASSERT_TRUE(!words.empty());
    std::string bad;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (!is_lowercase_word(words[i])) bad += "[" + words[i] + "] not a-z ";
        if (i > 0 && !(words[i - 1] < words[i])) bad += "[" + words[i] + "] out of order ";
    }
    ASSERT_EQ(bad, std::string());
}

TEST(blocklisted_words_are_not_in_open_common) {
    std::string found;
    for (const auto& word : blocklist())
        if (open_common().contains(word)) found += word + " ";
    ASSERT_EQ(found, std::string());
}

TEST(blocklisted_words_stay_in_open_full) {
    std::string missing;
    for (const auto& word : blocklist())
        if (!open_full().contains(word)) missing += word + " ";
    ASSERT_EQ(missing, std::string());
}

TEST(blocklisted_words_all_come_from_common_words) {
    // A word that isn't in common.words filters nothing: a typo or a dead entry.
    const std::set<std::string> common(common_words().begin(), common_words().end());
    std::string dead;
    for (const auto& word : blocklist())
        if (common.count(word) == 0) dead += word + " ";
    ASSERT_EQ(dead, std::string());
}

TEST(open_common_is_common_words_minus_blocklist) {
    // Fails when someone edits a source list and forgets to rerun
    // scripts/build-open-dictionary.sh.
    const std::set<std::string> blocked(blocklist().begin(), blocklist().end());
    std::vector<std::string> want;
    for (const auto& word : common_words())
        if (blocked.count(word) == 0) want.push_back(word);
    const std::vector<std::string> got = open_common().words();
    ASSERT_EQ(got.size(), want.size());
    ASSERT_TRUE(got == want);
    ASSERT_EQ(common_words().size(), std::size_t{22460});   // the raw list keeps the blocked words
    ASSERT_EQ(got.size(), std::size_t{22360});              // 22,460 - 100 blocked
}

TEST(blocklist_matches_whole_words_only) {
    // Words that contain a blocked word (spic, coon, niggers, fag, pussy,
    // cum, jew) are ordinary words and stay; so do "ass" and "cock", whose
    // rude sense is the lesser one.
    std::string missing;
    for (const char* word : {"spice", "raccoon", "sniggers", "fagin", "pussycat", "cumin", "jewel", "ass", "cock"})
        if (!open_common().contains(word)) missing += std::string(word) + " ";
    ASSERT_EQ(missing, std::string());
}

TEST(open_common_still_has_enough_anagram_hubs) {
    // A hub is a 5-8 letter rack candidate with at least MIN_SUB_ANAGRAMS
    // (15) common sub-anagrams, the test the Classic bag applies.
    // docs/dictionary-sources.md states both counts; the floor is 200.
    const auto pool = tt2::Bag::make_pool(open_common());
    std::size_t hubs = 0;
    for (const auto& word : pool)
        if (open_common().sub_anagrams(word).size() >= tt2::MIN_SUB_ANAGRAMS) ++hubs;
    ASSERT_EQ(pool.size(), std::size_t{19181});   // 19,256 before the blocklist
    ASSERT_EQ(hubs, std::size_t{15233});          // 15,292 before the blocklist
    ASSERT_TRUE(hubs >= 200);
}
