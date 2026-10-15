// IMPROVEMENTS: rows, arrows, OK / CANCEL (improvements_dialog.hpp; deviation UX-2).
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/improvements_dialog.hpp"

using namespace tt2ui;

namespace {
struct Rig {
    std::vector<SoundId> sounds;
    ImprovementsDialog::Services services() {
        return {[this](SoundId id) { sounds.push_back(id); }};
    }
};
tt2::Improvements windowed() { return tt2::Improvements{}; }
std::string value(const ImprovementsDialog& d, const char* row) {
    return d.dialog().label(std::string("VALUE_") + row)->text;
}
}  // namespace

TEST(improvements_dialog_has_the_display_mode_row) {
    Rig r;
    const ImprovementsDialog d(windowed(), r.services());
    const Dialog& g = d.dialog();
    ASSERT_EQ(g.label("IMPROVEMENTS_HEADER")->text, std::string("IMPROVEMENTS"));
    ASSERT_EQ(g.label("LABEL_WINDOW")->text, std::string("DISPLAY MODE"));
    ASSERT_EQ(value(d, "WINDOW"), std::string("Windowed"));
    ASSERT_TRUE(g.button("BUTTON_WINDOW_PREV") != nullptr);
    ASSERT_TRUE(g.button("BUTTON_WINDOW_NEXT") != nullptr);
    ASSERT_EQ(g.button("BUTTON_WINDOW_NEXT")->sprite.sheet.image_id, std::string("BUTTON_ARROW_RIGHT"));
    ASSERT_EQ(g.button("BUTTON_WINDOW_NEXT")->sprite.sheet.frame_h, IMPROVEMENTS_ARROW);
    ASSERT_TRUE(g.button("BUTTON_OK") != nullptr && g.button("BUTTON_CANCEL") != nullptr);
    ASSERT_TRUE(!g.label("REASON_WINDOW")->visible);
}

TEST(improvements_dialog_starts_from_the_saved_values) {
    Rig r;
    tt2::Improvements imp;
    imp.window = tt2::WindowMode::Borderless;
    const ImprovementsDialog d(imp, r.services());
    ASSERT_EQ(value(d, "WINDOW"), std::string("Borderless"));
    ASSERT_EQ(d.row("WINDOW")->index, 1);
}

TEST(improvements_dialog_arrows_cycle_and_wrap) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    ASSERT_TRUE(d.press("BUTTON_WINDOW_NEXT") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
    ASSERT_EQ(value(d, "WINDOW"), std::string("Borderless"));
    d.press("BUTTON_WINDOW_NEXT");                                       // wraps round
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
    d.press("BUTTON_WINDOW_PREV");                                       // and backwards
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
    ASSERT_EQ(r.sounds.size(), std::size_t{3});
}

TEST(improvements_dialog_ok_keeps_the_new_value) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    d.press("BUTTON_WINDOW_NEXT");
    ASSERT_TRUE(d.press("BUTTON_OK") == ImprovementsDialog::Result::Ok);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
}

TEST(improvements_dialog_cancel_restores_the_saved_value) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    d.press("BUTTON_WINDOW_NEXT");
    ASSERT_TRUE(d.press("BUTTON_CANCEL") == ImprovementsDialog::Result::Cancel);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
    ASSERT_EQ(value(d, "WINDOW"), std::string("Windowed"));
}

TEST(improvements_dialog_disabled_row_ignores_its_arrows) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    d.set_row_enabled("WINDOW", false, "Not with this renderer");
    ASSERT_TRUE(!d.dialog().button("BUTTON_WINDOW_NEXT")->enabled);
    ASSERT_TRUE(d.press("BUTTON_WINDOW_NEXT") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
    ASSERT_TRUE(r.sounds.empty());
    const TextLabel* why = d.dialog().label("REASON_WINDOW");
    ASSERT_TRUE(why->visible);
    ASSERT_EQ(why->text, std::string("Not with this renderer"));
    d.set_row_enabled("WINDOW", true);
    ASSERT_TRUE(!d.dialog().label("REASON_WINDOW")->visible);
    d.press("BUTTON_WINDOW_NEXT");
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
}

TEST(improvements_dialog_unknown_button_does_nothing) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    ASSERT_TRUE(d.press("BUTTON_DOES_NOT_EXIST") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.press("") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(r.sounds.empty());
    ASSERT_TRUE(d.values() == windowed());
}

TEST(improvements_dialog_mouse_click_on_an_arrow) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    const MenuButton* next = d.dialog().button("BUTTON_WINDOW_NEXT");
    const float x = d.dialog().left() + next->sprite.left() + IMPROVEMENTS_ARROW / 2.0f;
    const float y = d.dialog().top() + next->sprite.top() + IMPROVEMENTS_ARROW / 2.0f;
    d.mouse_move(x, y);
    ASSERT_TRUE(d.pointer_hot());
    d.mouse_down(x, y);
    ASSERT_TRUE(d.mouse_up(x, y) == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
    d.mouse_move(0, 0);                                                  // outside: nothing hot
    ASSERT_TRUE(!d.pointer_hot());
}

TEST(improvements_dialog_sync_window_survives_cancel) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    d.sync_window(tt2::WindowMode::Borderless);                          // Alt+Enter while open
    ASSERT_EQ(value(d, "WINDOW"), std::string("Borderless"));
    ASSERT_TRUE(r.sounds.empty());                                       // silent
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);      // not reverted
}

TEST(improvements_dialog_fits_the_screen_with_every_planned_row) {
    // Later phases add rows (resolution, renderer, frame rate, ...): at
    // least 8 of them must fit inside 800x600.
    ASSERT_TRUE(improvements_dialog_height(8) <= SCREEN_H);
    Rig r;
    const ImprovementsDialog d(windowed(), r.services());
    ASSERT_TRUE(d.dialog().w <= SCREEN_W && d.dialog().h <= SCREEN_H);
    ASSERT_TRUE(d.dialog().left() >= 0 && d.dialog().top() >= 0);
    for (const MenuButton& b : d.dialog().buttons) {
        ASSERT_TRUE(b.sprite.left() >= 0 && b.sprite.top() >= 0);
        ASSERT_TRUE(b.sprite.left() + b.sprite.sheet.frame_w <= d.dialog().w);
        ASSERT_TRUE(b.sprite.top() + b.sprite.sheet.frame_h <= d.dialog().h);
    }
}

// ---- RENDERER (P3, deviation UX-1) --------------------------------------

namespace {
ImprovementsDialog::Context running(Backend active, tt2::RendererPref launched) {
    ImprovementsDialog::Context c;
    c.active = active;
    c.launched = launched;
    return c;
}
std::string note(const ImprovementsDialog& d) { return d.dialog().label("NOTE_RENDERER")->text; }
}  // namespace

TEST(improvements_renderer_row_cycles_four_choices) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    ASSERT_EQ(value(d, "RENDERER"), std::string("Auto"));
    const char* expected[] = {"Vulkan", "OpenGL", "Software", "Auto"};
    for (const char* e : expected) {
        d.press("BUTTON_RENDERER_NEXT");
        ASSERT_EQ(value(d, "RENDERER"), std::string(e));
    }
    d.press("BUTTON_RENDERER_PREV");                                     // wraps backwards
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Software);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);        // the other row is untouched
}

TEST(improvements_renderer_note_names_the_renderer_in_use) {
    Rig r;
    const ImprovementsDialog d(windowed(), r.services(), running(Backend::Vulkan, tt2::RendererPref::Auto));
    ASSERT_TRUE(d.dialog().label("NOTE_RENDERER")->visible);
    ASSERT_EQ(note(d), std::string("Using Vulkan"));                    // no restart note: nothing changed
}

TEST(improvements_renderer_note_asks_for_a_restart_only_after_a_change) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    d.press("BUTTON_RENDERER_NEXT");                                     // Vulkan
    ASSERT_EQ(note(d), std::string("Using OpenGL, applies after a restart"));
    d.press("BUTTON_RENDERER_PREV");                                     // back to what this run started with
    ASSERT_EQ(note(d), std::string("Using OpenGL"));
}

TEST(improvements_renderer_note_compares_with_the_launch_not_the_file) {
    // --renderer software overrides an "auto" file for one run: the saved
    // choice already differs from what is running.
    Rig r;
    const ImprovementsDialog d(windowed(), r.services(), running(Backend::Software, tt2::RendererPref::Software));
    ASSERT_EQ(note(d), std::string("Using Software, applies after a restart"));
}

TEST(improvements_renderer_cancel_restores_the_saved_choice) {
    Rig r;
    tt2::Improvements saved;
    saved.renderer = tt2::RendererPref::OpenGL;
    ImprovementsDialog d(saved, r.services(), running(Backend::OpenGL, tt2::RendererPref::OpenGL));
    d.press("BUTTON_RENDERER_NEXT");
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Software);
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::OpenGL);
    ASSERT_EQ(value(d, "RENDERER"), std::string("OpenGL"));
}

// ---- VSYNC and FRAME LIMIT (P4, deviation UX-7) --------------------------

TEST(improvements_vsync_row_starts_on_and_toggles) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    ASSERT_EQ(d.dialog().label("LABEL_VSYNC")->text, std::string("VSYNC"));
    ASSERT_EQ(value(d, "VSYNC"), std::string("On"));                    // the original presents with vsync
    d.press("BUTTON_VSYNC_NEXT");
    ASSERT_TRUE(!d.values().vsync);
    ASSERT_EQ(value(d, "VSYNC"), std::string("Off"));
    d.press("BUTTON_VSYNC_PREV");
    ASSERT_TRUE(d.values().vsync);
}

TEST(improvements_frame_limit_row_cycles_the_offered_limits) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    ASSERT_EQ(d.dialog().label("LABEL_FRAME_LIMIT")->text, std::string("FRAME LIMIT"));
    ASSERT_EQ(value(d, "FRAME_LIMIT"), std::string("Unlimited"));
    const char* expected[] = {"60 fps", "120 fps", "144 fps", "165 fps", "240 fps", "Unlimited"};
    const int fps[] = {60, 120, 144, 165, 240, 0};
    for (int i = 0; i < 6; ++i) {
        d.press("BUTTON_FRAME_LIMIT_NEXT");
        ASSERT_EQ(value(d, "FRAME_LIMIT"), std::string(expected[i]));
        ASSERT_EQ(d.values().frame_limit, fps[i]);
    }
    d.press("BUTTON_FRAME_LIMIT_PREV");                                  // wraps backwards
    ASSERT_EQ(d.values().frame_limit, 240);
}

TEST(improvements_frame_limit_starts_from_the_saved_value_and_cancels) {
    Rig r;
    tt2::Improvements saved;
    saved.vsync = false;
    saved.frame_limit = 144;
    ImprovementsDialog d(saved, r.services());
    ASSERT_EQ(value(d, "VSYNC"), std::string("Off"));
    ASSERT_EQ(value(d, "FRAME_LIMIT"), std::string("144 fps"));
    d.press("BUTTON_FRAME_LIMIT_NEXT");
    d.press("BUTTON_VSYNC_NEXT");
    ASSERT_EQ(d.values().frame_limit, 165);
    ASSERT_TRUE(d.values().vsync);
    d.press("BUTTON_CANCEL");
    ASSERT_EQ(d.values().frame_limit, 144);
    ASSERT_TRUE(!d.values().vsync);
}

TEST(improvements_vsync_row_can_be_disabled) {
    // A renderer that refuses vsync: the host disables the row.
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    d.set_row_enabled("VSYNC", false, improvements_strings::NO_VSYNC);
    d.press("BUTTON_VSYNC_NEXT");
    ASSERT_TRUE(d.values().vsync);                                       // unchanged
    ASSERT_TRUE(d.dialog().label("REASON_VSYNC")->visible);
    ASSERT_EQ(d.values().frame_limit, 0);                                // the limit row still works
    d.press("BUTTON_FRAME_LIMIT_NEXT");
    ASSERT_EQ(d.values().frame_limit, 60);
}
