// Improvements and improvements.txt (improvements.hpp; docs/design/improve-ui-ux.md).
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "tt2/improvements.hpp"
#include "tt2/profile_store.hpp"

using namespace tt2;

namespace {
std::filesystem::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = std::filesystem::temp_directory_path() /
                   ("tt2_improvements_test_" + std::string(tag) + "_" + std::to_string(rd()));
    std::filesystem::remove_all(p);
    return p;
}
}  // namespace

TEST(improvements_default_to_a_window) {
    ASSERT_TRUE(Improvements{}.window == WindowMode::Windowed);
    ASSERT_TRUE(!Improvements{}.legacy_fullscreen());
}

TEST(improvements_text_round_trip_defaults) {
    const std::string text = serialize(Improvements{});
    ASSERT_EQ(text, std::string("format=re-twist-tt2 improvements\nversion=1\nwindow=windowed\nrenderer=auto\nvsync=1\nframe_limit=0\n"));
    Improvements back;
    back.window = WindowMode::Borderless;
    ASSERT_TRUE(parse(text, back));
    ASSERT_TRUE(back == Improvements{});
}

TEST(improvements_text_round_trip_borderless) {
    Improvements imp;
    imp.window = WindowMode::Borderless;
    Improvements back;
    ASSERT_TRUE(parse(serialize(imp), back));
    ASSERT_TRUE(back.window == WindowMode::Borderless);
    ASSERT_TRUE(back.legacy_fullscreen());
}

TEST(improvements_text_missing_key_keeps_its_value) {
    Improvements imp;
    imp.window = WindowMode::Borderless;
    ASSERT_TRUE(parse("format=re-twist-tt2 improvements\nversion=1\n", imp));
    ASSERT_TRUE(imp.window == WindowMode::Borderless);
}

TEST(improvements_text_accepts_crlf_and_comments) {
    Improvements imp;
    ASSERT_TRUE(parse("format=re-twist-tt2 improvements\r\n# a note\r\nversion=1\r\nwindow=borderless\r\n", imp));
    ASSERT_TRUE(imp.window == WindowMode::Borderless);
}

TEST(improvements_text_ignores_keys_from_newer_builds) {
    // Unlike settings.txt: a 0.3.1 key must never stop 0.3.0 from loading.
    Improvements imp;
    ASSERT_TRUE(parse("format=re-twist-tt2 improvements\nversion=1\nupscaler=epx\nwindow=borderless\n", imp));
    ASSERT_TRUE(imp.window == WindowMode::Borderless);
}

TEST(improvements_text_rejects_garbage) {
    const char* bad[] = {
        "",                                                                  // no header
        "format=re-twist-tt2 settings\nversion=1\n",                          // another file
        "window=borderless\nformat=re-twist-tt2 improvements\n",              // header not first
        "format=re-twist-tt2 improvements\nversion=2\n",
        "format=re-twist-tt2 improvements\nwindow=fullscreen-ish\n",          // known key, bad value
        "format=re-twist-tt2 improvements\nwindow=\n",
        "format=re-twist-tt2 improvements\nrenderer=metal\n",               // not a backend we offer
        "format=re-twist-tt2 improvements\nrenderer=direct3d11\n",          // excluded (Q12)
        "format=re-twist-tt2 improvements\nvsync=2\n",
        "format=re-twist-tt2 improvements\nvsync=on\n",
        "format=re-twist-tt2 improvements\nframe_limit=59\n",                // not an offered limit
        "format=re-twist-tt2 improvements\nframe_limit=-60\n",
        "format=re-twist-tt2 improvements\nframe_limit=0060\n",              // too long
        "format=re-twist-tt2 improvements\nframe_limit=\n",
        "format=re-twist-tt2 improvements\nno equals sign\n",
    };
    for (const char* text : bad) {
        Improvements imp;
        imp.window = WindowMode::Borderless;
        ASSERT_TRUE(!parse(text, imp));
        ASSERT_TRUE(imp.window == WindowMode::Borderless);                   // untouched
    }
}

TEST(improvements_from_legacy_maps_fullscreen_to_borderless) {
    GameSettings g;
    g.fullscreen = true;
    ASSERT_TRUE(Improvements::from_legacy(g).window == WindowMode::Borderless);
    g.fullscreen = false;
    ASSERT_TRUE(Improvements::from_legacy(g).window == WindowMode::Windowed);
}

TEST(improvements_store_save_and_load) {
    const auto dir = temp_dir("store");
    ProfileStore store(dir);
    ASSERT_TRUE(!store.has_improvements());               // missing: the caller uses from_legacy
    Improvements imp;
    ASSERT_TRUE(store.load(imp));                         // and load keeps the defaults
    ASSERT_TRUE(imp == Improvements{});
    imp.window = WindowMode::Borderless;
    ASSERT_TRUE(store.save(imp));
    ASSERT_TRUE(store.has_improvements());
    Improvements back;
    ASSERT_TRUE(store.load(back));
    ASSERT_TRUE(back == imp);
    std::filesystem::remove_all(dir);
}

TEST(improvements_store_corrupt_file_keeps_defaults) {
    const auto dir = temp_dir("corrupt");
    std::filesystem::create_directories(dir);
    ProfileStore store(dir);
    { std::ofstream f(store.improvements_path()); f << "format=re-twist-tt2 improvements\nwindow=sideways\n"; }
    ASSERT_TRUE(store.has_improvements());
    Improvements imp;
    ASSERT_TRUE(!store.load(imp));
    ASSERT_TRUE(imp == Improvements{});
    std::filesystem::remove_all(dir);
}

TEST(improvements_never_change_the_settings_txt_format) {
    // 0.2.x reads settings.txt with unknown keys rejected: the mirror is
    // only the old fullscreen flag.
    GameSettings g;
    Improvements imp;
    imp.window = WindowMode::Borderless;
    g.fullscreen = imp.legacy_fullscreen();
    GameSettings back;
    ASSERT_TRUE(parse(serialize(g), back));
    ASSERT_TRUE(back.fullscreen);
}

TEST(improvements_renderer_round_trips_every_choice) {
    for (RendererPref r : {RendererPref::Auto, RendererPref::Vulkan, RendererPref::OpenGL, RendererPref::Software}) {
        Improvements imp;
        imp.renderer = r;
        Improvements back;
        ASSERT_TRUE(parse(serialize(imp), back));
        ASSERT_TRUE(back.renderer == r);
    }
    Improvements imp;
    ASSERT_TRUE(parse("format=re-twist-tt2 improvements\nversion=1\nwindow=windowed\n", imp));
    ASSERT_TRUE(imp.renderer == RendererPref::Auto);                    // a P2 file: the default
}

TEST(improvements_vsync_and_frame_limit_round_trip) {
    for (int fps : FRAME_LIMITS) {
        for (bool vsync : {true, false}) {
            Improvements imp;
            imp.vsync = vsync;
            imp.frame_limit = fps;
            Improvements back;
            ASSERT_TRUE(parse(serialize(imp), back));
            ASSERT_TRUE(back == imp);
        }
    }
    Improvements imp;
    ASSERT_TRUE(parse("format=re-twist-tt2 improvements\nversion=1\nrenderer=vulkan\n", imp));
    ASSERT_TRUE(imp.vsync);                                              // a P3 file: the defaults
    ASSERT_EQ(imp.frame_limit, 0);
}

TEST(improvements_frame_limits_are_the_offered_ones) {
    for (int fps : {0, 60, 120, 144, 165, 240}) ASSERT_TRUE(valid_frame_limit(fps));
    for (int fps : {-1, 30, 59, 61, 75, 300}) ASSERT_TRUE(!valid_frame_limit(fps));
}
