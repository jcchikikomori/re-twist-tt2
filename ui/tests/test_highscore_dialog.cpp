// DIALOG_HIGHSCORES: openers, tabs, rows and the blinking new entry
// (highscore_dialog.hpp), the tab each opener starts on, plus
// DIALOG_CONGRATS' letterballs.
#include "test_harness.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/wotd_day.hpp"
#include "tt2ui/bitmap_font.hpp"
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

// FONT_FONT_HIGHSCORE_TEXT, the rows' font, from the committed placeholder.
// The imported original measures the same for dates: 7 px per digit, 4 px
// per '-', no extra spacing.
BitmapFont highscore_text_font() {
    std::ifstream in(std::string(TT2_SOURCE_DATA_DIR) + "/placeholder/fonts/font_highscore_text.xml");
    std::stringstream ss;
    ss << in.rdbuf();
    BitmapFont f;
    ASSERT_TRUE(BitmapFont::parse(ss.str(), f));
    return f;
}
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

// X1: the wotd row's extra is the W1 date, "yyyy'-'MM'-'dd" cut from
// 0x43ff60 (game-scene.md §8.7, W26, W27). Every date with a four-digit
// year is 10 characters; 9999-12-31 is the last one civil_date writes.
// LABEL_EXTRA has no width in dialogs.xml (no wrap, no truncate), so the
// centred text must fit on its own: 64 px around x 503 is 471..535, inside
// TAB_PAGE02 (191 + 383 = 574) and the 614 px dialog.
TEST(highscore_dialog_the_longest_date_fits_the_date_column) {
    Scores s;
    const std::string longest = tt2::civil_date(tt2::days_from_civil(9999, 12, 31));
    ASSERT_EQ(longest, std::string("9999-12-31"));
    s.hs.submit(tt2::GameMode::WordOfTheDay, "Mugwump", 20000, longest);
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::WordOfTheDay);
    const TextLabel& date = *d.dialog().label("ROW_EXTRA_0");
    ASSERT_EQ(date.text, longest);
    ASSERT_EQ(label(d, "HEADER_EXTRA").text, std::string("Date:"));
    const BitmapFont font = highscore_text_font();
    ASSERT_EQ(font.measure(date.text, date.char_spacing), 64);   // 8 digits x 7 + 2 dashes x 4
    const SpriteState& page = *d.dialog().sprite_named("TAB_PAGE02");
    const float page_right = page.left() + static_cast<float>(page.sheet.frame_w);
    ASSERT_TRUE(std::fabs(page_right - 574.0f) < 0.01f);
    const float half = static_cast<float>(font.measure(date.text, date.char_spacing)) / 2.0f;
    ASSERT_TRUE(std::fabs(date.x - 503.0f) < 0.01f);
    ASSERT_TRUE(date.x - half >= page.left());
    ASSERT_TRUE(date.x + half <= page_right);
    ASSERT_TRUE(date.x + half <= static_cast<float>(HighScoreDialog::W));
    // The bound bites: a locale long date (GetDateFormatA DATE_LONGDATE),
    // which the exe does not use, would run past the page.
    ASSERT_TRUE(date.x + static_cast<float>(font.measure("Wednesday, October 22, 2026")) / 2.0f > page_right);
}

// crossword.md §5.3: game over 0x41f800 stores the round as "%d"
// (0x449c90, INFERRED as in Blitz); the fill 0x4068d0 heads every table
// but wotd "Round:" (TEXT_HIGHSCORE_EXTRA, profiles.md §4.1).
TEST(highscore_dialog_crossword_tab_shows_the_round) {
    Scores s;
    s.hs.submit(tt2::GameMode::Crossword, "Mugwump", 20000, "3");
    HighScoreDialog d(s.hs, HighScoreDialog::Opener::Menu, tt2::GameMode::Crossword);
    ASSERT_EQ(d.dialog().sprite_named("TAB_PAGE01")->frame, 4);
    ASSERT_EQ(label(d, "HEADER_EXTRA").text, std::string("Round:"));
    ASSERT_EQ(label(d, "ROW_NAME_0").text, std::string("Mugwump"));
    ASSERT_EQ(label(d, "ROW_EXTRA_0").text, std::string("3"));
}

// 0x42d1b9: the menu ctor stores Timed (0) in the mode global 0x5fdea8, so
// the menu's first HIGH SCORES opens on TIMED (TAB_PAGE01 frame 1).
TEST(highscore_dialog_from_the_menu_opens_on_timed_at_launch) {
    Scores s;
    HighScoreDialog d = HighScoreDialog::from_menu(s.hs, s.sink());
    ASSERT_TRUE(d.opener() == HighScoreDialog::Opener::Menu);
    ASSERT_TRUE(d.mode() == tt2::GameMode::Timed);
    ASSERT_EQ(d.dialog().sprite_named("TAB_PAGE01")->frame, 1);
    ASSERT_EQ(d.dialog().button("SWITCH_TIMED")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_TRUE(!d.dialog().button("BUTTON_MAINMENU")->sprite.visible);
}

// profiles.md §4.4: the menu's enter 0x42d9b0 stores Timed (0) in 0x5fdea8
// every time the menu comes back (@0x42dc27), so a Word of the Day or a
// Crossword game just played, its score the newest entry, does not move the
// menu's tab: TIMED, with no "Date:" header and nothing blinking.
TEST(highscore_dialog_from_the_menu_opens_on_timed_after_any_game) {
    Scores s;
    ASSERT_EQ(s.hs.submit(tt2::GameMode::WordOfTheDay, "Mugwump", 20000, "2026-10-22"), 0);
    HighScoreDialog after_wotd = HighScoreDialog::from_menu(s.hs);
    ASSERT_TRUE(after_wotd.mode() == tt2::GameMode::Timed);
    ASSERT_EQ(after_wotd.dialog().sprite_named("TAB_PAGE01")->frame, 1);
    ASSERT_EQ(after_wotd.dialog().button("SWITCH_TIMED")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(after_wotd.dialog().button("SWITCH_WOD")->sprite.frame, 0);
    ASSERT_EQ(label(after_wotd, "HEADER_EXTRA").text, std::string("Round:"));
    ASSERT_EQ(after_wotd.blinking_row(), -1);

    ASSERT_EQ(s.hs.submit(tt2::GameMode::Crossword, "Mugwump", 20000, "3"), 0);
    HighScoreDialog after_crossword = HighScoreDialog::from_menu(s.hs);
    ASSERT_TRUE(after_crossword.mode() == tt2::GameMode::Timed);
    ASSERT_EQ(after_crossword.dialog().sprite_named("TAB_PAGE01")->frame, 1);
    ASSERT_EQ(after_crossword.dialog().button("SWITCH_TIMED")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(after_crossword.dialog().button("SWITCH_CROSSWORD")->sprite.frame, 0);
    ASSERT_EQ(after_crossword.blinking_row(), -1);
    // The played table is one tab away, and only the first fill blinks.
    after_crossword.press("SWITCH_CROSSWORD");
    ASSERT_EQ(after_crossword.dialog().sprite_named("TAB_PAGE01")->frame, 4);
    ASSERT_EQ(label(after_crossword, "ROW_EXTRA_0").text, std::string("3"));
    ASSERT_EQ(after_crossword.blinking_row(), -1);
}

// Negative: the game's copy, opened by DIALOG_CONGRATS' OK (0x428540),
// stays on the mode being played. 0x5fdea8 keeps the mode its menu button
// stored until the menu's next enter (no other write in the exe), and each
// scene passes its own mode (wotd_high_score_stores_the_utc_date and
// crossword_game_over_submits_the_round_to_the_crossword_table pin that
// path). The new entry blinks on that tab.
TEST(highscore_dialog_from_the_game_opens_on_the_mode_played) {
    Scores s;
    ASSERT_EQ(s.hs.submit(tt2::GameMode::WordOfTheDay, "Mugwump", 20000, "2026-10-22"), 0);
    HighScoreDialog wotd(s.hs, HighScoreDialog::Opener::Game, tt2::GameMode::WordOfTheDay);
    ASSERT_TRUE(wotd.mode() == tt2::GameMode::WordOfTheDay);
    ASSERT_EQ(wotd.dialog().sprite_named("TAB_PAGE01")->frame, 2);
    ASSERT_EQ(wotd.dialog().button("SWITCH_WOD")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(wotd.dialog().button("SWITCH_TIMED")->sprite.frame, 0);
    ASSERT_EQ(label(wotd, "HEADER_EXTRA").text, std::string("Date:"));
    ASSERT_EQ(label(wotd, "ROW_EXTRA_0").text, std::string("2026-10-22"));
    ASSERT_EQ(wotd.blinking_row(), 0);

    ASSERT_EQ(s.hs.submit(tt2::GameMode::Crossword, "Mugwump", 20000, "3"), 0);
    HighScoreDialog crossword(s.hs, HighScoreDialog::Opener::Game, tt2::GameMode::Crossword);
    ASSERT_TRUE(crossword.mode() == tt2::GameMode::Crossword);
    ASSERT_EQ(crossword.dialog().sprite_named("TAB_PAGE01")->frame, 4);
    ASSERT_EQ(crossword.dialog().button("SWITCH_CROSSWORD")->sprite.frame, SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(crossword.dialog().button("SWITCH_TIMED")->sprite.frame, 0);
    ASSERT_EQ(label(crossword, "HEADER_EXTRA").text, std::string("Round:"));
    ASSERT_EQ(label(crossword, "ROW_EXTRA_0").text, std::string("3"));
    ASSERT_EQ(crossword.blinking_row(), 0);
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
