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
// 9-slice frame, the 0.7 darkening (DIALOG_BACKGROUND_ALPHA) and the
// labels; this file owns the state and the rules.
#pragma once

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
};

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
    void mouse_move(float x, float y) {
        if (!open()) return;
        for (auto& b : dialog_.buttons) {
            const bool over = hit(b, x, y);
            if (over && !b.hover && b.enabled) sound(SoundId::ButtonRollover);
            b.hover = over;
            if (!over) b.pressed = false;
        }
    }
    void mouse_down(float x, float y) {
        if (!open()) return;
        for (auto& b : dialog_.buttons) b.pressed = hit(b, x, y);
    }
    void mouse_up(float x, float y) {
        if (!open()) return;
        std::string clicked;
        for (auto& b : dialog_.buttons) {
            if (b.pressed && b.enabled && hit(b, x, y)) clicked = b.sprite.name;
            b.pressed = false;
        }
        if (!clicked.empty()) press(clicked);
    }

    void update(int dt_ms) {
        if (dialog_.kind != DialogKind::NewPlayer) return;
        cursor_ms_ += dt_ms;
        const int period = cursor_on_ ? CURSOR_ON_MS : CURSOR_OFF_MS;
        while (cursor_ms_ >= period) {
            cursor_ms_ -= period;
            cursor_on_ = !cursor_on_;
        }
        for (auto& b : dialog_.buttons) b.sprite.frame = b.frame_for_state();
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
        Dialog d;
        d.kind = DialogKind::Confirm;
        d.w = 400;
        d.h = 180;
        d.labels.push_back(TextLabel{"HEADER", "FONT_DIALOG_HEADERS2", header, 200, 13, TextAlign::Center, Anchor::Top, -1});
        d.labels.push_back(TextLabel{"MESSAGE", "FONT_SETTINGSTEXT", message, 200, 87, TextAlign::Center, Anchor::Top, -1, 390});
        d.buttons.push_back(button("BUTTON_YES", "BUTTON_YES", 190, 100, Anchor::TopRight));
        d.buttons.push_back(button("BUTTON_NO", "BUTTON_NO", 210, 100, Anchor::TopLeft));
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
        Dialog d;
        d.kind = DialogKind::Message;
        d.w = 400;
        d.h = 180;
        d.labels.push_back(TextLabel{"HEADER", "FONT_DIALOG_HEADERS2", header, 200, 13, TextAlign::Center, Anchor::Top, -1});
        d.labels.push_back(TextLabel{"MESSAGE", "FONT_SETTINGSTEXT", message, 200, 87, TextAlign::Center, Anchor::Top, -1, 390});
        d.buttons.push_back(button("BUTTON_OK", "BUTTON_OK", 200, 100, Anchor::Top));
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
    static int row_y(int row) { return NAME_ROW_Y + row * NAME_ROW_SPACING; }
    static MenuButton button(const char* name, const char* image, float x, float y, Anchor a, int w = 119,
                             int h = 200) {
        MenuButton b;
        b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
        b.sprite.anchor = a;
        return b;
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
