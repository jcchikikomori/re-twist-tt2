// Lightning (Blitz) rules: round_lightning.hpp, the Blitz trophies in
// trophies.hpp and the Blitz save in saved_game.hpp.

#include <string>
#include <vector>

#include "test_harness.hpp"
#include "tt2/bag.hpp"
#include "tt2/random.hpp"
#include "tt2/round_lightning.hpp"
#include "tt2/saved_game.hpp"
#include "tt2/trophies.hpp"
#include "tt2/word_tree.hpp"

using namespace tt2;

namespace {
WordTree common() {
    // Two rack families with >= 15 sub-anagrams each (the bag's rule).
    return WordTree::from_words({
        "crate", "caret", "trace", "react", "carte", "cater", "traces", "crates", "reacts",
        "ace", "act", "arc", "are", "art", "ate", "car", "cat", "ear", "eat", "era", "eta",
        "rat", "tar", "tea", "care", "race", "acre", "rate", "tare", "tear", "cart", "scat",
        "acts", "arcs", "cast", "east", "eats", "rats", "star", "tsar", "seat", "sect",
    });
}
WordTree full() {
    auto w = common().words();
    w.push_back("recat");   // full-only: an uncommon solve
    return WordTree::from_words(w);
}
std::vector<LightningRound::SavedWord> saved(std::initializer_list<const char*> words) {
    std::vector<LightningRound::SavedWord> out;
    for (const char* w : words) out.push_back({w, false});
    return out;
}
}  // namespace

TEST(lightning_word_count_follows_the_round) {
    ASSERT_EQ(lightning_word_count(1), std::size_t{5});
    ASSERT_EQ(lightning_word_count(5), std::size_t{5});
    ASSERT_EQ(lightning_word_count(6), std::size_t{6});
    ASSERT_EQ(lightning_word_count(26), std::size_t{10});
    ASSERT_EQ(lightning_word_count(100), std::size_t{10});
}

TEST(lightning_word_length_is_five_or_six_in_round_one) {
    Random rng(42);
    int fives = 0, sixes = 0;
    for (int i = 0; i < 400; ++i) {
        const std::size_t l = lightning_word_length(1, rng, 8);
        ASSERT_TRUE(l == 5 || l == 6 || l == 7);   // 7 only when u is exactly 0
        fives += l == 5;
        sixes += l == 6;
    }
    ASSERT_TRUE(fives > 120 && sixes > 120);
    // Round 11: 7 or 8, capped by the player's maximum.
    for (int i = 0; i < 50; ++i) ASSERT_TRUE(lightning_word_length(11, rng, 6) == 6);
}

TEST(lightning_start_deals_the_round_and_resets_the_clock) {
    auto com = common();
    auto ful = full();
    Random rng(3);
    Bag bag(com);
    LightningRound r(com, ful);
    r.start(1, rng, bag, 6);
    ASSERT_EQ(r.words().size(), std::size_t{5});
    for (const auto& w : r.words()) ASSERT_TRUE(w.length >= 5 && w.length <= 6);
    ASSERT_EQ(r.time_ms(), 80300);
    ASSERT_EQ(r.score(), 0);
    ASSERT_EQ(r.current(), std::size_t{0});
}

TEST(lightning_solve_scores_adds_time_and_moves_on) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 50000, 0, saved({"crate", "traces"}));
    // A short word is ignored; a wrong full-length one is rejected.
    ASSERT_TRUE(r.submit("cat").status == LightningRound::Submit::Ignored);
    ASSERT_TRUE(r.submit("zzzzz").status == LightningRound::Submit::Rejected);
    ASSERT_EQ(r.current_word().last_word, std::string("zzzzz"));
    // Any full-length anagram solves it: 10 * 5 * 5.
    const auto s = r.submit("trace");
    ASSERT_TRUE(s.status == LightningRound::Submit::Solved);
    ASSERT_EQ(s.points, 250);
    ASSERT_EQ(s.time_added_ms, 8000);
    ASSERT_EQ(r.time_ms(), 58000);
    r.advance();
    ASSERT_EQ(r.current(), std::size_t{1});
    ASSERT_TRUE(!r.passed());   // moving on after a solve is not a pass
}

TEST(lightning_time_bonus_is_capped) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 79000, 0, saved({"crate", "traces"}));
    ASSERT_EQ(r.submit("crate").time_added_ms, 1300);
    ASSERT_EQ(r.time_ms(), 80300);
}

TEST(lightning_last_word_wins_the_round_with_the_bonus_and_no_time) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(2, 2500, 100, saved({"crate", "traces"}));
    r.submit("crate");
    r.advance();
    const auto s = r.submit("recat");   // five letters on a six-letter word: ignored
    ASSERT_TRUE(s.status == LightningRound::Submit::Ignored);
    const auto w = r.submit("crates");
    ASSERT_TRUE(w.status == LightningRound::Submit::RoundWon);
    ASSERT_EQ(w.points, 360 + 1000);
    ASSERT_EQ(w.time_added_ms, 0);
    ASSERT_TRUE(r.won());
    ASSERT_EQ(r.score(), 100 + 250 + 1360);
    ASSERT_TRUE(r.submit("crate").status == LightningRound::Submit::Ignored);   // over
}

TEST(lightning_uncommon_solve_is_flagged) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 50000, 0, saved({"crate", "traces"}));
    ASSERT_TRUE(r.submit("recat").uncommon);
}

TEST(lightning_hints_come_after_nine_then_every_four_seconds) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 80300, 0, saved({"crate", "traces"}));
    ASSERT_TRUE(!r.tick(8999, false).hint);
    ASSERT_TRUE(r.tick(1, false).hint);
    ASSERT_EQ(r.current_word().hints, 1);
    ASSERT_TRUE(r.hinted());
    ASSERT_TRUE(!r.tick(3999, false).hint);
    // Held back while the player is typing, given once they stop.
    ASSERT_TRUE(!r.tick(1, true).hint);
    ASSERT_TRUE(r.tick(16, false).hint);
    ASSERT_EQ(r.current_word().hints, 2);
    // Each hint costs 10 * L: 10 * 5 * (5 - 2).
    ASSERT_EQ(r.submit("crate").points, 150);
}

TEST(lightning_warning_beeps_each_second_under_ten) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 12000, 0, saved({"crate", "traces"}));
    ASSERT_EQ(r.tick(1500, true).warnings, 0);    // 10.5 s
    ASSERT_EQ(r.tick(600, true).warnings, 1);     // 9.9 s: crossed 10
    ASSERT_EQ(r.tick(2000, true).warnings, 2);    // 7.9 s: 9 and 8
    // A solve re-arms at the second below the new time: 15.9 s -> 10 s.
    r.submit("crate");
    ASSERT_EQ(r.tick(5950, true).warnings, 1);    // 9.95 s
}

TEST(lightning_time_out_ends_the_game) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 100, 0, saved({"crate", "traces"}));
    ASSERT_TRUE(!r.tick(99, true).time_out);
    ASSERT_TRUE(r.tick(1, true).time_out);
    ASSERT_TRUE(r.lost());
    ASSERT_EQ(r.time_ms(), 0);
    ASSERT_TRUE(r.submit("crate").status == LightningRound::Submit::Ignored);
}

TEST(lightning_passes_wrap_and_are_counted) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 50000, 0, saved({"crate", "traces", "caret"}));
    r.pass(false);
    ASSERT_EQ(r.current(), std::size_t{2});
    ASSERT_TRUE(r.passed());
    r.pass(true);
    ASSERT_EQ(r.current(), std::size_t{0});
    r.submit("crate");
    ASSERT_TRUE(!r.select(0));                    // solved
    ASSERT_TRUE(r.select(2));
    ASSERT_EQ(r.current(), std::size_t{2});
}

TEST(lightning_pass_with_no_other_word_stays_but_counts) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 50000, 0, {{"crate", true}, {"traces", false}});
    ASSERT_EQ(r.current(), std::size_t{1});       // the first unsolved word
    r.pass(true);
    ASSERT_EQ(r.current(), std::size_t{1});
    ASSERT_TRUE(r.passed());
}

TEST(lightning_twist_and_give_up) {
    auto com = common();
    auto ful = full();
    LightningRound r(com, ful);
    r.restore(1, 50000, 0, saved({"crate", "traces"}));
    r.twist();
    ASSERT_TRUE(r.twisted());
    r.give_up();
    ASSERT_TRUE(r.lost() && r.over());
}

TEST(lightning_round_trophies_follow_the_flags) {
    PlayerProfile p;
    const auto all = award_lightning_round(p, LightningRoundFlags{false, false, false, 3000});
    ASSERT_EQ(all.size(), std::size_t{4});
    ASSERT_EQ(all[0], static_cast<int>(TROPHY_BRAINSTORM));
    ASSERT_EQ(all[3], static_cast<int>(TROPHY_NICK_OF_TIME));
    ASSERT_TRUE(award_lightning_round(p, LightningRoundFlags{}).empty());   // won once
    PlayerProfile q;
    const auto none = award_lightning_round(q, LightningRoundFlags{true, true, true, 3001});
    ASSERT_TRUE(none.empty());
}

TEST(lightning_save_round_trips_and_validates) {
    SavedGame g;
    g.mode = save_mode_name(GameMode::Blitz);
    g.round = 4;
    g.time_ms = 41000;
    g.score = 2210;
    g.blitz_words = {{"crate", true}, {"traces", false}};
    SavedGame back;
    ASSERT_TRUE(parse(serialize(g), back));
    ASSERT_EQ(back.round, 4);
    ASSERT_EQ(back.blitz_words.size(), std::size_t{2});
    ASSERT_TRUE(back.blitz_words[0].completed);
    ASSERT_EQ(back.blitz_words[1].word, std::string("traces"));
    // A bad flag or a 4-letter word is refused.
    std::string bad = serialize(g);
    bad.replace(bad.find("traces,0"), 8, "traces,2");
    ASSERT_TRUE(!parse(bad, back));
    g.blitz_words.push_back({"cart", false});
    ASSERT_TRUE(!parse(serialize(g), back));
}
