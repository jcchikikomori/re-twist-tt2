// Crossword scene (crossword_scene.hpp): the dialogs.xml layout, the button
// refresh of crossword.md §3.8 (AC-C1), the UI bar without a clock, the
// shared CRT generator (D-11) and the pause menu (plan T6b.1); the grid
// view (AC-C2), typing with locked cells skipped (AC-C13), ENTER and
// scoring (AC-C3, AC-C4), the 700 ms advance, the solved puzzle, GIVE UP
// and the game over (AC-C8) (plan T6b.3); TWIST and the grid's turn
// (AC-C5), ZOOM and the zoom-in flag (AC-C6), the arrows, row clicks and
// the highlight (AC-C7), saving and resuming (AC-C9), trophies 8 / 9
// (AC-C10), the tutorial (AC-C12), popups 4 / 7 and the click take-back
// (AC-C13) and the definition links (AC-C14) (plan T6b.4).
//
// The default fixture (LCG seed 5, CRT seed 1) deals, in placement order:
//   0 "planet"  across, a 6..11 on b 5 (the root)
//   1 "nap"     down a 8, b 4..6, its "a" on the root's
//   2 "peal"    down a 6, b 5..8, its "p" on the root's
//   3 "lap"     across b 7, a 5..7, its "a" on peal's
// so a full solve scores 360 + 90 + 160 + 90 + 1000 = 1700 (10 x len^2,
// +1000 on the last word, crossword.md §7).

#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/random.hpp"
#include "tt2/settings.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/crossword_scene.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/settings_dialog.hpp"
#include "tt2ui/tooltip.hpp"

using namespace tt2ui;

namespace {

// One pool word ("planet", no other full-length anagram) with plenty of
// 3-4 letter sub-anagrams; nothing else is 5..8 letters, so every round's
// root is "planet" (as in engine/tests/test_round_crossword.cpp).
tt2::WordTree common() {
    return tt2::WordTree::from_words({
        "planet",
        "plan", "plat", "lane", "lean", "pane", "peal", "plea", "pale",
        "leap", "neat", "lent", "pant", "pelt", "tale", "teal", "late",
        "ant", "ape", "apt", "eat", "tan", "tap", "ten", "net", "pen",
        "pet", "pat", "nap", "lap", "alp", "let", "ale", "tea",
    });
}
tt2::WordTree full() {
    auto words = common().words();
    words.push_back("nape");   // full only
    return tt2::WordTree::from_words(words);
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

const std::vector<std::string> kButtons = {
    "BUTTON_Menu", "BUTTON_Twist", "BUTTON_Enter", "BUTTON_LastWord", "BUTTON_Clear",
    "BUTTON_NextRound", "BUTTON_Zoom", "BUTTON_GiveUp", "BUTTON_NEXTWORD", "BUTTON_PREVWORD",
};

struct Fixture {
    tt2::WordTree com = common();
    tt2::WordTree ful = full();
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
    std::vector<std::string> names{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    tt2::Random hs_rng{1};
    tt2::HighScoreManager hs{names, hs_rng};
    tt2::CrtRandom crt;
    tt2::GameSettings settings;
    std::vector<SoundId> sounds;
    std::vector<std::string> urls;
    std::vector<tt2::SavedGame> saved;
    int exits = 0, changes = 0, settings_commits = 0, discards = 0;
    std::unique_ptr<CrosswordScene> scene;

    // `seed`: the scene's LCG; `crt_seed`: the shared CRT generator. The
    // round opens with the root's 500 ms pan, which is busy (0x41d274), so
    // the fixture waits it out.
    explicit Fixture(std::int64_t seed = 5, std::uint32_t crt_seed = 1) : crt(crt_seed) { start(seed); }
    // Another dictionary: `words` is the common list, the full list adds
    // nothing.
    Fixture(const std::vector<std::string>& words, std::int64_t seed)
        : com(tt2::WordTree::from_words(words)), ful(tt2::WordTree::from_words(words)), crt(1) {
        start(seed);
    }
    void start(std::int64_t seed) {
        profile.mode_flags.fill(true);   // tutorials seen
        scene = std::make_unique<CrosswordScene>(com, ful, profile, &hs, services(), seed, crt);
        settle();
    }
    CrosswordScene::Services services() {
        CrosswordScene::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_exit = [this] { ++exits; };
        s.on_profile_change = [this] { ++changes; };
        s.discard_save = [this] { ++discards; };
        s.save_game = [this](const tt2::SavedGame& g) { saved.push_back(g); };
        s.open_url = [this](const std::string& u) { urls.push_back(u); };
        s.settings.current = [this] { return settings; };
        s.settings.commit = [this](const tt2::GameSettings& g) { settings = g; ++settings_commits; };
        return s;
    }
    CrosswordScene& c() { return *scene; }
    const tt2::CrosswordRound& r() { return scene->round(); }
    const MenuButton& b(const std::string& name) { return *scene->button(name); }
    void run(int ms) { for (int t = 0; t < ms; t += 10) scene->update(10); }
    // Run until the view's chain (pan, turn, zoom) or the reveal ends.
    void settle() { for (int t = 0; t < 20000 && scene->busy(); t += 10) scene->update(10); }
    void type(const std::string& w) { for (char ch : w) scene->type_letter(ch); }
    void enter(const std::string& w) {
        type(w);
        scene->key_return();
    }
    // The selected word's answer without its locked letters: what a player
    // types to solve it, since locked cells are skipped.
    std::string answer_to_type() {
        const std::size_t sel = r().selected();
        const std::string& w = r().placements()[sel].word;
        std::string out;
        for (std::size_t i = 0; i < w.size(); ++i)
            if (r().locked_letter(sel, i) == '\0') out.push_back(w[i]);
        return out;
    }
    // Solve the selected word with its answer, then wait out the advance
    // (the queued select runs on the frame after the 700 ms) and the
    // select's pan and turn.
    void solve_and_advance() {
        enter(answer_to_type());
        run(CROSSWORD_ADVANCE_MS + 10);
        settle();
    }
    CrosswordCellView cell(int a, int b) { return scene->cells()[static_cast<std::size_t>(a * 11 + b)]; }
    bool played(SoundId id) const { return std::find(sounds.begin(), sounds.end(), id) != sounds.end(); }
    void click(const std::string& name) {
        const SpriteState& s = b(name).sprite;
        const float x = s.left() + s.sheet.frame_w * 0.5f, y = s.top() + s.sheet.frame_h * 0.5f;
        scene->mouse_move(x, y);
        scene->mouse_down(x, y);
        scene->mouse_up(x, y);
    }
    // The centre of a button of the open dialog, in screen coordinates (its
    // sprite is placed relative to the dialog's top-left corner).
    std::pair<float, float> dialog_point(const std::string& name) {
        const Dialog& d = scene->dialog();
        const SpriteState& s = d.button(name)->sprite;
        return {d.left() + s.left() + s.sheet.frame_w * 0.5f, d.top() + s.top() + s.sheet.frame_h * 0.5f};
    }
    int count(SoundId id) const { return static_cast<int>(std::count(sounds.begin(), sounds.end(), id)); }
    // A full click at a screen point (the grid takes the press, 0x421e50).
    void click_at(float x, float y) {
        scene->mouse_move(x, y);
        scene->mouse_down(x, y);
        scene->mouse_up(x, y);
    }
    void click_cell(int a, int b) {
        const auto [x, y] = scene->cell_center(a, b);
        click_at(x, y);
    }
    // Solve the four words with their answers from the root: 1700 points.
    void solve_all() {
        for (int i = 0; i < 3; ++i) solve_and_advance();
        enter(answer_to_type());
    }
};

// A profile with no tutorial seen yet.
tt2::PlayerProfile new_player() {
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Newbie", 7u);
    p.mode_flags.fill(false);
    return p;
}

}  // namespace

// AC-C1: every button at its dialogs.xml SCENE_CRISSCROSS place (lines
// 147-165), with the image and frame size its sheet is cut with.
TEST(crossword_buttons_sit_at_the_dialogs_xml_positions) {
    Fixture f;
    struct Want { const char* name; const char* image; float x, y; int w, h; };
    const std::vector<Want> want = {
        {"BUTTON_Menu", "BUTTON_MENU", 10, 554, 119, 50},
        {"BUTTON_Twist", "BUTTON_Twist", 154, 447, 119, 50},
        {"BUTTON_Enter", "BUTTON_Enter", 526, 447, 119, 50},
        {"BUTTON_LastWord", "BUTTON_LastWord", 402, 447, 119, 50},
        {"BUTTON_Clear", "BUTTON_Clear", 278, 447, 119, 50},
        {"BUTTON_NextRound", "BUTTON_NextRound", 321, 497, 157, 52},
        {"BUTTON_Zoom", "BUTTON_ZOOM", 706, 244, 68, 74},
        {"BUTTON_GiveUp", "BUTTON_GiveUp", 321, 497, 157, 52},
        {"BUTTON_NEXTWORD", "IMAGE_BTN_ARROW_NEXT", 722, 125, 52, 84},
        {"BUTTON_PREVWORD", "IMAGE_BTN_ARROW_PREV", 26, 125, 52, 84},
    };
    ASSERT_EQ(f.c().buttons().size(), want.size());
    for (const auto& w : want) {
        const MenuButton* b = f.c().button(w.name);
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(b->sprite.sheet.image_id, std::string(w.image));
        ASSERT_TRUE(near(b->sprite.left(), w.x) && near(b->sprite.top(), w.y));
        ASSERT_EQ(b->sprite.sheet.frame_w, w.w);
        ASSERT_EQ(b->sprite.sheet.frame_h, w.h);
        ASSERT_EQ(b->sprite.sheet.frames, 4);
    }
    // Negative: no Lightning or Classic button slipped in.
    ASSERT_TRUE(f.c().button("BUTTON_Pass") == nullptr);
    ASSERT_TRUE(f.c().button("BUTTON_Done") == nullptr);
}

// AC-C1 / §3.8 in play: NEXT ROUND hidden, GIVE UP shown; with an empty row
// and no last word, ENTER, CLEAR and LAST WORD are off, the rest on.
TEST(crossword_buttons_in_play_follow_the_refresh) {
    Fixture f;
    f.run(10);
    for (const auto& n : kButtons)
        ASSERT_EQ(f.b(n).sprite.visible, n != "BUTTON_NextRound");
    for (const char* on : {"BUTTON_Menu", "BUTTON_Twist", "BUTTON_GiveUp", "BUTTON_Zoom", "BUTTON_NEXTWORD",
                           "BUTTON_PREVWORD"})
        ASSERT_TRUE(f.b(on).enabled);
    for (const char* off : {"BUTTON_Enter", "BUTTON_Clear", "BUTTON_LastWord", "BUTTON_NextRound"})
        ASSERT_TRUE(!f.b(off).enabled);
    // The disabled ones show frame 3 of their sheet.
    ASSERT_EQ(f.b("BUTTON_Enter").sprite.frame, 3);
    ASSERT_EQ(f.b("BUTTON_Twist").sprite.frame, 0);
}

// §3.8 lock: a dialog turns all ten buttons off and leaves their
// visibility alone; closing it turns them back on.
TEST(crossword_dialog_disables_every_button_but_keeps_visibility) {
    Fixture f;
    f.c().press("BUTTON_Menu");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    f.run(10);
    for (const auto& n : kButtons) ASSERT_TRUE(!f.b(n).enabled);
    ASSERT_TRUE(f.b("BUTTON_GiveUp").sprite.visible);
    ASSERT_TRUE(!f.b("BUTTON_NextRound").sprite.visible);
    f.c().dialog_press("BUTTON_RESUME");
    f.run(10);
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_TRUE(f.b("BUTTON_GiveUp").enabled && f.b("BUTTON_Menu").enabled);
}

// Negative (AC-C1): NEXT ROUND never shows in this scene (0x41b510), and a
// press on it does nothing.
TEST(crossword_next_round_stays_hidden) {
    Fixture f;
    f.run(500);
    f.c().press("BUTTON_NextRound");
    f.c().focus_lost();
    f.c().key_escape();
    f.run(100);
    ASSERT_TRUE(!f.b("BUTTON_NextRound").sprite.visible);
    ASSERT_TRUE(!f.b("BUTTON_NextRound").enabled);
    ASSERT_TRUE(!f.c().dialog_open());   // the press opened nothing; Esc closed the menu
}

// The XML sprites: OVERLAY full screen and UI_BAR at y 547 show; the XML
// highlight and the panel pieces are visible="false" and stay hidden.
TEST(crossword_overlay_and_ui_bar_show_at_their_places) {
    Fixture f;
    const SpriteState* overlay = f.c().find_sprite("OVERLAY");
    const SpriteState* bar = f.c().find_sprite("UI_BAR");
    ASSERT_TRUE(overlay && bar);
    ASSERT_EQ(overlay->sheet.image_id, std::string("IMAGE_OVERLAY"));
    ASSERT_TRUE(overlay->visible && near(overlay->x, 0) && near(overlay->y, 0));
    ASSERT_EQ(overlay->sheet.frame_w, 800);
    ASSERT_EQ(overlay->sheet.frame_h, 600);
    ASSERT_EQ(bar->sheet.image_id, std::string("IMAGE_UI_BAR"));
    ASSERT_TRUE(bar->visible && near(bar->x, 0) && near(bar->y, 547));
    // The overlay is drawn before the bar (dialogs.xml order).
    ASSERT_TRUE(overlay < bar);
    ASSERT_EQ(f.c().background_id(), std::string("crosswordscene/BACKGROUND"));
    ASSERT_TRUE(f.c().background_id() != std::string("gamescene/BACKGROUND"));
}

// Negative: the hidden XML sprites stay hidden through updates, the pause
// menu and focus loss.
TEST(crossword_hidden_sprites_stay_hidden) {
    Fixture f;
    const std::vector<std::string> hidden = {"SPRITE_HIGHLIGHT", "PANEL_BG", "PANEL_BOTTOM", "PANEL_LEFTCORNER",
                                             "PANEL_LEFT", "PANEL_RIGHTCORNER", "PANEL_RIGHT"};
    const SpriteState* hl = f.c().find_sprite("SPRITE_HIGHLIGHT");
    ASSERT_TRUE(hl && near(hl->x, 101) && near(hl->y, 105) && hl->sheet.frames == 12);
    const SpriteState* panel = f.c().find_sprite("PANEL_BG");
    ASSERT_TRUE(panel && near(panel->left(), 0) && panel->anchor == Anchor::Top);   // x 400, anchored top
    f.run(1000);
    f.c().focus_lost();
    f.run(100);
    f.c().dialog_press("BUTTON_RESUME");
    f.run(100);
    for (const auto& n : hidden) {
        const SpriteState* s = f.c().find_sprite(n);
        ASSERT_TRUE(s != nullptr);
        ASSERT_TRUE(!s->visible);
    }
    ASSERT_EQ(f.c().sprites().size(), hidden.size() + 2);
    // COMMENTARY and GAME_OVER (alpha 0, never looked up) are not modelled.
    ASSERT_TRUE(f.c().find_sprite("COMMENTARY") == nullptr);
    ASSERT_TRUE(f.c().find_sprite("GAME_OVER") == nullptr);
}

// AC-C1 refined: the UI bar shows SCORE and ROUND, never TIME (0x419d40
// hides TIME_LABEL and both timer labels; crossword.md §4).
TEST(crossword_ui_bar_shows_score_and_round_but_no_time) {
    Fixture f;
    f.run(5000);   // no clock counts down
    const auto labels = f.c().labels();
    auto find = [&](const std::string& name) -> const TextLabel& {
        for (const auto& l : labels) if (l.name == name) return l;
        static const TextLabel none{};
        return none;
    };
    const TextLabel& score_label = find("SCORE_LABEL");
    const TextLabel& score = find("LABEL_Score");
    const TextLabel& round_label = find("LABEL_ROUND (copy)");
    const TextLabel& round = find("LABEL_ROUND");
    ASSERT_TRUE(score_label.visible && score.visible && round_label.visible && round.visible);
    ASSERT_EQ(score_label.text, std::string("SCORE:"));
    ASSERT_TRUE(near(score_label.x, 244) && near(score_label.y, 574) && score_label.align == TextAlign::Right);
    ASSERT_EQ(score.text, std::string("0"));
    ASSERT_TRUE(near(score.x, 251) && score.align == TextAlign::Left && score.font_id == "FONT_UI_NUMBERS");
    ASSERT_EQ(round_label.text, std::string("ROUND:"));
    ASSERT_TRUE(near(round_label.x, 537) && round_label.align == TextAlign::Right);
    ASSERT_EQ(round.text, std::string("1"));
    ASSERT_TRUE(near(round.x, 542) && round.align == TextAlign::Left);
    for (const char* n : {"TIME_LABEL", "LABEL_TimerMinutes", "LABEL_TimerSeconds"}) {
        ASSERT_EQ(find(n).name, std::string(n));   // present, as in the XML
        ASSERT_TRUE(!find(n).visible);
    }
    ASSERT_EQ(labels.size(), std::size_t{7});
}

// A new game deals round 1 on the engine: "planet" is the root and is
// selected first (0x41ba40), the score starts at 0, the bag is saved.
TEST(crossword_new_game_starts_round_one_with_the_root_selected) {
    Fixture f;
    ASSERT_EQ(f.c().round_number(), 1);
    ASSERT_EQ(f.c().score(), 0);
    ASSERT_EQ(f.c().round().root(), std::string("planet"));
    ASSERT_EQ(f.c().round().selected(), std::size_t{0});
    ASSERT_TRUE(!f.c().round().over());
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_TRUE(f.changes >= 1);   // profile.bag moved on
    ASSERT_EQ(f.c().save_mode(), std::string("CROSSWORD"));
    ASSERT_TRUE(!f.c().busy() && !f.c().finished());
}

// D-11: the scene draws from the caller's CrtRandom, not a copy, and never
// reseeds it, so a second game on the same generator continues the
// sequence (crossword.md §9).
TEST(crossword_scene_uses_the_shared_crt_random) {
    Fixture f;   // its scene was built on f.crt, seeded 1
    const std::uint32_t after_first = f.crt.state();
    ASSERT_NE(after_first, tt2::CrtRandom::DEFAULT_SEED);
    CrosswordScene second(f.com, f.ful, f.profile, &f.hs, f.services(), 5, f.crt);
    const std::uint32_t after_second = f.crt.state();
    ASSERT_NE(after_second, after_first);
    // Negative: a fresh generator with the same seed gives the first state
    // again, so the second scene really went on from where the first stopped.
    tt2::CrtRandom fresh{1};
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
    p.mode_flags.fill(true);
    CrosswordScene third(f.com, f.ful, p, &f.hs, f.services(), 5, fresh);
    ASSERT_EQ(fresh.state(), after_first);
}

TEST(crossword_menu_opens_the_pause_menu_and_stop_leaves) {
    Fixture f;
    f.click("BUTTON_Menu");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.count(SoundId::Button), 1);
    f.c().dialog_press("BUTTON_STOP");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::ConfirmExit);
    ASSERT_TRUE(f.c().under_kind() == GameDialog::PauseMenu);
    f.c().key_escape();   // NO: back to the pause menu
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.exits, 0);
    f.c().dialog_press("BUTTON_STOP");
    f.c().key_return();   // YES
    ASSERT_EQ(f.exits, 1);
    ASSERT_TRUE(f.c().finished());
    // Negative: a finished scene ignores the menu.
    f.c().key_escape();
    ASSERT_TRUE(!f.c().dialog_open());
}

TEST(crossword_focus_loss_under_a_dialog_opens_the_menu_once) {
    Fixture f;
    f.c().focus_lost();
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    f.c().dialog_press("BUTTON_STOP");
    f.c().focus_lost();   // the pause menu is under ConfirmExit: no second one
    ASSERT_TRUE(!f.c().menu_pending());
    f.c().dialog_press("BUTTON_NO");
    f.c().dialog_press("BUTTON_RESUME");
    f.run(10);
    ASSERT_TRUE(!f.c().dialog_open());
}

// dialogs.xml:165: the PREV arrow has no clicksound; every other button
// plays SOUND_BUTTON when clicked (crossword.md §1.1, §3.3).
TEST(crossword_prev_arrow_click_plays_no_button_sound) {
    Fixture f;
    f.run(10);
    f.click("BUTTON_PREVWORD");
    ASSERT_EQ(f.count(SoundId::Button), 0);
    f.settle();   // the select's pan (and turn) is busy
    f.click("BUTTON_NEXTWORD");
    ASSERT_EQ(f.count(SoundId::Button), 1);
    f.settle();
    f.click("BUTTON_Zoom");
    ASSERT_EQ(f.count(SoundId::Button), 2);
}

TEST(crossword_hand_cursor_only_over_enabled_buttons) {
    Fixture f;
    f.run(10);
    const SpriteState& twist = f.b("BUTTON_Twist").sprite;
    f.c().mouse_move(twist.left() + 5, twist.top() + 5);
    ASSERT_TRUE(f.c().pointer_over_clickable());
    const SpriteState& enter = f.b("BUTTON_Enter").sprite;   // off: the row is empty
    f.c().mouse_move(enter.left() + 5, enter.top() + 5);
    ASSERT_TRUE(!f.c().pointer_over_clickable());
    f.c().mouse_move(400, 300);
    ASSERT_TRUE(!f.c().pointer_over_clickable());
}

// With a dialog up the mouse goes to its buttons, not the scene's: the hand
// shows over RESUME, a press released off the button does nothing, and a
// full click on RESUME closes the menu.
TEST(crossword_pause_menu_buttons_answer_the_mouse) {
    Fixture f;
    f.click("BUTTON_Menu");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    const auto [rx, ry] = f.dialog_point("BUTTON_RESUME");
    f.c().mouse_move(rx, ry);
    const CrosswordScene& seen = f.c();
    ASSERT_TRUE(seen.pointer_over_clickable());
    ASSERT_TRUE(seen.dialog().button("BUTTON_RESUME")->hover);
    // Negative: pressed on RESUME, released on the empty screen corner.
    f.c().mouse_down(rx, ry);
    f.c().mouse_up(2, 2);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.count(SoundId::Button), 1);   // MENU only
    // Negative: the scene's MENU button under the dialog takes no click.
    f.click("BUTTON_Menu");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu && f.c().under_dialog() == nullptr);
    f.c().mouse_move(rx, ry);
    f.c().mouse_down(rx, ry);
    f.c().mouse_up(rx, ry);
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_EQ(f.count(SoundId::Button), 2);
    ASSERT_EQ(f.exits, 0);
}

// HOW TO PLAY over the pause menu: PREV is hidden on page 1, NEXT and PREV
// turn the page, OK (Return) and Esc go back to the menu, and the menu's
// Return is RESUME.
TEST(crossword_pause_menu_how_to_play_pages_and_returns) {
    Fixture f;
    f.c().press("BUTTON_Menu");
    f.c().dialog_press("BUTTON_HOWTOPLAY");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::HowToPlay);
    ASSERT_TRUE(f.c().under_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(f.c().paused());
    ASSERT_EQ(f.c().dialog().w, HELP_W);
    f.c().dialog_press("BUTTON_PREV");   // hidden on the first page: ignored
    ASSERT_EQ(f.c().help().page, 0);
    f.c().dialog_press("BUTTON_NEXT");
    ASSERT_EQ(f.c().help().page, 1);
    ASSERT_TRUE(f.c().help().fading());
    f.run(HELP_FADE_MS);   // the page fade runs under the pause
    ASSERT_TRUE(!f.c().help().fading());
    f.c().dialog_press("BUTTON_PREV");
    ASSERT_EQ(f.c().help().page, 0);
    f.c().key_return();   // OK
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(f.c().under_dialog() == nullptr);
    f.c().dialog_press("BUTTON_HOWTOPLAY");
    f.c().key_escape();   // also OK
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    f.c().key_return();   // RESUME
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_EQ(f.exits, 0);
}

// SETTINGS over the pause menu: OK keeps the new MAX word length on the
// profile, saves it and commits the settings; the round being played keeps
// its root.
TEST(crossword_pause_menu_settings_ok_saves_the_word_length) {
    Fixture f;
    f.c().press("BUTTON_Menu");
    f.c().dialog_press("BUTTON_SETTINGS");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::Settings);
    ASSERT_TRUE(f.c().under_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(f.c().paused());
    ASSERT_TRUE(f.c().settings_dialog() != nullptr);
    ASSERT_EQ(f.c().dialog().w, SETTINGS_W);
    ASSERT_EQ(f.c().settings_dialog()->max_word_length(), 8);
    const int changes = f.changes;
    f.c().dialog_press(SettingsDialog::length_switch(6));
    f.c().dialog_press("BUTTON_OK");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(f.c().settings_dialog() == nullptr);
    ASSERT_EQ(f.profile.max_word_length, 6);
    ASSERT_EQ(f.changes, changes + 1);
    ASSERT_EQ(f.settings_commits, 1);
    ASSERT_EQ(f.c().round().root(), std::string("planet"));
}

// Negative: CANCEL and Esc leave the profile and the settings alone.
TEST(crossword_pause_menu_settings_cancel_and_escape_change_nothing) {
    Fixture f;
    f.c().press("BUTTON_Menu");
    f.c().dialog_press("BUTTON_SETTINGS");
    f.c().dialog_press(SettingsDialog::length_switch(6));
    f.c().dialog_press("BUTTON_CANCEL");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.profile.max_word_length, 8);
    ASSERT_EQ(f.settings_commits, 0);
    f.c().dialog_press("BUTTON_SETTINGS");
    f.c().dialog_press(SettingsDialog::length_switch(7));
    f.c().key_escape();   // CANCEL
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(f.c().settings_dialog() == nullptr);
    ASSERT_EQ(f.profile.max_word_length, 8);
    ASSERT_EQ(f.settings_commits, 0);
}

// ---- T6b.3: the grid, typing, guesses, scoring and the round end ----------

// AC-C2 over 20 seeds: the view shows the engine's grid. The root "planet"
// lies along `a` on b = 5, centred: a = (18 - 6) / 2 = 6, cells 6..11
// (0x436180). Every other word crosses a word placed before it, at a right
// angle, on a letter they share; replaying the placements through the
// builder's commit (0x435d30) fits and rebuilds the same 20 x 11 cells.
// The view shows no letter yet and highlights the root, selected first.
TEST(crossword_cells_show_the_engine_grid_over_twenty_seeds) {
    std::set<std::string> layouts;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        Fixture f(static_cast<std::int64_t>(seed), seed);
        const auto& ps = f.r().placements();
        const auto cells = f.c().cells();
        ASSERT_EQ(cells.size(), std::size_t{18 * 11});
        ASSERT_TRUE(ps.size() >= 2);
        ASSERT_EQ(ps[0].word, std::string("planet"));
        ASSERT_TRUE(ps[0].horizontal && ps[0].a == 6 && ps[0].b == 5 && ps[0].crossing == -1);

        std::vector<bool> covered(18 * 11, false);
        std::string layout;
        for (std::size_t k = 0; k < ps.size(); ++k) {
            const auto& p = ps[k];
            layout += p.word + "@" + std::to_string(p.a) + "," + std::to_string(p.b) + (p.horizontal ? "H " : "V ");
            for (std::size_t i = 0; i < p.word.size(); ++i) {
                const int a = p.cell_a(static_cast<int>(i)), b = p.cell_b(static_cast<int>(i));
                ASSERT_TRUE(a >= 1 && a <= 17 && b >= 1 && b <= 9);   // inside the fit bounds (0x435f90)
                covered[static_cast<std::size_t>(a * 11 + b)] = true;
            }
            if (k == 0) continue;
            // The crossing letter sits on an earlier, perpendicular word's same letter.
            ASSERT_TRUE(p.crossing >= 0 && p.crossing < static_cast<int>(p.word.size()));
            const int ca = p.cell_a(p.crossing), cb = p.cell_b(p.crossing);
            bool crosses = false;
            for (std::size_t j = 0; j < k; ++j) {
                const int q = ps[j].index_of_cell(ca, cb);
                if (q >= 0 && ps[j].horizontal != p.horizontal &&
                    ps[j].word[static_cast<std::size_t>(q)] == p.word[static_cast<std::size_t>(p.crossing)])
                    crosses = true;
            }
            ASSERT_TRUE(crosses);
        }
        layouts.insert(layout);

        const auto replayed = tt2::CrosswordGrid::replay(ps);
        ASSERT_TRUE(replayed.has_value());
        for (int a = 0; a < tt2::CrosswordGrid::A_CELLS; ++a) {
            for (int b = 0; b < tt2::CrosswordGrid::B_LEN; ++b) {
                ASSERT_EQ(replayed->at(a, b).letter, f.r().grid().at(a, b).letter);
                ASSERT_EQ(replayed->at(a, b).blocked, f.r().grid().at(a, b).blocked);
            }
        }
        for (const auto& c : cells) {
            const std::size_t i = static_cast<std::size_t>(c.a * 11 + c.b);
            ASSERT_EQ(&c - cells.data(), static_cast<std::ptrdiff_t>(i));
            ASSERT_EQ(c.blocked, !covered[i]);
            ASSERT_EQ(c.letter, '\0');
            ASSERT_TRUE(!c.revealed);
            ASSERT_EQ(c.highlighted, c.b == 5 && c.a >= 6 && c.a <= 11);
        }
        // Negative: the cells either side of the root are never letters.
        ASSERT_TRUE(cells[5 * 11 + 5].blocked && cells[12 * 11 + 5].blocked);
    }
    ASSERT_TRUE(layouts.size() > 10);   // the seeds really deal different grids
}

// 0x41d920: a round opens in the 0.5 overview with the root selected
// (crossword.md §3.2, §3.7), and so does the next round.
TEST(crossword_round_opens_in_the_overview) {
    Fixture f;
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_EQ(f.c().rack().size(), std::size_t{6});
    std::string sorted = f.c().rack();
    std::sort(sorted.begin(), sorted.end());
    ASSERT_EQ(sorted, std::string("aelnpt"));   // every rack is the whole root (0x434f40)
}

// AC-C13, typing (crossword.md §3.4, the user's play-test of the original):
// locked cells are skipped, so a typed letter never lands on one. "nap"
// runs down a = 8 from b = 4 and shares its "a" (b = 5) with the solved
// root: "n" lands on b = 4 and "p" skips the locked cell to b = 6.
TEST(crossword_typed_letters_skip_the_locked_cell) {
    Fixture f;
    f.enter("planet");
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();   // the pan and the turn to the vertical word
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    ASSERT_EQ(f.r().placements()[1].word, std::string("nap"));
    ASSERT_EQ(f.r().locked_letter(1, 1), 'a');
    ASSERT_EQ(f.c().row_text(), std::string("a"));   // the locked letter alone
    const int places = f.count(SoundId::LetterPlace);
    f.type("np");
    ASSERT_EQ(f.c().typed(), std::string("np"));
    ASSERT_EQ(f.cell(8, 4).letter, 'n');
    ASSERT_TRUE(!f.cell(8, 4).revealed);
    ASSERT_EQ(f.cell(8, 5).letter, 'a');   // the root's, locked
    ASSERT_TRUE(f.cell(8, 5).revealed);
    ASSERT_EQ(f.cell(8, 6).letter, 'p');
    ASSERT_TRUE(!f.cell(8, 6).revealed);
    ASSERT_EQ(f.c().row_text(), std::string("nap"));
    ASSERT_EQ(f.count(SoundId::LetterPlace), places + 2);
    // Negative: the row is full, so a letter still in the rack goes nowhere.
    f.type("t");
    ASSERT_EQ(f.c().typed(), std::string("np"));
    ASSERT_EQ(f.count(SoundId::LetterPlace), places + 2);
    f.c().key_return();
    ASSERT_EQ(f.c().score(), 360 + 90);
    ASSERT_TRUE(f.r().slots()[1].solved());
}

// AC-C13, the other way (the user's play-test, CW74): a locked letter takes
// the rack's ball for it as the word is selected. On "peal" (down a = 6
// from b = 5, its "p" locked by the root) the rack's only "p" already sits
// in the locked cell, so typing "p" places nothing, and the cell keeps the
// root's "p" as a ball in the hole.
TEST(crossword_locked_letter_uses_up_its_rack_ball) {
    Fixture f;
    f.solve_and_advance();   // planet
    f.solve_and_advance();   // nap
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_EQ(f.r().locked_letter(2, 0), 'p');
    ASSERT_TRUE(f.c().rack_in_row(f.c().rack().find('p')));
    const int places = f.count(SoundId::LetterPlace);
    f.type("p");   // no second "p" in "planet"
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::LetterPlace), places);
    ASSERT_EQ(f.cell(6, 5).letter, 'p');
    ASSERT_TRUE(f.cell(6, 5).revealed && f.cell(6, 5).ball);
    ASSERT_EQ(f.cell(6, 6).letter, '\0');
    ASSERT_EQ(f.c().row_text(), std::string("p"));
    // Negative (AC-C13): Backspace takes back one typed letter, CLEAR every
    // typed one, and neither ever takes the locked letter's ball.
    f.type("ea");
    f.c().backspace();
    ASSERT_EQ(f.c().typed(), std::string("e"));
    ASSERT_EQ(f.count(SoundId::LetterClear), 1);
    f.c().key_delete();
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::LetterClear), 2);
    f.c().backspace();   // nothing typed: no sound, the locked ball stays
    ASSERT_EQ(f.count(SoundId::LetterClear), 2);
    ASSERT_EQ(f.cell(6, 5).letter, 'p');
    ASSERT_TRUE(f.c().rack_in_row(f.c().rack().find('p')));
    ASSERT_EQ(f.cell(6, 6).letter, '\0');
    f.enter("eal");
    ASSERT_EQ(f.c().score(), 360 + 90 + 160);
    ASSERT_EQ(f.r().slots()[2].played, std::string("peal"));
}

// AC-C3 (crossword.md §3.5, §7): ENTER fills the selected slot and adds
// 10 x len^2: "planet" is a bingo, 10 x 36 = 360 with SOUND_word_bingo;
// "nap" 10 x 9 = 90 with SOUND_word_correct. Word Nerd counts both.
TEST(crossword_enter_fills_the_selected_slot_and_scores) {
    Fixture f;
    f.enter("planet");
    ASSERT_EQ(f.c().score(), 360);
    ASSERT_EQ(f.r().slots()[0].played, std::string("planet"));
    ASSERT_EQ(f.count(SoundId::WordBingo), 1);
    ASSERT_EQ(f.count(SoundId::WordCorrect), 0);
    ASSERT_TRUE(f.c().typed().empty());
    for (int a = 6; a <= 11; ++a) ASSERT_TRUE(f.cell(a, 5).revealed);
    ASSERT_EQ(f.cell(9, 5).letter, 'n');
    ASSERT_EQ(f.c().labels()[1].text, std::string("360"));
    ASSERT_EQ(f.profile.solved, 1);
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    f.enter("np");
    ASSERT_EQ(f.c().score(), 450);
    ASSERT_EQ(f.count(SoundId::WordCorrect), 1);
    ASSERT_EQ(f.profile.solved, 2);
    ASSERT_EQ(f.profile.hard_solved, 0);   // both are common words
    ASSERT_TRUE(!f.r().mistake() && !f.r().complete());
}

// AC-C3 negative (@0x41a5c7): ENTER with fewer letters than the slot does
// nothing: no sound, no popup, the row stays; the row is kept as LAST WORD.
TEST(crossword_enter_with_a_short_row_does_nothing) {
    Fixture f;
    f.type("pla");
    const std::size_t sounds = f.sounds.size();
    f.c().press("BUTTON_Enter");
    f.c().key_return();
    f.run(10);
    ASSERT_EQ(f.sounds.size(), sounds);
    ASSERT_TRUE(!f.c().popup_kind() && f.c().popups_waiting() == 0);
    ASSERT_EQ(f.c().typed(), std::string("pla"));
    ASSERT_EQ(f.c().score(), 0);
    ASSERT_EQ(f.r().slots()[0].last_word, std::string("pla"));
    ASSERT_TRUE(!f.r().slots()[0].solved());
}

// AC-C3 negative and the advance (crossword.md §3.3, §3.6): Backspace on
// the word just solved takes it back (0x41eaf0); its 160 points stay and the
// mistake flag is set. The pending advance goes on to "lap"; after "lap" it
// wraps past the solved "planet" and "nap" to "peal", and filling "peal"
// again adds only the +1000: 360 + 90 + 160 + 90 + 1000 = 1700.
TEST(crossword_taken_back_word_scores_nothing_when_filled_again) {
    Fixture f;
    f.solve_and_advance();   // planet
    f.solve_and_advance();   // nap
    f.enter("eal");          // peal
    ASSERT_EQ(f.c().score(), 610);
    ASSERT_TRUE(f.r().slots()[2].solved());
    f.c().backspace();
    ASSERT_TRUE(!f.r().slots()[2].solved());
    ASSERT_TRUE(f.r().slots()[2].scored);
    ASSERT_TRUE(f.r().mistake());
    ASSERT_EQ(f.c().score(), 610);
    ASSERT_EQ(f.count(SoundId::LetterClear), 1);
    ASSERT_EQ(f.cell(6, 6).letter, '\0');   // the row is empty again
    f.run(CROSSWORD_ADVANCE_MS + 10);
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    f.settle();
    f.solve_and_advance();   // lap, typed whole: peal no longer locks its "a"
    ASSERT_EQ(f.c().score(), 700);
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_EQ(f.r().locked_letter(2, 2), 'a');   // now from "lap"
    f.enter("el");
    ASSERT_EQ(f.c().score(), 1700);
    ASSERT_TRUE(f.r().complete());
}

// The 700 ms advance (0x41e930 -> 0x41e140(1)): nothing moves before it;
// in the wait the solved word stays selected with the EV-2 button states
// (TWIST, LAST WORD, ENTER off; CLEAR on, its row holds the word), and
// typing into it does nothing.
TEST(crossword_advance_waits_700_ms_after_a_solve) {
    Fixture f;
    f.enter("planet");
    f.run(CROSSWORD_ADVANCE_MS);
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(!f.b("BUTTON_Twist").enabled && !f.b("BUTTON_LastWord").enabled && !f.b("BUTTON_Enter").enabled);
    ASSERT_TRUE(f.b("BUTTON_Clear").enabled && f.b("BUTTON_GiveUp").enabled);
    const int places = f.count(SoundId::LetterPlace);
    f.type("p");
    ASSERT_EQ(f.count(SoundId::LetterPlace), places);
    f.run(10);
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    f.settle();   // the buttons wait for the select's pan and turn
    ASSERT_TRUE(f.b("BUTTON_Twist").enabled && !f.b("BUTTON_Clear").enabled);
    ASSERT_TRUE(f.cell(8, 4).highlighted && !f.cell(6, 5).highlighted);
}

// AC-C4: a row that is not a full-list sub-anagram of the root plays
// SOUND_word_reject, shows popup 0 and clears the row; the slot stays open
// and the mistake flag clear. LAST WORD then puts the row back.
TEST(crossword_not_a_word_rejects_with_popup_0_and_clears_the_row) {
    Fixture f;
    f.enter("eltnap");
    ASSERT_EQ(f.count(SoundId::WordReject), 1);
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.c().score(), 0);
    ASSERT_TRUE(!f.r().slots()[0].solved() && !f.r().mistake());
    f.run(10);
    ASSERT_TRUE(f.c().popup_kind() == PopupKind::NotListed);
    ASSERT_TRUE(f.b("BUTTON_LastWord").enabled && !f.b("BUTTON_Enter").enabled);
    f.c().press("BUTTON_LastWord");
    ASSERT_EQ(f.count(SoundId::WordLast), 1);
    ASSERT_EQ(f.c().typed(), std::string("eltnap"));
    ASSERT_TRUE(!f.b("BUTTON_LastWord").enabled);
    // Negative: with letters in the row, Return is ENTER, not LAST WORD.
    f.c().key_return();
    ASSERT_EQ(f.count(SoundId::WordLast), 1);
    ASSERT_EQ(f.count(SoundId::WordReject), 2);
}

// LAST WORD (0x41ae20) skips the locked cells as typing does: on "peal" the
// rejected row "pela" comes back as "ela" around the locked "p".
TEST(crossword_last_word_refills_only_the_unlocked_cells) {
    Fixture f;
    f.solve_and_advance();   // planet
    f.solve_and_advance();   // nap
    f.enter("ela");
    ASSERT_EQ(f.r().slots()[2].last_word, std::string("pela"));
    ASSERT_EQ(f.count(SoundId::WordReject), 1);
    f.c().key_return();   // an empty row: LAST WORD
    ASSERT_EQ(f.count(SoundId::WordLast), 1);
    ASSERT_EQ(f.c().typed(), std::string("ela"));
    ASSERT_EQ(f.c().row_text(), std::string("pela"));
    ASSERT_EQ(f.cell(6, 6).letter, 'e');
    ASSERT_EQ(f.cell(6, 8).letter, 'a');
}

// AC-C4: a word already played in the grid plays SOUND_word_reject, shows
// popup 3 and clears the row. On "lap" (its "a" locked by "peal") the
// player types "n", "p": the row reads "nap", slot 1's word.
TEST(crossword_word_already_in_the_grid_rejects_with_popup_3) {
    Fixture f;
    f.solve_and_advance();   // planet
    f.solve_and_advance();   // nap
    f.solve_and_advance();   // peal
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    ASSERT_EQ(f.c().score(), 610);
    f.enter("np");
    ASSERT_EQ(f.count(SoundId::WordReject), 1);
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.c().score(), 610);
    ASSERT_TRUE(!f.r().mistake());
    f.run(10);
    ASSERT_TRUE(f.c().popup_kind() == PopupKind::AlreadyGuessed);
    f.enter("lp");
    ASSERT_EQ(f.c().score(), 1700);
}

// AC-C4 (OI-12): another valid word of the slot's length that keeps the
// locked letter fills the slot. "pale" goes into peal's slot (160 points),
// the slot shows it, and "lap" crossing it now has "l" locked, so its
// answer no longer fits while "alp" does: the locked "l" holds the rack's
// only "l" (the play-test, CW74), so typing the answer lands only its "p",
// and the short row is ignored with no sound (0x41a510 @0x41a5c7).
TEST(crossword_other_valid_word_fills_the_slot_and_locks_its_letters) {
    Fixture f;
    f.solve_and_advance();   // planet
    f.solve_and_advance();   // nap
    f.enter("ale");
    ASSERT_EQ(f.c().score(), 610);
    ASSERT_EQ(f.r().slots()[2].played, std::string("pale"));
    ASSERT_EQ(f.cell(6, 7).letter, 'l');
    ASSERT_TRUE(f.cell(6, 7).revealed);
    ASSERT_TRUE(!f.r().mistake());
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    ASSERT_EQ(f.r().locked_letter(3, 1), 'l');
    f.enter("lp");   // the answer's letters
    ASSERT_EQ(f.c().typed(), std::string("p"));
    ASSERT_EQ(f.c().row_text(), std::string("pl"));
    ASSERT_EQ(f.count(SoundId::WordReject), 0);
    ASSERT_TRUE(!f.r().slots()[3].solved());
    f.c().key_delete();
    f.enter("ap");
    ASSERT_EQ(f.r().slots()[3].played, std::string("alp"));
    ASSERT_EQ(f.c().score(), 1700);
}

// AC-C8 (crossword.md §5.1, §5.2): the last word adds +1000; the select of
// 0x41e930 pans for 500 ms, then the reveal plays SOUND_letter_sheen and the
// scene stays busy for 2500 ms, then plays SOUND_game_next_round_acheived
// and opens DIALOG_NEXTROUND by itself.
// Closing it counts Well Rounded's tracker 3 and starts round 2 with the
// score kept. AC-C1 negative: NEXT ROUND stays hidden the whole time.
TEST(crossword_solved_puzzle_opens_next_round_by_itself) {
    Fixture f;
    f.solve_and_advance();
    f.solve_and_advance();
    f.solve_and_advance();
    f.enter(f.answer_to_type());   // lap
    ASSERT_EQ(f.c().score(), 1700);
    ASSERT_TRUE(f.r().complete());
    ASSERT_TRUE(f.c().busy() && f.c().phase() == GamePhase::Reveal);
    f.run(CROSSWORD_PAN_MS);
    ASSERT_EQ(f.count(SoundId::LetterSheen), 0);   // 0x41cd00 starts the reveal after the pan
    f.run(10);
    ASSERT_EQ(f.count(SoundId::LetterSheen), 1);
    for (const auto& n : kButtons) ASSERT_TRUE(!f.b(n).enabled);
    ASSERT_TRUE(f.b("BUTTON_GiveUp").sprite.visible && !f.b("BUTTON_NextRound").sprite.visible);
    f.type("p");   // busy: ignored
    ASSERT_TRUE(f.c().typed().empty());
    f.run(CROSSWORD_SHEEN_MS - 10);
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_TRUE(!f.played(SoundId::NextRoundAchieved));
    f.run(10);   // the method runs on the frame after the wait
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    ASSERT_EQ(f.count(SoundId::NextRoundAchieved), 1);
    ASSERT_TRUE(!f.c().busy() && f.c().phase() == GamePhase::AwaitNext);
    ASSERT_TRUE(!f.b("BUTTON_NextRound").sprite.visible && !f.b("BUTTON_NextRound").enabled);
    ASSERT_EQ(f.c().dialog_balls().size(), std::size_t{8});   // "Great Job"
    f.c().key_return();   // OK
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Crossword)], 1);
    ASSERT_EQ(f.c().round_number(), 2);
    ASSERT_EQ(f.c().score(), 1700);
    ASSERT_TRUE(f.c().phase() == GamePhase::Playing && !f.r().over());
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    ASSERT_EQ(f.c().labels()[3].text, std::string("2"));
    f.settle();   // round 2's opening pan
    ASSERT_TRUE(!f.b("BUTTON_NextRound").sprite.visible && f.b("BUTTON_GiveUp").enabled);
    ASSERT_EQ(f.discards, 0);   // only GIVE UP deletes the save
}

// AC-C8: no timer and no time-out. Five minutes later the round is still
// in play, nothing opened, the TIME labels are hidden, and typing works.
TEST(crossword_no_time_label_and_no_time_out) {
    Fixture f;
    f.run(300000);
    ASSERT_TRUE(f.c().phase() == GamePhase::Playing && !f.r().over());
    ASSERT_TRUE(!f.c().dialog_open() && !f.c().busy());
    for (const auto& l : f.c().labels())
        ASSERT_EQ(l.visible, l.name != "TIME_LABEL" && l.name != "LABEL_TimerMinutes" && l.name != "LABEL_TimerSeconds");
    f.type("p");
    ASSERT_EQ(f.c().typed(), std::string("p"));
}

// AC-C8 negative (0x41fc30): GIVE UP asks first; NO (or Esc) keeps
// playing with the typed letters and the save left alone.
TEST(crossword_give_up_no_keeps_playing) {
    Fixture f;
    f.run(10);
    f.type("pl");
    f.click("BUTTON_GiveUp");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::ConfirmGiveUp);
    ASSERT_EQ(f.c().dialog().labels[0].text, std::string(game_strings::GIVEUP_HEADER));
    ASSERT_TRUE(f.c().paused());
    f.c().dialog_press("BUTTON_NO");
    ASSERT_TRUE(!f.c().dialog_open());
    f.click("BUTTON_GiveUp");
    f.c().key_escape();
    ASSERT_TRUE(!f.c().dialog_open());
    ASSERT_TRUE(f.c().phase() == GamePhase::Playing && !f.r().over());
    ASSERT_EQ(f.c().typed(), std::string("pl"));
    ASSERT_EQ(f.discards, 0);
    f.run(10);
    ASSERT_TRUE(f.b("BUTTON_GiveUp").enabled && f.b("BUTTON_Enter").enabled);
}

// AC-C8 (crossword.md §5.3): GIVE UP, YES deletes the save, clears the row
// and shows every answer; 2750 ms later the game is over. 1700 points in
// round 2 beat the prefilled 1000, so the `crossword` table gets the entry
// with the round in `extra`, SOUND_game_high_score_achieved plays and
// CONGRATS opens; its OK shows the high scores on the Crossword tab, whose
// MAIN MENU leaves (OK is the menu opener's button, hidden here).
TEST(crossword_game_over_submits_the_round_to_the_crossword_table) {
    Fixture f;
    for (int i = 0; i < 4; ++i) f.solve_and_advance();
    f.run(CROSSWORD_SHEEN_MS + 10);
    f.c().dialog_press("BUTTON_OK");   // round 2
    f.settle();
    f.type("pl");
    f.c().press("BUTTON_GiveUp");
    f.c().key_return();   // YES
    ASSERT_EQ(f.discards, 1);
    ASSERT_TRUE(f.r().state() == tt2::CrosswordRound::State::GivenUp);
    ASSERT_TRUE(f.c().busy() && f.c().phase() == GamePhase::Reveal);
    ASSERT_TRUE(f.c().typed().empty());
    for (const auto& p : f.r().placements()) {
        for (std::size_t i = 0; i < p.word.size(); ++i) {
            const CrosswordCellView c = f.cell(p.cell_a(static_cast<int>(i)), p.cell_b(static_cast<int>(i)));
            ASSERT_EQ(c.letter, p.word[i]);
            ASSERT_TRUE(c.revealed);
        }
    }
    f.run(CROSSWORD_PAN_MS + CROSSWORD_ANSWERS_MS);   // the select's pan, then the answers' wait
    ASSERT_TRUE(!f.c().dialog_open());
    f.run(10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::Congrats);
    ASSERT_TRUE(f.c().phase() == GamePhase::GameOver && !f.c().busy());
    ASSERT_EQ(f.count(SoundId::HighScoreAchieved), 1);
    const auto& table = f.hs.tables().at("crossword");
    ASSERT_EQ(table.last_index, 9);
    ASSERT_EQ(table.entries[9].name, std::string("Mugwump"));
    ASSERT_EQ(table.entries[9].score, 1700);
    ASSERT_EQ(table.entries[9].extra, std::string("2"));
    ASSERT_TRUE(!f.b("BUTTON_NextRound").sprite.visible);
    f.c().dialog_press("BUTTON_OK");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::HighScores);
    ASSERT_TRUE(f.c().highscore_dialog() && f.c().highscore_dialog()->mode() == tt2::GameMode::Crossword);
    f.c().dialog_press("BUTTON_OK");   // hidden for the game opener: nothing
    ASSERT_EQ(f.exits, 0);
    const auto [mx, my] = f.dialog_point("BUTTON_MAINMENU");
    f.c().mouse_move(mx, my);
    ASSERT_TRUE(f.c().pointer_over_clickable());
    f.c().mouse_down(mx, my);
    f.c().mouse_up(mx, my);
    ASSERT_EQ(f.exits, 1);
    ASSERT_TRUE(f.c().finished());
}

// AC-C8 negatives: a score under the table's lowest (360 < 1000) makes no
// rank, so ENDGAME opens with no sound; a score of 0 enters nothing at all.
// PLAY AGAIN starts a new game at round 1, score 0.
TEST(crossword_give_up_without_a_rank_shows_end_game) {
    Fixture f;
    f.enter("planet");
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    f.run(CROSSWORD_PAN_MS + CROSSWORD_ANSWERS_MS + 10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(!f.played(SoundId::HighScoreAchieved));
    for (const auto& e : f.hs.tables().at("crossword").entries) ASSERT_TRUE(e.name != "Mugwump");
    f.c().dialog_press("BUTTON_REPLAY");
    ASSERT_EQ(f.c().round_number(), 1);
    ASSERT_EQ(f.c().score(), 0);
    ASSERT_TRUE(f.c().phase() == GamePhase::Playing && !f.r().over());
    ASSERT_EQ(f.exits, 0);

    Fixture zero;
    zero.c().press("BUTTON_GiveUp");
    zero.c().dialog_press("BUTTON_YES");
    zero.run(CROSSWORD_PAN_MS + CROSSWORD_ANSWERS_MS + 10);
    ASSERT_TRUE(zero.c().dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(zero.hs.tables().find("crossword") == zero.hs.tables().end());
    zero.c().dialog_press("BUTTON_OK");   // MENU
    ASSERT_EQ(zero.exits, 1);
}

// Negative: after GIVE UP nothing types, and the queued advance of a word
// solved just before does not move the selection under the reveal.
TEST(crossword_give_up_stops_typing_and_the_pending_advance) {
    Fixture f;
    f.enter("planet");
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    f.type("n");
    f.c().backspace();
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::LetterPlace), 6);
    f.run(CROSSWORD_ADVANCE_MS + 10);
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(f.r().slots()[0].solved());   // Backspace took nothing back
}

// CLEAR on the word just solved (its row holds the word, so CLEAR is on)
// takes it back as Backspace does: 0x41a3d0 returns each letter through
// 0x41b390 (INFERRED, crossword.md §3.6). Negatives: Delete on an empty row
// plays nothing, and typing ignores case and skips what is not a letter.
TEST(crossword_clear_on_a_solved_word_takes_it_back) {
    Fixture f;
    f.c().key_delete();
    ASSERT_EQ(f.count(SoundId::LetterClear), 0);
    f.type("PL4a");
    ASSERT_EQ(f.c().typed(), std::string("pla"));
    f.c().key_delete();
    ASSERT_EQ(f.count(SoundId::LetterClear), 1);
    f.enter("planet");
    ASSERT_TRUE(f.r().slots()[0].solved());
    f.c().press("BUTTON_Clear");
    ASSERT_TRUE(!f.r().slots()[0].solved());
    ASSERT_TRUE(f.r().mistake());
    ASSERT_EQ(f.c().score(), 360);
    ASSERT_EQ(f.count(SoundId::LetterClear), 2);
    ASSERT_EQ(f.cell(6, 5).letter, '\0');
}

// Without a high-score manager the game over enters nothing and shows
// ENDGAME, whatever the score.
TEST(crossword_game_over_without_a_high_score_manager_shows_end_game) {
    Fixture f;
    tt2::CrtRandom crt{1};
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
    p.mode_flags.fill(true);
    CrosswordScene bare(f.com, f.ful, p, nullptr, f.services(), 5, crt);
    for (int t = 0; t <= CROSSWORD_PAN_MS; t += 10) bare.update(10);   // the opening pan
    for (char ch : std::string("planet")) bare.type_letter(ch);
    bare.key_return();
    ASSERT_EQ(bare.score(), 360);
    bare.press("BUTTON_GiveUp");
    bare.dialog_press("BUTTON_YES");
    for (int t = 0; t <= CROSSWORD_PAN_MS + CROSSWORD_ANSWERS_MS; t += 10) bare.update(10);
    ASSERT_TRUE(bare.dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(!f.played(SoundId::HighScoreAchieved));
    ASSERT_TRUE(f.hs.tables().find("crossword") == f.hs.tables().end());
}

// +0x20c = 2 (crossword.md §2.1): focus lost under DIALOG_NEXTROUND waits;
// once OK has started the next round, the next update opens the pause menu.
TEST(crossword_focus_loss_under_next_round_opens_the_menu_after_it) {
    Fixture f;
    for (int i = 0; i < 4; ++i) f.solve_and_advance();
    f.run(CROSSWORD_SHEEN_MS + 10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    f.c().focus_lost();
    ASSERT_TRUE(f.c().menu_pending());
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    f.c().dialog_press("BUTTON_OK");
    ASSERT_EQ(f.c().round_number(), 2);
    ASSERT_TRUE(!f.c().dialog_open());
    f.run(10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(!f.c().menu_pending());
}

// ---------------------------------------------------------------------------
// T6b.4: controls, saving, trophies, popups
// ---------------------------------------------------------------------------

// 0x434f40 deals each word object's rack through 0x43c370 with the root
// as the word (word+0x18, crossword.md §2.2), in placement order, from the
// shared CRT (game-scene.md §4.2). The load restores the grid without a
// draw (INFERRED: the racks are dealt again), so on a resume the racks are
// the CRT's only draws.
TEST(crossword_racks_are_dealt_with_the_crt_like_0x43c370) {
    tt2::CrtRandom crt(195), expect(195);   // declared first: they outlive f's scene
    Fixture f;
    const tt2::SavedGame save = f.c().snapshot();
    f.scene = std::make_unique<CrosswordScene>(f.com, f.ful, f.profile, &f.hs, f.services(), 5, crt, &save);
    ASSERT_TRUE(!f.c().resume_failed());
    std::vector<std::string> want;
    for (std::size_t i = 0; i < f.r().placements().size(); ++i)
        want.push_back(tt2::deal_order("planet", "planet", expect));
    ASSERT_EQ(crt.state(), expect.state());
    ASSERT_EQ(f.c().rack(), std::string("ptanel"));   // word 0, srand(195): one pass (0x439370)
    for (std::size_t i = 0; i < want.size(); ++i) {
        f.settle();
        ASSERT_EQ(f.r().selected(), i);
        ASSERT_EQ(f.c().rack(), want[i]);
        ASSERT_NE(f.c().rack(), std::string("planet"));
        f.c().key_word(true);
    }
    f.scene.reset();
}

// TWIST on a full rack: the deal compares the whole row with the root too,
// so a twist never spells it and never keeps the order.
TEST(crossword_twist_never_deals_the_root_or_the_order_before) {
    Fixture f;
    for (int i = 0; i < 12; ++i) {
        const std::string before = f.c().rack();
        tt2::CrtRandom expect = f.crt;
        const std::string want = tt2::deal_order(before, "planet", expect);
        f.c().key_space();
        ASSERT_EQ(f.c().rack(), want);
        ASSERT_NE(f.c().rack(), before);
        ASSERT_NE(f.c().rack(), std::string("planet"));
        ASSERT_EQ(f.crt.state(), expect.state());
    }
}

// AC-C5 (0x41b060, CW16): TWIST plays SOUND_word_twist, shuffles the
// selected word's rack with the shared CRT (0x43c370) and sets +0x370; the
// twist counter counts it (0x40f170). INFERRED: the balls typed into the
// row stay. Space (key 0x11) calls 0x41b060 straight, with no checks, so it
// twists a solved word too, though the button is off there.
TEST(crossword_twist_shuffles_the_rack_and_plays_the_twist_sound) {
    Fixture f;
    f.type("pl");
    const std::string before = f.c().rack();
    // What 0x43c370 must give: the free balls dealt by the same CRT, with
    // the root (word+0x18) as the word.
    tt2::CrtRandom expect = f.crt;
    std::string free_letters;
    for (std::size_t k = 0; k < before.size(); ++k)
        if (!f.c().rack_in_row(k)) free_letters.push_back(before[k]);
    const std::string dealt = tt2::deal_order(free_letters, "planet", expect);
    std::string want = before;
    for (std::size_t k = 0, i = 0; k < want.size(); ++k)
        if (!f.c().rack_in_row(k)) want[k] = dealt[i++];
    ASSERT_NE(want, before);   // a deal always moves a letter
    const int changes = f.changes;
    f.click("BUTTON_Twist");
    ASSERT_EQ(f.count(SoundId::WordTwist), 1);
    ASSERT_EQ(f.count(SoundId::Button), 1);
    ASSERT_EQ(f.c().rack(), want);
    ASSERT_EQ(f.crt.state(), expect.state());
    ASSERT_EQ(f.c().typed(), std::string("pl"));   // the row keeps its letters
    ASSERT_TRUE(f.r().twisted());
    ASSERT_EQ(f.profile.twists, 1);
    ASSERT_TRUE(f.changes > changes);
    f.c().key_space();
    ASSERT_EQ(f.count(SoundId::WordTwist), 2);
    ASSERT_EQ(f.profile.twists, 2);
    std::string sorted = f.c().rack();
    std::sort(sorted.begin(), sorted.end());
    ASSERT_EQ(sorted, std::string("aelnpt"));
    // A solved word: the button is off (0x41b510), Space still twists.
    f.c().key_delete();
    f.enter("planet");
    ASSERT_TRUE(!f.b("BUTTON_Twist").enabled);
    f.c().key_space();
    ASSERT_EQ(f.count(SoundId::WordTwist), 3);
    // Negatives: nothing twists while busy or after the round.
    f.run(CROSSWORD_ADVANCE_MS + 10);
    ASSERT_TRUE(f.c().busy());   // the advance's pan
    f.c().key_space();
    f.c().press("BUTTON_Twist");
    ASSERT_EQ(f.count(SoundId::WordTwist), 3);
    f.settle();
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    f.c().key_space();
    ASSERT_EQ(f.count(SoundId::WordTwist), 3);
    ASSERT_EQ(f.profile.twists, 3);
}

// AC-C5 (0x41d390 -> 0x41c910, file read): a word of the other direction
// pans first (500 ms), then plays SOUND_crossword_rotate_word, waits 300 ms
// and turns the grid by 90 degrees over 800 ms (linear), so the word reads
// left to right through (400, 189). Busy until the chain ends (0x41d130).
// Negative: a word of the same direction pans with no sound and no turn.
TEST(crossword_selecting_a_crossing_word_rotates_the_grid) {
    Fixture f;
    ASSERT_TRUE(near(f.c().view_turn(), 0.0f));
    f.c().key_word(true);   // Tab: "nap", down
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    ASSERT_TRUE(f.c().busy());
    f.run(CROSSWORD_PAN_MS);
    ASSERT_TRUE(near(f.c().view().focus_a, 8.5f) && near(f.c().view().focus_b, 5.5f));   // nap's centre
    ASSERT_EQ(f.count(SoundId::CrosswordRotateWord), 0);
    f.run(10);   // 0x41d390 runs on the frame after the pan
    ASSERT_EQ(f.count(SoundId::CrosswordRotateWord), 1);
    f.run(CROSSWORD_TURN_WAIT_MS - 10);
    ASSERT_TRUE(near(f.c().view_turn(), 0.0f));
    f.run(CROSSWORD_TURN_MS / 2);
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL * 0.5f));
    f.run(CROSSWORD_TURN_MS / 2);
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL));
    ASSERT_TRUE(f.c().busy());
    f.run(10);
    ASSERT_TRUE(!f.c().busy());
    // "n", "a", "p" left to right, one 34.5 px cell apart at 0.5.
    const auto [nx, ny] = f.c().cell_center(8, 4);
    const auto [ax, ay] = f.c().cell_center(8, 5);
    const auto [px, py] = f.c().cell_center(8, 6);
    ASSERT_TRUE(near(ax, 400.0f) && near(ay, 189.0f));
    ASSERT_TRUE(near(nx, 400.0f - 34.5f) && near(ny, 189.0f));
    ASSERT_TRUE(near(px, 400.0f + 34.5f) && near(py, 189.0f));
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));   // the flag is off: no zoom
    // Negative: "peal" is down too.
    f.c().key_word(true);
    f.settle();
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_EQ(f.count(SoundId::CrosswordRotateWord), 1);
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL));
    // "lap" is across: the grid turns back.
    f.c().key_word(true);
    f.settle();
    ASSERT_EQ(f.count(SoundId::CrosswordRotateWord), 2);
    ASSERT_TRUE(near(f.c().view_turn(), 0.0f));
    const auto [lx, ly] = f.c().cell_center(5, 7);
    const auto [rx, ry] = f.c().cell_center(7, 7);
    ASSERT_TRUE(lx < rx && near(ly, 189.0f) && near(ry, 189.0f));
}

// AC-C6 (0x41b1c0, CW46, CW47): ZOOM at 0.5 sets the flag (+0x3c9 = 1),
// pans to the selected word, then scales to 1.0 over 1500 ms; at 1.0 it
// clears the flag, scales to 0.5 over 1000 ms and pans to the root,
// keeping the turn (0x41cab0). Busy, so every button is off, throughout.
TEST(crossword_zoom_toggles_the_grid_view) {
    Fixture f;
    ASSERT_TRUE(!f.c().zoom_in_flag());
    f.c().key_word(true);   // "nap", turned
    f.settle();
    f.click("BUTTON_Zoom");
    ASSERT_TRUE(f.c().zoom_in_flag());
    ASSERT_TRUE(f.c().busy());
    for (const auto& n : kButtons) ASSERT_TRUE(!f.b(n).enabled);
    f.run(CROSSWORD_PAN_MS);   // the pan comes first (0x41d160 @0x41b308)
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    f.run(CROSSWORD_ZOOM_IN_MS / 2);
    ASSERT_TRUE(near(f.c().view_scale(), 0.75f));   // smoothstep(0.5) = 0.5
    f.run(CROSSWORD_ZOOM_IN_MS / 2);
    ASSERT_TRUE(near(f.c().view_scale(), 1.0f));
    ASSERT_TRUE(f.c().busy());
    f.run(10);
    ASSERT_TRUE(!f.c().busy());
    ASSERT_EQ(f.count(SoundId::CrosswordRotateWord), 1);   // the turn to "nap" only
    // 69 px cells on the selected word (CW73).
    const auto [ax, ay] = f.c().cell_center(8, 5);
    const auto [nx, ny] = f.c().cell_center(8, 4);
    ASSERT_TRUE(near(ax, 400.0f) && near(ay, 189.0f));
    ASSERT_TRUE(near(nx, 331.0f) && near(ny, 189.0f));
    // NEXT at 1.0 stays at 1.0.
    f.c().key_word(true);
    f.settle();
    ASSERT_TRUE(near(f.c().view_scale(), 1.0f));
    f.c().press("BUTTON_Zoom");
    ASSERT_TRUE(!f.c().zoom_in_flag());
    ASSERT_TRUE(f.c().busy());
    f.run(CROSSWORD_ZOOM_OUT_MS);
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    ASSERT_TRUE(near(f.c().view().focus_a, 6.5f) && near(f.c().view().focus_b, 7.0f));   // still on "peal"
    f.run(CROSSWORD_PAN_MS);
    ASSERT_TRUE(near(f.c().view().focus_a, 9.0f) && near(f.c().view().focus_b, 5.5f));   // the root's centre
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL));                      // the turn is kept
    ASSERT_TRUE(f.c().busy());
    f.run(10);
    ASSERT_TRUE(!f.c().busy());
    ASSERT_EQ(f.r().selected(), std::size_t{2});   // the selection stays
}

// The grid view's durations are the exe's immediates, CONFIRMED by batch O4
// (game-scene.md §9.1; crossword.md §3.3, §3.7): pan 0x1f4 (CW49), turn
// wait 0x12c and turn 0x320 (CW50, 0x41c910), zoom in 0x5dc (CW50), zoom out
// 0x3e8 (CW47). Half-way, linear and smoothstep agree (0.5), so this looks a
// quarter of the way in: the turn is linear (+0x54 = 0), -pi/2 * 0.25; the
// zoom back in is smoothstep, 0.5 + 0.5 * (3 - 2 * 0.25) * 0.25^2 = 0.578125
// (linear would be 0.625).
TEST(crossword_view_durations_and_easings_match_the_exe) {
    ASSERT_EQ(CROSSWORD_PAN_MS, 500);
    ASSERT_EQ(CROSSWORD_TURN_WAIT_MS, 300);
    ASSERT_EQ(CROSSWORD_TURN_MS, 800);
    ASSERT_EQ(CROSSWORD_ZOOM_IN_MS, 1500);
    ASSERT_EQ(CROSSWORD_ZOOM_OUT_MS, 1000);
    Fixture f;
    f.c().key_word(true);   // "nap", down: the pan, then the turn
    f.run(CROSSWORD_PAN_MS + CROSSWORD_TURN_WAIT_MS);
    ASSERT_TRUE(near(f.c().view_turn(), 0.0f));
    f.run(CROSSWORD_TURN_MS / 4);
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL * 0.25f));
    f.settle();
    f.click("BUTTON_Zoom");   // at 0.5: the pan (already there), then 1.0
    f.run(CROSSWORD_PAN_MS);
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    f.run(CROSSWORD_ZOOM_IN_MS / 4);
    ASSERT_TRUE(near(f.c().view_scale(), 0.578125f));
}

// AC-C6 negative (0x41f120, 0x41b910): while ZOOM animates, keys, clicks
// and buttons are ignored, ZOOM itself included.
TEST(crossword_keys_during_the_zoom_are_ignored) {
    Fixture f;
    f.c().key_zoom();   // Up
    ASSERT_TRUE(f.c().zoom_in_flag() && f.c().busy());
    f.run(CROSSWORD_PAN_MS + 100);
    f.c().key_zoom();
    f.c().press("BUTTON_Zoom");
    f.c().key_word(true);
    f.c().press("BUTTON_NEXTWORD");
    f.type("p");
    f.c().key_space();
    f.click_cell(8, 4);
    ASSERT_TRUE(f.c().zoom_in_flag());
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::WordTwist), 0);
    f.settle();
    ASSERT_TRUE(near(f.c().view_scale(), 1.0f));
    f.type("p");   // and the scene takes input again
    ASSERT_EQ(f.c().typed(), std::string("p"));
}

// AC-C6: dialogs.xml SCENE_CRISSCROSS gives ZOOM CTOOLTIP_GiveUp and the NEXT
// arrow CTOOLTIP_Clear (both sic), the PREV arrow none, and no code sets
// another (crossword.md §1.1). Negative: the Classic / Lightning surface is
// unchanged.
TEST(crossword_zoom_tooltip_matches_the_original) {
    auto tip = [](TooltipSurface s, const char* n) { return std::string(tooltip_text(s, n)); };
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_Zoom"), std::string("End this game"));
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_NEXTWORD"), std::string("Remove all letters"));
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_PREVWORD"), std::string(""));
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_GiveUp"), std::string("End this game"));
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_Twist"), std::string("Scramble the letters"));
    ASSERT_EQ(tip(TooltipSurface::Crossword, "BUTTON_Menu"), std::string("Show the game menu"));
    ASSERT_EQ(tip(TooltipSurface::Game, "BUTTON_PREVWORD"), std::string("Remove all letters"));   // Lightning's
    ASSERT_EQ(tip(TooltipSurface::Game, "BUTTON_Zoom"), std::string(""));
}

// AC-C6 (0x41d920, CW57): a fresh round and a resumed game open at 0.5 at
// once with word 0 selected; the flag is not reset by a new round, so with
// it set the opening select zooms back in after its pan. The reveal zooms
// out from 1.0 without touching the flag (0x41fed0).
TEST(crossword_round_and_resume_open_in_the_overview) {
    Fixture f;
    f.click("BUTTON_Zoom");
    f.settle();
    ASSERT_TRUE(near(f.c().view_scale(), 1.0f));
    f.solve_all();
    ASSERT_TRUE(f.r().complete());
    f.run(CROSSWORD_PAN_MS + 10 + CROSSWORD_ZOOM_OUT_MS);
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    ASSERT_TRUE(f.c().zoom_in_flag());
    for (int t = 0; t < 10000 && !f.c().dialog_open(); t += 10) f.run(10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    f.c().dialog_press("BUTTON_OK");
    ASSERT_EQ(f.c().round_number(), 2);
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(f.c().busy());
    f.run(CROSSWORD_PAN_MS);
    ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
    f.settle();
    ASSERT_TRUE(near(f.c().view_scale(), 1.0f));
    ASSERT_TRUE(near(f.c().view().focus_a, 9.0f) && near(f.c().view().focus_b, 5.5f));

    // A resume: the same, from a save made with "nap" selected.
    Fixture g;
    g.solve_and_advance();
    ASSERT_EQ(g.r().selected(), std::size_t{1});
    const tt2::SavedGame save = g.c().snapshot();
    for (bool flag : {true, false}) {
        g.scene = std::make_unique<CrosswordScene>(g.com, g.ful, g.profile, &g.hs, g.services(), 5, g.crt, &save);
        g.c().set_zoom_in_flag(flag);   // the front end carries it (main.cpp)
        ASSERT_TRUE(!g.c().resume_failed());
        ASSERT_TRUE(near(g.c().view_scale(), 0.5f));
        ASSERT_EQ(g.r().selected(), std::size_t{0});
        ASSERT_TRUE(near(g.c().view_turn(), 0.0f));
        g.settle();
        ASSERT_TRUE(near(g.c().view_scale(), flag ? 1.0f : 0.5f));
        ASSERT_EQ(g.c().zoom_in_flag(), flag);
    }
}

// AC-C6 negative (0x41d776..0x41d797): with the flag off a selection only
// pans; the view stays at 0.5 on every word.
TEST(crossword_with_the_flag_off_next_keeps_the_view_at_half_scale) {
    Fixture f;
    const std::vector<std::pair<float, float>> centres = {{8.5f, 5.5f}, {6.5f, 7.0f}, {6.5f, 7.5f}, {9.0f, 5.5f}};
    for (const auto& c : centres) {
        f.c().key_word(true);
        f.settle();
        ASSERT_TRUE(near(f.c().view_scale(), 0.5f));
        ASSERT_TRUE(near(f.c().view().focus_a, c.first) && near(f.c().view().focus_b, c.second));
    }
    ASSERT_TRUE(!f.c().zoom_in_flag());
}

// AC-C7 (0x41e140(0), 0x41e230): NEXT / PREV step by one, wrapping, solved
// words included, while the 700 ms advance skips solved ones. Only the
// NEXT arrow has a click sound (dialogs.xml:155, :165); keys play none.
TEST(crossword_arrows_wrap_and_include_solved_words) {
    Fixture f;
    f.solve_and_advance();   // "planet"; the advance goes to "nap"
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    const int buttons = f.count(SoundId::Button);
    f.click("BUTTON_NEXTWORD");
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_EQ(f.count(SoundId::Button), buttons + 1);
    f.settle();
    f.c().key_word(true);   // Tab / Right
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    f.settle();
    f.c().key_word(true);   // wraps onto the solved root
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    f.settle();
    f.click("BUTTON_PREVWORD");   // wraps back
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    f.settle();
    f.c().key_word(false);   // Left
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_EQ(f.count(SoundId::Button), buttons + 1);
}

// AC-C7 (0x421e50, CW42): a click on another word's row selects it, only
// inside x 85..715, y 20..360 and never while busy. A crossing cell goes to
// the selected word first (INFERRED). No sound.
TEST(crossword_row_click_selects_its_word_inside_the_click_box) {
    Fixture f;
    f.click_cell(8, 4);   // "nap"'s "n"
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    ASSERT_TRUE(f.c().busy());
    f.click_cell(6, 6);   // busy: "peal" is not selected
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    f.settle();
    f.click_cell(8, 5);   // the crossing cell: "nap" stays
    f.click_cell(8, 4);   // an empty cell of the selected row: nothing
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    ASSERT_TRUE(!f.c().busy() && f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::Button), 0);
    f.c().key_word(false);
    f.settle();
    f.click("BUTTON_Zoom");
    f.settle();
    f.enter("planet");   // a solved root shows no rack under the grid
    const auto [x, y] = f.c().cell_center(6, 8);   // "peal"'s "l" at 1.0
    ASSERT_TRUE(near(y, 189.0f + 3 * 69.0f));        // 396: below the box
    f.click_at(x, y);
    ASSERT_EQ(f.r().selected(), std::size_t{0});
    ASSERT_TRUE(!f.c().busy() && f.r().slots()[0].solved());
    f.click_cell(6, 6);   // y 258: inside
    ASSERT_EQ(f.r().selected(), std::size_t{2});
}

// The hand shows over a word's row and a rack ball, not over an empty
// cell, and not while busy.
TEST(crossword_hand_cursor_over_rows_and_rack_balls) {
    Fixture f;
    auto over = [&](float x, float y) {
        f.c().mouse_move(x, y);
        return f.c().pointer_over_clickable();
    };
    const auto [rx, ry] = f.c().cell_center(8, 4);
    ASSERT_TRUE(over(rx, ry));
    const auto [ex, ey] = f.c().cell_center(3, 2);   // no word there
    ASSERT_TRUE(!over(ex, ey));
    ASSERT_TRUE(over(ball_center_x(6, 0), CROSSWORD_RACK_Y + BALL_HALF));
    f.click_cell(8, 4);
    ASSERT_TRUE(f.c().busy());
    const auto [bx, by] = f.c().cell_center(6, 6);
    ASSERT_TRUE(!over(bx, by));
}

// 0x41f2b0 (CW37): a click on a rack ball types it; its place is a gap
// afterwards, so a second click there does nothing.
TEST(crossword_rack_ball_click_types_the_ball) {
    Fixture f;
    const char first = f.c().rack()[0];
    const float x = ball_center_x(6, 0), y = CROSSWORD_RACK_Y + BALL_HALF;
    f.click_at(x, y);
    ASSERT_EQ(f.c().typed(), std::string(1, first));
    ASSERT_TRUE(f.c().rack_in_row(0));
    ASSERT_EQ(f.count(SoundId::LetterPlace), 1);
    f.click_at(x, y);
    ASSERT_EQ(f.c().typed().size(), std::size_t{1});
    ASSERT_EQ(f.count(SoundId::LetterPlace), 1);
}

// AC-C7 (0x41de10 / 0x41df10): the highlight is IMAGE_CROSSWORDHIGHLIGHT,
// frame len - 3, its top-left (10, 11) px (scaled) before the selected
// word's first cell; it moves at once on a select.
TEST(crossword_highlight_frame_follows_the_selected_word) {
    Fixture f;
    ASSERT_EQ(f.c().highlight_frame(), 3);   // "planet"
    SpriteState h = f.c().highlight_sprite();
    ASSERT_TRUE(h.visible);
    ASSERT_EQ(h.sheet.image_id, std::string("IMAGE_CROSSWORDHIGHLIGHT"));
    ASSERT_EQ(h.sheet.frame_w, 572);
    ASSERT_EQ(h.sheet.frame_h, 90);
    ASSERT_EQ(h.frame, 3);
    ASSERT_TRUE(near(h.scale, 0.5f));
    ASSERT_TRUE(near(h.x, 400.0f - 3 * 34.5f - 5.0f) && near(h.y, 189.0f - 17.25f - 5.5f));
    f.c().key_word(true);
    ASSERT_EQ(f.c().highlight_frame(), 0);   // "nap", before the pan ends
    f.settle();
    f.c().key_word(true);
    f.settle();
    h = f.c().highlight_sprite();   // "peal", turned: level again
    ASSERT_EQ(h.frame, 1);
    ASSERT_TRUE(near(h.x, 400.0f - 2 * 34.5f - 5.0f) && near(h.y, 189.0f - 17.25f - 5.5f));
    f.click("BUTTON_Zoom");
    f.settle();
    h = f.c().highlight_sprite();
    ASSERT_TRUE(near(h.scale, 1.0f));
    ASSERT_TRUE(near(h.x, 400.0f - 2 * 69.0f - 10.0f) && near(h.y, 189.0f - 34.5f - 11.0f));
    ASSERT_TRUE(!f.c().dead_end() && !f.c().popup_kind());
}

// AC-C7 (0x41df10 -> 0x41e060, INFERRED meaning): a select that finds no
// valid word left for an unsolved slot shows popup 9 and the red frame
// len + 3. "ant" in "nap"'s slot and "pane" in "peal"'s lock "lap"'s
// middle to "n", and "ant" is the only such word: a dead end. The root is
// one too ("p?n..."). Negative: a solved word, or a slot with words left,
// shows neither.
TEST(crossword_dead_end_shows_popup_9_and_the_red_frame) {
    Fixture f;
    f.c().key_word(true);
    f.settle();
    f.enter("ant");   // the root is unsolved, so nothing is locked yet
    ASSERT_EQ(f.r().slots()[1].played, std::string("ant"));
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    ASSERT_EQ(f.r().selected(), std::size_t{2});
    ASSERT_TRUE(!f.c().dead_end());
    f.enter("pane");
    f.run(CROSSWORD_ADVANCE_MS + 10);
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    ASSERT_EQ(f.c().highlight_frame(), 0);   // 0x41df10 runs at the chain's end
    f.settle();
    ASSERT_TRUE(f.c().dead_end());
    ASSERT_EQ(f.c().highlight_frame(), 6);   // 3 + 3
    ASSERT_EQ(f.c().highlight_sprite().frame, 6);
    ASSERT_TRUE(f.c().popup_kind() == PopupKind::DeadEnd);
    f.c().key_word(true);   // the root
    f.settle();
    ASSERT_EQ(f.c().highlight_frame(), 9);   // 6 + 3
    ASSERT_EQ(f.c().popups_waiting(), std::size_t{1});   // a second popup 9 waits for the first
    f.c().key_word(true);   // "ant", solved
    f.settle();
    ASSERT_TRUE(!f.c().dead_end());
    ASSERT_EQ(f.c().highlight_frame(), 0);

    Fixture g;   // the answers: "lap" keeps its words
    for (int i = 0; i < 3; ++i) g.solve_and_advance();
    ASSERT_EQ(g.r().selected(), std::size_t{3});
    ASSERT_TRUE(!g.c().dead_end());
    ASSERT_EQ(g.c().highlight_frame(), 0);
    g.run(10);
    ASSERT_TRUE(!g.c().popup_kind());
}

// AC-C13 (0x41e270, INFERRED meaning): on "lap", whose middle crosses the
// unsolved "peal" (its "p" locked by the root), a "p" there would leave
// "peal" needing two "p"s: popup 4, and the letter goes back through
// 0x41b390 with SOUND_letter_clear. Negative: "a" fits and is placed with
// no popup.
TEST(crossword_letter_that_breaks_a_crossing_shows_popup_4) {
    Fixture f;
    f.solve_and_advance();   // "planet"
    f.c().key_word(false);
    f.settle();
    f.c().key_word(false);   // wraps to "lap"
    f.settle();
    ASSERT_EQ(f.r().selected(), std::size_t{3});
    f.type("l");
    ASSERT_EQ(f.c().typed(), std::string("l"));
    const int places = f.count(SoundId::LetterPlace), clears = f.count(SoundId::LetterClear);
    f.type("p");
    ASSERT_EQ(f.c().typed(), std::string("l"));
    ASSERT_EQ(f.count(SoundId::LetterPlace), places + 1);
    ASSERT_EQ(f.count(SoundId::LetterClear), clears + 1);
    f.run(10);
    ASSERT_TRUE(f.c().popup_kind() == PopupKind::CrosswordIntersection);
    ASSERT_EQ(f.cell(6, 7).letter, '\0');
    f.type("ap");
    ASSERT_EQ(f.c().typed(), std::string("lap"));
    ASSERT_EQ(f.count(SoundId::LetterClear), clears + 1);
    ASSERT_EQ(f.c().popups_waiting(), std::size_t{0});
    f.c().key_return();
    ASSERT_TRUE(f.r().slots()[3].solved());
}

// AC-C13 negative (crossword.md §3.4): locked cells are skipped, so typing
// the locked letter never clashes with it: no popup 7. "planet" has one
// "a", and the locked cell holds its ball (CW74), so nothing is placed.
TEST(crossword_typing_the_locked_letter_shows_no_popup_7) {
    Fixture f;
    f.solve_and_advance();   // "nap", its "a" locked
    f.type("a");
    ASSERT_EQ(f.cell(8, 4).letter, '\0');   // the first free cell stays empty
    ASSERT_EQ(f.cell(8, 5).letter, 'a');    // the locked one, unchanged
    f.run(10);
    ASSERT_TRUE(!f.c().popup_kind() && f.c().popups_waiting() == 0);
}

// AC-C13 (0x41f2b0): a click on the last typed letter takes it back; on a
// solved row a click on its last letter takes the word back (0x41eaf0:
// points kept, the mistake flag set). Negatives: any other cell does
// nothing.
TEST(crossword_click_on_the_last_letter_takes_it_back) {
    Fixture f;
    f.type("pla");
    f.click_cell(7, 5);   // "l": not the last
    ASSERT_EQ(f.c().typed(), std::string("pla"));
    ASSERT_EQ(f.count(SoundId::LetterClear), 0);
    f.click_cell(8, 5);   // "a", the last typed
    ASSERT_EQ(f.c().typed(), std::string("pl"));
    ASSERT_EQ(f.count(SoundId::LetterClear), 1);
    f.click_cell(11, 5);   // empty
    ASSERT_EQ(f.c().typed(), std::string("pl"));
    f.c().key_delete();
    f.enter("planet");
    f.click_cell(8, 5);   // a solved row's middle
    ASSERT_TRUE(f.r().slots()[0].solved());
    f.click_cell(11, 5);  // its last letter
    ASSERT_TRUE(!f.r().slots()[0].solved());
    ASSERT_TRUE(f.r().mistake());
    ASSERT_EQ(f.c().score(), 360);
    ASSERT_EQ(f.count(SoundId::LetterClear), 3);
}

// AC-C9 (0x41f7d0 -> 0x4203f0, crossword.md §5.4, §5.5): leaving saves the
// T5.2 snapshot while the round is in play, busy or not: WORD the root,
// ROUND, TIME 0, SCORE, one xword= per placement. The background saves and
// pauses; STOP -> YES saves before it leaves.
TEST(crossword_leave_saves_the_round_while_it_is_in_play) {
    Fixture f;
    f.enter("planet");
    f.c().leave();
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    const tt2::SavedGame& g = f.saved[0];
    ASSERT_TRUE(g.crossword() && g.valid());
    ASSERT_EQ(g.word, std::string("planet"));
    ASSERT_EQ(g.round, 1);
    ASSERT_EQ(g.time_ms, 0);
    ASSERT_EQ(g.score, 360);
    ASSERT_EQ(g.day, -1);
    ASSERT_TRUE(g.found_words.empty() && g.blitz_words.empty());
    ASSERT_EQ(g.crossword_words.size(), f.r().placements().size());
    for (std::size_t i = 0; i < g.crossword_words.size(); ++i) {
        const auto& w = g.crossword_words[i];
        const auto& p = f.r().placements()[i];
        ASSERT_EQ(w.answer, p.word);
        ASSERT_TRUE(w.a == p.a && w.b == p.b && w.horizontal == p.horizontal && w.crossing == p.crossing);
        ASSERT_EQ(w.scored, i == 0);
        ASSERT_EQ(w.played, std::string(i == 0 ? "planet" : ""));
    }
    tt2::SavedGame back;
    ASSERT_TRUE(tt2::parse(tt2::serialize(g), back));
    ASSERT_EQ(tt2::serialize(back), tt2::serialize(g));
    f.run(CROSSWORD_ADVANCE_MS + 10);
    ASSERT_TRUE(f.c().busy());
    f.c().leave();
    ASSERT_EQ(f.saved.size(), std::size_t{2});
    f.settle();
    f.c().enter_background();
    ASSERT_EQ(f.saved.size(), std::size_t{3});
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::PauseMenu);
    f.c().dialog_press("BUTTON_STOP");
    f.c().dialog_press("BUTTON_YES");
    ASSERT_EQ(f.saved.size(), std::size_t{4});
    ASSERT_EQ(f.exits, 1);
}

// Negative (0x41f7d0 tests +0x264 == 0): after GIVE UP (the save is
// deleted instead), in the reveal and under DIALOG_NEXTROUND nothing saves.
TEST(crossword_leave_after_the_round_saves_nothing) {
    Fixture f;
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    ASSERT_EQ(f.discards, 1);
    f.c().leave();
    ASSERT_TRUE(f.saved.empty());
    Fixture s;
    s.solve_all();
    s.c().leave();
    for (int t = 0; t < 10000 && !s.c().dialog_open(); t += 10) s.run(10);
    ASSERT_TRUE(s.c().dialog_kind() == GameDialog::NextRound);
    s.c().leave();
    ASSERT_TRUE(s.saved.empty());
}

// AC-C9: a save made mid-round, through its text, restores the same 20 x 11
// cell storage, cell by cell, the same placements, score and round, and
// each slot's played word and scored flag ("peal" was taken back: scored,
// not solved). The load selects word 0 (0x420fa0, CW54), uses the save up
// (0x461840) and runs no round setup, so no tutorial.
TEST(crossword_resume_restores_the_grid_cell_by_cell) {
    Fixture f;
    f.solve_and_advance();          // "planet" 360
    f.solve_and_advance();          // "nap" 90
    f.enter(f.answer_to_type());    // "peal" 160
    f.c().backspace();              // taken back
    ASSERT_EQ(f.c().score(), 610);
    f.c().leave();
    tt2::SavedGame g;
    ASSERT_TRUE(tt2::parse(tt2::serialize(f.saved.at(0)), g));

    Fixture h;   // another run: its own generator and profile
    h.profile.mode_flags.fill(false);
    h.scene = std::make_unique<CrosswordScene>(h.com, h.ful, h.profile, &h.hs, h.services(), 99, h.crt, &g);
    ASSERT_TRUE(!h.c().resume_failed());
    ASSERT_EQ(h.discards, 1);
    ASSERT_TRUE(!h.c().dialog_open());
    ASSERT_TRUE(!h.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Crossword)]);
    ASSERT_EQ(h.r().root(), std::string("planet"));
    ASSERT_EQ(h.c().round_number(), 1);
    ASSERT_EQ(h.c().score(), 610);
    using Grid = tt2::CrosswordGrid;
    for (int a = 0; a < Grid::A_CELLS; ++a) {
        for (int b = 0; b < Grid::B_LEN; ++b) {
            ASSERT_EQ(h.r().grid().at(a, b).letter, f.r().grid().at(a, b).letter);
            ASSERT_EQ(h.r().grid().at(a, b).blocked, f.r().grid().at(a, b).blocked);
        }
    }
    ASSERT_EQ(h.r().placements().size(), f.r().placements().size());
    for (std::size_t i = 0; i < f.r().placements().size(); ++i) {
        const auto& p = f.r().placements()[i];
        const auto& q = h.r().placements()[i];
        ASSERT_TRUE(p.word == q.word && p.a == q.a && p.b == q.b && p.horizontal == q.horizontal &&
                    p.crossing == q.crossing);
        ASSERT_EQ(h.r().slots()[i].played, f.r().slots()[i].played);
        ASSERT_EQ(h.r().slots()[i].scored, f.r().slots()[i].scored);
    }
    ASSERT_TRUE(h.r().slots()[2].scored && !h.r().slots()[2].solved());
    const auto before = f.c().cells(), after = h.c().cells();
    for (std::size_t i = 0; i < before.size(); ++i) {
        ASSERT_EQ(after[i].letter, before[i].letter);
        ASSERT_EQ(after[i].revealed, before[i].revealed);
        ASSERT_EQ(after[i].blocked, before[i].blocked);
    }
    ASSERT_EQ(h.r().selected(), std::size_t{0});   // solved or not
    ASSERT_TRUE(!h.r().twisted() && !h.r().mistake());
    // It plays on: "peal" again pays nothing (scored is kept).
    h.settle();
    h.c().key_word(false);
    h.settle();
    h.c().key_word(false);
    h.settle();
    ASSERT_EQ(h.r().selected(), std::size_t{2});
    h.enter("eal");
    ASSERT_TRUE(h.r().slots()[2].solved());
    ASSERT_EQ(h.c().score(), 610);
}

// AC-C9 negatives: a corrupt save (no word=, xword[0] not the root, a
// placement that does not fit, a played word of the wrong length) starts
// a new game; resume_failed() makes the front end log a warning; the save
// is not used up.
TEST(crossword_corrupt_save_starts_a_new_game) {
    Fixture f;
    f.enter("planet");
    f.c().leave();
    const tt2::SavedGame good = f.saved.at(0);
    std::vector<tt2::SavedGame> bad(4, good);
    bad[0].word.clear();
    std::swap(bad[1].crossword_words[0], bad[1].crossword_words[1]);
    bad[2].crossword_words[1].a += 1;   // "nap"'s "a" on the root's "n"
    ASSERT_TRUE(bad[2].valid());        // in bounds: restore() is what refuses it
    bad[3].crossword_words[0].played = "plan";
    for (const auto& save : bad) {
        Fixture h;
        const std::string text = tt2::serialize(save);
        h.scene = std::make_unique<CrosswordScene>(h.com, h.ful, h.profile, &h.hs, h.services(), 5, h.crt, &save);
        ASSERT_TRUE(h.c().resume_failed());
        ASSERT_EQ(h.discards, 0);
        ASSERT_EQ(h.c().round_number(), 1);
        ASSERT_EQ(h.c().score(), 0);
        ASSERT_EQ(h.r().solved_count(), std::size_t{0});
        ASSERT_TRUE(h.c().phase() == GamePhase::Playing);
        ASSERT_EQ(tt2::serialize(save), text);
    }
}

// AC-C9 negative: the other modes' saves are left alone. The scene refuses
// them (a new game, no discard), and each still parses back unchanged.
TEST(crossword_other_modes_saves_are_left_alone) {
    std::vector<tt2::SavedGame> others(4);
    others[0].mode = tt2::save_mode_name(tt2::GameMode::Timed);
    others[0].word = "planet";
    others[0].time_ms = 60000;
    others[0].found_words = {"plan", "planet"};
    others[1].mode = tt2::save_mode_name(tt2::GameMode::Untimed);
    others[1].word = "planet";
    others[1].found_words = {"tan"};
    others[2].mode = tt2::save_mode_name(tt2::GameMode::WordOfTheDay);
    others[2].word = "planet";
    others[2].day = 6623;
    others[3].mode = tt2::save_mode_name(tt2::GameMode::Blitz);
    others[3].round = 2;
    others[3].time_ms = 50000;
    others[3].score = 300;
    others[3].blitz_words = {{"planet", true}, {"plant", false}};
    for (const auto& save : others) {
        ASSERT_TRUE(save.valid() && !save.crossword());
        const std::string text = tt2::serialize(save);
        Fixture h;
        h.scene = std::make_unique<CrosswordScene>(h.com, h.ful, h.profile, &h.hs, h.services(), 5, h.crt, &save);
        ASSERT_TRUE(h.c().resume_failed());
        ASSERT_EQ(h.discards, 0);
        ASSERT_EQ(h.c().round_number(), 1);
        tt2::SavedGame back;
        ASSERT_TRUE(tt2::parse(text, back));
        ASSERT_EQ(tt2::serialize(back), text);
    }
}

// AC-C10 (0x41e930, award_crossword_round): a clean round wins Puzzle Pro
// (8) and Perfect Puzzler (9), shown by the shared trophy popup in id order.
// Rejected guesses (popups 0 and 3) block neither.
TEST(crossword_clean_round_wins_puzzle_pro_and_perfect_puzzler) {
    Fixture f;
    f.enter("petlan");   // not a word: popup 0
    f.solve_and_advance();   // "planet"
    f.enter("tn");           // "tan" in "nap"'s slot
    ASSERT_EQ(f.r().slots()[1].played, std::string("tan"));
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    f.solve_and_advance();   // "peal"
    f.enter("tn");           // "tan" again on "lap": popup 3
    ASSERT_EQ(f.count(SoundId::WordReject), 2);
    ASSERT_TRUE(!f.r().slots()[3].solved());
    ASSERT_TRUE(!f.profile.has_trophy(tt2::TROPHY_PUZZLE_PRO));
    f.enter("lp");
    ASSERT_TRUE(f.r().complete());
    ASSERT_TRUE(f.profile.has_trophy(tt2::TROPHY_PUZZLE_PRO));
    ASSERT_TRUE(f.profile.has_trophy(tt2::TROPHY_PERFECT_PUZZLER));
    f.run(10);
    ASSERT_EQ(f.c().trophy_popup_id(), tt2::TROPHY_PUZZLE_PRO);
    ASSERT_EQ(f.c().trophies_waiting(), std::size_t{1});
    ASSERT_EQ(f.count(SoundId::TrophyWon), 1);
}

// AC-C10 negatives: one TWIST (+0x370) blocks Puzzle Pro; one take-back
// (+0x371) blocks Perfect Puzzler.
TEST(crossword_twist_or_take_back_blocks_its_trophy) {
    Fixture t;
    t.c().key_space();
    t.solve_all();
    ASSERT_TRUE(t.r().complete());
    ASSERT_TRUE(!t.profile.has_trophy(tt2::TROPHY_PUZZLE_PRO));
    ASSERT_TRUE(t.profile.has_trophy(tt2::TROPHY_PERFECT_PUZZLER));
    t.run(10);
    ASSERT_EQ(t.c().trophy_popup_id(), tt2::TROPHY_PERFECT_PUZZLER);

    Fixture m;
    m.enter("planet");
    m.c().backspace();   // the take-back
    m.solve_all();
    ASSERT_TRUE(m.r().complete());
    ASSERT_TRUE(m.profile.has_trophy(tt2::TROPHY_PUZZLE_PRO));
    ASSERT_TRUE(!m.profile.has_trophy(tt2::TROPHY_PERFECT_PUZZLER));
}

// AC-C12 (0x41ba40 step 2): a player's first Crossword round opens
// DIALOG_TUTORIAL "Crossword Mode" and sets mode flag 3 only; READ MORE
// opens HOW TO PLAY over it. Negatives: no tutorial on the next round or
// in a second game, and other modes' flags do not count.
TEST(crossword_tutorial_shows_once_per_player) {
    Fixture f;
    f.profile = new_player();
    f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Blitz)] = true;   // another mode seen
    const int changes = f.changes;
    f.scene = std::make_unique<CrosswordScene>(f.com, f.ful, f.profile, &f.hs, f.services(), 5, f.crt);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::Tutorial);
    ASSERT_EQ(f.c().dialog().labels[0].text, std::string("Crossword Mode"));
    ASSERT_TRUE(f.changes > changes);
    for (std::size_t m = 0; m < f.profile.mode_flags.size(); ++m)
        ASSERT_EQ(f.profile.mode_flags[m], m == 3 || m == static_cast<std::size_t>(tt2::GameMode::Blitz));
    f.c().dialog_press("BUTTON_HELP");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::HowToPlay);
    f.c().dialog_press("BUTTON_OK");
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::Tutorial);
    f.c().dialog_press("BUTTON_OK");
    ASSERT_TRUE(!f.c().dialog_open());
    f.settle();
    f.solve_all();
    for (int t = 0; t < 10000 && !f.c().dialog_open(); t += 10) f.run(10);
    f.c().dialog_press("BUTTON_OK");   // NEXT ROUND
    ASSERT_EQ(f.c().round_number(), 2);
    ASSERT_TRUE(!f.c().dialog_open());
    f.scene = std::make_unique<CrosswordScene>(f.com, f.ful, f.profile, &f.hs, f.services(), 5, f.crt);
    ASSERT_TRUE(!f.c().dialog_open());
}

// AC-C14 (0x421e50, crossword.md §3.9): after a solved round a row click
// opens the definition of its played word. At 1.0 the reveal's zoom-out
// chain clears busy 1500 ms in (0x41cce0) while the 2500 ms wait still runs
// (it hangs off 0x41cab0's node, @0x42019d), so rows answer in between.
// Negatives: a click in play selects, a click while busy or under the
// dialog opens nothing, and popup 10 never shows.
TEST(crossword_definition_links_open_the_played_word_after_the_round) {
    Fixture f;
    f.click("BUTTON_Zoom");
    f.settle();
    f.click_cell(8, 4);   // in play: selects "nap"
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    f.settle();
    f.c().key_word(false);
    f.settle();
    f.solve_and_advance();   // "planet"
    f.solve_and_advance();   // "nap"
    f.enter("ale");          // "pale" in "peal"'s slot
    ASSERT_EQ(f.r().slots()[2].played, std::string("pale"));
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    f.enter("ap");           // "alp": "lap"'s middle is "pale"'s "l"
    ASSERT_TRUE(f.r().complete());
    ASSERT_TRUE(f.urls.empty());
    for (int t = 0; t < CROSSWORD_PAN_MS + 10 + CROSSWORD_ZOOM_OUT_MS + CROSSWORD_PAN_MS - 10; t += 10) {
        f.run(10);
        f.click_cell(6, 6);
    }
    ASSERT_TRUE(f.c().busy());
    ASSERT_TRUE(f.urls.empty());
    f.run(10);
    ASSERT_TRUE(!f.c().busy() && !f.c().dialog_open());
    ASSERT_TRUE(f.c().zoom_in_flag());   // the reveal left the flag alone
    f.click_cell(6, 6);
    f.click_cell(5, 7);
    f.click_cell(11, 5);
    f.click_cell(3, 2);   // no word
    const std::vector<std::string> want = {definition_url("pale"), definition_url("alp"), definition_url("planet")};
    ASSERT_TRUE(f.urls == want);
    f.type("p");   // the round is over: no typing
    ASSERT_TRUE(f.c().typed().empty());
    for (int t = 0; t < 10000 && !f.c().dialog_open(); t += 10) {
        f.run(10);
        ASSERT_TRUE(f.c().popup_kind() != PopupKind::Definitions);
    }
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    f.click_cell(6, 6);
    ASSERT_EQ(f.urls.size(), want.size());
}

// AC-C14 after GIVE UP: rows open their answers (0x43ce60), "peal" for the
// slot the player filled with "pale".
TEST(crossword_definition_links_after_give_up_open_the_answers) {
    Fixture f;
    f.click("BUTTON_Zoom");
    f.settle();
    f.solve_and_advance();   // "planet"
    f.solve_and_advance();   // "nap"
    f.enter("ale");          // "pale"
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    for (int t = 0; t < CROSSWORD_PAN_MS + 10 + CROSSWORD_ZOOM_OUT_MS + CROSSWORD_PAN_MS - 10; t += 10) {
        f.run(10);
        f.click_cell(6, 6);
    }
    ASSERT_TRUE(f.urls.empty());
    f.run(10);
    f.click_cell(6, 6);
    f.click_cell(5, 7);
    const std::vector<std::string> want = {definition_url("peal"), definition_url("lap")};
    ASSERT_TRUE(f.urls == want);
    for (int t = 0; t < 10000 && !f.c().dialog_open(); t += 10) f.run(10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::EndGame);
    f.click_cell(6, 6);
    ASSERT_EQ(f.urls.size(), want.size());
}

// AC-C14 negative: at 0.5 the reveal is busy from start to dialog, so a
// row click in it opens nothing, solved or given up.
TEST(crossword_reveal_in_the_overview_opens_no_definition) {
    Fixture f;
    f.solve_all();
    for (int t = 0; t < 10000 && !f.c().dialog_open(); t += 10) {
        f.run(10);
        if (!f.c().dialog_open()) f.click_cell(6, 6);
    }
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(f.urls.empty());

    Fixture g;
    g.enter("planet");
    g.c().press("BUTTON_GiveUp");
    g.c().dialog_press("BUTTON_YES");
    for (int t = 0; t < 10000 && !g.c().dialog_open(); t += 10) {
        g.run(10);
        if (!g.c().dialog_open()) g.click_cell(6, 6);
    }
    ASSERT_TRUE(g.c().dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(g.urls.empty());
}

// ---- the view (plan T6b.5) ---------------------------------------------

namespace {

std::vector<SpriteState> named(const std::vector<SpriteState>& all, const std::string& name) {
    std::vector<SpriteState> out;
    for (const auto& s : all) if (s.name == name) out.push_back(s);
    return out;
}
int letter_frame(char c, int frame) { return (c - 'a') * BALL_FRAMES_PER_LETTER + frame; }

}  // namespace

// The user's play-test of the original (CW74, CW75): a locked crossing
// letter takes a ball of its own out of the selected word's rack, so its
// slot shows the gold dot; a typed ball empties its slot in place, and the
// rack never closes up. Buttons: with only the locked letter, TWIST on and
// CLEAR, LAST WORD, ENTER off; one typed letter turns CLEAR and ENTER on.
// TWIST keeps the locked letter's slot empty. "nap", its "a" locked by the
// solved root; "planet" has one "a".
TEST(crossword_locked_letter_takes_a_ball_from_the_rack) {
    Fixture f;
    f.solve_and_advance();
    ASSERT_EQ(f.r().selected(), std::size_t{1});
    ASSERT_EQ(f.r().locked_letter(1, 1), 'a');
    const std::size_t a_slot = f.c().rack().find('a');
    ASSERT_TRUE(f.c().rack_in_row(a_slot));
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.c().row_text(), std::string("a"));
    std::vector<SpriteState> rack = f.c().rack_sprites();
    ASSERT_EQ(named(rack, "RACK_SLOT").size(), std::size_t{6});
    ASSERT_EQ(named(rack, "RACK_BALL").size(), std::size_t{5});
    for (const auto& b : named(rack, "RACK_BALL")) ASSERT_TRUE(!near(b.x, ball_center_x(6, static_cast<int>(a_slot))));
    ASSERT_TRUE(f.b("BUTTON_Twist").enabled);   // CW74
    ASSERT_TRUE(!f.b("BUTTON_Clear").enabled && !f.b("BUTTON_LastWord").enabled && !f.b("BUTTON_Enter").enabled);
    // The locked letter's ball is in the row: no "a" left to type.
    const int places = f.count(SoundId::LetterPlace);
    f.type("a");
    ASSERT_TRUE(f.c().typed().empty());
    ASSERT_EQ(f.count(SoundId::LetterPlace), places);
    // TWIST moves only the balls still in the rack.
    f.c().key_space();
    ASSERT_EQ(f.c().rack()[a_slot], 'a');
    ASSERT_TRUE(f.c().rack_in_row(a_slot));
    // One typed letter (CW75): its slot empties where it was.
    const std::size_t n_slot = f.c().rack().find('n');
    f.type("n");
    ASSERT_EQ(f.c().typed(), std::string("n"));
    ASSERT_TRUE(f.c().rack_in_row(n_slot));
    rack = f.c().rack_sprites();
    ASSERT_EQ(named(rack, "RACK_SLOT").size(), std::size_t{6});
    ASSERT_EQ(named(rack, "RACK_BALL").size(), std::size_t{4});
    ASSERT_TRUE(f.b("BUTTON_Twist").enabled && f.b("BUTTON_Clear").enabled && f.b("BUTTON_Enter").enabled);
    ASSERT_TRUE(!f.b("BUTTON_LastWord").enabled);
    // Both letters are balls in the row; only the locked one is a solved word's.
    ASSERT_TRUE(f.cell(8, 4).letter == 'n' && f.cell(8, 4).ball && !f.cell(8, 4).revealed);
    ASSERT_TRUE(f.cell(8, 5).letter == 'a' && f.cell(8, 5).ball && f.cell(8, 5).revealed);
    // Negative: the take-back returns the typed ball, never the locked one.
    f.c().backspace();
    f.c().backspace();
    ASSERT_TRUE(f.c().typed().empty() && !f.c().rack_in_row(n_slot));
    ASSERT_TRUE(f.c().rack_in_row(a_slot));
    ASSERT_EQ(f.count(SoundId::LetterClear), 1);
}

// CW74 with a second copy: the root "seated" has two "e"s. Once the root is
// solved, a word that crosses it on an "e" locks one "e" ball into its row,
// and the other "e" can still be typed, once.
TEST(crossword_locked_letter_leaves_a_second_copy_to_type) {
    const std::vector<std::string> words = {
        "seated",
        "seat", "east", "eats", "teas", "sate", "date", "seed", "teed", "ease", "tees", "sees",
        "sea", "eat", "tea", "ate", "eta", "set", "sat", "sad", "dee", "see", "tee", "tad", "ted",
        "tease", "eased", "dates",
    };
    bool found = false;
    for (std::int64_t seed = 1; seed <= 60 && !found; ++seed) {
        Fixture f(words, seed);
        if (f.r().root() != "seated") continue;
        f.enter(f.answer_to_type());
        f.run(CROSSWORD_ADVANCE_MS + 10);
        f.settle();
        for (std::size_t step = 1; step < f.r().placements().size() && !found; ++step) {
            const std::size_t sel = f.r().selected();
            const std::string& w = f.r().placements()[sel].word;
            int locked_e = -1;
            for (std::size_t i = 0; i < w.size(); ++i)
                if (f.r().locked_letter(sel, i) == 'e') locked_e = static_cast<int>(i);
            const bool one_e = std::count(w.begin(), w.end(), 'e') == 1;
            if (locked_e >= 0 && one_e && !f.r().slots()[sel].solved()) {
                const std::string& r = f.c().rack();
                std::vector<std::size_t> es;
                for (std::size_t k = 0; k < r.size(); ++k) if (r[k] == 'e') es.push_back(k);
                ASSERT_EQ(es.size(), std::size_t{2});
                ASSERT_TRUE(f.c().rack_in_row(es[0]) && !f.c().rack_in_row(es[1]));   // the leftmost "e" went
                f.type("e");
                ASSERT_EQ(f.c().typed(), std::string("e"));
                ASSERT_TRUE(f.c().rack_in_row(es[1]));
                f.type("e");   // no third "e"
                ASSERT_EQ(f.c().typed(), std::string("e"));
                found = true;
            }
            f.c().key_word(true);
            f.settle();
        }
    }
    ASSERT_TRUE(found);
}

// 0x41de10: the highlight is a child of the grid, so it turns with it. On
// "nap" (down) it moves at once and stands across the word, pi/2, its
// top-left (11, -10) px (scaled) from the top-right corner of the first
// cell; half-way through the turn it is at pi/2 - pi/4; at rest it is
// level. Negative: on an across word with the grid level it never turns.
TEST(crossword_highlight_turns_with_the_grid) {
    Fixture f;
    ASSERT_TRUE(near(f.c().highlight_sprite().rotation, 0.0f));
    f.c().key_word(true);
    SpriteState h = f.c().highlight_sprite();
    ASSERT_TRUE(near(h.rotation, CROSSWORD_HIGHLIGHT_VERTICAL));
    auto corner = [&](float a, float b) { return f.c().view().to_screen(a, b); };
    auto [cx, cy] = corner(9.0f + 11.0f / 69.0f, 4.0f - 10.0f / 69.0f);
    ASSERT_TRUE(near(h.x, cx) && near(h.y, cy));
    ASSERT_TRUE(near(h.x, f.c().cell_center(8, 4).first + 17.25f + 5.5f));   // grid still level: right of the column
    f.run(CROSSWORD_PAN_MS);
    f.run(10);
    f.run(CROSSWORD_TURN_WAIT_MS - 10);
    f.run(CROSSWORD_TURN_MS / 2);
    ASSERT_TRUE(near(f.c().view_turn(), CROSSWORD_TURN_VERTICAL * 0.5f));
    h = f.c().highlight_sprite();
    ASSERT_TRUE(near(h.rotation, CROSSWORD_HIGHLIGHT_VERTICAL + CROSSWORD_TURN_VERTICAL * 0.5f));
    std::tie(cx, cy) = corner(9.0f + 11.0f / 69.0f, 4.0f - 10.0f / 69.0f);
    ASSERT_TRUE(near(h.x, cx) && near(h.y, cy));
    f.settle();
    h = f.c().highlight_sprite();
    ASSERT_TRUE(std::fabs(h.rotation) < 1e-6f);
    ASSERT_TRUE(near(h.x, 400.0f - 1.5f * 34.5f - 5.0f) && near(h.y, 189.0f - 17.25f - 5.5f));
    f.c().key_word(true);   // "peal", down: no turn, still level
    f.settle();
    f.c().key_word(true);   // "lap", across: the grid turns back
    h = f.c().highlight_sprite();
    ASSERT_TRUE(near(h.rotation, CROSSWORD_TURN_VERTICAL));   // across a turned grid, until it turns back
    f.settle();
    ASSERT_TRUE(near(f.c().highlight_sprite().rotation, 0.0f));
}

// The grid's sprites (CW72..CW75): every covered cell is XwordBox frame 0
// under the hole, centred on the cell and scaled with the view; a typed
// letter is a pearl (frame 11), a solved word's letter a tile (frame 16),
// and the selected row's locked letter a pearl over the crossing word's
// tile. Boxes and holes come before every letter. Negative: blocked cells
// draw nothing, and no sheen shows in play.
TEST(crossword_grid_sprites_draw_holes_pearls_and_tiles) {
    Fixture f;
    std::size_t covered = 0;
    for (const auto& c : f.c().cells()) covered += c.blocked ? 0 : 1;
    std::vector<SpriteState> g = f.c().grid_sprites();
    const auto boxes = named(g, "XWORD_BOX"), holes = named(g, "XWORD_HOLE");
    ASSERT_EQ(boxes.size(), covered);
    ASSERT_EQ(holes.size(), covered);
    ASSERT_EQ(g.size(), 2 * covered);
    ASSERT_EQ(boxes[0].sheet.image_id, std::string("XwordBox"));
    ASSERT_EQ(boxes[0].frame, CROSSWORD_CELL_FRAME);
    ASSERT_EQ(holes[0].sheet.image_id, std::string("IMAGE_XWORD_OVERLAY"));
    const auto [x, y] = f.c().cell_center(6, 5);
    bool on_root = false;
    for (const auto& b : boxes) {
        ASSERT_TRUE(b.anchor == Anchor::Center && near(b.scale, 0.5f) && near(b.rotation, 0.0f));
        on_root = on_root || (near(b.x, x) && near(b.y, y));
    }
    ASSERT_TRUE(on_root);
    f.type("pl");
    g = f.c().grid_sprites();
    auto letters = named(g, "LETTER");
    ASSERT_EQ(letters.size(), std::size_t{2});
    ASSERT_EQ(letters[0].frame, letter_frame('p', BALL_IDLE_FRAME));
    ASSERT_EQ(letters[1].frame, letter_frame('l', BALL_IDLE_FRAME));
    ASSERT_TRUE(near(letters[0].x, x) && near(letters[0].y, y));
    ASSERT_EQ(g[2 * covered].name, std::string("LETTER"));   // after every box and hole
    f.c().key_delete();
    f.solve_and_advance();   // "planet", then "nap" with its "a" locked
    g = f.c().grid_sprites();
    letters = named(g, "LETTER");
    ASSERT_EQ(letters.size(), std::size_t{6});
    int tiles = 0, pearls = 0;
    for (const auto& l : letters) {
        if (l.frame % BALL_FRAMES_PER_LETTER == CROSSWORD_TILE_FRAME) ++tiles;
        if (l.frame == letter_frame('a', BALL_IDLE_FRAME)) ++pearls;
    }
    ASSERT_EQ(tiles, 5);   // the root's other letters
    ASSERT_EQ(pearls, 1);  // the locked "a", a ball in "nap"'s row
    ASSERT_TRUE(named(g, "SHEEN").empty());
    ASSERT_EQ(f.c().sheen_frame(), -1);
}

// The rack (word+0xb4, CW74, CW75): one XwordBox frame 1 dot per root
// letter at x = 227.5 + 69 k, y = 409.5, and the letter's pearl over each
// slot whose ball is still there. Negatives: no rack on a solved word or
// after the round.
TEST(crossword_rack_sprites_keep_every_slot) {
    Fixture f;
    std::vector<SpriteState> rack = f.c().rack_sprites();
    const auto dots = named(rack, "RACK_SLOT");
    auto balls = named(rack, "RACK_BALL");
    ASSERT_EQ(dots.size(), std::size_t{6});
    ASSERT_EQ(balls.size(), std::size_t{6});
    for (int k = 0; k < 6; ++k) {
        const auto& d = dots[static_cast<std::size_t>(k)];
        ASSERT_EQ(d.sheet.image_id, std::string("XwordBox"));
        ASSERT_EQ(d.frame, CROSSWORD_RACK_FRAME);
        ASSERT_TRUE(d.anchor == Anchor::Center && near(d.x, 227.5f + 69.0f * k) && near(d.y, 409.5f));
        const auto& b = balls[static_cast<std::size_t>(k)];
        ASSERT_TRUE(near(b.x, d.x) && near(b.y, d.y));
        ASSERT_EQ(b.frame, letter_frame(f.c().rack()[static_cast<std::size_t>(k)], BALL_IDLE_FRAME));
    }
    ASSERT_EQ(rack[6].name, std::string("RACK_BALL"));   // the dots first
    const std::size_t slot = f.c().rack().find('t');
    f.type("t");
    balls = named(f.c().rack_sprites(), "RACK_BALL");
    ASSERT_EQ(balls.size(), std::size_t{5});
    for (const auto& b : balls) ASSERT_TRUE(!near(b.x, 227.5f + 69.0f * static_cast<float>(slot)));
    f.c().key_delete();
    f.enter("planet");
    ASSERT_TRUE(f.c().rack_sprites().empty());   // solved
    f.run(CROSSWORD_ADVANCE_MS + 10);
    f.settle();
    ASSERT_EQ(f.c().rack_sprites().size(), std::size_t{6 + 5});   // "nap": the locked "a" left
    f.c().press("BUTTON_GiveUp");
    f.c().dialog_press("BUTTON_YES");
    ASSERT_TRUE(f.c().rack_sprites().empty());
}

// The sheen (0x41fed0 -> 0x43d310, file read): with the solved puzzle's
// reveal every letter shows IMAGE_CHICKLET_SHEEN, frames 0..8 at 100 ms,
// once, then rests on frame 8; the next round has none. Negative: GIVE
// UP's reveal runs no sheen.
TEST(crossword_solved_puzzle_runs_the_sheen_over_every_letter) {
    Fixture f;
    f.solve_all();
    f.run(CROSSWORD_PAN_MS);
    ASSERT_EQ(f.c().sheen_frame(), -1);
    f.run(10);   // the reveal: SOUND_letter_sheen and the sheen together
    ASSERT_EQ(f.count(SoundId::LetterSheen), 1);
    ASSERT_EQ(f.c().sheen_frame(), 0);
    std::vector<SpriteState> g = f.c().grid_sprites();
    const auto sheens = named(g, "SHEEN");
    ASSERT_EQ(sheens.size(), named(g, "LETTER").size());
    ASSERT_EQ(sheens.size(), std::size_t{6 + 2 + 3 + 2});   // planet, nap, peal, lap less the crossings
    ASSERT_EQ(sheens[0].sheet.image_id, std::string("IMAGE_CHICKLET_SHEEN"));
    ASSERT_EQ(sheens[0].sheet.frames, CROSSWORD_SHEEN_FRAMES);
    ASSERT_TRUE(near(sheens[0].scale, 0.5f) && sheens[0].anchor == Anchor::Center);
    for (const auto& l : named(g, "LETTER")) ASSERT_EQ(l.frame % BALL_FRAMES_PER_LETTER, CROSSWORD_TILE_FRAME);
    ASSERT_EQ(g.back().name, std::string("SHEEN"));   // over the letters
    f.run(CROSSWORD_SHEEN_FRAME_MS);
    ASSERT_EQ(f.c().sheen_frame(), 1);
    f.run(7 * CROSSWORD_SHEEN_FRAME_MS);
    ASSERT_EQ(f.c().sheen_frame(), 8);
    f.run(1000);
    ASSERT_EQ(f.c().sheen_frame(), 8);   // rests on the last frame
    for (int t = 0; t < 5000 && !f.c().dialog_open(); t += 10) f.run(10);
    ASSERT_TRUE(f.c().dialog_kind() == GameDialog::NextRound);
    f.c().key_return();
    ASSERT_EQ(f.c().sheen_frame(), -1);
    ASSERT_TRUE(named(f.c().grid_sprites(), "SHEEN").empty());

    Fixture g2;
    g2.c().press("BUTTON_GiveUp");
    g2.c().dialog_press("BUTTON_YES");
    for (int t = 0; t < 10000 && !g2.c().dialog_open(); t += 10) {
        g2.run(10);
        ASSERT_EQ(g2.c().sheen_frame(), -1);
    }
}

// The preload list (boot_preload.hpp, PRELOAD_CROSSWORD): every image the
// scene draws, in play, in the sheen and in its popups, is in art(), once.
// Negatives: the hidden XML sprites, the hidden NEXT ROUND button, the
// crosswordscene LetterBox and the letter bar are never drawn, so never
// listed.
TEST(crossword_art_lists_every_image_it_draws) {
    Fixture f;
    const std::vector<SheetSpec> art = f.c().art();
    auto listed = [&art](const std::string& id) {
        return std::count_if(art.begin(), art.end(), [&](const SheetSpec& s) { return s.image_id == id; });
    };
    auto covers = [&](const std::vector<SpriteState>& sprites) {
        for (const auto& s : sprites) if (s.visible) ASSERT_EQ(listed(s.sheet.image_id), 1);
    };
    for (const auto& s : art) ASSERT_EQ(listed(s.image_id), 1);
    ASSERT_EQ(listed(f.c().background_id()), 1);
    covers(f.c().sprites());
    for (const auto& b : f.c().buttons()) if (b.sprite.visible) ASSERT_EQ(listed(b.sprite.sheet.image_id), 1);
    covers({f.c().highlight_sprite()});
    covers(f.c().rack_sprites());
    f.type("pl");
    covers(f.c().grid_sprites());
    f.c().key_delete();
    f.solve_all();
    f.run(CROSSWORD_PAN_MS + 10);
    covers(f.c().grid_sprites());   // tiles and the sheen
    for (PopupKind k : {PopupKind::NotListed, PopupKind::AlreadyGuessed, PopupKind::CrosswordIntersection,
                        PopupKind::LockedLetter, PopupKind::DeadEnd})
        ASSERT_EQ(listed(popup_sheet(k).image_id), 1);
    for (const char* id : {"IMAGE_CROSSWORDHIGHLIGHT", "IMAGE_IMAGE_POPUP_4", "IMAGE_IMAGE_POPUP_7",
                           "IMAGE_IMAGE_POPUP_9", "XwordBox", "IMAGE_XWORD_OVERLAY", "IMAGE_CHICKLET_SHEEN",
                           "crosswordscene/BACKGROUND", "IMAGE_OVERLAY", "IMAGE_BTN_ARROW_NEXT", "IMAGE_BTN_ARROW_PREV"})
        ASSERT_EQ(listed(id), 1);
    for (const char* id : {"IMAGE_PANEL_BG", "IMAGE_PANEL_BOTTOM", "BUTTON_NextRound", "crosswordscene/LetterBox",
                           "IMAGE_LETTERBAR", "IMAGE_XBOX", "LetterBox"})
        ASSERT_EQ(listed(id), 0);
}
