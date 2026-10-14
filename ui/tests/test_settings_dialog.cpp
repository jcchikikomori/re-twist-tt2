// DIALOG_SETTINGS: layout, sliders, switches, OK / CANCEL
// (docs/reverse-engineering/help-and-settings.md §3).
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/settings_dialog.hpp"

using namespace tt2ui;

namespace {
struct Rig {
    std::vector<SoundId> sounds;
    std::vector<float> fx, music;
    SettingsDialog::Services services() {
        return {[this](SoundId id) { sounds.push_back(id); }, [this](float v) { fx.push_back(v); },
                [this](float v) { music.push_back(v); }};
    }
    int count(SoundId id) const {
        int n = 0;
        for (auto s : sounds) if (s == id) ++n;
        return n;
    }
};
tt2::GameSettings saved() {
    tt2::GameSettings s;
    s.sound_volume = 0.5f;
    s.music_volume = 0.25f;
    return s;
}
int frame(const SettingsDialog& d, const char* name) { return d.dialog().button(name)->sprite.frame; }
// Screen coordinates of a dialog-local point.
float sx(const SettingsDialog& d, float x) { return d.dialog().left() + x; }
float sy(const SettingsDialog& d, float y) { return d.dialog().top() + y; }
}  // namespace

TEST(settings_layout_matches_dialog_settings) {
    Rig r;
    const SettingsDialog d(saved(), 7, true, r.services());
    const Dialog& g = d.dialog();
    ASSERT_EQ(g.w, 436);
    ASSERT_EQ(g.h, 440);
    ASSERT_EQ(g.label("SETTING_HEADER")->text, std::string("SETTINGS"));
    ASSERT_EQ(g.label("SETTING_HEADER")->char_spacing, 4);
    ASSERT_EQ(g.label("LABEL_DIFFICULTY")->text, std::string("MAXIMUM WORD LENGTH"));
    ASSERT_TRUE(g.button("SWITCH_LENGTH_8")->sprite.x == 289.0f && g.button("SWITCH_LENGTH_8")->sprite.y == 298.0f);
    ASSERT_EQ(g.button("SWITCH_FULLSCREEN")->sprite.sheet.frame_h, 25);   // 24x200, 8 rows
    ASSERT_TRUE(g.button("BUTTON_OK")->sprite.anchor == Anchor::TopRight);
    ASSERT_TRUE(!g.button("BUTTON_INFO")->enabled);                        // no credits yet
    const SettingsSlider& music = d.slider("SLIDER_MUSICVOL");
    ASSERT_TRUE(music.x == 123.0f && music.y == 98.0f);
    ASSERT_TRUE(music.value == 0.25f);
    // 0x468440: knob x = 123 + 30 + 0.25 * 246
    ASSERT_TRUE(music.knob_x() == 214.5f);
    ASSERT_TRUE(music.knob_y() == 110.0f);
    ASSERT_TRUE(d.slider("SLIDER_FXVOL").knob_x() == 276.0f);              // 0.5
}

TEST(settings_switches_show_the_current_values) {
    Rig r;
    tt2::GameSettings s = saved();
    s.fullscreen = true;
    const SettingsDialog d(s, 8, true, r.services());
    ASSERT_EQ(frame(d, "SWITCH_FULLSCREEN"), SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_8"), SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_6"), 0);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_7"), 0);
}

TEST(settings_odd_max_length_checks_six) {
    Rig r;
    const SettingsDialog d(saved(), 5, true, r.services());               // 0x40bdf0: else -> _6
    ASSERT_EQ(d.max_word_length(), 6);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_6"), SWITCH_ON_ROW_OFFSET);
}

TEST(settings_length_radio_group_keeps_one_on) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    ASSERT_TRUE(d.press("SWITCH_LENGTH_8") == SettingsDialog::Result::None);
    ASSERT_EQ(d.max_word_length(), 8);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_8"), SWITCH_ON_ROW_OFFSET);
    ASSERT_EQ(frame(d, "SWITCH_LENGTH_7"), 0);
    ASSERT_EQ(r.count(SoundId::CheckboxOn), 1);
    d.press("SWITCH_LENGTH_8");                                          // already on: no change, no sound
    ASSERT_EQ(d.max_word_length(), 8);
    ASSERT_EQ(r.count(SoundId::CheckboxOn), 1);
}

TEST(settings_fullscreen_switch_toggles_with_sounds) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    d.press("SWITCH_FULLSCREEN");
    ASSERT_TRUE(d.values().fullscreen);
    ASSERT_EQ(r.count(SoundId::CheckboxOn), 1);
    d.press("SWITCH_FULLSCREEN");
    ASSERT_TRUE(!d.values().fullscreen);
    ASSERT_EQ(r.count(SoundId::CheckboxOff), 1);
}

TEST(settings_fullscreen_switch_disabled_when_unsupported) {
    Rig r;
    SettingsDialog d(saved(), 6, false, r.services());
    ASSERT_TRUE(!d.dialog().button("SWITCH_FULLSCREEN")->enabled);
    d.press("SWITCH_FULLSCREEN");
    ASSERT_TRUE(!d.values().fullscreen);
    ASSERT_TRUE(r.sounds.empty());
}

TEST(settings_slider_drag_applies_live_and_clamps) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    const SettingsSlider& music = d.slider("SLIDER_MUSICVOL");
    d.mouse_down(sx(d, music.knob_x()), sy(d, music.knob_y()));            // grab the knob
    ASSERT_TRUE(music.held);
    d.mouse_move(sx(d, 123 + 30 + 246), sy(d, 110));                       // right end
    ASSERT_TRUE(d.values().music_volume == 1.0f);
    ASSERT_TRUE(r.music.back() == 1.0f);
    d.mouse_move(sx(d, 1000), sy(d, 110));                                 // past the end: clamped
    ASSERT_TRUE(d.values().music_volume == 1.0f);
    d.mouse_move(sx(d, 0), sy(d, 400));                                    // far left, off the track
    ASSERT_TRUE(d.values().music_volume == 0.0f);
    d.mouse_up(sx(d, 0), sy(d, 400));
    ASSERT_TRUE(!music.held);
    d.mouse_move(sx(d, 300), sy(d, 110));                                  // released: no effect
    ASSERT_TRUE(d.values().music_volume == 0.0f);
    ASSERT_TRUE(r.fx.empty());                                             // the other slider untouched
}

TEST(settings_click_on_the_track_jumps) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    d.mouse_down(sx(d, 123 + 30 + 0.75f * 246), sy(d, 149 + 5));          // FX track
    ASSERT_TRUE(d.values().sound_volume == 0.75f);
    ASSERT_TRUE(r.fx.back() == 0.75f);
}

TEST(settings_fx_knob_held_plays_slider_sound_every_500_ms) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    const SettingsSlider& fx = d.slider("SLIDER_FXVOL");
    d.mouse_down(sx(d, fx.knob_x()), sy(d, fx.knob_y()));
    d.tick(1);
    ASSERT_EQ(r.count(SoundId::Slider), 1);                                 // at once
    d.tick(499);
    ASSERT_EQ(r.count(SoundId::Slider), 1);
    d.tick(1);                                                              // 500 ms after the first
    ASSERT_EQ(r.count(SoundId::Slider), 2);
    d.mouse_up(0, 0);
    d.tick(2000);
    ASSERT_EQ(r.count(SoundId::Slider), 2);
}

TEST(settings_music_knob_plays_no_slider_sound) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    const SettingsSlider& music = d.slider("SLIDER_MUSICVOL");
    d.mouse_down(sx(d, music.knob_x()), sy(d, music.knob_y()));
    d.tick(2000);
    ASSERT_EQ(r.count(SoundId::Slider), 0);
}

TEST(settings_cancel_restores_the_saved_volumes) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.set_volume("SLIDER_FXVOL", 0.9f);
    d.set_volume("SLIDER_MUSICVOL", 0.1f);
    d.press("SWITCH_LENGTH_6");
    ASSERT_TRUE(d.press("BUTTON_CANCEL") == SettingsDialog::Result::Cancel);
    ASSERT_TRUE(r.fx.back() == 0.5f);
    ASSERT_TRUE(r.music.back() == 0.25f);
    ASSERT_TRUE(d.values() == saved());
}

TEST(settings_ok_keeps_the_new_values) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.set_volume("SLIDER_FXVOL", 2.0f);                                    // clamped
    d.press("SWITCH_LENGTH_6");
    ASSERT_TRUE(d.press("BUTTON_OK") == SettingsDialog::Result::Ok);
    ASSERT_TRUE(d.values().sound_volume == 1.0f);
    ASSERT_EQ(d.max_word_length(), 6);
    ASSERT_TRUE(d.press("BUTTON_INFO") == SettingsDialog::Result::None);  // disabled
    ASSERT_TRUE(d.press("BUTTON_NOPE") == SettingsDialog::Result::None);
}
