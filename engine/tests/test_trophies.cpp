// Trophy award rules (trophies.hpp).

#include "test_harness.hpp"
#include "tt2/trophies.hpp"

using tt2::GameMode;
using tt2::PlayerProfile;

TEST(trophy_award_marks_the_slot_new_once) {
    PlayerProfile p;
    ASSERT_EQ(tt2::award_trophy(p, tt2::TROPHY_DIZZY), static_cast<int>(tt2::TROPHY_DIZZY));
    ASSERT_EQ(p.trophies[tt2::TROPHY_DIZZY], tt2::TROPHY_NEW);
    ASSERT_EQ(tt2::award_trophy(p, tt2::TROPHY_DIZZY), -1);
    p.trophies[tt2::TROPHY_DIZZY] = tt2::TROPHY_SEEN;
    ASSERT_EQ(tt2::award_trophy(p, tt2::TROPHY_DIZZY), -1);
    ASSERT_EQ(p.trophies[tt2::TROPHY_DIZZY], tt2::TROPHY_SEEN);
}

TEST(trophy_award_rejects_out_of_range_ids) {
    PlayerProfile p;
    ASSERT_EQ(tt2::award_trophy(p, -1), -1);
    ASSERT_EQ(tt2::award_trophy(p, 24), -1);
}

TEST(trophy_streaks_award_on_exact_counts_only) {
    PlayerProfile p;
    ASSERT_EQ(tt2::award_no_twist_streak(p, 4), -1);
    ASSERT_EQ(tt2::award_no_twist_streak(p, 5), static_cast<int>(tt2::TROPHY_QUICK_DRAW));
    ASSERT_EQ(tt2::award_no_twist_streak(p, 6), -1);
    ASSERT_EQ(tt2::award_no_twist_streak(p, 10), static_cast<int>(tt2::TROPHY_STRAIGHT_SHOOTER));
    ASSERT_EQ(tt2::award_no_twist_streak(p, 20), static_cast<int>(tt2::TROPHY_BULLS_EYE));
    ASSERT_EQ(tt2::award_no_twist_streak(p, 5), -1);   // already won
    ASSERT_EQ(tt2::award_bingo_streak(p, 5), static_cast<int>(tt2::TROPHY_BINGO_BLASTER));
    ASSERT_EQ(tt2::award_bingo_streak(p, 10), static_cast<int>(tt2::TROPHY_BINGO_BOSS));
    ASSERT_EQ(tt2::award_bingo_streak(p, 20), static_cast<int>(tt2::TROPHY_BONKERS_FOR_BINGOS));
    ASSERT_EQ(tt2::award_bingo_streak(p, 21), -1);
}

TEST(trophy_counters_count_and_award_at_thresholds) {
    PlayerProfile p;
    p.twists = 998;
    ASSERT_EQ(tt2::count_twist(p), -1);
    ASSERT_EQ(tt2::count_twist(p), static_cast<int>(tt2::TROPHY_WITH_A_TWIST));
    ASSERT_EQ(p.twists, 1000);
    p.solved = 4999;
    ASSERT_EQ(tt2::count_word(p), static_cast<int>(tt2::TROPHY_WORDSMITH));
    ASSERT_EQ(p.solved, 5000);
    p.hard_solved = 499;
    ASSERT_EQ(tt2::count_uncommon(p), static_cast<int>(tt2::TROPHY_HUMAN_DICTIONARY));
    ASSERT_EQ(tt2::count_uncommon(p), -1);
    ASSERT_EQ(p.hard_solved, 501);
}

TEST(trophy_counters_past_the_threshold_never_award) {
    PlayerProfile p;
    p.solved = 1500;
    ASSERT_EQ(tt2::count_word(p), -1);
    ASSERT_EQ(p.trophies[tt2::TROPHY_WORD_NERD], tt2::TROPHY_NOT_WON);
}

TEST(trophy_round_count_needs_every_mode) {
    PlayerProfile p;
    ASSERT_EQ(tt2::count_round(p, GameMode::Timed), -1);
    ASSERT_EQ(tt2::count_round(p, GameMode::Untimed), -1);
    ASSERT_EQ(tt2::count_round(p, GameMode::Blitz), -1);
    ASSERT_EQ(tt2::count_round(p, GameMode::Crossword), -1);
    ASSERT_EQ(tt2::count_round(p, GameMode::WordOfTheDay), static_cast<int>(tt2::TROPHY_WELL_ROUNDED));
    ASSERT_EQ(p.rounds_completed[static_cast<std::size_t>(GameMode::WordOfTheDay)], 1);
    ASSERT_EQ(tt2::count_round(p, GameMode::Timed), -1);   // Well Rounded is already won
}

TEST(trophy_round_count_awards_one_tier_per_call) {
    PlayerProfile p;
    p.rounds_completed = {29, 30, 30, 30, 30};
    ASSERT_EQ(tt2::count_round(p, GameMode::Timed), static_cast<int>(tt2::TROPHY_FANATIC));
    ASSERT_EQ(p.trophies[tt2::TROPHY_MANIAC], tt2::TROPHY_NOT_WON);
    // 30+ with Fanatic won: the lower tiers are not tried.
    ASSERT_EQ(tt2::count_round(p, GameMode::Timed), -1);
    PlayerProfile q;
    q.rounds_completed = {9, 10, 10, 10, 10};
    ASSERT_EQ(tt2::count_round(q, GameMode::Timed), static_cast<int>(tt2::TROPHY_MANIAC));
}

TEST(trophy_speed_speller_and_look_ma) {
    PlayerProfile p;
    ASSERT_EQ(tt2::award_speed_speller(p, 5001), -1);
    ASSERT_EQ(tt2::award_speed_speller(p, 5000), static_cast<int>(tt2::TROPHY_SPEED_SPELLER));
    ASSERT_EQ(tt2::award_look_ma(p, true), -1);
    ASSERT_EQ(tt2::award_look_ma(p, false), static_cast<int>(tt2::TROPHY_LOOK_MA));
}
