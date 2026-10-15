// DIALOG_SETTINGS, headless: the music and sound sliders, the fullscreen
// switch and the maximum word length radio group.
//
// Layout: properties/dialogs.xml; behaviour: ctor 0x40bdf0, FX slider
// 0x40c5c0, music slider 0x40c5f0, OK 0x40c620, CANCEL 0x40c700,
// update hook 0x40c3b0 (docs/reverse-engineering/help-and-settings.md §3).
// The sliders apply live; OK keeps the new values, CANCEL puts the saved
// volumes back. The host owns persistence and the window mode.
//
// Deliberate deviation (OP-7): the original DIALOG_SETTINGS has no asset
// row. When the host passes an AssetRow (the main menu does; the in-game
// settings don't), the port adds "ORIGINAL ASSETS" with its import status
// and an IMPORT button, and grows the dialog by one 50 px row, because the
// original game is required until the placeholder art is ready
// (docs/design/import-assets.md, help-and-settings.md §3 "Port deviations").
#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>

#include "tt2/settings.hpp"
#include "tt2ui/asset_import.hpp"
#include "tt2ui/credits_dialog.hpp"
#include "tt2ui/game_dialogs.hpp"

namespace tt2ui {

namespace settings_strings {
inline constexpr const char* HEADER = "SETTINGS";               // CTEXT_SETTINGS_HEADER
inline constexpr const char* MUSIC = "MUSIC";                   // CTEXT_SETTINGS_MUSIC
inline constexpr const char* SOUND = "SOUND";                   // CTEXT_SETTINGS_SOUND
inline constexpr const char* FULLSCREEN = "FULLSCREEN";         // CTEXT_SETTINGS_FULLSCREEN
inline constexpr const char* MAX_LENGTH = "MAXIMUM WORD LENGTH";
// The port's asset row (deviation, OP-7).
inline constexpr const char* ASSETS = "ORIGINAL ASSETS";
inline constexpr const char* ASSETS_IMPORTED = "Imported";
inline constexpr const char* ASSETS_NOT_IMPORTED = "Not imported";
inline constexpr const char* ASSETS_NEEDS_UPDATE = "Needs updating";
}  // namespace settings_strings

constexpr int SETTINGS_W = 436;
constexpr int SETTINGS_H = 440;
// Deviation (OP-7): one more row for the asset import, 50 px, between the
// word-length tab and the bottom buttons (which move from y 357 to 407).
constexpr int SETTINGS_ASSET_ROW_H = 50;
constexpr float SETTINGS_ASSET_ROW_Y = 357.0f;

inline const char* asset_status_text(ImportStatus s) {
    switch (s) {
    case ImportStatus::Imported: return settings_strings::ASSETS_IMPORTED;
    case ImportStatus::Stale: return settings_strings::ASSETS_NEEDS_UPDATE;
    case ImportStatus::NotImported:
    case ImportStatus::Incomplete: return settings_strings::ASSETS_NOT_IMPORTED;
    }
    return settings_strings::ASSETS_NOT_IMPORTED;
}
// Slider: SLIDER_BACK 284x19; the knob (SLIDER_BUTTON 33x140, 4 rows,
// anchor centre) runs from minx 30 to maxx 276 at y 12 (0x468440).
constexpr int SLIDER_BACK_W = 284;
constexpr int SLIDER_BACK_H = 19;
constexpr float SLIDER_MIN_X = 30.0f;
constexpr float SLIDER_MAX_X = 276.0f;
constexpr float SLIDER_KNOB_Y = 12.0f;
// 0x40c3b0: SOUND_SLIDER_1 at most every 500 ms while the FX knob is held.
constexpr int SLIDER_SOUND_MS = 500;
// CHECKBOX02 (button_check.png 24x200): 8 rows of 25, rows 4..7 = on.
constexpr int SWITCH_ON_ROW_OFFSET = 4;

struct SettingsSlider {
    std::string name;   // SLIDER_MUSICVOL / SLIDER_FXVOL
    float x = 0, y = 0; // dialog-local top-left of the track
    float value = 0.0f; // 0..1 (maxvalue 1)
    bool held = false;
    bool hover = false;

    float knob_x() const { return x + SLIDER_MIN_X + value * (SLIDER_MAX_X - SLIDER_MIN_X); }
    float knob_y() const { return y + SLIDER_KNOB_Y; }
    // The value under a dialog-local pointer x, clamped to the track.
    float value_at(float px) const {
        return std::clamp((px - x - SLIDER_MIN_X) / (SLIDER_MAX_X - SLIDER_MIN_X), 0.0f, 1.0f);
    }
    // INFERRED: the track and the knob both take a press.
    bool contains(float px, float py) const {
        const bool on_track = px >= x && px < x + SLIDER_BACK_W && py >= y && py < y + SLIDER_BACK_H;
        const bool on_knob = px >= knob_x() - 16.5f && px < knob_x() + 16.5f && py >= knob_y() - 17.5f &&
                             py < knob_y() + 17.5f;
        return on_track || on_knob;
    }
    int knob_frame() const { return held ? 2 : (hover ? 1 : 0); }
};

// Alt+Enter flips the window mode. Deliberate deviation: the original has
// no such shortcut; its key translator 0x484770 only sends Esc to windowed
// mode, and WM_SIZE / SIZE_MAXIMIZED (0x484036) to fullscreen
// (help-and-settings.md §3.4). A held key's auto-repeat does not toggle.
inline bool is_fullscreen_shortcut(bool alt_down, bool enter_key, bool repeat) {
    return alt_down && enter_key && !repeat;
}

// What DIALOG_SETTINGS needs from the application.
struct SettingsHost {
    std::function<tt2::GameSettings()> current;              // the live settings
    std::function<void(float)> apply_sound_volume;           // 0x463db0
    std::function<void(float)> apply_music_volume;           // 0x463e30
    std::function<void(const tt2::GameSettings&)> commit;    // OK: save settings.txt, set the window mode
    bool fullscreen_supported = true;                        // [0x5fdea0]+0x3d
};

class SettingsDialog {
public:
    // ImportAssets: IMPORT on the asset row. The dialog is done: the host
    // keeps the values like OK, closes it and starts the import.
    enum class Result { None, Ok, Cancel, Info, ImportAssets };

    // The port's asset row (deviation, OP-7); without one the dialog is the
    // original's.
    struct AssetRow {
        ImportStatus status = ImportStatus::NotImported;
        bool can_import = true;   // false: no per-user folder or no manifest
    };

    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void(float)> apply_sound_volume;   // 0x463db0, live
        std::function<void(float)> apply_music_volume;   // 0x463e30, live
    };

    // `saved` is the live settings, `max_word_length` the current player's
    // (6/7/8), `fullscreen_supported` mirrors [0x5fdea0]+0x3d.
    SettingsDialog(const tt2::GameSettings& saved, int max_word_length, bool fullscreen_supported, Services services,
                   std::optional<AssetRow> asset_row = std::nullopt)
        : saved_(saved), values_(saved), max_len_(normal_length(max_word_length)), services_(std::move(services)) {
        build(fullscreen_supported, asset_row);
    }

    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    // DIALOG_CREDITS while it is open over the settings (INFO, 0x40c760).
    const Dialog* credits() const { return credits_ ? &credits_->dialog() : nullptr; }
    const CreditsDialog* credits_dialog() const { return credits_ ? &*credits_ : nullptr; }
    const tt2::GameSettings& values() const { return values_; }
    int max_word_length() const { return max_len_; }
    const std::array<SettingsSlider, 2>& sliders() const { return sliders_; }
    const SettingsSlider& slider(const std::string& name) const { return sliders_[index_of(name)]; }

    // ---- input ----------------------------------------------------------
    void mouse_move(float x, float y) {
        if (credits_) { dialog_mouse_move(credits_->dialog(), x, y); return; }
        dialog_mouse_move(dialog_, x, y);
        const float lx = x - dialog_.left(), ly = y - dialog_.top();
        for (std::size_t i = 0; i < sliders_.size(); ++i) {
            auto& s = sliders_[i];
            s.hover = s.contains(lx, ly);
            if (s.held) set_value(i, s.value_at(lx));
        }
    }
    void mouse_down(float x, float y) {
        if (credits_) { dialog_mouse_down(credits_->dialog(), x, y); return; }
        dialog_mouse_down(dialog_, x, y);
        const float lx = x - dialog_.left(), ly = y - dialog_.top();
        for (std::size_t i = 0; i < sliders_.size(); ++i) {
            if (!sliders_[i].contains(lx, ly)) continue;
            sliders_[i].held = true;
            slider_sound_ms_ = SLIDER_SOUND_MS;   // INFERRED: the first sound plays at once
            set_value(i, sliders_[i].value_at(lx));
        }
    }
    Result mouse_up(float x, float y) {
        for (auto& s : sliders_) s.held = false;
        if (credits_) return press(dialog_mouse_up(credits_->dialog(), x, y));
        return press(dialog_mouse_up(dialog_, x, y));
    }

    // A button or switch by name (mouse, tests). While the credits are up
    // they take every press: their OK (0x408c00) closes them, PREV / NEXT
    // turn their page, and the hosts' Esc (CANCEL) closes them too. The
    // original dialogs take no keys at all (help-and-settings.md §4); the
    // keys are a port convenience.
    Result press(const std::string& name) {
        if (credits_) {
            const MenuButton* b = credits_->dialog().button(name);
            const bool usable = b && b->enabled && b->sprite.visible;
            if (name == "BUTTON_CANCEL" || usable) {
                sound(SoundId::Button);
                if (name == "BUTTON_CANCEL" || credits_->press(name)) credits_.reset();
            }
            return Result::None;
        }
        MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return Result::None;
        if (name == "SWITCH_FULLSCREEN") {
            values_.fullscreen = !values_.fullscreen;
            sound(values_.fullscreen ? SoundId::CheckboxOn : SoundId::CheckboxOff);
            refresh_switches();
            return Result::None;
        }
        for (int len = 6; len <= 8; ++len) {
            if (name != length_switch(len)) continue;
            // Radio group 30. INFERRED: the checked switch stays on, silently.
            if (len != max_len_) {
                max_len_ = len;
                sound(SoundId::CheckboxOn);
                refresh_switches();
            }
            return Result::None;
        }
        sound(SoundId::Button);
        if (name == "BUTTON_OK") return Result::Ok;   // the host saves (0x40c620)
        if (name == "BUTTON_CANCEL") {                // 0x40c700: put the saved volumes back
            values_ = saved_;
            apply(services_.apply_sound_volume, saved_.sound_volume);
            apply(services_.apply_music_volume, saved_.music_volume);
            return Result::Cancel;
        }
        if (name == "BUTTON_INFO") {
            credits_.emplace();   // 0x40c760: over the settings
            return Result::Info;
        }
        if (name == "BUTTON_IMPORT") return Result::ImportAssets;   // deviation, OP-7
        return Result::None;
    }

    // The window mode changed outside the dialog (Alt+Enter): show it, and
    // keep it on CANCEL and OK. Silent, unlike a click on the switch.
    void sync_fullscreen(bool on) {
        saved_.fullscreen = on;
        values_.fullscreen = on;
        refresh_switches();
    }

    // Slider changes, as the slider callbacks 0x40c5c0 / 0x40c5f0 see them.
    void set_volume(const std::string& slider_name, float v) { set_value(index_of(slider_name), v); }

    void tick(int dt_ms) {
        const SettingsSlider& fx = sliders_[1];
        if (fx.held) {
            slider_sound_ms_ += dt_ms;
            if (slider_sound_ms_ >= SLIDER_SOUND_MS) {
                slider_sound_ms_ = 0;
                sound(SoundId::Slider);
            }
        }
        dialog_tick(dialog_, dt_ms);
        if (credits_) credits_->tick(dt_ms);
        refresh_switches();
    }

    bool pointer_hot() const {
        if (credits_) return dialog_pointer_hot(credits_->dialog());
        if (dialog_pointer_hot(dialog_)) return true;
        for (const auto& s : sliders_) if (s.hover) return true;
        return false;
    }

    static const char* length_switch(int len) {
        return len == 7 ? "SWITCH_LENGTH_7" : len == 8 ? "SWITCH_LENGTH_8" : "SWITCH_LENGTH_6";
    }
    // 0x40bdf0: 7 -> _7, 8 -> _8, anything else -> _6.
    static int normal_length(int len) { return len == 7 || len == 8 ? len : 6; }

private:
    void build(bool fullscreen_supported, const std::optional<AssetRow>& asset_row) {
        Dialog d;
        d.w = SETTINGS_W;
        d.h = SETTINGS_H + (asset_row ? SETTINGS_ASSET_ROW_H : 0);
        const float bottom_y = 357.0f + (asset_row ? SETTINGS_ASSET_ROW_H : 0);
        d.labels.push_back(TextLabel{"SETTING_HEADER", "FONT_DIALOG_HEADERS", settings_strings::HEADER, 226, 33,
                                     TextAlign::Center, Anchor::Center, 4});
        // Labels without anchorpoint: (x, y) is the top-left (INFERRED from the fit).
        d.labels.push_back(TextLabel{"MUSIC_TITLE", "FONT_SETTINGSTEXT", settings_strings::MUSIC, 45, 94,
                                     TextAlign::Left, Anchor::Top, -1});
        d.labels.push_back(TextLabel{"SOUND", "FONT_SETTINGSTEXT", settings_strings::SOUND, 43, 145,
                                     TextAlign::Left, Anchor::Top, -1});
        d.labels.push_back(TextLabel{"LABEL_FULLSCREEN", "FONT_SETTINGSTEXT", settings_strings::FULLSCREEN, 189, 207,
                                     TextAlign::Left, Anchor::Top, -1});
        d.labels.push_back(TextLabel{"LABEL_DIFFICULTY", "FONT_SETTINGSTEXT", settings_strings::MAX_LENGTH, 107, 263,
                                     TextAlign::Left, Anchor::Top, -1});
        // anchorpoint="left": x is the left edge, y the vertical centre.
        d.labels.push_back(TextLabel{"LABEL_6", "FONT_SETTINGSTEXT", "6 Letter", 79, 309, TextAlign::Left,
                                     Anchor::Center, -1});
        d.labels.push_back(TextLabel{"LABEL_7", "FONT_SETTINGSTEXT", "7 Letter", 202, 309, TextAlign::Left,
                                     Anchor::Center, -1});
        d.labels.push_back(TextLabel{"LABEL_8", "FONT_SETTINGSTEXT", "8 Letter", 320, 309, TextAlign::Left,
                                     Anchor::Center, -1});
        SpriteState tab = sprite("DIFFICULTY_TAB", sheet("IMAGE_DIFFICULTY_TAB", 383, 102), 220, 299);
        tab.anchor = Anchor::Center;
        d.sprites.push_back(tab);
        d.sprites.push_back(sprite("SLIDER_MUSICVOL", sheet("SLIDER_BACK", SLIDER_BACK_W, SLIDER_BACK_H), 123, 98));
        d.sprites.push_back(sprite("SLIDER_FXVOL", sheet("SLIDER_BACK", SLIDER_BACK_W, SLIDER_BACK_H), 123, 149));
        d.buttons.push_back(make_switch("SWITCH_FULLSCREEN", 161, 208));
        d.buttons.back().enabled = fullscreen_supported;   // 0x40c1cd
        d.buttons.push_back(make_switch("SWITCH_LENGTH_6", 46, 298));
        d.buttons.push_back(make_switch("SWITCH_LENGTH_7", 170, 298));
        d.buttons.push_back(make_switch("SWITCH_LENGTH_8", 289, 298));
        if (asset_row) {
            // Deviation (OP-7): ORIGINAL ASSETS, its status, and IMPORT.
            d.labels.push_back(TextLabel{"LABEL_ASSETS", "FONT_SETTINGSTEXT", settings_strings::ASSETS, 45,
                                         SETTINGS_ASSET_ROW_Y, TextAlign::Left, Anchor::Top, -1});
            d.labels.push_back(TextLabel{"LABEL_ASSETS_STATUS", "FONT_SETTINGSTEXT",
                                         asset_status_text(asset_row->status), 45, SETTINGS_ASSET_ROW_Y + 24,
                                         TextAlign::Left, Anchor::Top, -1});
            d.buttons.push_back(make_dialog_button("BUTTON_IMPORT", "BUTTON_IMPORT", 391, SETTINGS_ASSET_ROW_Y,
                                                   Anchor::TopRight));
            d.buttons.back().enabled = asset_row->can_import;
        }
        d.buttons.push_back(make_dialog_button("BUTTON_INFO", "BUTTON_INFO", 85, bottom_y, Anchor::Top));   // CREDITS
        d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 278, bottom_y, Anchor::TopRight));
        d.buttons.push_back(make_dialog_button("BUTTON_CANCEL", "BUTTON_CANCEL", 294, bottom_y, Anchor::TopLeft));
        dialog_ = std::move(d);
        sliders_[0] = SettingsSlider{"SLIDER_MUSICVOL", 123, 98, values_.music_volume};
        sliders_[1] = SettingsSlider{"SLIDER_FXVOL", 123, 149, values_.sound_volume};
        refresh_switches();
    }

    static MenuButton make_switch(const char* name, float x, float y) {
        MenuButton b;
        b.sprite = sprite(name, sheet("CHECKBOX02", 24, 200, 1, 8, 8), x, y);
        return b;
    }

    bool switch_on(const std::string& name) const {
        if (name == "SWITCH_FULLSCREEN") return values_.fullscreen;
        return name == length_switch(max_len_);
    }
    void refresh_switches() {
        for (auto& b : dialog_.buttons) {
            if (b.sprite.name.rfind("SWITCH_", 0) != 0) continue;
            b.sprite.frame = b.frame_for_state() + (switch_on(b.sprite.name) ? SWITCH_ON_ROW_OFFSET : 0);
        }
    }

    static std::size_t index_of(const std::string& name) { return name == "SLIDER_FXVOL" ? 1 : 0; }

    void set_value(std::size_t i, float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        sliders_[i].value = v;
        if (i == 0) {
            values_.music_volume = v;
            apply(services_.apply_music_volume, v);
        } else {
            values_.sound_volume = v;
            apply(services_.apply_sound_volume, v);
        }
    }
    static void apply(const std::function<void(float)>& f, float v) { if (f) f(v); }
    void sound(SoundId id) { if (services_.play_sound) services_.play_sound(id); }

    tt2::GameSettings saved_;
    tt2::GameSettings values_;
    int max_len_ = 6;
    Services services_;
    Dialog dialog_;
    std::optional<CreditsDialog> credits_;   // DIALOG_CREDITS over the settings
    std::array<SettingsSlider, 2> sliders_{};
    int slider_sound_ms_ = 0;
};

}  // namespace tt2ui
