// Tooltips (GF2::Tooltip / GF2::ToolTipLabel), headless.
//
// CONFIRMED (REA, docs/reverse-engineering/tooltips.md):
//   - The Tooltip ctor 0x474390 sets the show delay (+0x38) to 1000 ms and
//     the time on screen (+0x40) to 10000 ms.
//   - Each frame 0x4748d0 asks 0x474ae0 for the deepest widget under the
//     cursor that has a tooltip (+0x150). A new widget restarts both
//     timers; on the same widget the delay counts down by the frame time
//     and, when it runs out, the box is placed once (tooltip_place) at the
//     cursor of that moment. It shows while the delay is spent and the time
//     on screen is not, which counts down only while it shows. After the 10
//     s it stays hidden until the cursor reaches another widget.
//   - ToolTipLabel (ctor 0x473ac0, loader 0x473be0): background
//     Color(1, 1, 1, 0.882) in GF2's A, R, G, B order = #FFFFE1, border
//     Color(1, 0, 0, 0) = black, padding 3 (dialogs.xml repeats it), and
//     DEFAULT_TOOLTIP adds font FONT_TOOLTIP (arial12), text #000000,
//     alpha 220 and maxwidth 200.
// The text comes from text.xml / commontext.xml through each widget's
// tooltip="CTOOLTIP_..." in dialogs.xml; tooltip_text() is that table.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "tt2ui/platform.hpp"

namespace tt2ui {

inline constexpr int TOOLTIP_DELAY_MS = 1000;    // +0x38
inline constexpr int TOOLTIP_SHOW_MS = 10000;    // +0x40
inline constexpr int TOOLTIP_PADDING = 3;        // padding="3"
inline constexpr int TOOLTIP_MAX_W = 200;        // maxwidth="200"
inline constexpr int TOOLTIP_ALPHA = 220;        // alpha="220" (of 255)
inline constexpr std::uint32_t TOOLTIP_BACKGROUND = 0xFFFFE1;   // Color(1, 1, 1, 0.882)
inline constexpr std::uint32_t TOOLTIP_BORDER = 0x000000;       // Color(1, 0, 0, 0)
inline constexpr std::uint32_t TOOLTIP_TEXT = 0x000000;         // color="#000000"
// 0x4748d0's placement constants.
inline constexpr float TOOLTIP_BELOW = 24.0f;    // under the cursor
inline constexpr float TOOLTIP_ASIDE = 16.0f;    // beside it, when there is no room below
inline constexpr float TOOLTIP_MARGIN = 3.0f;    // added to the box size in the edge tests
inline constexpr float TOOLTIP_GAP = 2.0f;       // left of the cursor, when flipped

namespace tooltip_strings {
// text.xml
inline constexpr const char* TWIST = "Scramble the letters";
inline constexpr const char* ENTER = "Submit the current word";
inline constexpr const char* LAST_WORD = "Play the last letters again";
inline constexpr const char* CLEAR = "Remove all letters";
inline constexpr const char* NEXT_ROUND = "Advance to the next round";
inline constexpr const char* GIVE_UP = "End this game";
// commontext.xml
inline constexpr const char* NAMESELECT_ADD = "Add a new name";
inline constexpr const char* NAMESELECT_DELETE = "Delete the selected name";
inline constexpr const char* NAMESELECT_OK = "Play as the selected player";
inline constexpr const char* NAMESELECT_PLAYER = "Click on a name to\nselect a player";
inline constexpr const char* SETTINGS_FX = "Drag to adjust the volume\nof the sound effects.";
inline constexpr const char* SETTINGS_MUSIC = "Drag to adjust the volume\nof the music.";
// Sic: "to to", and the original also puts it on the max-length switches.
inline constexpr const char* SETTINGS_FULLSCREEN =
    "Play the game fullscreen or in a window.\nYou can use the ESCAPE key\nto to play in a window.";
inline constexpr const char* SETTINGS_INFO = "Show information about the game";
inline constexpr const char* SELECTION_CHANGE_NAME = "Click here to select another name";
inline constexpr const char* SELECTION_PLAY = "Go to the game selection screen";
inline constexpr const char* SELECTION_HELP = "View the game instructions";
inline constexpr const char* SELECTION_SETTINGS = "Adjust the volume and the screen settings";
inline constexpr const char* SELECTION_HIGHSCORES = "View your trophies";
inline constexpr const char* SELECTION_QUIT = "Quit the game and return to Windows";   // CTOOLTIP_SELECTION_QUIT_WIN
inline constexpr const char* MENU_HELP = "View the game instructions";
inline constexpr const char* MENU_SETTINGS = "Adjust the volume and the screen settings";
inline constexpr const char* MENU_STOP = "Exit this level";
inline constexpr const char* MENU_CONTINUE = "Continue with the game";
inline constexpr const char* INGAME_MENU = "Show the game menu";
}  // namespace tooltip_strings

// Where a widget lives: the same name means different things in different
// dialogs (BUTTON_SETTINGS on the menu and in the pause menu).
enum class TooltipSurface {
    Selection,      // the main menu scene
    Game,           // the Classic and Lightning scenes' own buttons
    Crossword,      // SCENE_CRISSCROSS's own buttons (dialogs.xml lines 147-165)
    PauseMenu,      // DIALOG_MENU
    RoundDialog,    // DIALOG_TUTORIAL / NEXTROUND / PASSROUND / ENDGAME / CONGRATS
    PlayerSelect,   // DIALOG_PLAYERSELECT
    Settings,       // DIALOG_SETTINGS
};

// dialogs.xml: the tooltip of widget `name` on `surface`, or "" for none.
inline const char* tooltip_text(TooltipSurface surface, std::string_view name) {
    namespace s = tooltip_strings;
    switch (surface) {
        case TooltipSurface::Selection:
            if (name == "BUTTON_CHANGENAME") return s::SELECTION_CHANGE_NAME;
            if (name == "BUTTON_PLAY" || name == "BUTTON_PLAY_WOD") return s::SELECTION_PLAY;
            if (name == "BUTTON_HOWTOPLAY") return s::SELECTION_HELP;
            if (name == "BUTTON_SETTINGS") return s::SELECTION_SETTINGS;
            if (name == "BUTTON_HIGHSCORES") return s::SELECTION_HIGHSCORES;
            if (name == "BUTTON_QUIT") return s::SELECTION_QUIT;
            break;
        case TooltipSurface::Game:
            if (name == "BUTTON_Twist") return s::TWIST;
            if (name == "BUTTON_Enter") return s::ENTER;
            if (name == "BUTTON_LastWord") return s::LAST_WORD;
            // Lightning's word arrows reuse CTOOLTIP_Clear (sic, dialogs.xml SCENE_BLITZ).
            if (name == "BUTTON_Clear" || name == "BUTTON_PREVWORD" || name == "BUTTON_NEXTWORD") return s::CLEAR;
            // BUTTON_WOTD_DONE keeps NEXT ROUND's CTOOLTIP_NextRound, a leftover
            // the original never changed (game-scene.md §8.4).
            if (name == "BUTTON_NextRound" || name == "BUTTON_WOTD_DONE") return s::NEXT_ROUND;
            if (name == "BUTTON_Pass" || name == "BUTTON_Pass_2" || name == "BUTTON_GiveUp") return s::GIVE_UP;
            if (name == "BUTTON_Menu") return s::INGAME_MENU;
            break;
        case TooltipSurface::Crossword:
            // Sic: ZOOM carries CTOOLTIP_GiveUp and the NEXT arrow
            // CTOOLTIP_Clear; the PREV arrow has none, and no code sets
            // another (no CTOOLTIP string in the exe, crossword.md §1.1).
            if (name == "BUTTON_Zoom") return s::GIVE_UP;
            if (name == "BUTTON_PREVWORD") return "";
            return tooltip_text(TooltipSurface::Game, name);
        case TooltipSurface::PauseMenu:
            if (name == "BUTTON_HOWTOPLAY") return s::MENU_HELP;
            if (name == "BUTTON_SETTINGS") return s::MENU_SETTINGS;
            if (name == "BUTTON_STOP") return s::MENU_STOP;
            if (name == "BUTTON_RESUME") return s::MENU_CONTINUE;
            break;
        case TooltipSurface::RoundDialog:
            // Sic: every round dialog button reuses the player-select OK text.
            if (name == "BUTTON_OK" || name == "BUTTON_HELP" || name == "BUTTON_REPLAY") return s::NAMESELECT_OK;
            break;
        case TooltipSurface::PlayerSelect:
            if (name == "BUTTON_ADD") return s::NAMESELECT_ADD;
            if (name == "BUTTON_REMOVE") return s::NAMESELECT_DELETE;
            if (name == "BUTTON_OK") return s::NAMESELECT_OK;
            if (name.substr(0, 5) == "NAME_") return s::NAMESELECT_PLAYER;
            break;
        case TooltipSurface::Settings:
            if (name == "BUTTON_INFO") return s::SETTINGS_INFO;
            if (name == "SLIDER_FXVOL") return s::SETTINGS_FX;
            if (name == "SLIDER_MUSICVOL") return s::SETTINGS_MUSIC;
            if (name == "SWITCH_FULLSCREEN" || name.substr(0, 14) == "SWITCH_LENGTH_") return s::SETTINGS_FULLSCREEN;
            break;
    }
    return "";
}

// 0x4748d0: the box's top-left for a w x h box when the cursor is at
// (mx, my) on a screen_w x screen_h screen. Below the cursor by 24; with
// no room below it moves 16 right and up to the cursor (or as low as
// fits); with no room on the right it goes to the cursor's left, 2 px
// clear, when it was moved, or against the right edge when it was not.
struct TooltipPlace {
    float x = 0, y = 0;
};
inline TooltipPlace tooltip_place(float mx, float my, float w, float h, float screen_w, float screen_h) {
    const float bw = w + TOOLTIP_MARGIN;
    const float bh = h + TOOLTIP_MARGIN;
    TooltipPlace p{mx, my + TOOLTIP_BELOW};
    bool moved = false;
    if (screen_h < p.y + bh) {
        p.x += TOOLTIP_ASIDE;
        p.y -= TOOLTIP_BELOW;
        moved = true;
        if (p.y + bh > screen_h) p.y = screen_h - bh;
    }
    if (screen_w < bw + p.x) p.x = moved ? mx - bw - TOOLTIP_GAP : screen_w - bw;
    return p;
}

// One tooltip for the whole screen. Call update() every frame with the
// widget under the cursor that has a tooltip (`key` names it, "" = none)
// and its text.
class TooltipTracker {
public:
    // Android (T2.1, user decision D7): no tooltips. A finger never rests on
    // a button, and SDL leaves the mouse where the last tap was, so the
    // tooltip of whatever was just tapped would pop up a second later.
    explicit TooltipTracker(Platform platform = THIS_PLATFORM) : enabled_(platform == Platform::Desktop) {}

    void update(const std::string& key, const std::string& text, float mx, float my, int dt_ms) {
        if (!enabled_ || key.empty() || text.empty()) {
            key_.clear();
            visible_ = false;
            return;
        }
        if (key != key_) {   // a new widget: both timers start again
            key_ = key;
            text_ = text;
            delay_ms_ = TOOLTIP_DELAY_MS;
            show_ms_ = TOOLTIP_SHOW_MS;
        } else if (delay_ms_ > 0) {
            delay_ms_ -= dt_ms;
            if (delay_ms_ < 1) {   // placed once, where the cursor is now
                anchor_x_ = mx;
                anchor_y_ = my;
            }
        }
        visible_ = delay_ms_ < 1 && show_ms_ > 0;
        if (visible_) show_ms_ -= dt_ms;
    }

    bool visible() const { return visible_; }
    const std::string& text() const { return text_; }
    // The cursor when the delay ran out; tooltip_place() takes it from here.
    float anchor_x() const { return anchor_x_; }
    float anchor_y() const { return anchor_y_; }

private:
    std::string key_;
    std::string text_;
    int delay_ms_ = 0;
    int show_ms_ = 0;
    float anchor_x_ = 0, anchor_y_ = 0;
    bool visible_ = false;
    bool enabled_ = true;
};

}  // namespace tt2ui
