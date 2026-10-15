// IMPROVEMENTS, headless: the port's own display options
// (docs/design/improve-ui-ux.md). Nothing here exists in the original
// game; the dialog is deviation UX-2, its file improvements.txt UX-4.
//
// It reuses the DIALOG_SETTINGS building blocks (Dialog, MenuButton,
// TextLabel), so the renderer draws it like any other dialog. Each row is
// a cycle control: a label, the value text, and left / right arrows that
// step through the choices (wrapping round). A row can be disabled, with
// a reason line under it, and any row can carry a note line (the RENDERER
// row names the renderer in use). OK keeps the edited values and CANCEL restores
// the saved ones, the same contract as SETTINGS; the host owns saving and
// applying them.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "tt2/improvements.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/render_backend.hpp"

namespace tt2ui {

namespace improvements_strings {
inline constexpr const char* HEADER = "IMPROVEMENTS";
inline constexpr const char* WINDOW = "DISPLAY MODE";
inline constexpr const char* WINDOWED = "Windowed";
inline constexpr const char* BORDERLESS = "Borderless";
inline constexpr const char* RENDERER = "RENDERER";
inline constexpr const char* AUTO = "Auto";
inline constexpr const char* USING = "Using ";
inline constexpr const char* AFTER_RESTART = ", applies after a restart";
}  // namespace improvements_strings

// Layout, dialog-local. Rows start under the header; OK / CANCEL sit 83 px
// above the bottom edge like SETTINGS (y 357 of 440).
constexpr int IMPROVEMENTS_W = 520;
constexpr float IMPROVEMENTS_ROWS_TOP = 96.0f;
constexpr int IMPROVEMENTS_ROW_H = 46;
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
    };

    // One cycle row. `id` names its buttons and labels: LABEL_<id>,
    // VALUE_<id>, REASON_<id>, NOTE_<id>, BUTTON_<id>_PREV, BUTTON_<id>_NEXT.
    struct Row {
        std::string id;
        std::string title;
        std::vector<std::string> choices;
        int index = 0;
        bool enabled = true;
        std::string reason;   // shown while disabled
        std::string note;     // shown while enabled
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

    // ---- input ----------------------------------------------------------
    void mouse_move(float x, float y) { dialog_mouse_move(dialog_, x, y); }
    void mouse_down(float x, float y) { dialog_mouse_down(dialog_, x, y); }
    Result mouse_up(float x, float y) { return press(dialog_mouse_up(dialog_, x, y)); }

    // A button by name (mouse, keys, tests). Unknown, hidden or disabled
    // buttons do nothing.
    Result press(const std::string& name) {
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return Result::None;
        sound(SoundId::Button);
        if (name == "BUTTON_OK") return Result::Ok;
        if (name == "BUTTON_CANCEL") {
            values_ = saved_;
            refresh();
            return Result::Cancel;
        }
        for (auto& r : rows_) {
            const int step = name == "BUTTON_" + r.id + "_NEXT" ? 1 : name == "BUTTON_" + r.id + "_PREV" ? -1 : 0;
            if (step == 0 || !r.enabled || r.choices.empty()) continue;
            const int n = static_cast<int>(r.choices.size());
            r.index = (r.index + step + n) % n;
            store(r);
            refresh();
        }
        return Result::None;
    }

    // A row the current renderer or display can't offer (later phases):
    // its arrows stop working and `reason` shows under it.
    void set_row_enabled(const std::string& id, bool enabled, const std::string& reason = {}) {
        for (auto& r : rows_)
            if (r.id == id) {
                r.enabled = enabled;
                r.reason = reason;
            }
        refresh();
    }

    // The window mode changed outside the dialog (Alt+Enter): show it, and
    // keep it on CANCEL and OK. Silent, like SettingsDialog::sync_fullscreen.
    void sync_window(tt2::WindowMode m) {
        saved_.window = m;
        values_.window = m;
        refresh();
    }

    void tick(int dt_ms) { dialog_tick(dialog_, dt_ms); }
    bool pointer_hot() const { return dialog_pointer_hot(dialog_); }

private:
    void build() {
        rows_.push_back(Row{"WINDOW", improvements_strings::WINDOW,
                            {improvements_strings::WINDOWED, improvements_strings::BORDERLESS}, 0, true, {}, {}});
        rows_.push_back(Row{"RENDERER", improvements_strings::RENDERER,
                            {improvements_strings::AUTO, backend_title(Backend::Vulkan), backend_title(Backend::OpenGL),
                             backend_title(Backend::Software)},
                            0, true, {}, {}});
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
            d.labels.push_back(TextLabel{"REASON_" + r.id, "FONT_SETTINGSTEXT", r.reason, IMPROVEMENTS_LABEL_X,
                                         cy + 22.0f, TextAlign::Left, Anchor::Center, -1});
            d.labels.push_back(TextLabel{"NOTE_" + r.id, "FONT_SETTINGSTEXT", r.note, IMPROVEMENTS_LABEL_X,
                                         cy + 22.0f, TextAlign::Left, Anchor::Center, -1});
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

    // values_ -> row indices and notes.
    void load_rows() {
        for (auto& r : rows_) {
            if (r.id == "WINDOW") r.index = values_.window == tt2::WindowMode::Borderless ? 1 : 0;
            if (r.id == "RENDERER") {
                for (int i = 0; i < 4; ++i)
                    if (kRendererOrder[i] == values_.renderer) r.index = i;
                r.note = std::string(improvements_strings::USING) + backend_title(context_.active) +
                         (values_.renderer != context_.launched ? improvements_strings::AFTER_RESTART : "");
            }
        }
    }
    // One row's index -> values_.
    void store(const Row& r) {
        if (r.id == "WINDOW") values_.window = r.index == 1 ? tt2::WindowMode::Borderless : tt2::WindowMode::Windowed;
        if (r.id == "RENDERER") values_.renderer = kRendererOrder[r.index];
    }

    void refresh() {
        load_rows();
        for (const auto& r : rows_) {
            if (TextLabel* v = dialog_.label("VALUE_" + r.id))
                v->text = r.choices.empty() ? std::string() : r.choices[static_cast<std::size_t>(r.index)];
            if (TextLabel* why = dialog_.label("REASON_" + r.id)) {
                why->text = r.reason;
                why->visible = !r.enabled && !r.reason.empty();
            }
            if (TextLabel* note = dialog_.label("NOTE_" + r.id)) {
                note->text = r.note;
                note->visible = r.enabled && !r.note.empty();
            }
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
    std::vector<Row> rows_;
};

}  // namespace tt2ui
