// GameSettings and settings.txt (help-and-settings.md §3).
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "tt2/profile_store.hpp"
#include "tt2/settings.hpp"

using namespace tt2;

namespace {
std::filesystem::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = std::filesystem::temp_directory_path() /
                   ("tt2_settings_test_" + std::string(tag) + "_" + std::to_string(rd()));
    std::filesystem::remove_all(p);
    return p;
}
}  // namespace

TEST(settings_defaults_follow_the_factory) {
    const GameSettings s;
    ASSERT_TRUE(s.sound_volume == 0.5f);                 // 0x40e720
    ASSERT_TRUE(s.music_volume == 0.5f);
    ASSERT_TRUE(!s.fullscreen);                          // deliberate deviation: windowed first run
}

TEST(settings_volume_range_is_zero_to_one) {
    ASSERT_TRUE(GameSettings::valid_volume(0.0f));
    ASSERT_TRUE(GameSettings::valid_volume(1.0f));
    ASSERT_TRUE(GameSettings::valid_volume(0.25f));
    ASSERT_TRUE(!GameSettings::valid_volume(-0.01f));
    ASSERT_TRUE(!GameSettings::valid_volume(1.01f));
    ASSERT_TRUE(!GameSettings::valid_volume(std::nanf("")));
}

TEST(settings_text_round_trip) {
    GameSettings s;
    s.fullscreen = true;
    s.sound_volume = 0.25f;
    s.music_volume = 1.0f;
    const std::string text = serialize(s);
    ASSERT_TRUE(text.find("soundvolume=0.25\n") != std::string::npos);
    GameSettings back;
    ASSERT_TRUE(parse(text, back));
    ASSERT_TRUE(back == s);
}

TEST(settings_text_missing_keys_keep_their_value) {
    GameSettings s;
    s.music_volume = 0.75f;
    ASSERT_TRUE(parse("format=re-twist-tt2 settings\nversion=1\nsoundvolume=0\n", s));
    ASSERT_TRUE(s.sound_volume == 0.0f);
    ASSERT_TRUE(s.music_volume == 0.75f);               // untouched, like 0x465a80
}

TEST(settings_text_rejects_garbage) {
    const char* bad[] = {
        "",                                                              // no header
        "format=re-twist-tt2 profiles\nversion=1\n",
        "soundvolume=0.5\nformat=re-twist-tt2 settings\n",               // header not first
        "format=re-twist-tt2 settings\nversion=2\n",
        "format=re-twist-tt2 settings\nsoundvolume=1.5\n",               // out of range
        "format=re-twist-tt2 settings\nmusicvolume=-0.1\n",
        "format=re-twist-tt2 settings\nmusicvolume=0,5\n",               // locale comma
        "format=re-twist-tt2 settings\nmusicvolume=\n",
        "format=re-twist-tt2 settings\nmusicvolume=nan\n",
        "format=re-twist-tt2 settings\nfullscreen=yes\n",
        "format=re-twist-tt2 settings\nusehints=1\n",                     // unknown key
        "format=re-twist-tt2 settings\nno equals sign\n",
    };
    for (const char* text : bad) {
        GameSettings s;
        s.sound_volume = 0.3f;
        ASSERT_TRUE(!parse(text, s));
        ASSERT_TRUE(s.sound_volume == 0.3f);                         // untouched
    }
}

TEST(settings_store_save_and_load) {
    const auto dir = temp_dir("store");
    ProfileStore store(dir);
    GameSettings s;
    ASSERT_TRUE(store.load(s));                          // missing file -> defaults
    ASSERT_TRUE(s == GameSettings{});
    s.music_volume = 0.1f;
    s.fullscreen = true;
    ASSERT_TRUE(store.save(s));
    ASSERT_TRUE(std::filesystem::exists(store.settings_path()));
    GameSettings back;
    ASSERT_TRUE(store.load(back));
    ASSERT_TRUE(back == s);
    std::filesystem::remove_all(dir);
}

TEST(settings_store_corrupt_file_keeps_defaults) {
    const auto dir = temp_dir("corrupt");
    std::filesystem::create_directories(dir);
    ProfileStore store(dir);
    { std::ofstream f(store.settings_path()); f << "format=re-twist-tt2 settings\nsoundvol"; }   // truncated
    GameSettings s;
    ASSERT_TRUE(!store.load(s));
    ASSERT_TRUE(s == GameSettings{});
    std::filesystem::remove_all(dir);
}
