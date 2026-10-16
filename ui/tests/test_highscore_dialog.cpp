// DIALOG_HIGHSCORES: openers, tabs, rows and the blinking new entry
// (highscore_dialog.hpp), plus DIALOG_CONGRATS' letterballs.
#include "test_harness.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2ui/game_scene.hpp"
#include "tt2ui/highscore_dialog.hpp"

using namespace tt2ui;

namespace {
struct Scores {
    std::vector<std::string> names{"Susan", "Karen", "Linda"};
    tt2::Random rng{1};
    tt2::HighScoreManager hs{names, rng};
    std::vector<SoundId> sounds;
    std::function<void(SoundId)> sink() {
        return [this](SoundId id) { sounds.push_back(id); };
    }
};
const TextLabel& label(const HighScoreDialog& d, const std::string& name) { return *d.dialog().label(name); }
}  // namespace

TEST(highscore_dialog_from_the_menu_shows_ok_only) {
    Scores s;
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::Untimed);
    ASSERT_TRUE(d.dialog().button("BUTTON_OK")->sprite.visible);
    ASSERT_TRUE(!d.dialog().button("BUTTON_MAINMENU")->sprite.visible);
    ASSERT_TRUE(!d.dialog().button("BUTTON_MAINMENU")->enabled);
    ASSERT_TRUE(d.press("BUTTON_MAINMENU") == HighScoreDialog::Result::None);
    ASSERT_TRUE(d.press("BUTTON_OK") == HighScoreDialog::Result::Close);
}

TEST(highscore_dialog_after_a_game_shows_main_menu_only) {
    Scores s;
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Game, tt2::GameMode::Timed, s.sink());
    ASSERT_TRUE(!d.dialog().button("BUTTON_OK")->sprite.visible);
    ASSERT_TRUE(d.press("BUTTON_OK") == HighScoreDialog::Result::None);
    ASSERT_TRUE(s.sounds.empty());
    ASSERT_TRUE(d.press("BUTTON_MAINMENU") == HighScoreDialog::Result::MainMenu);
    ASSERT_EQ(s.sounds.size(), std::size_t{1});
}

TEST(highscore_dialog_fills_ten_rows_from_the_table) {
    Scores s;
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::Timed);
    // The prefilled table: 10000 at the top, 1000 at the bottom, "%d".
    ASSERT_EQ(label(d, "ROW_SCORE_0").text, std::string("10000"));
    ASSERT_EQ(label(d, "ROW_SCORE_9").text, std::string("1000"));
    ASSERT_EQ(label(d, "ROW_NAME_0").text, s.hs.table(tt2::GameMode::Timed).entries[0].name);
    // Rows 30 px apart from (216, 107); the name centres in its 136 px.
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_0").y - 107.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_9").y - 377.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_0").x - 284.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(label(d, "ROW_SCORE_0").x - 375.0f) < 0.01f);
    ASSERT_EQ(label(d, "HEADER_EXTRA").text, std::string("Round:"));
}

TEST(highscore_dialog_tabs_switch_tables_and_the_date_header) {
    Scores s;
    s.hs.submit(tt2::GameMode::WordOfTheDay, "Mugwump", 20000, "Oct 16, 2026");
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::Untimed, s.sink());
    ASSERT_EQ(d.dialog().sprite_named("TAB_PAGE01")->frame, 0);
    d.press("SWITCH_WOD");
    ASSERT_TRUE(d.mode() == tt2::GameMode::WordOfTheDay);
    ASSERT_EQ(d.dialog().sprite_named("TAB_PAGE01")->frame, 2);
    ASSERT_EQ(label(d, "HEADER_EXTRA").text, std::string("Date:"));
    ASSERT_EQ(label(d, "ROW_NAME_0").text, std::string("Mugwump"));
    ASSERT_EQ(label(d, "ROW_EXTRA_0").text, std::string("Oct 16, 2026"));
    ASSERT_EQ(s.sounds.size(), std::size_t{1});
    // The checked tab uses the "on" rows of TABS.
    ASSERT_EQ(d.dialog().button("SWITCH_WOD")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(d.dialog().button("SWITCH_UNTIMED")->sprite.frame, 0);
    d.press("SWITCH_CROSSWORD");
    ASSERT_EQ(d.dialog().sprite_named("TAB_PAGE01")->frame, 4);
    ASSERT_EQ(label(d, "HEADER_EXTRA").text, std::string("Round:"));
}

TEST(highscore_dialog_blinks_the_newest_entry_once) {
    Scores s;
    const int rank = s.hs.submit(tt2::GameMode::Timed, "Mugwump", 4500, "3");
    ASSERT_EQ(rank, 6);
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Game, tt2::GameMode::Timed);
    ASSERT_EQ(d.blinking_row(), 6);
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_6").alpha - 0.5f) < 0.001f);
    d.tick(300);
    ASSERT_TRUE(std::fabs(label(d, "ROW_SCORE_6").alpha - 1.0f) < 0.001f);
    d.tick(200);
    ASSERT_TRUE(std::fabs(label(d, "ROW_EXTRA_6").alpha - 0.5f) < 0.001f);
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_5").alpha - 1.0f) < 0.001f);
    // A tab change stops it, and coming back does not start it again.
    d.press("SWITCH_UNTIMED");
    d.press("SWITCH_TIMED");
    ASSERT_EQ(d.blinking_row(), -1);
    ASSERT_TRUE(std::fabs(label(d, "ROW_NAME_6").alpha - 1.0f) < 0.001f);
}

TEST(highscore_dialog_without_a_new_entry_does_not_blink) {
    Scores s;
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::Timed);
    ASSERT_EQ(d.blinking_row(), -1);
}

TEST(congrats_letterballs_spell_awesome_on_seven_sprites) {
    const auto balls = congrats_balls(CONGRATS_LETTERBALLS);
    ASSERT_EQ(balls.size(), std::size_t{7});
    ASSERT_EQ(balls[0].letter, 'a');
    ASSERT_EQ(balls[6].letter, 'e');
    ASSERT_TRUE(std::fabs(balls[0].x - 17.0f) < 0.01f && std::fabs(balls[0].y - 99.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(balls[6].x - 365.0f) < 0.01f);
    ASSERT_EQ(balls[0].reveal_delay_ms, 500);
    ASSERT_EQ(balls[6].reveal_delay_ms, 1700);
    // Next round keeps its own eight sprites.
    ASSERT_EQ(next_round_balls(NEXT_ROUND_LETTERBALLS).size(), std::size_t{8});
}
