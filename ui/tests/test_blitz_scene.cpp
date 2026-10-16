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
        scene = std::make_unique<BlitzScene>(com, ful, profile, &hs, s, 5, words.empty() ? nullptr : &g);
    }
    BlitzScene& b() { return *scene; }
    void type(const std::string& w) { for (char c : w) scene->type_letter(c); }
    void enter(const std::string& w) { type(w); scene->key_return(); }
    void run(int ms) { for (int t = 0; t < ms; t += 10) scene->update(10); }
    bool played(SoundId id) const { return std::find(sounds.begin(), sounds.end(), id) != sounds.end(); }
    int count(SoundId id) const { return static_cast<int>(std::count(sounds.begin(), sounds.end(), id)); }
};

std::vector<tt2::SavedGame::BlitzWord> two() { return {{"crate", false}, {"trace", false}}; }

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
    f.run(BLITZ_TIME_ADDED_MS);
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
    ASSERT_TRUE(near(f.b().timer_bar().scale_x, 9900.0f / 80300.0f));
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
    f.run(BLITZ_TIME_ADDED_MS + BLITZ_SWITCH_MS);
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
    BlitzScene b(f.com, f.ful, f.profile, nullptr, s, 1, &g);
    ASSERT_EQ(b.round_number(), 1);
    ASSERT_EQ(b.words().size(), std::size_t{5});
}

TEST(blitz_panel_frames_the_list) {
    const auto p = blitz_panel(5);
    ASSERT_EQ(p.size(), std::size_t{6});
    ASSERT_TRUE(near(p[0].y, 175.0f));                     // 10 px above the first rack
    ASSERT_TRUE(near(p[0].scale_y * 600.0f, 210.0f));     // to 185 + 5 * 40
}
