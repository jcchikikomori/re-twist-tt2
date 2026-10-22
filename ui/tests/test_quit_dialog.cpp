// The main menu's QUIT confirm (quit_dialog.hpp): BUTTON_QUIT opens
// DIALOG_YESNO "Quit" (0x40ec20), YES quits (0x40eb90), NO closes it
// (0x43fcd0), and a second QUIT never stacks a second confirm
// (docs/reverse-engineering/welcome-flow.md §3).
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/quit_dialog.hpp"

using namespace tt2ui;

namespace {
struct Host {
    std::vector<SoundId> sounds;
    QuitConfirm quit{[this](SoundId id) { sounds.push_back(id); }};
};

// The middle of a dialog button, in screen coordinates.
void click(QuitConfirm& q, const std::string& name, float& x, float& y) {
    const Dialog& d = q.dialog();
    const MenuButton* b = d.button(name);
    ASSERT_TRUE(b != nullptr);
    x = d.left() + b->sprite.left() + static_cast<float>(b->sprite.sheet.frame_w) / 2.0f;
    y = d.top() + b->sprite.top() + static_cast<float>(b->sprite.sheet.frame_h) / 2.0f;
}
}  // namespace

TEST(quit_confirm_is_closed_until_quit_is_clicked) {
    Host h;
    ASSERT_TRUE(!h.quit.open());
    // No YES to press: nothing quits without the confirm.
    ASSERT_TRUE(h.quit.press("BUTTON_YES") == QuitConfirm::Result::None);
    ASSERT_TRUE(h.quit.key_return() == QuitConfirm::Result::None);
    ASSERT_TRUE(h.quit.key_escape() == QuitConfirm::Result::None);
    ASSERT_TRUE(h.quit.mouse_up(400, 300) == QuitConfirm::Result::None);
    ASSERT_TRUE(!h.quit.pointer_hot());
    ASSERT_TRUE(!h.quit.open());
    ASSERT_TRUE(h.sounds.empty());
}

TEST(quit_opens_the_yes_no_confirm_with_the_original_text) {
    Host h;
    ASSERT_TRUE(h.quit.request());
    ASSERT_TRUE(h.quit.open());
    const Dialog& d = h.quit.dialog();
    ASSERT_TRUE(d.kind == DialogKind::Confirm);
    ASSERT_EQ(d.label("HEADER")->text, std::string("Quit"));                              // commontext.xml:18
    ASSERT_EQ(d.label("MESSAGE")->text, std::string("Are you sure you want to quit?"));   // commontext.xml:19
    // DIALOG_YESNO through 0x40b310, as "Remove player" and "Exit Game?".
    const Dialog yes_no = build_yes_no("Quit", "Are you sure you want to quit?");
    ASSERT_EQ(d.w, yes_no.w);
    ASSERT_EQ(d.h, yes_no.h);
    ASSERT_EQ(d.buttons.size(), std::size_t{2});
    ASSERT_TRUE(d.button("BUTTON_YES") != nullptr);
    ASSERT_TRUE(d.button("BUTTON_NO") != nullptr);
    // The menu button clicks SOUND_BUTTON itself (SelectionScene::mouse_up).
    ASSERT_TRUE(h.sounds.empty());
}

TEST(quit_yes_requests_quit_with_one_click_sound) {
    Host h;
    h.quit.request();
    ASSERT_TRUE(h.quit.press("BUTTON_YES") == QuitConfirm::Result::Quit);
    ASSERT_TRUE(!h.quit.open());
    ASSERT_EQ(h.sounds.size(), std::size_t{1});
    ASSERT_TRUE(h.sounds[0] == SoundId::Button);
}

TEST(quit_no_closes_the_confirm_and_stays) {
    Host h;
    h.quit.request();
    ASSERT_TRUE(h.quit.press("BUTTON_NO") == QuitConfirm::Result::Stay);
    ASSERT_TRUE(!h.quit.open());
    ASSERT_EQ(h.sounds.size(), std::size_t{1});
    ASSERT_TRUE(h.sounds[0] == SoundId::Button);
    // The dialog is gone: its YES no longer quits.
    ASSERT_TRUE(h.quit.press("BUTTON_YES") == QuitConfirm::Result::None);
    ASSERT_TRUE(h.quit.key_return() == QuitConfirm::Result::None);
    ASSERT_EQ(h.sounds.size(), std::size_t{1});
}

TEST(quit_second_click_neither_stacks_nor_quits) {
    Host h;
    ASSERT_TRUE(h.quit.request());
    float x = 0, y = 0;
    click(h.quit, "BUTTON_YES", x, y);
    h.quit.mouse_move(x, y);
    ASSERT_TRUE(h.quit.dialog().button("BUTTON_YES")->hover);
    // 0x40ec20 @0x40eca5-0x40ece1: the confirm is already up, so nothing happens.
    ASSERT_TRUE(!h.quit.request());
    ASSERT_TRUE(h.quit.open());
    ASSERT_TRUE(h.quit.dialog().button("BUTTON_YES")->hover);   // the same dialog, not a new one
    ASSERT_TRUE(h.sounds.empty());
    // One NO closes it: there was never a second one under it.
    ASSERT_TRUE(h.quit.press("BUTTON_NO") == QuitConfirm::Result::Stay);
    ASSERT_TRUE(!h.quit.open());
}

TEST(quit_mouse_click_on_yes_quits_and_on_no_stays) {
    Host h;
    h.quit.request();
    float x = 0, y = 0;
    click(h.quit, "BUTTON_NO", x, y);
    h.quit.mouse_move(x, y);
    ASSERT_TRUE(h.quit.pointer_hot());
    h.quit.mouse_down(x, y);
    h.quit.tick(16);
    ASSERT_EQ(h.quit.dialog().button("BUTTON_NO")->sprite.frame, 2);   // pressed
    ASSERT_TRUE(h.quit.mouse_up(x, y) == QuitConfirm::Result::Stay);
    ASSERT_TRUE(!h.quit.open());

    h.quit.request();
    click(h.quit, "BUTTON_YES", x, y);
    h.quit.mouse_down(x, y);
    ASSERT_TRUE(h.quit.mouse_up(x, y) == QuitConfirm::Result::Quit);
    ASSERT_EQ(h.sounds.size(), std::size_t{2});
}

TEST(quit_release_off_the_buttons_does_nothing) {
    Host h;
    h.quit.request();
    float x = 0, y = 0;
    click(h.quit, "BUTTON_YES", x, y);
    h.quit.mouse_down(x, y);
    ASSERT_TRUE(h.quit.mouse_up(10, 10) == QuitConfirm::Result::None);   // released outside
    ASSERT_TRUE(h.quit.mouse_up(x, y) == QuitConfirm::Result::None);     // no press left
    ASSERT_TRUE(h.quit.press("BUTTON_OK") == QuitConfirm::Result::None); // not a button of this dialog
    ASSERT_TRUE(h.quit.open());
    ASSERT_TRUE(h.sounds.empty());
}

// Port deviation: the original binds no keys; Return / Esc answer YES / NO
// as in every other yes/no dialog of the port.
TEST(quit_return_is_yes_and_escape_is_no) {
    Host h;
    h.quit.request();
    ASSERT_TRUE(h.quit.key_escape() == QuitConfirm::Result::Stay);
    ASSERT_TRUE(!h.quit.open());
    h.quit.request();
    ASSERT_TRUE(h.quit.key_return() == QuitConfirm::Result::Quit);
    ASSERT_TRUE(!h.quit.open());
    ASSERT_EQ(h.sounds.size(), std::size_t{2});
}

TEST(quit_message_goes_through_the_word_wrap) {
    std::vector<int> widths;
    QuitConfirm q({}, [&](const std::string& text, int width) {
        widths.push_back(width);
        return text.substr(0, 12) + "\n" + text.substr(12);
    });
    q.request();
    ASSERT_EQ(widths.size(), std::size_t{1});
    ASSERT_EQ(widths[0], YESNO_MESSAGE_W);
    ASSERT_EQ(q.dialog().label("MESSAGE")->text, std::string("Are you sure\n you want to quit?"));
    ASSERT_EQ(q.dialog().h, build_yes_no("Quit", "a\nb").h);   // two lines: the dialog grows (0x40b470)
}
