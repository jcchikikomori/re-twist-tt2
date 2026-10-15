// In-game dialogs of the Classic scene, headless: the pause menu, the
// yes/no confirmations (exit, give up, continue a saved game), and the
// round-end feedback (next round, game over, new high score).
//
// Layout numbers are the dialogs.xml values quoted in
// docs/reverse-engineering/game-scene.md §6. Coordinates are local to
// the dialog's top-left corner; every dialog here is `autocenter`, so
// Dialog::left() / top() place it in the middle of the 800x600 screen.
// The renderer draws the DEFAULT_PAINTER frame, the backdrop and the
// show / hide fade (dialog_fade.hpp) exactly as for the profile dialogs.
#pragma once

#include <algorithm>
#include <functional>
#include <string>

#include "tt2ui/profile_dialogs.hpp"

namespace tt2ui {

// Strings from text.xml / commontext.xml.
namespace game_strings {
inline constexpr const char* PAUSE_MENU = "PAUSE MENU";                       // CTEXT_MAKE_CHOICE
inline constexpr const char* EXIT_HEADER = "Exit Game?";                      // CTEXT_MENU_STOP_HEADER
inline constexpr const char* EXIT_BODY =
    "Are you sure you want\nto leave this game?\nYour game will be\nsaved for next time.";   // CTEXT_MENU_STOP
inline constexpr const char* GIVEUP_HEADER = "Give Up?";                      // TEXT_DIALOG_GIVEUP_HEADER
inline constexpr const char* GIVEUP_BODY = "Are you sure you wish\nto end this game?";   // TEXT_DIALOG_GIVEUP
inline constexpr const char* PASS_HEADER = "PASS?";                           // TEXT_DIALOG_PASS_HEADER
inline constexpr const char* PASS_BODY =
    "Do you want to use a pass\nto move to the next word?";                   // TEXT_DIALOG_PASS
inline constexpr const char* SAVEGAME_HEADER = "Continue the game?";          // CTEXT_SAVEGAME_HEADER
inline constexpr const char* SAVEGAME_BODY =
    "Do you want to continue your last played game?\n(Select 'No' to start again.)";   // CTEXT_START_SAVEGAME
inline constexpr const char* NEXT_ROUND_BODY = "Click to continue\nto the next round!";   // TEXT_NEXT_ROUND_BODY
inline constexpr const char* GAME_OVER_HEADER = "GAME OVER";
inline constexpr const char* ENDGAME_BODY = "Sorry, you didn't qualify\nfor the next round.";   // TEXT_ENDGAME_BODY
inline constexpr const char* CONGRATS_BODY = "Congratulations!\nYou earned a\nnew high score!";  // TEXT_CONGRATS_BODY
// TEXT_TUTORIAL_TITLE_n / TEXT_TUTORIAL_BODY_n, n = the mode index
// (0x40e040 reads the global 0x5fdea8): Timed, Untimed, Lightning,
// Crossword, Word of the Day.
inline constexpr const char* TUTORIAL_TITLES[] = {"Timed Mode", "Untimed Mode", "Lightning Mode", "Crossword Mode",
                                                  "Word of the Day"};
inline constexpr const char* TUTORIAL_BODIES[] = {
    "Form words from the letters\nprovided.  Find the bingo word to\ngo to the next round before\ntime runs out!",
    "Form words from the letters\nprovided.  Find the bingo word to\ngo to the next round!",
    "Find all the bingo words before\nthe time runs out to progress to\nthe next round!  Solving words\ngives you "
    "more time.",
    "Fill every position in the\ncrossword with a word from\nthe same set of letters.\nHINT: fill the bingo word "
    "first.",
    "A quick game with a new word\neach day! Find as many words\nas you can before the time expires.",
};
inline constexpr const char* PASS_ROUND_TITLE = "Round Passed";              // TEXT_PASS_ROUND_TITLE
inline constexpr const char* PASS_ROUND_BODY =
    "One of your passes will\nbe used to progress to\nthe next round.";      // TEXT_PASS_ROUND_BODY
}  // namespace game_strings

// Kinds used by the game scene (the profile kinds stay in DialogKind).
enum class GameDialog {
    None, PauseMenu, ConfirmExit, ConfirmPass, ConfirmGiveUp, HowToPlay, Settings, NextRound, EndGame, Congrats,
    PassRound, Tutorial
};

inline int text_line_count(const std::string& s) {
    int n = 1;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

// DIALOG_MENU 322x390 (0x407f40 wires the four buttons).
inline Dialog build_pause_menu() {
    Dialog d;
    d.w = 322;
    d.h = 390;
    d.labels.push_back(TextLabel{"PAUSE_HEADER", "FONT_DIALOG_HEADERS2", game_strings::PAUSE_MENU, 163, 19,
                                 TextAlign::Center, Anchor::Top, 0});
    // 181x208 sheets, 4 rows of 52.
    d.buttons.push_back(make_dialog_button("BUTTON_HOWTOPLAY", "BUTTON_PAUSE_HELP", 162, 87, Anchor::Top, 181, 208));
    d.buttons.push_back(make_dialog_button("BUTTON_SETTINGS", "BUTTON_PAUSE_SETTINGS", 163, 155, Anchor::Top, 181, 208));
    d.buttons.push_back(make_dialog_button("BUTTON_STOP", "BUTTON_PAUSE_END", 163, 224, Anchor::Top, 181, 208));
    d.buttons.push_back(make_dialog_button("BUTTON_RESUME", "BUTTON_PAUSE_RESUME", 163, 290, Anchor::Top, 181, 208));
    return d;
}

// DIALOG_YESNO: 400x180 minimum. 0x40b470 moves both buttons down by
// the height of the MESSAGE label (lineheight 30 per line) and grows the
// dialog to fit. INFERRED: no extra padding, so a one-line message gives
// exactly the 180 px minimum (100 + 30 + a 50 px button row).
constexpr int YESNO_MIN_W = 400;
constexpr int YESNO_MIN_H = 180;
constexpr int YESNO_LINE_H = 30;
constexpr int YESNO_BUTTON_Y = 100;
constexpr int YESNO_BUTTON_H = 50;
constexpr int YESNO_BOTTOM_PAD = 0;
// MESSAGE is width="390" wrapstyle="wordwrap" (game-scene.md §6).
constexpr int YESNO_MESSAGE_W = 390;

// Breaks `text` into "\n"-separated lines no wider than `max_width`;
// main.cpp backs it with wrap_text() and the loaded FONT_SETTINGSTEXT.
using LineBreaker = std::function<std::string(const std::string& text, int max_width)>;

// Without a breaker only the message's own "\n" break lines. `width`
// widens the dialog for port-only dialogs with longer text (the original
// ones all use 400); everything stays centred.
inline Dialog build_yes_no(const char* header, const std::string& text, bool single_ok = false,
                           const LineBreaker& wrap = {}, int width = YESNO_MIN_W) {
    Dialog d;
    d.w = std::max(YESNO_MIN_W, width);
    const float cx = static_cast<float>(d.w) / 2.0f;
    const int msg_w = YESNO_MESSAGE_W + (d.w - YESNO_MIN_W);
    const std::string message = wrap ? wrap(text, msg_w) : text;
    const int msg_h = text_line_count(message) * YESNO_LINE_H;
    const int button_y = YESNO_BUTTON_Y + msg_h;
    d.h = std::max(YESNO_MIN_H, button_y + YESNO_BUTTON_H + YESNO_BOTTOM_PAD);
    d.labels.push_back(TextLabel{"HEADER", "FONT_DIALOG_HEADERS2", header, cx, 13, TextAlign::Center, Anchor::Top, -1});
    TextLabel msg{"MESSAGE", "FONT_SETTINGSTEXT", message, cx, 87, TextAlign::Center, Anchor::Top, -1, msg_w};
    msg.line_height = YESNO_LINE_H;
    d.labels.push_back(msg);
    if (single_ok) {
        d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", cx, static_cast<float>(button_y), Anchor::Top));
    } else {
        d.buttons.push_back(make_dialog_button("BUTTON_YES", "BUTTON_YES", cx - 10, static_cast<float>(button_y), Anchor::TopRight));
        d.buttons.push_back(make_dialog_button("BUTTON_NO", "BUTTON_NO", cx + 10, static_cast<float>(button_y), Anchor::TopLeft));
    }
    return d;
}

// The orange IMAGE_BURST sparkle behind the round-end text
// (IMAGE_BURST 2000x400, 10x2 frames of 200x200, frametime 40,
// colour #FFA800, additive).
inline SpriteState sparkle(float x, float y, int first_frame) {
    SpriteState s = sprite("SPARKLE", sheet("IMAGE_BURST", 2000, 400, 10, 2, 20), x, y);
    s.anchor = Anchor::Center;
    s.frame = first_frame;
    s.frame_time_ms = 40;
    s.additive = true;
    s.tint = 0xFFA800;
    return s;
}

// DIALOG_NEXTROUND 380x320. Its eight letterballs ("Great Job") are
// LetterBalls owned by the scene (next_round_balls() in game_scene.hpp).
inline Dialog build_next_round() {
    Dialog d;
    d.w = 380;
    d.h = 320;
    d.sprites.push_back(sparkle(191, 154, 8));
    TextLabel body{"BODY_TEXT", "FONT_NEXTROUND02", game_strings::NEXT_ROUND_BODY, 192, 133, TextAlign::Center,
                   Anchor::Top, 2};
    body.line_height = 36;
    d.labels.push_back(body);
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 190, 239, Anchor::Top));
    return d;
}

// DIALOG_ENDGAME 380x260: MENU (BUTTON_OK) and PLAY AGAIN (BUTTON_REPLAY),
// 157x208 sheets.
inline Dialog build_end_game() {
    Dialog d;
    d.w = 380;
    d.h = 260;
    d.labels.push_back(TextLabel{"HEADER", "FONT_DIALOG_HEADERS2", game_strings::GAME_OVER_HEADER, 190, 14,
                                 TextAlign::Center, Anchor::Top, 0});
    TextLabel body{"BODY_TEXT", "FONT_NEXTROUND02", game_strings::ENDGAME_BODY, 192, 88, TextAlign::Center,
                   Anchor::Top, 1};
    body.line_height = 38;
    d.labels.push_back(body);
    d.buttons.push_back(make_dialog_button("BUTTON_REPLAY", "BUTTON_END_PLAYAGAIN", 271, 173, Anchor::Top, 157, 208));
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_END_MENU", 109, 173, Anchor::Top, 157, 208));
    return d;
}

// DIALOG_PASSROUND 380x280: a Timed round ran out unqualified with passes
// left (0x425420(1) -> 0x428a80 -> state 6).
inline Dialog build_pass_round() {
    Dialog d;
    d.w = 380;
    d.h = 280;
    TextLabel header{"HEADER", "FONT_DIALOG_HEADERS2", game_strings::PASS_ROUND_TITLE, 191, 16, TextAlign::Center,
                     Anchor::Top, 0};
    header.line_height = 36;
    d.labels.push_back(header);
    TextLabel body{"BODY_TEXT", "FONT_NEXTROUND02", game_strings::PASS_ROUND_BODY, 192, 85, TextAlign::Center,
                   Anchor::Top, 2};
    body.line_height = 36;
    d.labels.push_back(body);
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 189, 199, Anchor::Top));
    return d;
}

// DIALOG_TUTORIAL 444x320: a player's first game in a mode (0x425c50).
// READ MORE (BUTTON_HELP, 0x40e1f0) opens DIALOG_HELP over it; PLAY
// (BUTTON_OK, 0x40e1c0) closes it. `mode` is the GameMode index.
inline Dialog build_tutorial(int mode) {
    const int n = std::clamp(mode, 0, 4);
    Dialog d;
    d.w = 444;
    d.h = 320;
    TextLabel header{"HEADER", "FONT_DIALOG_HEADERS2", game_strings::TUTORIAL_TITLES[n], 221, 16, TextAlign::Center,
                     Anchor::Top, 0};
    header.line_height = 36;
    d.labels.push_back(header);
    TextLabel body{"BODY_TEXT", "FONT_TUTORIAL", game_strings::TUTORIAL_BODIES[n], 224, 155, TextAlign::Center,
                   Anchor::Center, 0};
    body.line_height = 32;
    d.labels.push_back(body);
    d.buttons.push_back(make_dialog_button("BUTTON_HELP", "IMAGE_BUTTON_READMORE", 135, 235, Anchor::Top, 157, 208));
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "IMAGE_BUTTON_PLAY", 317, 235, Anchor::Top, 157, 208));
    return d;
}

// DIALOG_CONGRATS 380x320 (new high score).
inline Dialog build_congrats() {
    Dialog d;
    d.w = 380;
    d.h = 320;
    d.sprites.push_back(sparkle(191, 138, 4));
    TextLabel body{"BODY_TEXT", "FONT_NEXTROUND02", game_strings::CONGRATS_BODY, 193, 107, TextAlign::Center,
                   Anchor::Top, 2};
    body.line_height = 38;
    d.labels.push_back(body);
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 190, 239, Anchor::Top));
    return d;
}

// ---- input helpers shared by every dialog ----------------------------

inline bool dialog_hit(const Dialog& d, const MenuButton& b, float x, float y) {
    return b.enabled && b.sprite.visible && b.contains(x - d.left(), y - d.top());
}

// Hover state only; buttons play no hover sound (welcome-flow.md §3).
inline void dialog_mouse_move(Dialog& d, float x, float y) {
    for (auto& b : d.buttons) {
        const bool over = dialog_hit(d, b, x, y);
        b.hover = over;
        if (!over) b.pressed = false;
    }
}
inline void dialog_mouse_down(Dialog& d, float x, float y) {
    for (auto& b : d.buttons) b.pressed = dialog_hit(d, b, x, y);
}
// The name of the button released over, or "".
inline std::string dialog_mouse_up(Dialog& d, float x, float y) {
    std::string clicked;
    for (auto& b : d.buttons) {
        if (b.pressed && dialog_hit(d, b, x, y)) clicked = b.sprite.name;
        b.pressed = false;
    }
    return clicked;
}
inline bool dialog_pointer_hot(const Dialog& d) {
    for (const auto& b : d.buttons) if (b.hover && b.enabled) return true;
    return false;
}
inline void dialog_tick(Dialog& d, int dt_ms) {
    for (auto& s : d.sprites) s.tick_auto_frames(dt_ms);
    for (auto& b : d.buttons) b.sprite.frame = b.frame_for_state();
}

}  // namespace tt2ui
