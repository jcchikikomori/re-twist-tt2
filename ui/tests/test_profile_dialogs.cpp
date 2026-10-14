// Headless tests for the name-input / player-select dialogs
// (docs/reverse-engineering/profiles.md §2 and §3).

#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2ui/profile_dialogs.hpp"

using namespace tt2ui;

namespace {

struct Fixture {
    tt2::PlayerManager pm;
    std::vector<SoundId> sounds;
    int changes = 0;
    ProfileFlow flow;

    Fixture()
        : flow(pm, ProfileFlow::Services{[this](SoundId s) { sounds.push_back(s); },
                                         [this] { ++changes; },
                                         [] { return 0x1234u; }}) {}

    // Screen position of a dialog button's centre.
    std::pair<float, float> centre(const char* name) const {
        const MenuButton* b = flow.dialog().button(name);
        ASSERT_TRUE(b != nullptr);
        const float x = flow.dialog().left() + b->sprite.left() + b->sprite.sheet.frame_w * 0.5f;
        const float y = flow.dialog().top() + b->sprite.top() + b->sprite.sheet.frame_h * 0.5f;
        return {x, y};
    }
    void click(const char* name) {
        auto [x, y] = centre(name);
        flow.mouse_move(x, y);
        flow.mouse_down(x, y);
        flow.mouse_up(x, y);
    }
    void type(const char* s) { flow.text_input(s); }
};

}  // namespace

TEST(dialog_fill_rects_cover_frame_minus_shadows_and_corners) {
    // DIALOG_NEWPLAYER is 402x223.
    auto r = dialog_fill_rects(402, 223);
    // Horizontal bar: full height, corners cut on the left and right.
    ASSERT_TRUE(r[0] == (FillRect{11, 1, 374, 206}));
    // Vertical bar: full width, corners cut at the top and bottom.
    ASSERT_TRUE(r[1] == (FillRect{1, 11, 394, 186}));
    // Union spans x in [1, 395) and y in [1, 207): the opaque right
    // band starts at 402-22=380 and the bottom band at 223-35=188, so
    // both bands sit on top of the fill and the drop shadows do not.
    ASSERT_EQ(r[1].x + r[1].w, 402 - FRAME_FILL_RIGHT);
    ASSERT_EQ(r[0].y + r[0].h, 223 - FRAME_FILL_BOTTOM);
    ASSERT_TRUE(r[1].x + r[1].w > 380);
    ASSERT_TRUE(r[0].y + r[0].h > 188);
    // The corner pixel itself stays uncovered.
    ASSERT_TRUE(r[0].x > FRAME_FILL_LEFT && r[1].y > FRAME_FILL_TOP);
    // Degenerate dialog: the rects collapse instead of going negative-safe.
    auto tiny = dialog_fill_rects(10, 10);
    ASSERT_TRUE(tiny[0].w < 0 && tiny[1].h < 0);
}

TEST(welcome_text_follows_returning_flag) {
    tt2::PlayerManager pm;
    ASSERT_EQ(welcome_text(pm), std::string());
    pm.add("Ann", 1);
    ASSERT_EQ(welcome_text(pm), std::string("Welcome Ann"));
    tt2::PlayerProfile old = tt2::PlayerProfile::create("Bob", 2);
    pm.add_loaded(old);
    pm.select("Bob");
    ASSERT_EQ(welcome_text(pm), std::string("Welcome back Bob"));
}

TEST(menu_entry_with_no_players_opens_name_input_without_cancel) {
    Fixture f;
    f.flow.on_menu_entered();
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    ASSERT_TRUE(f.flow.wants_text_input());
    const Dialog& d = f.flow.dialog();
    ASSERT_EQ(d.w, 402);
    ASSERT_EQ(d.h, 223);
    ASSERT_EQ(d.left(), 199.0f);
    ASSERT_EQ(d.top(), 188.0f);
    ASSERT_EQ(d.label("NEWPLAYER_HEADER")->text, std::string("What is your name?"));
    ASSERT_TRUE(!d.button("BUTTON_CANCEL")->enabled);
    // Cancel does nothing for the first player.
    f.flow.key_escape();
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    // OK with an empty name keeps the dialog open.
    f.flow.key_return();
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    ASSERT_EQ(f.pm.count(), 0);
}

TEST(menu_entry_with_players_opens_nothing) {
    Fixture f;
    f.pm.add("Ann", 1);
    f.flow.on_menu_entered();
    ASSERT_TRUE(!f.flow.open());
}

TEST(name_input_limits_to_15_code_points_and_strips_controls) {
    Fixture f;
    f.flow.open_new_player(true);
    f.type("ab\tc\n");
    ASSERT_EQ(f.flow.input_text(), std::string("abc"));
    f.type("\xC3\x92");                               // Ò
    ASSERT_EQ(f.flow.input_text(), std::string("abc\xC3\x92"));
    f.type("0123456789ABCDEF");
    ASSERT_EQ(tt2::utf8_length(f.flow.input_text()), std::size_t{15});
    ASSERT_EQ(f.flow.input_text(), std::string("abc\xC3\x92" "0123456789A"));
    f.flow.backspace();
    f.flow.backspace();
    ASSERT_EQ(f.flow.input_text(), std::string("abc\xC3\x92" "012345678"));
    for (int i = 0; i < 20; ++i) f.flow.backspace();
    ASSERT_EQ(f.flow.input_text(), std::string());
    f.flow.backspace();                               // empty: harmless
}

TEST(name_input_ok_adds_player_and_closes) {
    Fixture f;
    f.flow.on_menu_entered();
    f.type("Ann");
    f.click("BUTTON_OK");
    ASSERT_TRUE(!f.flow.open());
    ASSERT_EQ(f.pm.count(), 1);
    ASSERT_EQ(f.pm.current()->name, std::string("Ann"));
    ASSERT_EQ(f.pm.current()->bag.seed, std::uint32_t{0x1234});
    ASSERT_EQ(f.changes, 1);
    ASSERT_TRUE(!f.sounds.empty());
    ASSERT_TRUE(f.sounds.back() == SoundId::Button);
}

TEST(name_input_duplicate_shows_message_then_returns_to_input) {
    Fixture f;
    f.pm.add("Ann", 1);
    f.flow.open_new_player(true);
    f.type("Ann");
    f.flow.key_return();
    ASSERT_TRUE(f.flow.active() == DialogKind::Message);
    ASSERT_EQ(f.flow.dialog().label("HEADER")->text, std::string("Player exists."));
    ASSERT_EQ(f.flow.dialog().label("MESSAGE")->text,
              std::string("That name has already been used. Please choose another."));
    ASSERT_EQ(f.pm.count(), 1);
    f.flow.key_return();
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    ASSERT_EQ(f.flow.input_text(), std::string("Ann"));   // keeps what was typed
    f.flow.backspace();
    f.type("e");
    f.flow.key_return();
    ASSERT_EQ(f.pm.count(), 2);
    ASSERT_EQ(f.pm.current()->name, std::string("Ane"));
}

TEST(name_input_cancel_returns_to_player_select_when_opened_from_add) {
    Fixture f;
    f.pm.add("Ann", 1);
    f.flow.open_player_select();
    f.click("BUTTON_ADD");
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    ASSERT_TRUE(f.flow.dialog().button("BUTTON_CANCEL")->enabled);
    f.type("x");
    f.click("BUTTON_CANCEL");
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    ASSERT_EQ(f.pm.count(), 1);
}

TEST(cursor_blinks_300_on_300_off) {
    Fixture f;
    f.flow.open_new_player(true);
    ASSERT_TRUE(f.flow.cursor_visible());
    f.flow.update(299);
    ASSERT_TRUE(f.flow.cursor_visible());
    f.flow.update(1);
    ASSERT_TRUE(!f.flow.cursor_visible());
    f.flow.update(300);
    ASSERT_TRUE(f.flow.cursor_visible());
    f.flow.update(650);   // 300 off + 300 on + 50 -> on
    ASSERT_TRUE(f.flow.cursor_visible());
    f.flow.update(250);   // 50 + 250 = 300 -> off
    ASSERT_TRUE(!f.flow.cursor_visible());
}

TEST(player_select_layout_and_rows) {
    Fixture f;
    f.pm.add("Ann", 1);
    f.pm.add("Bob", 2);
    f.pm.select("Ann");
    f.flow.open_player_select();
    const Dialog& d = f.flow.dialog();
    ASSERT_TRUE(d.kind == DialogKind::PlayerSelect);
    ASSERT_EQ(d.w, 422);
    ASSERT_EQ(d.h, 367);
    ASSERT_EQ(d.label("SELECTNAME_HEADER")->text, std::string("Select your name"));
    ASSERT_EQ(d.label("SELECTNAME_HEADER")->x, 211.0f);
    ASSERT_EQ(d.label("SELECTNAME_HEADER")->y, 17.0f);
    // Rows: 80, 120, ... ; only the first two visible.
    ASSERT_EQ(d.button("NAME_0")->sprite.y, 80.0f);
    ASSERT_EQ(d.button("NAME_1")->sprite.y, 120.0f);
    ASSERT_EQ(d.button("NAME_0")->sprite.left(), 61.0f);   // 300 wide, centred at 211
    ASSERT_TRUE(d.button("NAME_1")->sprite.visible);
    ASSERT_TRUE(!d.button("NAME_2")->sprite.visible);
    ASSERT_TRUE(!d.button("NAME_2")->enabled);
    ASSERT_EQ(d.label("NAMELABEL_1")->text, std::string("Bob"));
    ASSERT_EQ(d.label("NAMELABEL_1")->y, 124.0f);
    ASSERT_TRUE(!d.label("NAMELABEL_4")->visible);
    // Highlight on the current player (Ann, row 0).
    ASSERT_EQ(f.flow.selected_row(), 0);
    const SpriteState* border = nullptr;
    for (const auto& s : d.sprites) if (s.name == "BORDER") border = &s;
    ASSERT_TRUE(border != nullptr && border->visible);
    ASSERT_EQ(border->y, 80.0f);
    ASSERT_EQ(border->x, 61.0f);
    // Buttons.
    ASSERT_EQ(d.button("BUTTON_ADD")->sprite.left(), 146.0f - 119.0f);
    ASSERT_EQ(d.button("BUTTON_REMOVE")->sprite.left(), 153.0f);
    ASSERT_EQ(d.button("BUTTON_OK")->sprite.left(), 337.0f - 59.5f);
    ASSERT_EQ(d.button("BUTTON_OK")->sprite.sheet.frame_h, 50);
    ASSERT_TRUE(d.button("BUTTON_ADD")->enabled);
    ASSERT_TRUE(d.button("BUTTON_REMOVE")->enabled);
}

TEST(player_select_click_row_selects_and_ok_closes) {
    Fixture f;
    f.pm.add("Ann", 1);
    f.pm.add("Bob", 2);
    f.pm.select("Ann");
    f.flow.open_player_select();
    f.click("NAME_1");
    ASSERT_EQ(f.pm.current()->name, std::string("Bob"));
    ASSERT_EQ(f.flow.selected_row(), 1);
    ASSERT_EQ(f.changes, 1);
    // Clicking an empty row does nothing (invisible button).
    f.click("NAME_3");
    ASSERT_EQ(f.pm.current()->name, std::string("Bob"));
    f.click("BUTTON_OK");
    ASSERT_TRUE(!f.flow.open());
}

TEST(player_select_add_disabled_when_full) {
    Fixture f;
    for (const char* n : {"A", "B", "C", "D", "E"}) f.pm.add(n, 1);
    f.flow.open_player_select();
    ASSERT_TRUE(!f.flow.dialog().button("BUTTON_ADD")->enabled);
    f.click("BUTTON_ADD");
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
}

TEST(player_select_remove_asks_then_removes_and_reselects) {
    Fixture f;
    f.pm.add("A", 1);
    f.pm.add("B", 2);
    f.pm.add("C", 3);
    f.pm.select("A");
    f.flow.open_player_select();
    f.click("BUTTON_REMOVE");
    ASSERT_TRUE(f.flow.active() == DialogKind::Confirm);
    ASSERT_EQ(f.flow.dialog().label("HEADER")->text, std::string("Remove player"));
    ASSERT_EQ(f.flow.dialog().label("MESSAGE")->text,
              std::string("Are you sure you want to remove the selected player?"));
    // "No" keeps everything.
    f.click("BUTTON_NO");
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    ASSERT_EQ(f.pm.count(), 3);
    // "Yes": A removed; selection_after_remove(0, 2): old + 1 = 1 and
    // 2 <= 1 is false, so row 1 of [B, C] -> "C" (0x40aef0).
    f.click("BUTTON_REMOVE");
    f.flow.key_return();
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    ASSERT_EQ(f.pm.count(), 2);
    ASSERT_EQ(f.pm.players()[0].name, std::string("B"));
    ASSERT_EQ(f.flow.selected_row(), 1);
    ASSERT_EQ(f.pm.current()->name, std::string("C"));
    // Remove C (row 1): old + 1 = 2, 1 <= 2 -> 1 - 2 = -1 -> no selection.
    f.click("BUTTON_REMOVE");
    f.click("BUTTON_YES");
    ASSERT_EQ(f.pm.count(), 1);
    ASSERT_EQ(f.flow.selected_row(), -1);
    ASSERT_TRUE(f.pm.current() == nullptr);
    ASSERT_TRUE(!f.flow.dialog().button("BUTTON_REMOVE")->enabled);
    ASSERT_TRUE(!f.flow.dialog().button("NAME_2")->sprite.visible);
}

TEST(removing_last_player_reopens_name_input) {
    Fixture f;
    f.pm.add("Solo", 1);
    f.flow.open_player_select();
    f.click("BUTTON_REMOVE");
    f.click("BUTTON_YES");
    ASSERT_EQ(f.pm.count(), 0);
    ASSERT_TRUE(f.flow.active() == DialogKind::NewPlayer);
    ASSERT_TRUE(!f.flow.dialog().button("BUTTON_CANCEL")->enabled);
    ASSERT_TRUE(f.pm.current() == nullptr);
}

TEST(player_select_with_no_current_player_disables_remove) {
    Fixture f;
    f.pm.add("A", 1);
    f.pm.clear_current();
    f.flow.open_player_select();
    ASSERT_EQ(f.flow.selected_row(), -1);
    ASSERT_TRUE(!f.flow.dialog().button("BUTTON_REMOVE")->enabled);
    f.click("BUTTON_REMOVE");
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    f.flow.key_escape();   // OK: no selection, no change
    ASSERT_TRUE(!f.flow.open());
    ASSERT_EQ(f.changes, 0);
}

TEST(mouse_outside_buttons_and_disabled_buttons_do_nothing) {
    Fixture f;
    f.pm.add("A", 1);
    f.flow.open_player_select();
    f.flow.mouse_move(1, 1);
    f.flow.mouse_down(1, 1);
    f.flow.mouse_up(1, 1);
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    ASSERT_TRUE(f.sounds.empty());
    // Hover is silent (no `oversound` in the install XML, 0x48d390).
    auto [x, y] = f.centre("BUTTON_OK");
    f.flow.mouse_move(x, y);
    f.flow.mouse_move(x + 1, y);
    ASSERT_TRUE(f.sounds.empty());
    // Press, drag off, release: no click.
    f.flow.mouse_down(x, y);
    f.flow.mouse_move(1, 1);
    f.flow.mouse_up(1, 1);
    ASSERT_TRUE(f.flow.active() == DialogKind::PlayerSelect);
    ASSERT_TRUE(f.sounds.empty());
    // Events with no dialog open are ignored.
    f.flow.close();
    f.flow.mouse_down(x, y);
    f.flow.mouse_up(x, y);
    f.flow.key_return();
    f.flow.text_input("zzz");
    ASSERT_TRUE(!f.flow.open());
    ASSERT_EQ(f.flow.input_text(), std::string());
}

TEST(pointer_hot_tracks_clickable_hover) {
    Fixture f;
    f.pm.add("A", 1);
    f.pm.clear_current();          // no current player: Remove is disabled
    f.flow.open_player_select();
    ASSERT_TRUE(!f.flow.pointer_over_clickable());
    // Disabled (Remove): the pointer stays the arrow.
    auto [rx, ry] = f.centre("BUTTON_REMOVE");
    f.flow.mouse_move(rx, ry);
    ASSERT_TRUE(!f.flow.pointer_over_clickable());
    // Enabled (OK): the hand pointer.
    auto [x, y] = f.centre("BUTTON_OK");
    f.flow.mouse_move(x, y);
    ASSERT_TRUE(f.flow.pointer_over_clickable());
    f.flow.mouse_move(1, 1);
    ASSERT_TRUE(!f.flow.pointer_over_clickable());
    // A closed dialog has nothing to click.
    f.flow.mouse_move(x, y);
    f.flow.close();
    ASSERT_TRUE(!f.flow.pointer_over_clickable());
}
