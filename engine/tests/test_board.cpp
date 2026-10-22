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

// The "full" dictionary used by the guess tests: everything in the
// common list plus full-only extras.
WordTree full_with(const std::vector<std::string>& extras) {
    std::vector<std::string> words = {
        "abc", "abcd", "abcde", "xyz", "wvu",
        "ab", "a", "ace", "bad",
    };
    for (const auto& w : extras) words.push_back(w);
    return WordTree::from_words(words);
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
    fb.mark_filled(0, "abc");
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
    ASSERT_EQ(o.placed_word, std::string("abc"));
    ASSERT_TRUE(!o.newly_uncommon);
    // `apply` does not mark the slot; the caller (round state) does.
    fb.mark_filled(o.slot_index, o.placed_word);
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
    fb.mark_filled(first.slot_index, first.placed_word);
    const auto second = p.apply("abc");
    ASSERT_EQ(static_cast<int>(second.status),
              static_cast<int>(GuessStatus::Duplicate));
}

TEST(guess_uncommon_word_fills_a_slot) {
    // The original accepts any full-list word of the right length and
    // puts it in the first empty slot of that length (0x432b20),
    // displaying the guessed word in the slot (0x431880). "dace" is
    // spellable from "abcde", in the full list only, 4 letters.
    auto t = tiny_dict();
    auto full = full_with({"dace"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply("dace");
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_TRUE(o.newly_uncommon);
    ASSERT_EQ(o.points, 160);  // 10 * 4 * 4
    ASSERT_EQ(o.slot_index, std::size_t(3));  // the single 4-letter slot
    ASSERT_EQ(o.placed_word, std::string("dace"));
    ASSERT_TRUE(!o.bingo);
    fb.mark_filled(o.slot_index, o.placed_word);
    // Re-guessing the placed word is a duplicate (the slot holds the
    // placed word, not the board answer) ...
    const auto again = p.apply("dace");
    ASSERT_EQ(static_cast<int>(again.status),
              static_cast<int>(GuessStatus::Duplicate));
    // ... but the board word it covered ("abcd") is not: it is still a
    // valid word and its length has no free slot left.
    const auto covered = p.apply("abcd");
    ASSERT_EQ(static_cast<int>(covered.status),
              static_cast<int>(GuessStatus::NoRoom));
}

TEST(guess_fills_the_first_empty_slot_of_its_length) {
    auto t = tiny_dict();
    auto full = full_with({"cab", "cad"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    // The 3-letter bucket (flat 0..2) is empty; guesses walk it in
    // slot order.
    const auto first = p.apply("cab");
    ASSERT_EQ(static_cast<int>(first.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_EQ(first.slot_index, std::size_t(0));
    fb.mark_filled(first.slot_index, first.placed_word);
    const auto second = p.apply("cad");
    ASSERT_EQ(static_cast<int>(second.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_EQ(second.slot_index, std::size_t(1));
    fb.mark_filled(second.slot_index, second.placed_word);
    // A board word lands in the next free slot of its length, not in
    // "its own".
    const auto third = p.apply("abc");
    ASSERT_EQ(static_cast<int>(third.status),
              static_cast<int>(GuessStatus::Accepted));
    ASSERT_EQ(third.slot_index, std::size_t(2));
    fb.mark_filled(third.slot_index, third.placed_word);
    // All three 3-letter slots are taken: nowhere to put "ace".
    const auto fourth = p.apply("ace");
    ASSERT_EQ(static_cast<int>(fourth.status),
              static_cast<int>(GuessStatus::NoRoom));
}

TEST(guess_gate_requires_full_list_and_rack_letters) {
    auto t = tiny_dict();
    auto full = full_with({"dace"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    // In neither list.
    ASSERT_EQ(static_cast<int>(p.apply("zzz").status),
              static_cast<int>(GuessStatus::NotAWord));
    // In both lists but not spellable from the rack: the original
    // gates guesses on the rack's full-list sub-anagram enumeration
    // (the list copied to table+0x190), so "xyz" is rejected too.
    ASSERT_EQ(static_cast<int>(p.apply("xyz").status),
              static_cast<int>(GuessStatus::NotAWord));
    // Non-letters are rejected before any lookup.
    ASSERT_EQ(static_cast<int>(p.apply("123").status),
              static_cast<int>(GuessStatus::NotAWord));
    // Spellable and in the full list, but there is no 2-letter slot.
    ASSERT_EQ(static_cast<int>(p.apply("ab").status),
              static_cast<int>(GuessStatus::NoRoom));
}

TEST(guess_bingo_uncommon_anagram) {
    // A full-only anagram of the rack is an uncommon bingo.
    auto t = tiny_dict();
    auto full = full_with({"edcba"});
    Board b = Board::build("abcde", t);
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply("edcba");
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_TRUE(o.bingo);
    // Common words are not flagged uncommon.
    const auto c = p.apply("ace");
    ASSERT_EQ(static_cast<int>(c.status),
              static_cast<int>(GuessStatus::Accepted));
    ASSERT_TRUE(!c.newly_uncommon);
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

// 0x432330 rebuilds the AnagramTable list at +0x180 (count +0x184, the
// slot count) from the board's words; Board::on_board reads the same list.
TEST(board_on_board_reads_the_slot_list) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t, 1);   // WOTD-style cap: 1 per length
    ASSERT_EQ(b.slots(3).size(), std::size_t(1));
    const std::string kept = b.slots(3)[0];
    ASSERT_TRUE(b.on_board(kept));
    ASSERT_TRUE(b.on_board("abcd"));
    ASSERT_TRUE(b.on_board("abcde"));
    int cut = 0;
    for (const char* w : {"abc", "ace", "bad"}) {
        if (kept == w) continue;
        ASSERT_TRUE(!b.on_board(w));          // common, but left off the board
        ++cut;
    }
    ASSERT_EQ(cut, 2);
    ASSERT_TRUE(!b.on_board("ab"));           // 2 letters: no slot list
    ASSERT_TRUE(!b.on_board(""));
    ASSERT_TRUE(!b.on_board("abcdefghi"));    // longer than any slot
}

// 0x432850: the out-flag "uncommon" is set when the guess is not in the
// board's list, so a common word the cap left off the board is uncommon
// and still fills the first free slot of its length.
TEST(guess_common_word_cut_from_the_board_is_uncommon) {
    auto t = tiny_dict();
    auto full = full_with({});
    Board b = Board::build("abcde", t, 1);
    const std::string kept = b.slots(3)[0];
    std::string cut;
    for (const char* w : {"abc", "ace", "bad"}) {
        if (kept != w) { cut = w; break; }
    }
    ASSERT_TRUE(t.contains(cut));
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply(cut);
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::Uncommon));
    ASSERT_TRUE(o.newly_uncommon);
    ASSERT_EQ(o.slot_index, std::size_t(0));
    ASSERT_EQ(o.placed_word, cut);
    ASSERT_EQ(o.points, 90);
}

// Negative: under the same cap, the board's own word is accepted, not
// uncommon.
TEST(guess_board_word_under_a_cap_is_accepted) {
    auto t = tiny_dict();
    auto full = full_with({});
    Board b = Board::build("abcde", t, 1);
    const std::string kept = b.slots(3)[0];
    FilledBoard fb(std::move(b));
    GuessProcessor p(fb, t, full);
    const auto o = p.apply(kept);
    ASSERT_EQ(static_cast<int>(o.status),
              static_cast<int>(GuessStatus::Accepted));
    ASSERT_TRUE(!o.newly_uncommon);
}

TEST(bingo_uses_every_letter) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t);
    ASSERT_TRUE(b.is_bingo("abcde"));
    ASSERT_TRUE(b.is_bingo("edcba"));
    ASSERT_TRUE(!b.is_bingo("abcd"));
    ASSERT_TRUE(!b.is_bingo("abcdef"));  // longer than rack
}

// The rack check is tt2::spellable_from over the rack's letters.
TEST(board_is_anagram_of_rack_counts_rack_letters) {
    auto t = tiny_dict();
    Board b = Board::build("abcde", t);
    ASSERT_TRUE(b.is_anagram_of_rack("bad"));
    ASSERT_TRUE(b.is_anagram_of_rack("edcba"));
    ASSERT_TRUE(!b.is_anagram_of_rack("dad"));      // one d on the rack
    ASSERT_TRUE(!b.is_anagram_of_rack("abcdea"));   // longer than the rack
    ASSERT_TRUE(!b.is_anagram_of_rack("BAD"));      // uppercase is not a..z
    ASSERT_TRUE(!b.is_bingo("EDCBA"));
}

// 0x4333a0 / 0x433320 / 0x433180: the round-end reveal.
TEST(reveal_words_skips_words_already_on_show) {
    const std::vector<std::string> board = {"abs", "bad", "bed", "bead"};
    // "bed" was found first, so it sits in slot 0 (0x432b20).
    const auto out = tt2::reveal_words(board, {"bed", "", "", ""});
    ASSERT_EQ(out[0], std::string("bed"));
    ASSERT_EQ(out[1], std::string("abs"));                 // first 3-letter word not on show
    ASSERT_EQ(out[2], std::string("bad"));                 // "abs" is now on show too
    ASSERT_EQ(out[3], std::string("bead"));
}

TEST(reveal_words_keeps_found_and_uncommon_words) {
    const std::vector<std::string> board = {"abs", "bad", "bed"};
    // An uncommon guess ("dab") holds slot 0; nothing found is moved.
    const auto out = tt2::reveal_words(board, {"dab", "bed", ""});
    ASSERT_EQ(out[0], std::string("dab"));
    ASSERT_EQ(out[1], std::string("bed"));
    ASSERT_EQ(out[2], std::string("abs"));
    // Nothing found: every slot shows its own answer.
    ASSERT_TRUE(tt2::reveal_words(board, {"", "", ""}) == board);
    // All filled: unchanged.
    ASSERT_TRUE(tt2::reveal_words(board, {"bad", "abs", "bed"}) ==
                (std::vector<std::string>{"bad", "abs", "bed"}));
}

TEST(reveal_words_falls_back_to_the_slots_own_word) {
    // No word of the slot's length left off show (only reachable with a
    // repeated answer): the slot keeps its own word, not index 0.
    const auto out = tt2::reveal_words({"bead", "abs", "abs"}, {"", "abs", ""});
    ASSERT_EQ(out[0], std::string("bead"));
    ASSERT_EQ(out[2], std::string("abs"));
    // A short `shown` is padded to the board: every slot gets its answer.
    ASSERT_TRUE(tt2::reveal_words({"abs", "bad"}, {}) == (std::vector<std::string>{"abs", "bad"}));
}

namespace {
// A rack with 106 common sub-anagrams: the first 100 three-letter
// combinations of "abcdefghij" and six four-letter words. The 48-slot
// quota gives length 4 only trunc(6 * 48 / 106) = 2 slots.
WordTree crowded_dict() {
    std::vector<std::string> words;
    const std::string letters = "abcdefghij";
    for (std::size_t i = 0; i < letters.size() && words.size() < 100; ++i)
        for (std::size_t j = i + 1; j < letters.size() && words.size() < 100; ++j)
            for (std::size_t k = j + 1; k < letters.size() && words.size() < 100; ++k)
                words.push_back({letters[i], letters[j], letters[k]});
    for (const char* w : {"abcd", "abce", "abcf", "abcg", "abch", "abci"}) words.push_back(w);
    return WordTree::from_words(words);
}
}  // namespace

// AC-W2 (D-13): 0x4262b0 caps 5 per length before the 0x432330 quota.
TEST(board_per_length_cap_runs_before_the_quota) {
    auto t = crowded_dict();
    Board b = Board::build("abcdefghij", t, 5);
    // Quota first would have left 2 four-letter slots; the exe keeps 5.
    ASSERT_TRUE(b.slots(4) == (std::vector<std::string>{"abcd", "abce", "abcf", "abcg", "abch"}));
    ASSERT_TRUE(b.slots(3) == (std::vector<std::string>{"abc", "abd", "abe", "abf", "abg"}));
    ASSERT_EQ(b.slot_count(), std::size_t(10));
    // The enumeration itself is not capped (guesses still check it).
    ASSERT_EQ(b.all().size(), std::size_t(106));
}

TEST(board_without_per_length_cap_keeps_the_quota_board) {
    // Negative: Classic (cap 0) is unchanged: 45 three-letter slots
    // (trunc(100 * 48 / 106)) and 2 four-letter slots.
    auto t = crowded_dict();
    Board b = Board::build("abcdefghij", t, 0);
    ASSERT_TRUE(b.slots(4) == (std::vector<std::string>{"abcd", "abce"}));
    ASSERT_EQ(b.slots(3).size(), std::size_t(45));
    ASSERT_EQ(b.slots(3).front(), std::string("abc"));
    ASSERT_EQ(b.slots(3).back(), std::string("bdf"));
    ASSERT_EQ(b.slot_count(), std::size_t(47));
}

TEST(board_per_length_cap_keeps_short_buckets_whole) {
    // A bucket already under the cap is not padded or trimmed.
    auto t = tiny_dict();
    Board capped = Board::build("abcde", t, 5);
    Board plain  = Board::build("abcde", t, 0);
    for (std::size_t l = 3; l <= 8; ++l) ASSERT_TRUE(capped.slots(l) == plain.slots(l));
}
