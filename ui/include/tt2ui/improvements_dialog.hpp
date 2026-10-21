// IMPROVEMENTS, headless: the port's own display options
// (docs/design/improve-ui-ux.md), plus ASK TO RESUME (the "Continue the
// game?" prompt; Off is the original's silent resume). Nothing here exists in the original
// game; the dialog is deviation UX-2, its file improvements.txt UX-4.
//
// It reuses the DIALOG_SETTINGS building blocks (Dialog, MenuButton,
// TextLabel), so the renderer draws it like any other dialog. Each row is
// a cycle control: a label, the value text, and left / right arrows that
// step through the choices (wrapping round, past any the display can't
// offer). Every row is one line: a row this run can't offer is greyed out
// with no reason under it, and what actually runs goes in the value, in
// parentheses ("Auto (OpenGL)", "Native (failed)"). A renderer change
// applies on the next launch, so OK first shows a one-button "Restart
// needed" notice over the dialog. OK keeps the edited values and CANCEL
// restores the saved ones, the same contract as SETTINGS; the host owns
// saving and applying them.
#pragma once

#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "tt2/improvements.hpp"
#include "tt2ui/display.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/render_backend.hpp"

namespace tt2ui {

namespace improvements_strings {
inline constexpr const char* HEADER = "IMPROVEMENTS";
inline constexpr const char* WINDOW = "DISPLAY MODE";
inline constexpr const char* WINDOWED = "Windowed";
inline constexpr const char* BORDERLESS = "Borderless";
inline constexpr const char* NATIVE = "Native";
inline constexpr const char* FAILED = " (failed)";
inline constexpr const char* RESOLUTION = "RESOLUTION";
inline constexpr const char* RENDERER = "RENDERER";
inline constexpr const char* AUTO = "Auto";
inline constexpr const char* RESTART_HEADER = "Restart needed";
inline constexpr const char* RESTART_TEXT = "The new renderer is used\nafter you restart the game.";
inline constexpr const char* VSYNC = "VSYNC";
inline constexpr const char* ON = "On";
inline constexpr const char* OFF = "Off";
inline constexpr const char* FRAME_LIMIT = "FRAME LIMIT";
inline constexpr const char* UNLIMITED = "Unlimited";
inline constexpr const char* FPS = " fps";
inline constexpr const char* WIDESCREEN_FILL = "WIDESCREEN FILL";
inline constexpr const char* FILTER = "TEXTURE FILTER";
inline constexpr const char* SHARP = "Sharp";
inline constexpr const char* SMOOTH = "Smooth";
inline constexpr const char* PIXEL_ART = "Pixel art";
inline constexpr const char* UPSCALER = "UPSCALER";
inline constexpr const char* SUPER_XBR = "Super-xBR";
inline constexpr const char* RESUME_PROMPT = "ASK TO RESUME";
}  // namespace improvements_strings

// Layout, dialog-local. Rows start under the header; OK / CANCEL sit 83 px
// above the bottom edge like SETTINGS (y 357 of 440).
constexpr int IMPROVEMENTS_W = 520;
constexpr float IMPROVEMENTS_ROWS_TOP = 96.0f;
constexpr int IMPROVEMENTS_ROW_H = 42;   // 9 rows fit in 600 px (578)
constexpr int IMPROVEMENTS_FOOTER_H = 104;   // the gap, the buttons and the frame's bottom edge
constexpr int IMPROVEMENTS_ARROW = 36;       // button_arrow_*.png: 36x36 frames, 4 rows
constexpr float IMPROVEMENTS_LABEL_X = 45.0f;
constexpr float IMPROVEMENTS_PREV_X = 268.0f;    // arrow centres
constexpr float IMPROVEMENTS_NEXT_X = 472.0f;
constexpr float IMPROVEMENTS_VALUE_X = 370.0f;   // value text centre

constexpr int improvements_dialog_height(int rows) {
    return static_cast<int>(IMPROVEMENTS_ROWS_TOP) + rows * IMPROVEMENTS_ROW_H + IMPROVEMENTS_FOOTER_H;
}

class ImprovementsDialog {
public:
    enum class Result { None, Ok, Cancel };

    struct Services {
        std::function<void(SoundId)> play_sound;
    };

    // What this run started with: the renderer change applies on the next
    // launch (Q4), so the dialog compares against it.
    struct Context {
        Backend active = Backend::Software;
        tt2::RendererPref launched = tt2::RendererPref::Auto;
        DisplayInfo display;          // the sizes and modes this display allows (UX-5)
        bool native_failed = false;   // Native was asked for but fell back to Borderless
        Platform platform = THIS_PLATFORM;   // Android: no DISPLAY MODE / RESOLUTION rows
    };

    // One cycle row. `id` names its buttons and labels: LABEL_<id>,
    // VALUE_<id>, BUTTON_<id>_PREV, BUTTON_<id>_NEXT.
    struct Row {
        std::string id;
        std::string title;
        std::vector<std::string> choices;
        int index = 0;
        bool enabled = true;      // false: greyed out, the arrows do nothing
        std::vector<bool> skip;   // choices the arrows step over (same length as choices, or empty)
    };

    ImprovementsDialog(const tt2::Improvements& saved, Services services)
        : ImprovementsDialog(saved, std::move(services), Context()) {}
    ImprovementsDialog(const tt2::Improvements& saved, Services services, Context context)
        : saved_(saved), values_(saved), services_(std::move(services)), context_(context) {
        build();
    }

    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    const tt2::Improvements& values() const { return values_; }
    const std::vector<Row>& rows() const { return rows_; }
    const Row* row(const std::string& id) const {
        for (const auto& r : rows_) if (r.id == id) return &r;
        return nullptr;
    }

    // The "Restart needed" notice OK opens over the dialog; null otherwise.
    const Dialog* notice() const { return notice_ ? &*notice_ : nullptr; }
    // OK would change the renderer this run started with: picked here (not
    // only saved that way) and not what is running.
    bool restart_needed() const {
        return values_.renderer != saved_.renderer && values_.renderer != context_.launched;
    }

    // ---- input ----------------------------------------------------------
    void mouse_move(float x, float y) { dialog_mouse_move(top(), x, y); }
    void mouse_down(float x, float y) { dialog_mouse_down(top(), x, y); }
    Result mouse_up(float x, float y) { return press(dialog_mouse_up(top(), x, y)); }

    // A button by name (mouse, keys, tests). Unknown, hidden or disabled
    // buttons do nothing. While the notice is up it takes every press: its
    // OK, or the hosts' Esc (CANCEL), closes it and finishes OK, which the
    // player already chose.
    Result press(const std::string& name) {
        if (notice_) {
            const MenuButton* b = notice_->button(name);
            if (name != "BUTTON_CANCEL" && !(b && b->enabled && b->sprite.visible)) return Result::None;
            sound(SoundId::Button);
            notice_.reset();
            return Result::Ok;
        }
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return Result::None;
        sound(SoundId::Button);
        if (name == "BUTTON_OK") {
            if (!restart_needed()) return Result::Ok;
            notice_ = build_yes_no(improvements_strings::RESTART_HEADER, improvements_strings::RESTART_TEXT, true);
            return Result::None;
        }
        if (name == "BUTTON_CANCEL") {
            values_ = saved_;
            refresh();
            return Result::Cancel;
        }
        for (auto& r : rows_) {
            const int step = name == "BUTTON_" + r.id + "_NEXT" ? 1 : name == "BUTTON_" + r.id + "_PREV" ? -1 : 0;
            if (step == 0 || !r.enabled || r.choices.empty()) continue;
            const int n = static_cast<int>(r.choices.size());
            for (int tries = 0; tries < n; ++tries) {
                r.index = (r.index + step + n) % n;
                if (r.skip.empty() || !r.skip[static_cast<std::size_t>(r.index)]) break;
            }
            store(r);
            refresh();
        }
        return Result::None;
    }

    // A row the current renderer or display can't offer: greyed out, its
    // arrows stop working.
    void set_row_enabled(const std::string& id, bool enabled) {
        for (auto& r : rows_)
            if (r.id == id) r.enabled = enabled;
        refresh();
    }

    // The window mode changed outside the dialog (Alt+Enter): show it, and
    // keep it on CANCEL and OK. Silent, like SettingsDialog::sync_fullscreen.
    void sync_window(tt2::WindowMode m) {
        saved_.set_window(m);
        values_.set_window(m);
        refresh();
    }

    void tick(int dt_ms) {
        dialog_tick(dialog_, dt_ms);
        if (notice_) dialog_tick(*notice_, dt_ms);
    }
    bool pointer_hot() const { return dialog_pointer_hot(notice_ ? *notice_ : dialog_); }

private:
    Dialog& top() { return notice_ ? *notice_ : dialog_; }

    void build() {
        if (window_modes_offered(context_.platform)) {   // always full screen on Android (display.hpp)
            rows_.push_back(Row{"WINDOW", improvements_strings::WINDOW,
                                {improvements_strings::WINDOWED, improvements_strings::BORDERLESS,
                                 improvements_strings::NATIVE},
                                0, true, {}});
            rows_.push_back(Row{"RESOLUTION", improvements_strings::RESOLUTION, {}, 0, true, {}});
        }
        rows_.push_back(Row{"WIDESCREEN_FILL", improvements_strings::WIDESCREEN_FILL,
                            {improvements_strings::ON, improvements_strings::OFF}, 0, true, {}});
        rows_.push_back(Row{"FILTER", improvements_strings::FILTER,
                            {improvements_strings::SHARP, improvements_strings::SMOOTH, improvements_strings::PIXEL_ART},
                            0, true, {}});
        rows_.push_back(Row{"UPSCALER", improvements_strings::UPSCALER,
                            {improvements_strings::OFF, improvements_strings::SUPER_XBR}, 0, true, {}});
        rows_.push_back(Row{"RENDERER", improvements_strings::RENDERER,
                            {improvements_strings::AUTO, backend_title(Backend::Vulkan),
                             backend_title(opengl_backend(context_.platform)),
                             backend_title(Backend::Software)},
                            0, true, {}});
        rows_.push_back(Row{"VSYNC", improvements_strings::VSYNC,
                            {improvements_strings::ON, improvements_strings::OFF}, 0, true, {}});
        std::vector<std::string> limits;
        for (int fps : tt2::FRAME_LIMITS)
            limits.push_back(fps == 0 ? std::string(improvements_strings::UNLIMITED)
                                      : std::to_string(fps) + improvements_strings::FPS);
        rows_.push_back(Row{"FRAME_LIMIT", improvements_strings::FRAME_LIMIT, std::move(limits), 0, true, {}});
        rows_.push_back(Row{"RESUME_PROMPT", improvements_strings::RESUME_PROMPT,
                            {improvements_strings::ON, improvements_strings::OFF}, 0, true, {}});
        Dialog d;
        d.w = IMPROVEMENTS_W;
        d.h = improvements_dialog_height(static_cast<int>(rows_.size()));
        d.labels.push_back(TextLabel{"IMPROVEMENTS_HEADER", "FONT_DIALOG_HEADERS", improvements_strings::HEADER,
                                     IMPROVEMENTS_W / 2.0f, 33, TextAlign::Center, Anchor::Center, 4});
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i];
            const float cy = IMPROVEMENTS_ROWS_TOP + static_cast<float>(i) * IMPROVEMENTS_ROW_H + 18.0f;
            d.labels.push_back(TextLabel{"LABEL_" + r.id, "FONT_SETTINGSTEXT", r.title, IMPROVEMENTS_LABEL_X, cy,
                                         TextAlign::Left, Anchor::Center, -1});
            d.labels.push_back(TextLabel{"VALUE_" + r.id, "FONT_SETTINGSTEXT", {}, IMPROVEMENTS_VALUE_X, cy,
                                         TextAlign::Center, Anchor::Center, -1});
            d.buttons.push_back(arrow("BUTTON_" + r.id + "_PREV", "BUTTON_ARROW_LEFT", IMPROVEMENTS_PREV_X, cy));
            d.buttons.push_back(arrow("BUTTON_" + r.id + "_NEXT", "BUTTON_ARROW_RIGHT", IMPROVEMENTS_NEXT_X, cy));
        }
        const float bottom_y = static_cast<float>(d.h - 83);
        const float mid = IMPROVEMENTS_W / 2.0f;
        d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", mid - 8, bottom_y, Anchor::TopRight));
        d.buttons.push_back(make_dialog_button("BUTTON_CANCEL", "BUTTON_CANCEL", mid + 8, bottom_y, Anchor::TopLeft));
        dialog_ = std::move(d);
        load_rows();
        refresh();
    }

    static MenuButton arrow(const std::string& name, const char* image, float x, float y) {
        return make_dialog_button(name.c_str(), image, x, y, Anchor::Center, IMPROVEMENTS_ARROW, IMPROVEMENTS_ARROW * 4);
    }

    static constexpr tt2::RendererPref kRendererOrder[] = {tt2::RendererPref::Auto, tt2::RendererPref::Vulkan,
                                                            tt2::RendererPref::OpenGL, tt2::RendererPref::Software};

    static constexpr tt2::TextureFilter kFilterOrder[] = {tt2::TextureFilter::Sharp, tt2::TextureFilter::Smooth,
                                                           tt2::TextureFilter::PixelArt};
    static constexpr tt2::WindowMode kWindowOrder[] = {tt2::WindowMode::Windowed, tt2::WindowMode::Borderless,
                                                        tt2::WindowMode::Native};

    // The sizes the RESOLUTION row offers in mode `m` (Borderless: none).
    const std::vector<tt2::Resolution>& sizes_for(tt2::WindowMode m) const {
        static const std::vector<tt2::Resolution> none;
        if (m == tt2::WindowMode::Windowed) return context_.display.windowed;
        if (m == tt2::WindowMode::Native) return context_.display.native;
        return none;
    }

    // values_ -> row indices and value texts.
    void load_rows() {
        for (auto& r : rows_) {
            if (r.id == "WINDOW") {
                for (int i = 0; i < 3; ++i)
                    if (kWindowOrder[i] == values_.window) r.index = i;
                // Native needs an exact display mode of an offered size (the
                // original's fullscreen needs an exact 800x600 mode, §3).
                r.skip = {false, false, context_.display.native.empty()};
                // This run asked for Native and fell back to Borderless.
                r.choices[2] = std::string(improvements_strings::NATIVE) +
                               (context_.native_failed ? improvements_strings::FAILED : "");
            }
            if (r.id == "RESOLUTION") load_resolution_row(r);
            if (r.id == "RENDERER") {
                for (int i = 0; i < 4; ++i)
                    if (kRendererOrder[i] == values_.renderer) r.index = i;
                // Only Auto names a renderer: what this run picked for it.
                r.choices[0] = std::string(improvements_strings::AUTO) + " (" + backend_title(context_.active) + ")";
            }
            if (r.id == "VSYNC") r.index = values_.vsync ? 0 : 1;
            if (r.id == "WIDESCREEN_FILL") r.index = values_.widescreen_fill ? 0 : 1;
            if (r.id == "RESUME_PROMPT") r.index = values_.resume_prompt ? 0 : 1;
            if (r.id == "UPSCALER") {
                r.enabled = context_.active != Backend::Software;   // off there (render_options.hpp)
                r.index = r.enabled && values_.upscaler ? 1 : 0;
            }
            if (r.id == "FILTER") {
                // Software always draws Sharp (render_options.hpp): show that,
                // greyed out; the saved choice waits for a GPU launch.
                r.enabled = context_.active != Backend::Software;
                r.index = 0;
                for (int i = 0; i < 3 && r.enabled; ++i)
                    if (kFilterOrder[i] == values_.filter) r.index = i;
            }
            if (r.id == "FRAME_LIMIT")
                for (std::size_t i = 0; i < std::size(tt2::FRAME_LIMITS); ++i)
                    if (tt2::FRAME_LIMITS[i] == values_.frame_limit) r.index = static_cast<int>(i);
        }
    }
    // Borderless shows the desktop size, greyed out (Q5); a mode with no
    // size to offer keeps the current one, greyed out.
    void load_resolution_row(Row& r) {
        const std::vector<tt2::Resolution>& sizes = sizes_for(values_.window);
        r.choices.clear();
        r.index = 0;
        if (values_.window == tt2::WindowMode::Borderless || sizes.empty()) {
            const bool borderless = values_.window == tt2::WindowMode::Borderless;
            r.choices.push_back(tt2::to_string(borderless ? context_.display.desktop : values_.resolution));
            r.enabled = false;
            return;
        }
        // What will apply: the saved size, or the closest one this display offers.
        const tt2::Resolution shown = pick_resolution(sizes, values_.resolution);
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            r.choices.push_back(tt2::to_string(sizes[i]));
            if (sizes[i] == shown) r.index = static_cast<int>(i);
        }
        r.enabled = true;
    }

    // One row's index -> values_.
    void store(const Row& r) {
        if (r.id == "WINDOW") {
            values_.set_window(kWindowOrder[r.index]);
            const std::vector<tt2::Resolution>& sizes = sizes_for(values_.window);
            if (!sizes.empty()) values_.resolution = pick_resolution(sizes, values_.resolution);
        }
        if (r.id == "RESOLUTION") {
            const std::vector<tt2::Resolution>& sizes = sizes_for(values_.window);
            if (static_cast<std::size_t>(r.index) < sizes.size()) values_.resolution = sizes[static_cast<std::size_t>(r.index)];
        }
        if (r.id == "RENDERER") values_.renderer = kRendererOrder[r.index];
        if (r.id == "VSYNC") values_.vsync = r.index == 0;
        if (r.id == "WIDESCREEN_FILL") values_.widescreen_fill = r.index == 0;
        if (r.id == "RESUME_PROMPT") values_.resume_prompt = r.index == 0;
        if (r.id == "FILTER") values_.filter = kFilterOrder[r.index];
        if (r.id == "UPSCALER") values_.upscaler = r.index == 1;
        if (r.id == "FRAME_LIMIT") values_.frame_limit = tt2::FRAME_LIMITS[r.index];
    }

    void refresh() {
        load_rows();
        for (const auto& r : rows_) {
            if (TextLabel* v = dialog_.label("VALUE_" + r.id))
                v->text = r.choices.empty() ? std::string() : r.choices[static_cast<std::size_t>(r.index)];
            for (const char* side : {"_PREV", "_NEXT"})
                if (MenuButton* b = dialog_.button("BUTTON_" + r.id + side)) b->enabled = r.enabled;
        }
        for (auto& b : dialog_.buttons) b.sprite.frame = b.frame_for_state();
    }

    void sound(SoundId id) { if (services_.play_sound) services_.play_sound(id); }

    tt2::Improvements saved_;
    tt2::Improvements values_;
    Services services_;
    Context context_;
    Dialog dialog_;
    std::optional<Dialog> notice_;   // "Restart needed", over the dialog
    std::vector<Row> rows_;
};

}  // namespace tt2ui
