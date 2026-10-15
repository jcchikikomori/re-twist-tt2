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
    ASSERT_TRUE(g.label("REASON_WINDOW") == nullptr);                  // rows are one line
    ASSERT_TRUE(g.label("NOTE_WINDOW") == nullptr);
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
    d.set_row_enabled("WINDOW", false);
    ASSERT_TRUE(!d.dialog().button("BUTTON_WINDOW_NEXT")->enabled);
    ASSERT_TRUE(d.press("BUTTON_WINDOW_NEXT") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
    ASSERT_EQ(value(d, "WINDOW"), std::string("Windowed"));              // still shown, greyed out
    ASSERT_TRUE(r.sounds.empty());
    d.set_row_enabled("WINDOW", true);
    ASSERT_TRUE(d.dialog().button("BUTTON_WINDOW_NEXT")->enabled);
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
}  // namespace

TEST(improvements_renderer_row_cycles_four_choices) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    ASSERT_EQ(value(d, "RENDERER"), std::string("Auto (OpenGL)"));
    const char* expected[] = {"Vulkan", "OpenGL", "Software", "Auto (OpenGL)"};
    for (const char* e : expected) {
        d.press("BUTTON_RENDERER_NEXT");
        ASSERT_EQ(value(d, "RENDERER"), std::string(e));
    }
    d.press("BUTTON_RENDERER_PREV");                                     // wraps backwards
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Software);
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);        // the other row is untouched
}

TEST(improvements_renderer_auto_names_the_renderer_in_use) {
    Rig r;
    for (Backend b : {Backend::Vulkan, Backend::OpenGL, Backend::Software}) {
        const ImprovementsDialog d(windowed(), r.services(), running(b, tt2::RendererPref::Auto));
        ASSERT_EQ(value(d, "RENDERER"), "Auto (" + std::string(backend_title(b)) + ")");
    }
}

TEST(improvements_renderer_values_stay_single_line) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::Software, tt2::RendererPref::Software));
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(value(d, "RENDERER").find('\n') == std::string::npos);
        d.press("BUTTON_RENDERER_NEXT");
    }
}

TEST(improvements_renderer_names_no_renderer_unless_auto) {
    // A forced choice shows only its own name, even when this run fell
    // back to another renderer.
    Rig r;
    tt2::Improvements saved;
    saved.renderer = tt2::RendererPref::Vulkan;
    const ImprovementsDialog d(saved, r.services(), running(Backend::Software, tt2::RendererPref::Vulkan));
    ASSERT_EQ(value(d, "RENDERER"), std::string("Vulkan"));
}

TEST(improvements_renderer_change_asks_for_a_restart_on_ok) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    d.press("BUTTON_RENDERER_NEXT");                                     // Vulkan
    ASSERT_TRUE(d.notice() == nullptr);                                  // nothing until OK
    ASSERT_TRUE(d.press("BUTTON_OK") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.notice() != nullptr);
    ASSERT_EQ(d.notice()->label("HEADER")->text, std::string("Restart needed"));
    ASSERT_EQ(d.notice()->label("MESSAGE")->text, std::string("The new renderer is used\nafter you restart the game."));
    ASSERT_TRUE(d.notice()->button("BUTTON_OK") != nullptr);
    ASSERT_TRUE(d.notice()->button("BUTTON_CANCEL") == nullptr);         // one button
    ASSERT_TRUE(d.press("BUTTON_OK") == ImprovementsDialog::Result::Ok);  // the notice's OK finishes
    ASSERT_TRUE(d.notice() == nullptr);
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Vulkan);
}

TEST(improvements_restart_notice_esc_still_keeps_the_change) {
    // The player already chose OK; Esc (the hosts' CANCEL) only closes the notice.
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    d.press("BUTTON_RENDERER_NEXT");
    d.press("BUTTON_OK");
    ASSERT_TRUE(d.press("BUTTON_CANCEL") == ImprovementsDialog::Result::Ok);
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Vulkan);
}

TEST(improvements_restart_notice_ignores_the_dialog_under_it) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    d.press("BUTTON_RENDERER_NEXT");                                     // Vulkan
    d.press("BUTTON_OK");
    const std::size_t clicks = r.sounds.size();
    ASSERT_TRUE(d.press("BUTTON_RENDERER_NEXT") == ImprovementsDialog::Result::None);
    ASSERT_TRUE(d.values().renderer == tt2::RendererPref::Vulkan);       // the row didn't move
    ASSERT_EQ(r.sounds.size(), clicks);                                  // and made no sound
    ASSERT_TRUE(d.notice() != nullptr);
}

TEST(improvements_ok_without_a_renderer_change_skips_the_notice) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    d.press("BUTTON_VSYNC_NEXT");                                        // other rows apply now
    ASSERT_TRUE(d.press("BUTTON_OK") == ImprovementsDialog::Result::Ok);
    ASSERT_TRUE(d.notice() == nullptr);
    ImprovementsDialog back(windowed(), r.services(), running(Backend::OpenGL, tt2::RendererPref::Auto));
    back.press("BUTTON_RENDERER_NEXT");
    back.press("BUTTON_RENDERER_PREV");                                  // changed back to Auto
    ASSERT_TRUE(back.press("BUTTON_OK") == ImprovementsDialog::Result::Ok);
    ASSERT_TRUE(back.notice() == nullptr);
}

TEST(improvements_no_notice_for_the_renderer_already_running) {
    // --renderer software over a saved Vulkan: picking Software changes the
    // file, but this run already uses it, so nothing waits for a restart.
    Rig r;
    tt2::Improvements saved;
    saved.renderer = tt2::RendererPref::Vulkan;
    ImprovementsDialog d(saved, r.services(), running(Backend::Software, tt2::RendererPref::Software));
    d.press("BUTTON_RENDERER_NEXT");                                     // OpenGL
    d.press("BUTTON_RENDERER_NEXT");                                     // Software
    ASSERT_TRUE(!d.restart_needed());
    ASSERT_TRUE(d.press("BUTTON_OK") == ImprovementsDialog::Result::Ok);
    ASSERT_TRUE(d.notice() == nullptr);
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
    d.set_row_enabled("VSYNC", false);
    d.press("BUTTON_VSYNC_NEXT");
    ASSERT_TRUE(d.values().vsync);                                       // unchanged
    ASSERT_TRUE(!d.row("VSYNC")->enabled);
    ASSERT_TRUE(!d.dialog().button("BUTTON_VSYNC_NEXT")->enabled);
    ASSERT_EQ(d.values().frame_limit, 0);                                // the limit row still works
    d.press("BUTTON_FRAME_LIMIT_NEXT");
    ASSERT_EQ(d.values().frame_limit, 60);
}

// ---- DISPLAY MODE and RESOLUTION (P5, deviation UX-5) --------------------

namespace {
using tt2::Resolution;
// A 1920x1080 desktop: windowed sizes fit the usable bounds (1920x1040);
// the display has exact modes for 800x600, 1280x720 and 1920x1080.
ImprovementsDialog::Context on_1080p(bool native_failed = false) {
    ImprovementsDialog::Context c;
    c.display.desktop = {1920, 1080};
    c.display.windowed = available_resolutions(1920, 1040);
    c.display.native = {{800, 600}, {1280, 720}, {1920, 1080}};
    c.native_failed = native_failed;
    return c;
}
}  // namespace

TEST(improvements_display_mode_offers_native_when_the_display_has_a_mode) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), on_1080p());
    d.press("BUTTON_WINDOW_NEXT");
    d.press("BUTTON_WINDOW_NEXT");
    ASSERT_EQ(value(d, "WINDOW"), std::string("Native"));
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Native);
    ASSERT_TRUE(d.values().fullscreen_mode == tt2::WindowMode::Native);   // Alt+Enter remembers it
    d.press("BUTTON_WINDOW_NEXT");                                         // wraps round
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
}

TEST(improvements_display_mode_skips_native_without_a_mode) {
    Rig r;
    ImprovementsDialog::Context c = on_1080p();
    c.display.native.clear();
    ImprovementsDialog d(windowed(), r.services(), c);
    d.press("BUTTON_WINDOW_PREV");                                         // Windowed -> (Native skipped)
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Borderless);
    d.press("BUTTON_WINDOW_NEXT");
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
}

TEST(improvements_resolution_row_cycles_the_sizes_that_fit) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), on_1080p());
    ASSERT_EQ(d.dialog().label("LABEL_RESOLUTION")->text, std::string("RESOLUTION"));
    ASSERT_EQ(value(d, "RESOLUTION"), std::string("800x600"));
    // 1920x1040 usable: everything up to 1600x900 (1440x1080 and 1920x1080 are too tall).
    const char* expected[] = {"1024x768", "1280x960", "1280x720", "1600x900", "800x600"};
    for (const char* e : expected) {
        d.press("BUTTON_RESOLUTION_NEXT");
        ASSERT_EQ(value(d, "RESOLUTION"), std::string(e));
    }
    d.press("BUTTON_RESOLUTION_PREV");
    ASSERT_TRUE(d.values().resolution == (Resolution{1600, 900}));
}

TEST(improvements_resolution_row_greys_out_in_borderless) {
    Rig r;
    tt2::Improvements saved;
    saved.resolution = {1280, 720};
    ImprovementsDialog d(saved, r.services(), on_1080p());
    d.press("BUTTON_WINDOW_NEXT");                                         // Borderless
    ASSERT_EQ(value(d, "RESOLUTION"), std::string("1920x1080"));           // the desktop size (Q5)
    ASSERT_TRUE(!d.row("RESOLUTION")->enabled);
    ASSERT_TRUE(!d.dialog().button("BUTTON_RESOLUTION_NEXT")->enabled);
    d.press("BUTTON_RESOLUTION_NEXT");                                     // does nothing
    ASSERT_TRUE(d.values().resolution == (Resolution{1280, 720}));         // kept for a later window
}

TEST(improvements_resolution_shows_what_fits_without_saving_it) {
    // 1920x1080 saved, then opened on a 1366x768 laptop.
    Rig r;
    tt2::Improvements saved;
    saved.resolution = {1920, 1080};
    ImprovementsDialog::Context c;
    c.display.desktop = {1366, 768};
    c.display.windowed = available_resolutions(1366, 728);
    ImprovementsDialog d(saved, r.services(), c);
    ASSERT_EQ(value(d, "RESOLUTION"), std::string("1280x720"));            // the last offered size that fits
    ASSERT_TRUE(d.values().resolution == (Resolution{1920, 1080}));        // OK alone changes nothing
}

TEST(improvements_resolution_row_on_a_tiny_display_keeps_800x600) {
    Rig r;
    ImprovementsDialog::Context c;
    c.display.desktop = {640, 480};
    c.display.windowed = available_resolutions(640, 440);                  // none
    ImprovementsDialog d(windowed(), r.services(), c);
    ASSERT_EQ(value(d, "RESOLUTION"), std::string("800x600"));
    ASSERT_TRUE(!d.row("RESOLUTION")->enabled);
    ASSERT_TRUE(!d.dialog().button("BUTTON_RESOLUTION_NEXT")->enabled);
}

TEST(improvements_switching_to_native_picks_a_native_size) {
    Rig r;
    tt2::Improvements saved;
    saved.resolution = {1600, 900};                                        // no 1600x900 mode
    ImprovementsDialog d(saved, r.services(), on_1080p());
    d.press("BUTTON_WINDOW_PREV");                                         // Native
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Native);
    ASSERT_TRUE(d.values().resolution == (Resolution{1280, 720}));
    const char* expected[] = {"1920x1080", "800x600"};
    for (const char* e : expected) {
        d.press("BUTTON_RESOLUTION_NEXT");
        ASSERT_EQ(value(d, "RESOLUTION"), std::string(e));
    }
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Windowed);
    ASSERT_TRUE(d.values().resolution == (Resolution{1600, 900}));
}

TEST(improvements_native_failure_shows_in_the_value) {
    // The run fell back to Borderless: the saved choice stays Native, and
    // its value says it failed, like "Auto (OpenGL)" says what Auto runs.
    Rig r;
    tt2::Improvements saved;
    saved.set_window(tt2::WindowMode::Native);
    const ImprovementsDialog d(saved, r.services(), on_1080p(true));
    ASSERT_EQ(value(d, "WINDOW"), std::string("Native (failed)"));
    ASSERT_TRUE(d.values().window == tt2::WindowMode::Native);
    const ImprovementsDialog fine(saved, r.services(), on_1080p(false));
    ASSERT_EQ(value(fine, "WINDOW"), std::string("Native"));
}

TEST(improvements_rows_are_one_line_even_when_greyed_out) {
    // The worst case: software renderer, Native failed, vsync refused, and
    // a display too small for any offered size. Every row is its label
    // and its value; nothing sits under it.
    Rig r;
    tt2::Improvements saved;
    saved.set_window(tt2::WindowMode::Native);
    ImprovementsDialog::Context c;
    c.active = Backend::Software;
    c.native_failed = true;
    c.display.desktop = {640, 480};
    ImprovementsDialog d(saved, r.services(), c);
    d.set_row_enabled("VSYNC", false);
    ASSERT_TRUE(!d.row("FILTER")->enabled && !d.row("UPSCALER")->enabled && !d.row("RESOLUTION")->enabled);
    ASSERT_EQ(d.dialog().labels.size(), 1 + 2 * d.rows().size());       // the header, then label + value
    for (const TextLabel& l : d.dialog().labels)
        ASSERT_TRUE(l.text.find('\n') == std::string::npos);
}

TEST(improvements_sync_window_remembers_the_fullscreen_mode) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), on_1080p());
    d.sync_window(tt2::WindowMode::Native);                               // Alt+Enter while open
    ASSERT_EQ(value(d, "WINDOW"), std::string("Native"));
    ASSERT_TRUE(d.values().fullscreen_mode == tt2::WindowMode::Native);
}

// ---- WIDESCREEN FILL (P6, deviation UX-6) ---------------------------------

TEST(improvements_widescreen_fill_row_starts_on_and_toggles) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services());
    ASSERT_EQ(d.dialog().label("LABEL_WIDESCREEN_FILL")->text, std::string("WIDESCREEN FILL"));
    ASSERT_EQ(value(d, "WIDESCREEN_FILL"), std::string("On"));
    d.press("BUTTON_WIDESCREEN_FILL_NEXT");
    ASSERT_TRUE(!d.values().widescreen_fill);
    ASSERT_EQ(value(d, "WIDESCREEN_FILL"), std::string("Off"));
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().widescreen_fill);
}

TEST(improvements_widescreen_fill_works_on_every_renderer) {
    // One blit per bar, so it is not an enhancement: the software renderer keeps it.
    Rig r;
    ImprovementsDialog::Context c;
    c.active = Backend::Software;
    tt2::Improvements saved;
    saved.widescreen_fill = false;
    ImprovementsDialog d(saved, r.services(), c);
    ASSERT_TRUE(d.row("WIDESCREEN_FILL")->enabled);
    d.press("BUTTON_WIDESCREEN_FILL_PREV");
    ASSERT_TRUE(d.values().widescreen_fill);
}

// ---- TEXTURE FILTER (P7, deviation UX-8) ----------------------------------

namespace {
ImprovementsDialog::Context on(Backend b) {
    ImprovementsDialog::Context c;
    c.active = b;
    return c;
}
}  // namespace

TEST(improvements_filter_row_cycles_on_a_gpu) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), on(Backend::Vulkan));
    ASSERT_EQ(d.dialog().label("LABEL_FILTER")->text, std::string("TEXTURE FILTER"));
    ASSERT_EQ(value(d, "FILTER"), std::string("Sharp"));
    const char* expected[] = {"Smooth", "Pixel art", "Sharp"};
    for (const char* e : expected) {
        d.press("BUTTON_FILTER_NEXT");
        ASSERT_EQ(value(d, "FILTER"), std::string(e));
    }
    d.press("BUTTON_FILTER_PREV");
    ASSERT_TRUE(d.values().filter == tt2::TextureFilter::PixelArt);
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().filter == tt2::TextureFilter::Sharp);
}

TEST(improvements_filter_row_is_greyed_out_on_software) {
    Rig r;
    tt2::Improvements saved;
    saved.filter = tt2::TextureFilter::Smooth;                         // chosen on a GPU launch
    ImprovementsDialog d(saved, r.services(), on(Backend::Software));
    ASSERT_TRUE(!d.row("FILTER")->enabled);
    ASSERT_EQ(value(d, "FILTER"), std::string("Sharp"));                 // what software draws
    ASSERT_TRUE(!d.dialog().button("BUTTON_FILTER_NEXT")->enabled);
    d.press("BUTTON_FILTER_NEXT");
    ASSERT_TRUE(d.values().filter == tt2::TextureFilter::Smooth);      // kept, not overwritten
}

TEST(improvements_dialog_fits_with_eight_rows) {
    Rig r;
    const ImprovementsDialog d(windowed(), r.services(), on(Backend::OpenGL));
    ASSERT_EQ(d.rows().size(), std::size_t{8});
    ASSERT_TRUE(d.dialog().h <= SCREEN_H);
}

// ---- UPSCALER (P9, deviation UX-9) -----------------------------------------

TEST(improvements_upscaler_row_toggles_on_a_gpu) {
    Rig r;
    ImprovementsDialog d(windowed(), r.services(), on(Backend::Vulkan));
    ASSERT_EQ(value(d, "UPSCALER"), std::string("Off"));
    d.press("BUTTON_UPSCALER_NEXT");
    ASSERT_TRUE(d.values().upscaler);
    ASSERT_EQ(value(d, "UPSCALER"), std::string("Super-xBR"));
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(!d.values().upscaler);
}

TEST(improvements_upscaler_row_is_greyed_out_on_software) {
    Rig r;
    tt2::Improvements saved;
    saved.upscaler = true;
    ImprovementsDialog d(saved, r.services(), on(Backend::Software));
    ASSERT_TRUE(!d.row("UPSCALER")->enabled);
    ASSERT_EQ(value(d, "UPSCALER"), std::string("Off"));                // what software does
    d.press("BUTTON_UPSCALER_NEXT");
    ASSERT_TRUE(d.values().upscaler);                                    // the saved choice is kept
}
