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
    ASSERT_TRUE(g.button("BUTTON_INFO")->enabled);                         // opens the credits
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
    ASSERT_TRUE(d.press("BUTTON_NOPE") == SettingsDialog::Result::None);
}

TEST(settings_info_opens_the_credits_over_the_settings) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    ASSERT_TRUE(d.credits() == nullptr);
    ASSERT_TRUE(d.press("BUTTON_INFO") == SettingsDialog::Result::Info);   // 0x40c760
    const Dialog* c = d.credits();
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->w, 798);
    ASSERT_EQ(c->h, 595);
    ASSERT_TRUE(c->button("BUTTON_OK")->sprite.x == 714.0f && c->button("BUTTON_OK")->sprite.y == 535.0f);
    // Page 1 is the original 2008 credits: the GRA_CREDITS picture, with
    // the same names as text for builds that have no imported picture.
    ASSERT_TRUE(c->sprite_named("P0_GRA_CREDITS") != nullptr);
    ASSERT_TRUE(c->sprite_named("P0_GRA_CREDITS")->x == 46.0f && c->sprite_named("P0_GRA_CREDITS")->y == 124.0f);
    ASSERT_EQ(c->label("P0_ORIG_HEADING_0")->text, std::string("Producer"));
    ASSERT_EQ(c->label("P0_ORIG_NAMES_0")->text, std::string("Teagen Densmore"));
    ASSERT_EQ(c->label("P0_ORIG_NAMES_0")->fallback_for, std::string("P0_GRA_CREDITS"));
    ASSERT_EQ(c->label("P0_MESSAGE")->text, std::string("Copyright \u00a92008 GameHouse, Inc."));
    ASSERT_TRUE(c->label("P0_MESSAGE")->alpha == 1.0f);
    // Page 2, this recreation, waits behind NEXT.
    ASSERT_EQ(c->label("P1_HEADING_0")->text, std::string("Made possible by"));
    ASSERT_EQ(c->label("P1_LINES_0")->text, std::string("Claude (Anthropic)"));
    ASSERT_TRUE(c->label("P1_HEADING_0")->alpha == 0.0f && c->label("P1_MESSAGE")->alpha == 0.0f);
    ASSERT_TRUE(c->label("P1_MESSAGE")->text.find("not affiliated with GameHouse") != std::string::npos);
    ASSERT_EQ(c->label("VERSION")->text, std::string("VERSION ") + TT2_VERSION);
    ASSERT_TRUE(!c->button("BUTTON_PREV")->sprite.visible && c->button("BUTTON_NEXT")->sprite.visible);
    // The settings underneath ignore presses while the credits are up.
    const int len_before = d.max_word_length();
    ASSERT_TRUE(d.press("SWITCH_LENGTH_8") == SettingsDialog::Result::None);
    ASSERT_EQ(d.max_word_length(), len_before);
    ASSERT_TRUE(d.credits() != nullptr);
    // OK (0x408c00) closes them and the settings carry on.
    ASSERT_TRUE(d.press("BUTTON_OK") == SettingsDialog::Result::None);
    ASSERT_TRUE(d.credits() == nullptr);
    ASSERT_TRUE(d.press("BUTTON_OK") == SettingsDialog::Result::Ok);
}

TEST(settings_esc_closes_the_credits_not_the_settings) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.set_volume("SLIDER_FXVOL", 0.9f);
    d.press("BUTTON_INFO");
    ASSERT_TRUE(d.press("BUTTON_CANCEL") == SettingsDialog::Result::None);   // host Esc
    ASSERT_TRUE(d.credits() == nullptr);
    ASSERT_TRUE(d.values().sound_volume == 0.9f);                            // nothing reverted
}

TEST(settings_credits_take_the_mouse) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.press("BUTTON_INFO");
    const Dialog& c = *d.credits();
    const float ox = c.left() + 714.0f, oy = c.top() + 535.0f;              // OK's centre
    d.mouse_move(ox, oy);
    ASSERT_TRUE(d.pointer_hot());
    d.mouse_down(ox, oy);
    ASSERT_TRUE(d.mouse_up(ox, oy) == SettingsDialog::Result::None);
    ASSERT_TRUE(d.credits() == nullptr);
}

TEST(settings_credits_next_crossfades_to_the_recreation_page) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.press("BUTTON_INFO");
    ASSERT_TRUE(d.press("BUTTON_NEXT") == SettingsDialog::Result::None);
    ASSERT_EQ(d.credits_dialog()->page(), 1);
    ASSERT_TRUE(d.credits() != nullptr);                                     // NEXT does not close
    ASSERT_EQ(r.count(SoundId::Button), 2);                                  // INFO, NEXT
    d.tick(50);                                                              // t = 0.25
    const Dialog* c = d.credits();
    ASSERT_TRUE(c->label("P0_MESSAGE")->alpha == 0.75f && c->sprite_named("P0_GRA_CREDITS")->alpha == 0.75f);
    ASSERT_TRUE(c->label("P0_ORIG_HEADING_0")->alpha == 0.75f);
    ASSERT_TRUE(c->label("P1_HEADING_0")->alpha == 0.25f && c->label("P1_MESSAGE")->alpha == 0.25f);
    d.tick(HELP_FADE_MS);
    ASSERT_TRUE(c->label("P0_MESSAGE")->alpha == 0.0f && c->label("P1_LINES_0")->alpha == 1.0f);
    ASSERT_TRUE(c->button("BUTTON_PREV")->sprite.visible && !c->button("BUTTON_NEXT")->sprite.visible);
    // NEXT is hidden on the last page: a press is ignored, silently.
    ASSERT_TRUE(d.press("BUTTON_NEXT") == SettingsDialog::Result::None);
    ASSERT_EQ(d.credits_dialog()->page(), 1);
    ASSERT_EQ(r.count(SoundId::Button), 2);
    // PREV goes back to the original credits; OK closes from either page.
    d.press("BUTTON_PREV");
    ASSERT_EQ(d.credits_dialog()->page(), 0);
    d.tick(HELP_FADE_MS);
    ASSERT_TRUE(c->label("P0_MESSAGE")->alpha == 1.0f && c->label("P1_MESSAGE")->alpha == 0.0f);
    d.press("BUTTON_NEXT");
    ASSERT_TRUE(d.press("BUTTON_OK") == SettingsDialog::Result::None);
    ASSERT_TRUE(d.credits() == nullptr);
}

TEST(credits_prev_is_hidden_on_the_first_page) {
    CreditsDialog c;
    ASSERT_TRUE(!c.press("BUTTON_PREV"));
    ASSERT_EQ(c.page(), 0);
    ASSERT_TRUE(!c.press("BUTTON_NOPE"));
    ASSERT_TRUE(c.press("BUTTON_OK"));
}

TEST(credits_reopen_on_the_original_page) {
    Rig r;
    SettingsDialog d(saved(), 7, true, r.services());
    d.press("BUTTON_INFO");
    d.press("BUTTON_NEXT");
    d.press("BUTTON_CANCEL");                                                // host Esc
    d.press("BUTTON_INFO");
    ASSERT_EQ(d.credits_dialog()->page(), 0);
    ASSERT_TRUE(d.credits()->label("P0_MESSAGE")->alpha == 1.0f);
}

TEST(credits_name_every_original_section_once) {
    const Dialog c = build_credits();
    int headings = 0;
    for (const TextLabel& l : c.labels)
        if (l.name.rfind("P0_ORIG_HEADING_", 0) == 0) {
            ++headings;
            ASSERT_EQ(l.fallback_for, std::string("P0_GRA_CREDITS"));
        }
    ASSERT_EQ(headings, static_cast<int>(sizeof(credits_strings::ORIGINAL) / sizeof(credits_strings::ORIGINAL[0])));
    // Labels outside the stand-in always draw.
    ASSERT_TRUE(c.label("P1_HEADING_0")->fallback_for.empty() && c.label("VERSION")->fallback_for.empty());
}

TEST(settings_alt_enter_is_the_only_fullscreen_shortcut) {
    ASSERT_TRUE(is_fullscreen_shortcut(true, true, false));
    ASSERT_TRUE(!is_fullscreen_shortcut(true, true, true));                // auto-repeat
    ASSERT_TRUE(!is_fullscreen_shortcut(false, true, false));              // plain Enter submits
    ASSERT_TRUE(!is_fullscreen_shortcut(true, false, false));              // Alt + another key
}

TEST(settings_sync_fullscreen_survives_cancel_and_ok) {
    Rig r;
    SettingsDialog d(saved(), 6, true, r.services());
    d.sync_fullscreen(true);                                               // Alt+Enter while open
    ASSERT_EQ(frame(d, "SWITCH_FULLSCREEN"), SWITCH_ON_ROW_OFFSET);
    ASSERT_TRUE(r.sounds.empty());                                         // silent
    d.press("BUTTON_CANCEL");
    ASSERT_TRUE(d.values().fullscreen);                                    // not reverted
    SettingsDialog d2(saved(), 6, true, r.services());
    d2.sync_fullscreen(true);
    d2.press("BUTTON_OK");
    ASSERT_TRUE(d2.values().fullscreen);                                   // OK keeps it
}
