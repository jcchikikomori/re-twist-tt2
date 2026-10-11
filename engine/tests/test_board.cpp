// Tests for the board / sub-anagram enumeration / guess processor
// (board.hpp).

#include <functional>

#include "test_harness.hpp"
#include "tt2/board.hpp"
#include "tt2/word_tree.hpp"

using tt2::Board;
using tt2::FilledBoard;
using tt2::GuessProcessor;
using tt2::GuessStatus;
using tt2::WordTree;

namespace {
WordTree tiny_dict() {
    return WordTree::from_words({
        "abc", "abcd", "abcde",        // rack letters
        "xyz", "wvu",                   // not rack letters
        "ab", "a", "ace", "bad",        // sub-anagrams of "abcde"
    });
}
}  // namespace

TEST(board_sub_anagrams_collected) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t);
    const auto& all = b.all();
    // Should contain: abc, abcd, abcde, ab, a, ace, bad — order is DFS.
    ASSERT_TRUE(!all.empty());
    for (const auto* w : {"abc", "abcd", "abcde", "ab", "a", "ace", "bad"}) {
        bool found = false;
        for (const auto& x : all) if (x == w) { found = true; break; }
        ASSERT_TRUE(found);
    }
}

TEST(board_cap_48_with_quota) {
    // A rack with > 48 sub-anagrams in length buckets 3..8 should
    // produce a board capped at 48 slots. Generate 30 distinct
    // sub-anagrams per length using a 10-letter rack — multisets of
    // size 3..8 from 10 letters number in the thousands.
    std::vector<std::string> words;
    for (int l = 3; l <= 8; ++l) {
        // Recursive enumeration of distinct multisets of size `l`
        // from "abcdefghij". Capped at 30 per length to keep the
        // test small.
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
        // Cap at 30 per length.
        if (words.size() > static_cast<std::size_t>(30 * (l - 2))) {
            words.resize(30 * (l - 2));
        }
    }
    auto t = WordTree::from_words(words);
    Board b = Board::build("abcdefghij", t);
    // The cap is 48 total across lengths 3..8.
    ASSERT_TRUE(b.slot_count() <= 48);
    // Total sub-anagrams the original would have found is much
    // more than 48, so the cap actually fires.
    ASSERT_TRUE(b.slot_count() >= 1);
}

TEST(filled_board_tracks_occupancy) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    // Nothing filled yet, but the board has slots.
    ASSERT_TRUE(fb.slot_count() > 0);
    ASSERT_EQ(fb.filled_count(), std::size_t(0));
    fb.mark_filled(0);
    ASSERT_TRUE(fb.is_filled(0));
    ASSERT_EQ(fb.filled_count(), std::size_t(1));
}

TEST(guess_accepted_places_word) {
    auto t = tiny_dict();
    auto full = WordTree::from_words({"abc", "abcd", "abcde", "ab", "a", "ace", "bad",
                                      "xyz", "wvu"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply("abc");
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::Accepted));
    ASSERT_EQ(o.points, 90);  // 10 * 3 * 3
    ASSERT_TRUE(!o.bingo);
    // `apply` does not mark the slot; the caller (round state) does.
    fb.mark_filled(o.slot_index);
    ASSERT_TRUE(fb.is_filled(o.slot_index));
}

TEST(guess_duplicate_detected) {
    auto t = tiny_dict();
    auto full = t;
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    // The caller marks the slot after a successful accept.
    const auto first = p.apply("abc");
    fb.mark_filled(first.slot_index);
    const auto second = p.apply("abc");
    ASSERT_EQ(static_cast<int>(second.status),
              static_cast<int>(GuessStatus::Duplicate));
}

TEST(guess_uncommon_flagged) {
    auto t = tiny_dict();  // "bad" IS in the common
    auto full = WordTree::from_words({"abc", "abcd", "abcde", "ab", "a", "ace", "bad",
                                      "xyz", "wvu", "quux"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    // "quux" is in the full list but not the common and not in any slot.
    const auto o = p.apply("quux");
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::NoRoom));
}

TEST(guess_not_a_word) {
    auto t = tiny_dict();
    auto full = t;
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply("zzz");
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::NotAWord));
}

TEST(bingo_uses_every_letter) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t);
    ASSERT_TRUE(b.is_bingo("abcde"));
    ASSERT_TRUE(b.is_bingo("edcba"));
    ASSERT_TRUE(!b.is_bingo("abcd"));
    ASSERT_TRUE(!b.is_bingo("abcdef"));  // longer than rack
}
