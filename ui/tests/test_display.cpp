// Window sizes and the 4:3 game rectangle (display.hpp; deviation UX-5).
#include "test_harness.hpp"

#include <vector>

#include "tt2ui/display.hpp"

using namespace tt2ui;
using tt2::Resolution;

TEST(fit_4x3_is_the_identity_at_800x600) {
    const Fit4x3 f = fit_4x3(800, 600);
    ASSERT_TRUE(f.game.x == 0.0f && f.game.y == 0.0f && f.game.w == 800.0f && f.game.h == 600.0f);
    ASSERT_TRUE(f.scale == 1.0f);
    ASSERT_TRUE(pillar_bars(800, 600).left.empty() && pillar_bars(800, 600).right.empty());
}

TEST(fit_4x3_at_1080p_leaves_240_pixel_bars) {
    // scale = min(1920 / 800, 1080 / 600) = 1.8: a 1440x1080 game at x = 240.
    const Fit4x3 f = fit_4x3(1920, 1080);
    ASSERT_TRUE(f.game.x == 240.0f && f.game.y == 0.0f && f.game.w == 1440.0f && f.game.h == 1080.0f);
    ASSERT_TRUE(f.scale == 1.8f);
    const PillarBars b = pillar_bars(1920, 1080);
    ASSERT_TRUE(b.left.x == 0.0f && b.left.w == 240.0f && b.left.h == 1080.0f);
    ASSERT_TRUE(b.right.x == 1680.0f && b.right.w == 240.0f);
}

TEST(fit_4x3_on_a_steam_deck_floors_like_sdl) {
    // 1280x800: scale 800 / 600, 1066.67 floored to 1066, so both bars are 107.
    const Fit4x3 f = fit_4x3(1280, 800);
    ASSERT_TRUE(f.game.w == 1066.0f && f.game.h == 800.0f && f.game.x == 107.0f);
    const PillarBars b = pillar_bars(1280, 800);
    ASSERT_TRUE(b.left.w == 107.0f && b.right.x == 1173.0f && b.right.w == 107.0f);
}

TEST(fit_4x3_keeps_a_half_pixel_offset) {
    // 1281x720: 960 wide, x = 160.5, as SDL centres it.
    const Fit4x3 f = fit_4x3(1281, 720);
    ASSERT_TRUE(f.game.w == 960.0f && f.game.x == 160.5f);
}

TEST(fit_4x3_of_a_taller_output_letterboxes_without_pillars) {
    const Fit4x3 f = fit_4x3(1280, 1024);   // 5:4
    ASSERT_TRUE(f.game.x == 0.0f && f.game.w == 1280.0f && f.game.h == 960.0f && f.game.y == 32.0f);
    const PillarBars b = pillar_bars(1280, 1024);
    ASSERT_TRUE(b.left.empty() && b.right.empty());
}

TEST(fit_4x3_of_nothing_is_empty) {
    ASSERT_TRUE(fit_4x3(0, 0).game.empty());
    ASSERT_TRUE(fit_4x3(0, 0).scale == 0.0f);
    ASSERT_TRUE(fit_4x3(1920, -1).game.empty());
    ASSERT_TRUE(pillar_bars(0, 0).left.empty());
}

TEST(available_resolutions_on_a_1920x1200_display_offers_all_seven) {
    ASSERT_EQ(available_resolutions(1920, 1200).size(), std::size_t{7});
}

TEST(available_resolutions_on_a_1366x768_display) {
    const std::vector<Resolution> r = available_resolutions(1366, 768);
    ASSERT_EQ(r.size(), std::size_t{3});
    ASSERT_TRUE(r[0] == (Resolution{800, 600}) && r[1] == (Resolution{1024, 768}) && r[2] == (Resolution{1280, 720}));
}

TEST(available_resolutions_on_a_640x480_display_offers_none) {
    ASSERT_TRUE(available_resolutions(640, 480).empty());
    ASSERT_TRUE(available_resolutions(0, 0).empty());
}

TEST(pick_resolution_keeps_an_offered_size) {
    const std::vector<Resolution> r = available_resolutions(1920, 1080);
    ASSERT_TRUE(pick_resolution(r, Resolution{1600, 900}) == (Resolution{1600, 900}));
}

TEST(pick_resolution_steps_down_to_what_fits) {
    // A 1920x1080 window saved, then a 1366x768 laptop: the largest that fits.
    const std::vector<Resolution> r = available_resolutions(1366, 768);
    ASSERT_TRUE(pick_resolution(r, Resolution{1920, 1080}) == (Resolution{1280, 720}));
    ASSERT_TRUE(pick_resolution(r, Resolution{1440, 1080}) == (Resolution{1280, 720}));   // 921,600 > 786,432 px
    ASSERT_TRUE(pick_resolution(r, Resolution{1024, 600}) == (Resolution{800, 600}));     // 1024x768 is too tall
}

TEST(pick_resolution_with_nothing_offered_is_800x600) {
    ASSERT_TRUE(pick_resolution({}, Resolution{1920, 1080}) == Resolution{});
    // Nothing smaller offered: the first one.
    ASSERT_TRUE(pick_resolution({Resolution{1280, 720}}, Resolution{1024, 768}) == (Resolution{1280, 720}));
}

// Android is always full screen (T1.4): the saved mode is ignored, the file keeps it.
TEST(display_for_android_is_always_borderless) {
    for (tt2::WindowMode m : {tt2::WindowMode::Windowed, tt2::WindowMode::Borderless, tt2::WindowMode::Native}) {
        tt2::Improvements imp;
        imp.window = m;
        imp.resolution = {1024, 768};
        const tt2::Improvements out = display_for_platform(imp, Platform::Android);
        ASSERT_TRUE(out.window == tt2::WindowMode::Borderless);
        ASSERT_TRUE(out.resolution == imp.resolution);   // untouched; Borderless never reads it
        ASSERT_TRUE(out.renderer == imp.renderer && out.vsync == imp.vsync);
    }
    ASSERT_TRUE(!window_modes_offered(Platform::Android));
}

TEST(display_for_desktop_keeps_the_saved_mode) {
    for (tt2::WindowMode m : {tt2::WindowMode::Windowed, tt2::WindowMode::Borderless, tt2::WindowMode::Native}) {
        tt2::Improvements imp;
        imp.window = m;
        ASSERT_TRUE(display_for_platform(imp, Platform::Desktop).window == m);
    }
    ASSERT_TRUE(window_modes_offered(Platform::Desktop));
}

// Esc on the menu (help-and-settings.md §3.4): the exe's 0x484770 only sends
// the window to windowed mode; it never quits.
TEST(menu_escape_on_desktop_leaves_borderless_for_a_window) {
    ASSERT_TRUE(menu_escape(Platform::Desktop, tt2::WindowMode::Borderless) == MenuEscape::LeaveFullscreen);
}

TEST(menu_escape_on_desktop_leaves_native_for_a_window) {
    ASSERT_TRUE(menu_escape(Platform::Desktop, tt2::WindowMode::Native) == MenuEscape::LeaveFullscreen);
}

TEST(menu_escape_on_a_desktop_window_does_nothing) {
    // 0x483c30 returns early when the mode is unchanged: no quit, no switch.
    ASSERT_TRUE(menu_escape(Platform::Desktop, tt2::WindowMode::Windowed) == MenuEscape::Nothing);
}

TEST(menu_escape_on_android_goes_to_the_background) {
    // Back arrives as Esc; the game is always full screen there, whatever the file says.
    ASSERT_TRUE(menu_escape(Platform::Android, tt2::WindowMode::Borderless) == MenuEscape::Background);
    ASSERT_TRUE(menu_escape(Platform::Android, tt2::WindowMode::Windowed) == MenuEscape::Background);
    ASSERT_TRUE(menu_escape(Platform::Android, tt2::WindowMode::Native) == MenuEscape::Background);
}

TEST(menu_escape_then_alt_enter_brings_the_same_fullscreen_back) {
    // The host applies LeaveFullscreen as set_window(Windowed), which keeps
    // the last fullscreen mode for Alt+Enter (UX-5).
    tt2::Improvements imp;
    imp.set_window(tt2::WindowMode::Native);
    ASSERT_TRUE(menu_escape(Platform::Desktop, imp.window) == MenuEscape::LeaveFullscreen);
    imp.set_window(tt2::WindowMode::Windowed);
    ASSERT_TRUE(!imp.legacy_fullscreen());   // settings.txt's fullscreen=0
    ASSERT_TRUE(menu_escape(Platform::Desktop, imp.window) == MenuEscape::Nothing);
    ASSERT_TRUE(imp.toggled_window() == tt2::WindowMode::Native);
}

// Phones and tablets (T2.3): the worked examples from the Android plan. In
// landscape a phone's camera cutout sits on a short edge, inside a pillar
// bar: any phone wider than 4:3 has bars of at least 1/8 of its width, far
// deeper than a punch-hole (SDL draws into the cutout, CUTOUT_MODE_ALWAYS).
TEST(fit_4x3_on_a_20_by_9_phone_leaves_480_pixel_bars) {
    const Fit4x3 f = fit_4x3(2400, 1080);
    ASSERT_EQ(f.scale, 1.8f);
    ASSERT_EQ(f.game.x, 480.0f);
    ASSERT_EQ(f.game.w, 1440.0f);
    ASSERT_EQ(f.game.h, 1080.0f);
    const PillarBars b = pillar_bars(2400, 1080);
    ASSERT_EQ(b.left.w, 480.0f);
    ASSERT_EQ(b.right.x, 1920.0f);
    ASSERT_EQ(b.right.w, 480.0f);
}

TEST(fit_4x3_on_a_16_by_10_tablet_keeps_the_half_pixel) {
    const Fit4x3 f = fit_4x3(2560, 1600);
    ASSERT_EQ(f.game.w, 2133.0f);                          // 2133.33 floored
    ASSERT_EQ(f.game.x, 213.5f);
    ASSERT_EQ(pillar_bars(2560, 1600).right.w, 213.5f);
}

TEST(fit_4x3_on_the_test_tablet_matches_the_device_log) {
    // Galaxy Tab A7 Lite, full screen: "game 137,0 1066x800".
    const Fit4x3 f = fit_4x3(1340, 800);
    ASSERT_EQ(f.game.x, 137.0f);
    ASSERT_EQ(f.game.w, 1066.0f);
}
