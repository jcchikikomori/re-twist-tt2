// The main menu's QUIT confirm (DIALOG_YESNO "Quit"), headless.
//
// CONFIRMED (REA, docs/reverse-engineering/welcome-flow.md §3, "QUIT
// confirm"):
//   - Init 0x42d220 binds BUTTON_QUIT to 0x40ec20 (@0x42d773-0x42d78f),
//     its only reference: no key opens this confirm.
//   - 0x40ec20 does nothing while the scene already holds a child named
//     CTEXT_QUESTION_QUIT (@0x40eca5-0x40ece1), so clicks never stack
//     confirms. Otherwise 0x40b310 builds DIALOG_YESNO with the header
//     CTEXT_QUIT_HEADER and the message CTEXT_QUESTION_QUIT
//     (@0x40edea-0x40ee02): the factory of "Remove player" and "Exit
//     Game?", so build_yes_no() lays it out.
//   - YES (0x40eb90) sends message 0x402, which ends the main loop
//     0x46de50 at once: no fade, no music stop, no sound of its own.
//   - NO (0x43fcd0 -> 0x45c470) closes the dialog; the menu is untouched.
//   - BUTTON_QUIT, YES and NO each click SOUND_BUTTON (dialogs.xml:54,
//     :207-208). The menu button plays its own (SelectionScene::mouse_up);
//     this model plays YES's and NO's.
//   - The window's close button and Alt+F4 quit without asking: the
//     WndProc 0x483ef0 sends 0x402 itself on WM_CLOSE (@0x483f13-0x483f68).
//     In the port that is the front end's window-close event, which never
//     comes here.
//
// Port deviation: the original's dialogs bind no keys (help-and-settings.md
// §4; INFERRED for DialogYesNo). Return answers YES and Esc NO, as in the
// port's other yes/no dialogs (ProfileFlow, GameScene, ImportPromptFlow).
#pragma once

#include <functional>
#include <string>
#include <utility>

#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/profile_dialogs.hpp"

namespace tt2ui {

namespace quit_strings {
inline constexpr const char* HEADER = "Quit";                           // CTEXT_QUIT_HEADER (commontext.xml:18)
inline constexpr const char* BODY = "Are you sure you want to quit?";   // CTEXT_QUESTION_QUIT (commontext.xml:19)
}  // namespace quit_strings

class QuitConfirm {
public:
    // Quit: YES, end the program now. Stay: NO, back to the menu.
    enum class Result { None, Quit, Stay };

    // `wrap` word-wraps MESSAGE (wrapstyle="wordwrap"); empty keeps the
    // text on one line.
    explicit QuitConfirm(std::function<void(SoundId)> play_sound = {}, LineBreaker wrap = {})
        : play_sound_(std::move(play_sound)), wrap_(std::move(wrap)) {}

    bool open() const { return open_; }
    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }

    // BUTTON_QUIT (0x40ec20). False, with nothing changed, while the
    // confirm is already up.
    bool request() {
        if (open_) return false;
        dialog_ = build_yes_no(quit_strings::HEADER, quit_strings::BODY, false, wrap_);
        dialog_.kind = DialogKind::Confirm;
        open_ = true;
        return true;
    }

    // A dialog button by name (mouse, keys, --type). YES and NO click
    // SOUND_BUTTON and close the dialog; anything else does nothing.
    Result press(const std::string& name) {
        if (!open_) return Result::None;
        Result r = Result::None;
        if (name == "BUTTON_YES") r = Result::Quit;
        else if (name == "BUTTON_NO") r = Result::Stay;
        else return Result::None;
        if (play_sound_) play_sound_(SoundId::Button);
        open_ = false;
        return r;
    }
    Result key_return() { return press("BUTTON_YES"); }
    Result key_escape() { return press("BUTTON_NO"); }

    // Mouse in screen coordinates (the dialog offset is applied here).
    void mouse_move(float x, float y) {
        if (open_) dialog_mouse_move(dialog_, x, y);
    }
    void mouse_down(float x, float y) {
        if (open_) dialog_mouse_down(dialog_, x, y);
    }
    Result mouse_up(float x, float y) {
        if (!open_) return Result::None;
        return press(dialog_mouse_up(dialog_, x, y));
    }
    // True while the pointer sits on YES or NO (the hand cursor).
    bool pointer_hot() const { return open_ && dialog_pointer_hot(dialog_); }
    void tick(int dt_ms) {
        if (open_) dialog_tick(dialog_, dt_ms);
    }

private:
    std::function<void(SoundId)> play_sound_;
    LineBreaker wrap_;
    Dialog dialog_;
    bool open_ = false;
};

}  // namespace tt2ui
