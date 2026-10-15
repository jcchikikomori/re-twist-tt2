// Player-profile dialogs of the main menu, headless.
//
//   DIALOG_NEWPLAYER     "What is your name?"  (0x40aef0 / 0x45fa60)
//   DIALOG_PLAYERSELECT  "Select your name"    (0x40ad00 / 0x460c30)
//   DIALOG_YESNO         "Remove player"       (CTEXT_REMOVE_PLAYER)
//   message dialog       "Player exists."      (CTEXT_PLAYER_EXISTS)
//
// Layout numbers are the dialogs.xml values quoted in
// docs/reverse-engineering/profiles.md §3. Dialog-local coordinates are
// relative to the dialog's top-left corner; `autocenter` puts that at
// ((800 - w) / 2, (600 - h) / 2). The renderer draws the DEFAULT_PAINTER
// 9-slice frame, the backdrop and fade (dialog_fade.hpp) and the labels;
// this file owns the state and the rules.
#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

// A label the renderer draws with a bitmap font.
struct TextLabel {
    std::string name;
    std::string font_id;      // resources.xml font id
    std::string text;
    float x = 0, y = 0;       // dialog-local
    TextAlign align = TextAlign::Center;
    Anchor anchor = Anchor::Top;   // Top: (x, y) is the top-centre; Center: the middle
    int char_spacing = -1;    // -1: use the font's
    int max_width = 0;        // > 0: the renderer wraps / clips (unused by the model)
    bool visible = true;
    int line_height = 0;      // dialogs.xml `lineheight` for multi-line text ("\n"); 0 = glyph height
    float alpha = 1.0f;       // page fades (HelpPager)
    // When set, the label is a stand-in for the dialog sprite of that name:
    // the renderer draws it only while that sprite's image is a placeholder.
    std::string fallback_for{};
};

enum class DialogKind { None, NewPlayer, PlayerSelect, Confirm, Message };

// One dialog: a frame plus buttons, sprites and labels. Buttons reuse
// MenuButton (4-row sheets normal / hover / pressed / disabled).
struct Dialog {
    DialogKind kind = DialogKind::None;
    int w = 0, h = 0;
    std::vector<MenuButton> buttons;
    std::vector<SpriteState> sprites;
    std::vector<TextLabel> labels;

    float left() const { return static_cast<float>((SCREEN_W - w) / 2); }
    float top() const { return static_cast<float>((SCREEN_H - h) / 2); }

    MenuButton* button(const std::string& name) {
        for (auto& b : buttons) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    const MenuButton* button(const std::string& name) const {
        for (const auto& b : buttons) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    TextLabel* label(const std::string& name) {
        for (auto& l : labels) if (l.name == name) return &l;
        return nullptr;
    }
    const TextLabel* label(const std::string& name) const {
        for (const auto& l : labels) if (l.name == name) return &l;
        return nullptr;
    }
    SpriteState* sprite_named(const std::string& name) {
        for (auto& s : sprites) if (s.name == name) return &s;
        return nullptr;
    }
    const SpriteState* sprite_named(const std::string& name) const {
        for (const auto& s : sprites) if (s.name == name) return &s;
        return nullptr;
    }
};

// A dialog button: `image` is a 4-row sheet (normal / hover / pressed /
// disabled) of `w` x `h` pixels; (x, y) is dialog-local with anchor `a`.
inline MenuButton make_dialog_button(const char* name, const char* image, float x, float y, Anchor a, int w = 119,
                                     int h = 200) {
    MenuButton b;
    b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
    b.sprite.anchor = a;
    return b;
}

inline int text_line_count(const std::string& s) {
    int n = 1;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

// Where a label's first line goes. `anchorpoint="center"` centres the
// whole block of lines on (x, y), so the first line moves up by half the
// extra lines; every other anchor starts the block at y. INFERRED from
// DIALOG_TUTORIAL: its 4-line BODY_TEXT is centred at y 155 with line
// height 32, and a block starting there would put line 4 at 251, below
// the top of its buttons (235). Centred, the lines sit at 107..203.
inline float first_line_y(const TextLabel& l, int lines, int line_height) {
    if (l.anchor != Anchor::Center || lines <= 1) return l.y;
    return l.y - static_cast<float>((lines - 1) * line_height) / 2.0f;
}

// DIALOG_YESNO: 400x180 minimum. 0x40b470 moves both buttons down by
// the height of the MESSAGE label (lineheight 30 per line) and grows the
// dialog by the same amount, with no margin (OBSERVED in the decompiled
// 0x40b470: no constant is added). So in the original the 50 px button
// row ends on the dialog's bottom edge, across the frame's bottom band.
constexpr int YESNO_MIN_W = 400;
constexpr int YESNO_MIN_H = 180;
constexpr int YESNO_LINE_H = 30;
constexpr int YESNO_BUTTON_Y = 100;
constexpr int YESNO_BUTTON_H = 50;
// Deviation (port improvement): padding so nothing sits on the frame.
// The frame's window onto the centre starts 22 px in from each side
// (dialogdefault/left.png and right.png turn transparent there) and ends
// 35 px above the bottom edge (bottom.png). The original MESSAGE is
// width="390" (game-scene.md §6), which runs over both side borders; the
// port keeps it 12 px inside the window. The bottom padding keeps the
// button pill (rows 6..43 of its 50 px frame) 6 px above the window's
// bottom edge: 50 + 34 - 35 = 49 >= 43 + 6.
constexpr int YESNO_TEXT_INSET = 22 + 12;
constexpr int YESNO_BOTTOM_PAD = 34;
constexpr int YESNO_MESSAGE_W = YESNO_MIN_W - 2 * YESNO_TEXT_INSET;   // 332 (original 390)

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
    const int msg_w = d.w - 2 * YESNO_TEXT_INSET;
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

// DEFAULT_PAINTER frame fill. The nine border PNGs are opaque only in a
// thin outer band and fade to transparent towards the inside (an inner
// shadow that is meant to fall on the centre image), so the centre must
// be painted under the whole frame, not just inside the 37/86/53 px
// border boxes. Measured from the alpha channels of
// images/gui/dialogdefault/*.png: each side has a soft outer glow or
// shadow that falls outside the dialog. Left: alpha 4..53 in columns 1-5,
// opaque from x=6 (left.png row: 0, 4, 11, 22, 36, 53, 255, ...). Top:
// opaque from y=1. Right: a 7 px glow; bottom: a 16 px shadow. The
// rounded corners need a 16 px cut: top_left.png turns opaque at x=22 on
// row 1 and at x=6 from row 17 (bottom_left.png mirrors it). The fill is
// the dialog rect minus those margins, built from two overlapping
// rectangles so the corners stay clear. With these numbers no fill pixel
// shows outside the frame and no hole shows through it.
struct FillRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const FillRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
constexpr int FRAME_FILL_LEFT = 6;     // outer glow of left.png
constexpr int FRAME_FILL_TOP = 1;
constexpr int FRAME_FILL_RIGHT = 7;     // drop shadow of right.png
constexpr int FRAME_FILL_BOTTOM = 16;   // drop shadow of bottom.png
constexpr int FRAME_CORNER = 16;        // rounded-corner cut

// The two rectangles (a cross) that fill a w x h dialog, relative to
// its top-left corner. Empty rects (w or h <= 0) mean "too small".
inline std::array<FillRect, 2> dialog_fill_rects(int w, int h) {
    const int fw = w - FRAME_FILL_LEFT - FRAME_FILL_RIGHT;
    const int fh = h - FRAME_FILL_TOP - FRAME_FILL_BOTTOM;
    return {{
        {FRAME_FILL_LEFT + FRAME_CORNER, FRAME_FILL_TOP, fw - 2 * FRAME_CORNER, fh},
        {FRAME_FILL_LEFT, FRAME_FILL_TOP + FRAME_CORNER, fw, fh - 2 * FRAME_CORNER},
    }};
}

// Strings from commontext.xml / text.xml.
namespace strings {
inline constexpr const char* PLAYER_SELECT = "Select your name";
inline constexpr const char* ENTER_NAME = "What is your name?";
inline constexpr const char* REMOVE_PLAYER_HEADER = "Remove player";
inline constexpr const char* REMOVE_PLAYER = "Are you sure you want to remove the selected player?";
inline constexpr const char* PLAYER_EXISTS_HEADER = "Player exists.";
inline constexpr const char* PLAYER_EXISTS = "That name has already been used. Please choose another.";
inline constexpr const char* WELCOME = "Welcome";
inline constexpr const char* WELCOME_BACK = "Welcome back";
}  // namespace strings

// Text of LABEL_PLAYERNAME (0x42f1a0): TEXT_WELCOME or TEXT_WELCOME_BACK
// followed by the player's name. INFERRED: the separator is a space.
inline std::string welcome_text(const tt2::PlayerManager& pm) {
    const tt2::PlayerProfile* p = pm.current();
    if (!p) return {};
    return std::string(p->returning ? strings::WELCOME_BACK : strings::WELCOME) + " " + p->name;
}

// Drives the dialogs over a PlayerManager. `on_change` fires whenever
// the player list changes so the application can save it.
class ProfileFlow {
public:
    static constexpr int CURSOR_ON_MS = 300;    // durationon
    static constexpr int CURSOR_OFF_MS = 300;   // durationoff
    static constexpr int NAME_ROWS = tt2::MAX_PLAYERS;
    static constexpr int NAME_ROW_Y = 80;       // namebutton y
    static constexpr int NAME_ROW_SPACING = 40; // spacing

    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void()> on_change;
        std::function<std::uint32_t()> seed_millis = [] { return 0u; };   // timeGetTime for a new player
        LineBreaker wrap;   // word-wraps MESSAGE (wrapstyle="wordwrap"); empty: "\n" only
    };

    ProfileFlow(tt2::PlayerManager& pm, Services s) : pm_(pm), services_(std::move(s)) {}

    // --- state ---------------------------------------------------------
    DialogKind active() const { return dialog_.kind; }
    bool open() const { return dialog_.kind != DialogKind::None; }
    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    const std::string& input_text() const { return input_; }
    bool cursor_visible() const { return cursor_on_; }
    int selected_row() const { return selected_; }
    // True while the player wants text input (SDL_StartTextInput).
    bool wants_text_input() const { return dialog_.kind == DialogKind::NewPlayer; }

    // --- entry points --------------------------------------------------
    // SelectionScene enter (0x42d9b0): no players -> ask for a name.
    void on_menu_entered() {
        if (pm_.empty()) open_new_player(/*cancellable=*/false);
    }
    // BUTTON_CHANGENAME.
    void open_player_select() {
        selected_ = pm_.current_index();
        build_player_select();
    }
    void open_new_player(bool cancellable) {
        input_.clear();
        cursor_on_ = true;
        cursor_ms_ = 0;
        return_to_select_ = cancellable;
        build_new_player(cancellable);
    }
    void close() { dialog_ = Dialog{}; }

    // --- input ---------------------------------------------------------
    void text_input(const std::string& utf8) {
        if (dialog_.kind != DialogKind::NewPlayer) return;
        for (std::uint32_t cp : utf8_decode(utf8)) {
            if (cp < 0x20 || cp == 0x7F) continue;                  // control chars
            if (tt2::utf8_length(input_) >= tt2::MAX_PLAYER_NAME) break;   // maxlength=15
            append_utf8(input_, cp);
        }
    }
    void backspace() {
        if (dialog_.kind != DialogKind::NewPlayer || input_.empty()) return;
        std::size_t i = input_.size();
        do { --i; } while (i > 0 && (static_cast<unsigned char>(input_[i]) & 0xC0) == 0x80);
        input_.erase(i);
    }
    void key_return() {
        switch (dialog_.kind) {
            case DialogKind::NewPlayer: press("BUTTON_OK"); break;
            case DialogKind::PlayerSelect: press("BUTTON_OK"); break;
            case DialogKind::Confirm: press("BUTTON_YES"); break;
            case DialogKind::Message: press("BUTTON_OK"); break;
            default: break;
        }
    }
    void key_escape() {
        switch (dialog_.kind) {
            case DialogKind::NewPlayer: press("BUTTON_CANCEL"); break;
            case DialogKind::PlayerSelect: press("BUTTON_OK"); break;
            case DialogKind::Confirm: press("BUTTON_NO"); break;
            case DialogKind::Message: press("BUTTON_OK"); break;
            default: break;
        }
    }

    // Mouse in screen coordinates (the dialog offset is applied here).
    // True while the pointer sits on a clickable dialog button (the
    // SDL front end uses this for the hand cursor). Disabled buttons
    // and a closed dialog are not clickable.
    bool pointer_over_clickable() const {
        if (!open()) return false;
        for (const auto& b : dialog_.buttons)
            if (b.hover && b.enabled) return true;
        return false;
    }
    void mouse_move(float x, float y) {
        if (!open()) return;
        for (auto& b : dialog_.buttons) {
            const bool over = hit(b, x, y);
            b.hover = over;
            if (!over) b.pressed = false;
        }
        refresh_frames();
    }
    void mouse_down(float x, float y) {
        if (!open()) return;
        for (auto& b : dialog_.buttons) b.pressed = hit(b, x, y);
        refresh_frames();
    }
    void mouse_up(float x, float y) {
        if (!open()) return;
        std::string clicked;
        for (auto& b : dialog_.buttons) {
            if (b.pressed && b.enabled && hit(b, x, y)) clicked = b.sprite.name;
            b.pressed = false;
        }
        if (!clicked.empty()) press(clicked);
        refresh_frames();
    }

    void update(int dt_ms) {
        refresh_frames();   // every dialog, not only the name input
        if (dialog_.kind != DialogKind::NewPlayer) return;
        cursor_ms_ += dt_ms;
        const int period = cursor_on_ ? CURSOR_ON_MS : CURSOR_OFF_MS;
        while (cursor_ms_ >= period) {
            cursor_ms_ -= period;
            cursor_on_ = !cursor_on_;
        }
    }

    // Button handler, also used by the keyboard shortcuts.
    void press(const std::string& name) {
        MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled) return;
        sound(SoundId::Button);
        switch (dialog_.kind) {
            case DialogKind::NewPlayer: new_player_press(name); break;
            case DialogKind::PlayerSelect: player_select_press(name); break;
            case DialogKind::Confirm: confirm_press(name); break;
            case DialogKind::Message: close_message(); break;
            default: break;
        }
    }

private:
    // ---- DIALOG_NEWPLAYER 402x223 -------------------------------------
    void build_new_player(bool cancellable) {
        Dialog d;
        d.kind = DialogKind::NewPlayer;
        d.w = 402;
        d.h = 223;
        d.labels.push_back(TextLabel{"NEWPLAYER_HEADER", "FONT_DIALOG_HEADERS2", strings::ENTER_NAME,
                                     200, 19, TextAlign::Center, Anchor::Top, 0});
        SpriteState box = sprite("INPUT_NAME", sheet("INPUT_BOX", 237, 41), 198, 101);
        box.anchor = Anchor::Center;
        d.sprites.push_back(box);
        // The typed text: label x=4 y=-3 relative to the box centre.
        d.labels.push_back(TextLabel{"INPUT_TEXT", "FONT_SETTINGSTEXT", "", 198 + 4, 101 - 3,
                                     TextAlign::Center, Anchor::Center, -1, 140});
        d.buttons.push_back(button("BUTTON_OK", "BUTTON_OK", 180, 137, Anchor::TopRight));
        d.buttons.push_back(button("BUTTON_CANCEL", "BUTTON_CANCEL", 220, 137, Anchor::TopLeft));
        d.buttons.back().enabled = cancellable;   // first player: no way out
        dialog_ = std::move(d);
    }
    void new_player_press(const std::string& name) {
        if (name == "BUTTON_CANCEL") {
            if (return_to_select_) open_player_select();
            else close();
            return;
        }
        if (name != "BUTTON_OK") return;
        const auto r = pm_.add(input_, services_.seed_millis());   // 0x45fa60 + 0x460d10
        switch (r) {
            case tt2::AddPlayerResult::Added:
                changed();
                close();
                break;
            case tt2::AddPlayerResult::Exists:
                build_message(strings::PLAYER_EXISTS_HEADER, strings::PLAYER_EXISTS, DialogKind::NewPlayer);
                break;
            default:
                // Empty / too long / bad chars: the original's input box
                // simply keeps the dialog open. Nothing to do.
                break;
        }
    }

    // ---- DIALOG_PLAYERSELECT 422x367 ----------------------------------
    void build_player_select() {
        Dialog d;
        d.kind = DialogKind::PlayerSelect;
        d.w = 422;
        d.h = 367;
        d.labels.push_back(TextLabel{"SELECTNAME_HEADER", "FONT_DIALOG_HEADERS2", strings::PLAYER_SELECT,
                                     211, 17, TextAlign::Center, Anchor::Top, 0});
        // Selection highlight: PLAYER_BORDER 300x40 at x=61, y set to the row.
        SpriteState border = sprite("BORDER", sheet("PLAYER_BORDER", 300, 40), 61, 0);
        d.sprites.push_back(border);
        // One NAME_SELECT_BUTTON per player (template: anchor top, x centred, 40 apart).
        const auto& players = pm_.players();
        for (int i = 0; i < NAME_ROWS; ++i) {
            const std::string bname = "NAME_" + std::to_string(i);
            MenuButton b = button(bname.c_str(), "NAME_SELECT_BUTTON", 211, static_cast<float>(row_y(i)),
                                  Anchor::Top, 300, 160);
            const bool have = i < static_cast<int>(players.size());
            b.sprite.visible = have;
            b.enabled = have;
            d.buttons.push_back(b);
            d.labels.push_back(TextLabel{"NAMELABEL_" + std::to_string(i), "FONT_SETTINGSTEXT",
                                         have ? players[i].name : std::string(), 211,
                                         static_cast<float>(row_y(i) + 4), TextAlign::Center, Anchor::Top, -1, 0,
                                         have});
        }
        d.buttons.push_back(button("BUTTON_ADD", "BUTTON_ADD", 146, 282, Anchor::TopRight));
        d.buttons.push_back(button("BUTTON_REMOVE", "BUTTON_REMOVE", 153, 282, Anchor::TopLeft));
        d.buttons.push_back(button("BUTTON_OK", "BUTTON_OK", 337, 282, Anchor::Top));
        dialog_ = std::move(d);
        refresh_player_select();
    }
    void refresh_player_select() {
        const int n = pm_.count();
        if (SpriteState* b = dialog_.sprite_named("BORDER")) {
            b->visible = selected_ >= 0 && selected_ < n;
            b->y = static_cast<float>(row_y(selected_ < 0 ? 0 : selected_));
        }
        if (MenuButton* add = dialog_.button("BUTTON_ADD")) add->enabled = !pm_.full();          // 0x40ad00
        if (MenuButton* rem = dialog_.button("BUTTON_REMOVE")) rem->enabled = selected_ >= 0 && selected_ < n;
    }
    void player_select_press(const std::string& name) {
        if (name.rfind("NAME_", 0) == 0) {
            selected_ = std::atoi(name.c_str() + 5);
            select_row(selected_);
            refresh_player_select();
            return;
        }
        if (name == "BUTTON_OK") {
            if (selected_ != pm_.current_index()) select_row(selected_);
            close();
            return;
        }
        if (name == "BUTTON_ADD") {
            open_new_player(/*cancellable=*/true);
            return;
        }
        if (name == "BUTTON_REMOVE") {
            build_confirm(strings::REMOVE_PLAYER_HEADER, strings::REMOVE_PLAYER);
            return;
        }
    }

    // ---- DIALOG_YESNO 400x180 -----------------------------------------
    void build_confirm(const char* header, const char* message) {
        // DIALOG_YESNO wraps MESSAGE (width 390, wordwrap) and moves the
        // buttons below it (0x40b470), the same as the in-game confirms.
        Dialog d = build_yes_no(header, message, false, services_.wrap);
        d.kind = DialogKind::Confirm;
        dialog_ = std::move(d);
    }
    void confirm_press(const std::string& name) {
        if (name == "BUTTON_YES") {
            const int old = selected_;
            if (old >= 0 && old < pm_.count()) {
                pm_.remove(pm_.players()[static_cast<std::size_t>(old)].name);   // 0x460c30
                changed();
            }
            selected_ = tt2::PlayerManager::selection_after_remove(old, pm_.count());
            if (pm_.empty()) { open_new_player(false); return; }           // 0x40aef0
            select_row(selected_);
        }
        build_player_select();
    }

    // ---- message (DIALOG_YESNO frame with a single OK) ------------------
    void build_message(const char* header, const char* message, DialogKind back) {
        Dialog d = build_yes_no(header, message, true, services_.wrap);
        d.kind = DialogKind::Message;
        message_back_ = back;
        dialog_ = std::move(d);
    }
    void close_message() {
        if (message_back_ == DialogKind::NewPlayer) {
            const std::string keep = input_;
            build_new_player(return_to_select_);
            input_ = keep;
        } else {
            close();
        }
    }

    // ---- helpers ------------------------------------------------------
    // Make row `i` the current player (no-op for an invalid row).
    void select_row(int i) {
        if (i < 0 || i >= pm_.count()) return;
        pm_.select(pm_.players()[static_cast<std::size_t>(i)].name);   // 0x460e60
        changed();
    }
    // The sheet row each button shows: normal, hover, pressed, disabled.
    void refresh_frames() {
        for (auto& b : dialog_.buttons) b.sprite.frame = b.frame_for_state();
    }
    static int row_y(int row) { return NAME_ROW_Y + row * NAME_ROW_SPACING; }
    static MenuButton button(const char* name, const char* image, float x, float y, Anchor a, int w = 119,
                             int h = 200) {
        return make_dialog_button(name, image, x, y, a, w, h);
    }
    bool hit(const MenuButton& b, float x, float y) const {
        return b.sprite.visible && b.contains(x - dialog_.left(), y - dialog_.top());
    }
    void sound(SoundId id) { if (services_.play_sound) services_.play_sound(id); }
    void changed() { if (services_.on_change) services_.on_change(); }
    static void append_utf8(std::string& s, std::uint32_t cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    tt2::PlayerManager& pm_;
    Services services_;
    Dialog dialog_;
    std::string input_;
    bool cursor_on_ = true;
    int cursor_ms_ = 0;
    int selected_ = -1;
    bool return_to_select_ = false;
    DialogKind message_back_ = DialogKind::None;
};

}  // namespace tt2ui
