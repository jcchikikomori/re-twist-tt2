// Lightning scene: layout, solving, the clock, hints, passes, round end,
// game over and saving (blitz_scene.hpp), plus the shared popups
// (scene_popups.hpp).

#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/blitz_scene.hpp"

using namespace tt2ui;

namespace {

tt2::WordTree common() {
    return tt2::WordTree::from_words({
        "crate", "caret", "trace", "react", "carte", "cater",
        "ace", "act", "arc", "are", "art", "ate", "car", "cat", "ear", "eat", "era", "eta",
        "rat", "tar", "tea", "care", "race", "acre", "rate", "tare", "tear",
    });
}
tt2::WordTree full() {
    auto words = common().words();
    words.push_back("recta");   // full only: an uncommon solve
    return tt2::WordTree::from_words(words);
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

struct Fixture {
    tt2::WordTree com = common();
    tt2::WordTree ful = full();
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
    std::vector<std::string> names{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    tt2::Random hs_rng{1};
    tt2::HighScoreManager hs{names, hs_rng};
    std::vector<SoundId> sounds;
    std::vector<std::string> urls;
    std::vector<tt2::SavedGame> saved;
    int exits = 0, discards = 0;
    // The process-wide CRT the deal and TWIST draw from (0x43c370, D-11);
    // it outlives the scene.
    tt2::CrtRandom crt{1};
    std::unique_ptr<BlitzScene> scene;

    // Without `words` a fresh game is dealt; with them the round comes
    // from a save, so the racks are known.
    explicit Fixture(std::vector<tt2::SavedGame::BlitzWord> words = {}, int time_ms = 50000,
                     std::int32_t score = 0, bool tutorial_seen = true) {
        profile.mode_flags.fill(tutorial_seen);
        BlitzScene::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_exit = [this] { ++exits; };
        s.save_game = [this](const tt2::SavedGame& g) { saved.push_back(g); };
        s.discard_save = [this] { ++discards; };
        s.open_url = [this](const std::string& u) { urls.push_back(u); };
        tt2::SavedGame g;
        g.mode = tt2::save_mode_name(tt2::GameMode::Blitz);
        g.round = 1;
        g.time_ms = time_ms;
        g.score = score;
        g.blitz_words = words;
        scene = std::make_unique<BlitzScene>(com, ful, profile, &hs, s, 5, crt, words.empty() ? nullptr : &g);
    }
    BlitzScene& b() { return *scene; }
    void type(const std::string& w) { for (char c : w) scene->type_letter(c); }
    void enter(const std::string& w) { type(w); scene->key_return(); }
    void run(int ms) { for (int t = 0; t < ms; t += 10) scene->update(10); }
    bool played(SoundId id) const { return std::find(sounds.begin(), sounds.end(), id) != sounds.end(); }
    int count(SoundId id) const { return static_cast<int>(std::count(sounds.begin(), sounds.end(), id)); }
};

std::vector<tt2::SavedGame::BlitzWord> two() { return {{"crate", false}, {"trace", false}}; }

// A word's rack row, left to right: the balls still in it, by cell.
std::string rack_order(const BlitzWordView& w) {
    std::vector<const LetterBall*> row;
    for (const auto& b : w.balls) if (!b.played) row.push_back(&b);
    std::sort(row.begin(), row.end(), [](const LetterBall* a, const LetterBall* b) { return a->slot < b->slot; });
    std::string out;
    for (const auto* b : row) out.push_back(b->letter);
    return out;
}

}  // namespace

TEST(blitz_list_and_current_word_positions) {
    // n = 5: base 185, racks 40 apart; reveal 400 + 4 * 100 + 2000.
    ASSERT_TRUE(near(blitz_list_y(5, 0), 185.0f) && near(blitz_list_y(5, 4), 345.0f));
    ASSERT_TRUE(near(blitz_current_x(6), 286.0f));
    ASSERT_EQ(blitz_reveal_ms(5), 2800);
    Fixture f(two());
    const auto& w = f.b().words();
    ASSERT_TRUE(near(w[0].x, 320.5f) && near(w[0].y, 281.0f) && near(w[0].scale, 1.0f));
    ASSERT_TRUE(near(w[1].x, 10.0f) && near(w[1].y, 285.0f) && near(w[1].scale, BLITZ_LIST_SCALE));
    ASSERT_TRUE(near(w[0].entry_alpha, 1.0f) && near(w[1].entry_alpha, 0.0f));
    f.enter("ctare");
    f.run(10);
    ASSERT_TRUE(near(f.b().popup().x, BLITZ_POPUP_X) && near(f.b().popup().y, BLITZ_POPUP_Y));
}

TEST(blitz_new_game_deals_five_words_and_shows_the_tutorial_once) {
    Fixture f({}, 0, 0, false);
    ASSERT_EQ(f.b().words().size(), std::size_t{5});
    ASSERT_TRUE(f.played(SoundId::LightningStartRound));
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::Tutorial);
    ASSERT_TRUE(f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Blitz)]);
    // The tutorial holds the clock.
    f.run(1000);
    ASSERT_EQ(f.b().time_left_ms(), tt2::LIGHTNING_START_MS);
    f.b().dialog_press("BUTTON_OK");
    ASSERT_TRUE(!f.b().dialog_open());
    Fixture g;   // tutorial already seen
    ASSERT_TRUE(!g.b().dialog_open());
}

TEST(blitz_solve_scores_adds_time_and_moves_to_the_next_word) {
    Fixture f(two());
    f.enter("caret");
    ASSERT_EQ(f.b().score(), 250);
    ASSERT_EQ(f.b().time_left_ms(), 58000);
    ASSERT_TRUE(f.played(SoundId::WordBingo) && f.played(SoundId::LightningTimeAdded));
    ASSERT_TRUE(f.b().busy());
    ASSERT_TRUE(!f.b().button("BUTTON_Twist")->enabled);   // locked while the bar grows
    ASSERT_EQ(f.b().timer_bar().frame, 1);
    f.run(blitz_time_added_ms(5));
    ASSERT_TRUE(!f.b().busy());
    ASSERT_EQ(f.b().current(), std::size_t{1});
    ASSERT_TRUE(!f.b().round().passed());
    ASSERT_TRUE(f.b().popup_kind() == PopupKind::ExtraTime);
    f.run(BLITZ_SWITCH_MS);
    ASSERT_TRUE(near(f.b().words()[1].scale, 1.0f) && near(f.b().words()[0].scale, BLITZ_LIST_SCALE));
    // The solved letters turned into square tiles.
    for (const auto& ball : f.b().words()[0].balls) ASSERT_EQ(ball.frame, BLITZ_SOLVED_FRAME);
}

TEST(blitz_short_word_is_ignored_and_wrong_word_rejected) {
    Fixture f(two());
    f.enter("cat");
    ASSERT_EQ(f.b().typed(), std::string("cat"));   // nothing happens, the row stays
    ASSERT_TRUE(!f.played(SoundId::WordReject));
    f.b().key_delete();
    f.enter("ctare");
    ASSERT_TRUE(f.played(SoundId::WordReject));
    ASSERT_TRUE(f.b().typed().empty());
    f.run(10);
    ASSERT_TRUE(f.b().popup_kind() == PopupKind::NotListed);
    ASSERT_EQ(f.b().score(), 0);
}

TEST(blitz_enter_needs_a_full_row) {
    Fixture f(two());
    f.type("crat");
    ASSERT_TRUE(!f.b().button("BUTTON_Enter")->enabled);
    f.type("e");
    ASSERT_TRUE(f.b().button("BUTTON_Enter")->enabled);
    ASSERT_TRUE(f.b().button("BUTTON_Clear")->enabled);
    ASSERT_TRUE(!f.b().button("BUTTON_LastWord")->enabled);
}

TEST(blitz_clock_runs_beeps_and_stops_under_the_menu) {
    Fixture f(two(), 10500);
    f.run(600);
    ASSERT_EQ(f.count(SoundId::LightningTimeLow), 1);
    ASSERT_EQ(f.b().timer_minutes_text(), std::string("0"));
    ASSERT_EQ(f.b().timer_seconds_text(), std::string(":09"));
    ASSERT_TRUE(near(f.b().timer_bar().clip.w, 321.0f * 9900.0f / 80300.0f));
    f.b().key_escape();
    ASSERT_TRUE(f.b().pause_veil());
    f.run(5000);
    ASSERT_EQ(f.b().time_left_ms(), 9900);
    f.b().key_escape();
    ASSERT_TRUE(!f.b().pause_veil());
}

TEST(blitz_hint_after_nine_seconds_waits_while_typing) {
    Fixture f(two(), 80300);
    f.run(8990);
    ASSERT_TRUE(f.b().hint_letters().empty());
    f.type("c");
    f.run(100);
    ASSERT_TRUE(f.b().hint_letters().empty());   // deferred
    f.b().backspace();
    f.run(10);
    ASSERT_EQ(f.b().hint_letters().size(), std::size_t{1});
    ASSERT_EQ(f.b().hint_letters()[0].frame, 'c' - 0x60);
    ASSERT_TRUE(f.played(SoundId::LightningHint));
    ASSERT_TRUE(f.b().round().hinted());
    f.run(BLITZ_HINT_FADE_MS);
    ASSERT_TRUE(near(f.b().hint_letters()[0].alpha, BLITZ_HINT_ALPHA));
}

TEST(blitz_next_word_is_a_pass_and_clears_the_row) {
    Fixture f(two());
    f.type("cr");
    f.b().press("BUTTON_NEXTWORD");
    ASSERT_TRUE(f.played(SoundId::LightningSkip));
    ASSERT_TRUE(f.played(SoundId::LetterClear));
    ASSERT_TRUE(f.b().round().passed());
    ASSERT_EQ(f.b().current(), std::size_t{1});
    // The slide waits 500 ms for the cleared letters.
    f.run(BLITZ_SWITCH_CLEAR_MS);
    ASSERT_TRUE(near(f.b().words()[1].scale, BLITZ_LIST_SCALE));
    f.run(BLITZ_SWITCH_MS);
    ASSERT_TRUE(near(f.b().words()[1].scale, 1.0f));
    // Keys: Up goes back.
    f.b().key_word(false);
    ASSERT_EQ(f.b().current(), std::size_t{0});
}

TEST(blitz_rack_click_selects_a_word_or_opens_its_definition) {
    Fixture f(two());
    f.b().mouse_up(20.0f, 290.0f);   // word 1's rack in the list
    ASSERT_EQ(f.b().current(), std::size_t{1});
    ASSERT_TRUE(f.b().round().passed());
    ASSERT_TRUE(f.urls.empty());
    Fixture g(two(), 100);
    g.run(200);   // time out
    g.b().mouse_up(20.0f, 290.0f);
    ASSERT_EQ(g.urls.size(), std::size_t{1});
    ASSERT_EQ(g.urls[0], definition_url("trace"));
}

TEST(blitz_last_word_wins_the_round_with_trophies_and_next_round) {
    Fixture f(two(), 2500);
    f.enter("crate");
    f.run(blitz_time_added_ms(5) + BLITZ_SWITCH_MS);
    f.enter("trace");
    ASSERT_EQ(f.b().score(), 250 + 250 + 1000);
    ASSERT_TRUE(f.b().phase() == GamePhase::Reveal);
    // Brainstorm, Rolling Thunder, Lightning Reflexes; 10.5 s left is not Nick of Time.
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_BRAINSTORM], tt2::TROPHY_NEW);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_LIGHTNING_REFLEXES], tt2::TROPHY_NEW);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_NICK_OF_TIME], tt2::TROPHY_NOT_WON);
    f.run(blitz_reveal_ms(2) + 10);   // the method runs on the next frame
    ASSERT_EQ(f.b().words()[0].mark.frame, BLITZ_MARK_SOLVED_FRAME);
    ASSERT_TRUE(f.b().words()[1].mark.visible);
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(f.played(SoundId::NextRoundAchieved));
    ASSERT_EQ(f.discards, 1);   // only the resume deleted the save
    f.b().dialog_press("BUTTON_OK");
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Blitz)], 1);
    ASSERT_EQ(f.b().round_number(), 2);
    ASSERT_EQ(f.b().score(), 1500);
    ASSERT_EQ(f.b().time_left_ms(), tt2::LIGHTNING_START_MS);
}

TEST(blitz_twist_marks_the_round) {
    Fixture f(two());
    f.b().key_space();
    ASSERT_TRUE(f.played(SoundId::WordTwist));
    ASSERT_TRUE(f.b().round().twisted());
    ASSERT_EQ(f.profile.twists, 1);
}

// 0x414340 builds each word and deals it through 0x43c370 (game-scene.md
// §4.2): every rack, in word order, from the shared CRT, never in the
// word's own order. A resumed round is dealt afresh the same way.
TEST(blitz_deals_every_word_with_the_crt_like_0x43c370) {
    Fixture f;   // a new game on f.crt, seeded 1
    tt2::CrtRandom expect(1);
    for (const auto& w : f.b().words()) {
        ASSERT_EQ(rack_order(w), tt2::deal_order(w.answer, w.answer, expect));
        ASSERT_TRUE(rack_order(w) != w.answer);
    }
    ASSERT_EQ(f.crt.state(), expect.state());
    // From a save: "crate" and "trace" with srand(1) (0x4174c0 -> 0x414340).
    Fixture g(two());
    ASSERT_EQ(rack_order(g.b().words()[0]), std::string("ertac"));
    ASSERT_EQ(rack_order(g.b().words()[1]), std::string("tacer"));
}

// TWIST (0x413090 -> 0x43c370): the current word's row is dealt again,
// compared with the order before and with the word.
TEST(blitz_twist_deals_the_current_rack_with_the_crt) {
    Fixture f(two());
    for (int i = 0; i < 6; ++i) {
        const BlitzWordView& w = f.b().words()[f.b().current()];
        const std::string before = rack_order(w);
        tt2::CrtRandom expect = f.crt;
        const std::string want = tt2::deal_order(before, w.answer, expect);
        f.b().key_space();
        ASSERT_EQ(rack_order(f.b().words()[f.b().current()]), want);
        ASSERT_EQ(f.crt.state(), expect.state());
        f.run(BALL_RELAYOUT_MS + 10);
    }
}

TEST(blitz_time_out_reveals_then_ends_the_game) {
    Fixture f(two(), 100, 5000);   // 5000 makes the prefilled table (1000..10000)
    f.type("cr");
    f.run(100);
    ASSERT_TRUE(f.b().phase() == GamePhase::Reveal);
    ASSERT_TRUE(f.b().typed().empty());
    ASSERT_EQ(f.discards, 2);   // resume + the lost game (0x461840)
    f.run(BLITZ_LOSE_CLEAR_MS + blitz_reveal_ms(2) + 10);
    ASSERT_EQ(f.b().words()[0].mark.frame, 0);
    // The unsolved rack spells its answer.
    std::string rack(5, ' ');
    for (const auto& ball : f.b().words()[1].balls) rack[static_cast<std::size_t>(ball.slot)] = ball.letter;
    ASSERT_EQ(rack, std::string("trace"));
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::Congrats);
    ASSERT_TRUE(f.played(SoundId::HighScoreAchieved));
    f.b().dialog_press("BUTTON_OK");
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::HighScores);
}

TEST(blitz_give_up_with_no_score_shows_end_game) {
    Fixture f(two());
    f.b().press("BUTTON_GiveUp");
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::ConfirmGiveUp);
    f.b().dialog_press("BUTTON_NO");
    ASSERT_TRUE(f.b().phase() == GamePhase::Playing);
    f.b().press("BUTTON_GiveUp");
    f.b().dialog_press("BUTTON_YES");
    f.run(blitz_reveal_ms(2) + 10);   // the method runs on the next frame
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(!f.played(SoundId::HighScoreAchieved));
    f.b().dialog_press("BUTTON_REPLAY");
    ASSERT_EQ(f.b().round_number(), 1);
    ASSERT_EQ(f.b().words().size(), std::size_t{5});
}

TEST(blitz_leave_saves_a_round_in_play_only) {
    Fixture f({{"crate", true}, {"trace", false}}, 41000, 250);
    ASSERT_EQ(f.b().current(), std::size_t{1});   // the first unsolved word
    ASSERT_EQ(f.discards, 1);
    f.b().leave();
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    const auto& g = f.saved[0];
    ASSERT_TRUE(g.blitz());
    ASSERT_EQ(g.time_ms, 41000);
    ASSERT_EQ(g.blitz_words.size(), std::size_t{2});
    ASSERT_TRUE(g.blitz_words[0].completed && !g.blitz_words[1].completed);
    f.b().press("BUTTON_GiveUp");
    f.b().dialog_press("BUTTON_YES");
    f.b().leave();
    ASSERT_EQ(f.saved.size(), std::size_t{1});   // not after the round ended
}

TEST(blitz_rejects_a_classic_save) {
    Fixture f;
    tt2::SavedGame g;
    g.mode = tt2::save_mode_name(tt2::GameMode::Untimed);
    g.word = "crate";
    g.round = 3;
    BlitzScene::Services s;
    BlitzScene b(f.com, f.ful, f.profile, nullptr, s, 1, f.crt, &g);
    ASSERT_EQ(b.round_number(), 1);
    ASSERT_EQ(b.words().size(), std::size_t{5});
}

TEST(blitz_panel_follows_the_longest_word) {
    // n = 5, maxL = 6: top 175, w 146, h 200 (worked example in blitz_scene.hpp).
    ASSERT_TRUE(near(blitz_panel_w(6), 146.0f) && near(blitz_panel_w(8), 198.0f));
    const auto p = blitz_panel(5, 6);
    ASSERT_EQ(p.size(), std::size_t{6});
    // bg, top strip, bottom strip, right side, top corner, bottom corner.
    ASSERT_TRUE(near(p[0].y, 185.0f) && near(p[0].scale_x * 195.0f, 146.0f) && near(p[0].scale_y * 600.0f, 180.0f));
    ASSERT_TRUE(near(p[1].y, 175.0f) && near(p[1].scale_x, 146.0f));
    ASSERT_TRUE(near(p[2].y, 365.0f) && near(p[2].scale_x, 146.0f));
    ASSERT_TRUE(near(p[3].x, 146.0f) && near(p[3].y, 185.0f) && near(p[3].scale_y, 180.0f));
    ASSERT_TRUE(near(p[4].x, 146.0f) && near(p[4].y, 175.0f));
    ASSERT_TRUE(near(p[5].x, 146.0f) && near(p[5].y, 365.0f));
    // Eight letters widen it; the fixed 197 of the old port is gone.
    ASSERT_TRUE(near(blitz_panel(5, 8)[1].scale_x, 198.0f));
    ASSERT_TRUE(!near(blitz_panel(5, 5)[4].x, 197.0f));
}

TEST(blitz_arrows_and_marks_follow_the_panel) {
    // Two five-letter words: w = 120, top = 285 - 40 - 10 = 235, h = 80.
    Fixture f(two());
    ASSERT_EQ(f.b().max_word_length(), std::size_t{5});
    const MenuButton* prev = f.b().button("BUTTON_PREVWORD");
    const MenuButton* next = f.b().button("BUTTON_NEXTWORD");
    ASSERT_TRUE(near(prev->sprite.x, 22.0f) && near(prev->sprite.y, 173.0f));   // floor(60 - 38), 235 - 62
    ASSERT_TRUE(near(next->sprite.x, 22.0f) && near(next->sprite.y, 325.0f));   // 235 + 80 + 10
    for (const auto& w : f.b().words()) ASSERT_TRUE(near(w.mark.x, 130.0f));
}

TEST(blitz_timer_bar_is_clipped_not_scaled) {
    Fixture f(two(), 40150);   // half of 80300
    const SpriteState s = f.b().timer_bar();
    ASSERT_TRUE(s.has_clip);
    ASSERT_TRUE(near(s.clip.w, 160.5f) && near(s.clip.h, 46.0f));
    ASSERT_TRUE(near(s.scale_x, 1.0f) && near(s.scale, 1.0f));
    ASSERT_EQ(s.frame, 0);
    Fixture g(two(), 100);
    g.run(200);   // time out: nothing left to show
    ASSERT_TRUE(near(g.b().timer_bar().clip.w, 0.0f));
}

TEST(blitz_time_added_runs_for_the_word_length_plus_four_tenths) {
    ASSERT_EQ(blitz_time_added_ms(5), 900);
    ASSERT_EQ(blitz_time_added_ms(6), 1000);
    ASSERT_EQ(blitz_time_added_ms(8), 1200);
    Fixture f(two());
    f.enter("caret");
    ASSERT_EQ(f.b().time_added_total_ms(), 900);
    // The clip grows linearly from 50000 to 58000 of 80300.
    f.run(450);
    ASSERT_TRUE(near(f.b().timer_bar().clip.w, 321.0f * 54000.0f / 80300.0f));
    ASSERT_EQ(f.b().timer_bar().frame, 1);
    f.run(440);
    ASSERT_TRUE(f.b().busy());   // 10 ms left: input still waits
    ASSERT_EQ(f.b().current(), std::size_t{0});
    f.run(10);
    ASSERT_TRUE(!f.b().busy());
    ASSERT_EQ(f.b().current(), std::size_t{1});
    ASSERT_EQ(f.b().time_added_total_ms(), 0);
    ASSERT_EQ(f.b().timer_bar().frame, 0);
}

TEST(blitz_mark_grows_sideways_from_its_left_edge) {
    Fixture f(two());
    f.enter("caret");
    f.run(blitz_time_added_ms(5) + BLITZ_SWITCH_MS);
    f.b().press("BUTTON_GiveUp");
    f.b().dialog_press("BUTTON_YES");
    const auto& w0 = f.b().words()[0];
    while (w0.reveal_elapsed_ms < 0) f.b().update(10);
    ASSERT_TRUE(w0.mark.anchor == Anchor::TopLeft);
    ASSERT_TRUE(near(w0.mark.x, 130.0f) && near(w0.mark.y, 245.0f));
    ASSERT_TRUE(w0.mark.scale_x < 0.01f && near(w0.mark.scale, 1.0f) && near(w0.mark.scale_y, 1.0f));
    // The solved word's small tiles show at once; only the 60 px balls fade.
    int faint = 0;
    for (const auto& b : f.b().balls()) {
        if (b.alpha >= 0.1f) continue;
        ASSERT_TRUE(b.small);
        ++faint;
    }
    ASSERT_TRUE(faint >= 5);
    while (w0.reveal_elapsed_ms < BLITZ_MARK_GROW_MS) f.b().update(10);
    ASSERT_TRUE(std::fabs(w0.mark.scale_x - BLITZ_MARK_OVERSHOOT) < 0.01f);
    f.run(BLITZ_MARK_SETTLE_MS + 10);
    ASSERT_TRUE(near(w0.mark.scale_x, 1.0f) && near(w0.mark.alpha, 1.0f));
}

TEST(blitz_switch_scales_with_smoothstep_and_slides_linearly) {
    Fixture f(two());
    f.b().key_word(true);
    f.run(100);   // t = 0.25: smoothstep 0.15625
    const auto& w1 = f.b().words()[1];
    ASSERT_TRUE(near(w1.scale, BLITZ_LIST_SCALE + (1.0f - BLITZ_LIST_SCALE) * 0.15625f));
    ASSERT_TRUE(near(w1.x, 10.0f + (320.5f - 10.0f) * 0.25f));
    ASSERT_TRUE(near(w1.entry_alpha, 0.25f));
}

TEST(blitz_list_look_follows_the_switch) {
    Fixture f(two());
    ASSERT_TRUE(!f.b().words()[0].list_look && f.b().words()[1].list_look);
    ASSERT_EQ(f.b().blitz_dots().size(), std::size_t{5});
    int small = 0;
    for (const auto& b : f.b().balls()) small += b.small;
    ASSERT_EQ(small, 5);
    f.b().key_word(true);
    f.run(10);
    ASSERT_TRUE(!f.b().words()[1].list_look);   // off as the move up starts
    f.run(380);
    ASSERT_TRUE(!f.b().words()[0].list_look);   // still moving down
    f.run(10);
    ASSERT_TRUE(f.b().words()[0].list_look);    // on when the scale ends
    // With letters in the row the switch waits 500 ms first.
    Fixture g(two());
    g.type("c");
    g.b().key_word(true);
    g.run(490);
    ASSERT_TRUE(g.b().words()[1].list_look);
    g.run(20);
    ASSERT_TRUE(!g.b().words()[1].list_look);
}

TEST(blitz_hint_alpha_is_capped_by_the_row_not_multiplied) {
    Fixture f(two(), 80000);
    f.run(9000);
    f.run(BLITZ_HINT_FADE_MS);
    ASSERT_EQ(f.b().hint_letters().size(), std::size_t{1});
    ASSERT_TRUE(near(f.b().hint_letters()[0].alpha, BLITZ_HINT_ALPHA));
    f.b().key_word(true);
    f.run(BLITZ_SWITCH_MS);
    ASSERT_TRUE(f.b().hint_letters().empty());   // the row is hidden in the list
    f.b().key_word(true);   // two words: back to word 0
    f.run(100);
    ASSERT_TRUE(near(f.b().hint_letters()[0].alpha, 0.25f));   // the row's alpha, not 0.098
    f.run(100);
    ASSERT_TRUE(near(f.b().hint_letters()[0].alpha, BLITZ_HINT_ALPHA));   // capped, not 0.196
}

TEST(blitz_hand_cursor_skips_solved_racks_in_play) {
    Fixture f(two());
    f.b().mouse_move(20.0f, 290.0f);   // word 1, unsolved, in the list
    ASSERT_TRUE(f.b().pointer_over_clickable());
    f.enter("caret");
    f.run(blitz_time_added_ms(5) + BLITZ_SWITCH_MS);
    f.b().mouse_move(20.0f, 250.0f);   // word 0, solved
    ASSERT_TRUE(!f.b().pointer_over_clickable());
    // After the round every rack opens its definition.
    f.b().press("BUTTON_GiveUp");
    f.b().dialog_press("BUTTON_YES");
    f.run(BLITZ_SWITCH_MS);
    f.b().mouse_move(20.0f, 250.0f);
    ASSERT_TRUE(f.b().pointer_over_clickable());
}

TEST(blitz_focus_loss_under_a_dialog_opens_the_menu_later) {
    Fixture f(two());
    f.b().press("BUTTON_GiveUp");
    f.b().focus_lost();
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::ConfirmGiveUp);
    ASSERT_TRUE(f.b().menu_pending());
    f.b().dialog_press("BUTTON_NO");
    f.run(10);
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(!f.b().menu_pending());
    // Under the menu's own dialogs nothing is queued.
    f.b().dialog_press("BUTTON_HOWTOPLAY");
    f.b().focus_lost();
    ASSERT_TRUE(!f.b().menu_pending());
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::HowToPlay);
}

// Android: going to the background pauses Lightning and saves it at once
// (T2.2, docs/plans/android-support.md).
TEST(blitz_background_pauses_and_saves_with_the_same_time) {
    Fixture f({{"crate", false}, {"trace", false}}, 41000, 250);
    f.b().enter_background();
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    ASSERT_EQ(f.saved[0].time_ms, f.b().time_left_ms());
    const int frozen = f.b().time_left_ms();
    f.run(20000);
    ASSERT_EQ(f.b().time_left_ms(), frozen);               // the timer stopped
}

TEST(blitz_background_under_a_dialog_still_saves) {
    Fixture f(two());
    f.b().press("BUTTON_GiveUp");                          // "give up?" is up
    f.b().enter_background();
    ASSERT_TRUE(f.b().dialog_kind() == GameDialog::ConfirmGiveUp);
    ASSERT_TRUE(f.b().menu_pending());                     // the menu follows when it closes
    ASSERT_EQ(f.saved.size(), std::size_t{1});
}
