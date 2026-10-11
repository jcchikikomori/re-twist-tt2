// Tests for the scoring rules (scoring.hpp).

#include "test_harness.hpp"
#include "tt2/scoring.hpp"

using tt2::score_classic;
using tt2::score_lightning_word;
using tt2::score_crossword_word;

TEST(classic_per_word_lengths) {
    ASSERT_EQ(tt2::scoring::per_word(3), 90);
    ASSERT_EQ(tt2::scoring::per_word(4), 160);
    ASSERT_EQ(tt2::scoring::per_word(5), 250);
    ASSERT_EQ(tt2::scoring::per_word(6), 360);
    ASSERT_EQ(tt2::scoring::per_word(7), 490);
    ASSERT_EQ(tt2::scoring::per_word(8), 640);
}

TEST(classic_bingo_bonus_once_per_round) {
    auto p = score_classic(8, /*is_bingo=*/true, /*already_qualified=*/false,
                           /*round_complete=*/false, /*slot_count=*/19);
    ASSERT_TRUE(p.bingo);
    ASSERT_EQ(p.bingo_bonus, 1000);
    ASSERT_TRUE(p.qualified);
    ASSERT_EQ(p.total, 640 + 1000);

    // Already qualified → no second bingo bonus.
    auto p2 = score_classic(8, /*is_bingo=*/true, /*already_qualified=*/true,
                            /*round_complete=*/false, /*slot_count=*/19);
    ASSERT_EQ(p2.bingo_bonus, 0);
    ASSERT_EQ(p2.total, 640);
}

TEST(classic_all_words_bonus) {
    auto p = score_classic(3, /*is_bingo=*/false, /*already_qualified=*/false,
                           /*round_complete=*/true, /*slot_count=*/19);
    ASSERT_EQ(p.all_words_bonus, 19 * 100);
    ASSERT_EQ(p.word_points, 90);
    ASSERT_EQ(p.total, 90 + 19 * 100);
}

TEST(lightning_per_word_with_hints) {
    ASSERT_EQ(score_lightning_word(8, 0), 640);
    ASSERT_EQ(score_lightning_word(8, 2), 480);  // 10 * 8 * (8 - 2)
    ASSERT_EQ(score_lightning_word(8, 8), 0);    // all letters revealed
    // Hints clamped to length.
    ASSERT_EQ(score_lightning_word(5, 99), 0);
}

TEST(crossword_per_word) {
    ASSERT_EQ(score_crossword_word(3), 90);
    ASSERT_EQ(score_crossword_word(8), 640);
}
