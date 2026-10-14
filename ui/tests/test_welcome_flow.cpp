// Headless tests for the welcome flow (splash -> loading -> menu).
// Numbers come from docs/reverse-engineering/welcome-flow.md.

#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/modifier.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

using namespace tt2ui;

namespace {

// Records everything the scenes ask the host to do.
struct RecordingHost : FlowHost {
    std::vector<SoundId> sounds;
    std::vector<SoundId> streams;
    struct Transition { SceneId to; int ms; bool wait; };
    std::vector<Transition> transitions;
    std::vector<std::string> actions;
    bool done = false;
    float progress = 0.0f;
    bool preloaded = true;

    void play_sound(SoundId id) override { sounds.push_back(id); }
    void play_stream(SoundId id) override { streams.push_back(id); }
    void transition(SceneId to, int ms, bool wait) override { transitions.push_back({to, ms, wait}); }
    bool loader_done() override { return done; }
    float loader_progress() override { return progress; }
    bool preloading_done() override { return preloaded; }
    void menu_action(const std::string& name) override { actions.push_back(name); }
};

const SelectionScene::HitTest rect_only = [](const MenuButton&, float, float) { return true; };

int count(const std::vector<SoundId>& v, SoundId id) {
    int n = 0;
    for (auto s : v) if (s == id) ++n;
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sprite / modifier primitives
// ---------------------------------------------------------------------------

TEST(sheet_spec_frame_geometry) {
    SheetSpec s = sheet("IMAGE_LOGO_ANIM", 5700, 864, 12, 2, 24);
    ASSERT_EQ(s.frame_w, 475);
    ASSERT_EQ(s.frame_h, 432);
    ASSERT_EQ(s.frame_x(13), 475);
    ASSERT_EQ(s.frame_y(13), 432);
    ASSERT_EQ(s.frame_x(11), 11 * 475);
    ASSERT_EQ(s.frame_y(11), 0);
}

TEST(sprite_anchor_offsets) {
    SpriteState s = sprite("CLICK_TEXT", sheet("CLICK_HERE", 400, 20), 320, 440);
    s.anchor = Anchor::Top;
    ASSERT_EQ(s.left(), 120.0f);
    ASSERT_EQ(s.top(), 440.0f);
    s.anchor = Anchor::Center;
    ASSERT_EQ(s.top(), 430.0f);
    s.anchor = Anchor::BottomLeft;
    ASSERT_EQ(s.left(), 320.0f);
    ASSERT_EQ(s.top(), 420.0f);
}

TEST(sprite_auto_frames_loop) {
    SpriteState s = sprite("C", sheet("ANI_CIRCLES", 6000, 600, 15, 2, 30), 0, 0);
    s.frame_time_ms = 100;
    s.frame = 28;
    s.tick_auto_frames(250);      // 2 full frames, 50 ms left over
    ASSERT_EQ(s.frame, 0);
    ASSERT_EQ(s.frame_accum_ms, 50);
    s.frame_time_ms = 0;
    s.tick_auto_frames(1000);     // disabled: no change
    ASSERT_EQ(s.frame, 0);
}

TEST(delay_then_method_runs_in_order) {
    ModifierQueue q;
    int fired = 0;
    q.push_delay(100);
    q.push_method([&] { ++fired; });
    q.update(50);
    ASSERT_EQ(fired, 0);
    q.update(49);
    ASSERT_EQ(fired, 0);
    q.update(2);                  // 101 >= 100, 1 ms carried into the method
    ASSERT_EQ(fired, 1);
    ASSERT_TRUE(q.empty());
}

TEST(alpha_modifier_interpolates_and_clamps) {
    SpriteState s;
    ModifierQueue q;
    q.push_alpha(&s, 0.0f, 1.0f, 500);
    q.update(250);
    ASSERT_EQ(s.alpha, 0.5f);
    q.update(1000);
    ASSERT_EQ(s.alpha, 1.0f);
    ASSERT_TRUE(q.empty());
}

// 0x4513d0 smoothstep: (3 - 2t) * t^2. t = 0.25 -> 0.15625.
TEST(easing_linear_and_smoothstep) {
    ASSERT_EQ(ease(Easing::Linear, 0.25f), 0.25f);
    ASSERT_EQ(ease(Easing::Smoothstep, 0.0f), 0.0f);
    ASSERT_EQ(ease(Easing::Smoothstep, 0.25f), 0.15625f);
    ASSERT_EQ(ease(Easing::Smoothstep, 0.5f), 0.5f);
    ASSERT_EQ(ease(Easing::Smoothstep, 1.0f), 1.0f);
}

TEST(scale_modifier_eases_and_clamps) {
    SpriteState s;
    ModifierQueue q;
    q.push_scale(&s, 0.0f, 2.0f, 400, Easing::Smoothstep);
    q.update(100);
    ASSERT_EQ(s.scale, 2.0f * 0.15625f);
    q.update(1000);                                        // past the end: clamps to `to`
    ASSERT_EQ(s.scale, 2.0f);
    ASSERT_TRUE(q.empty());
    SpriteState z;
    ModifierQueue q2;
    q2.push_scale(&z, 1.0f, 3.0f, 0);                      // zero length: jumps, never divides by 0
    q2.update(16);
    ASSERT_EQ(z.scale, 3.0f);
    ASSERT_TRUE(q2.empty());
}

TEST(frame_modifier_steps_all_frames_and_passes) {
    SpriteState s = sprite("L", sheet("IMAGE_LOGO_ANIM", 5700, 864, 12, 2, 24), 0, 0);
    ModifierQueue q;
    q.push_frames(&s, 0, -1, 100);
    q.update(0);
    ASSERT_EQ(s.frame, 0);
    q.update(1150);
    ASSERT_EQ(s.frame, 11);
    q.update(1200);               // 2350 >= 2400? no: frame 23
    ASSERT_EQ(s.frame, 23);
    ASSERT_TRUE(!q.empty());
    q.update(50);                 // 2400: finished on the last frame
    ASSERT_EQ(s.frame, 23);
    ASSERT_TRUE(q.empty());

    SpriteState b = sprite("B", sheet("IMAGE_LOGO_BOUNCE", 1728, 252, 12, 1, 12), 0, 0);
    ModifierQueue q2;
    q2.push_frames(&b, 0, -1, 60, 2);
    q2.update(60 * 13);           // second pass, frame 1
    ASSERT_EQ(b.frame, 1);
    q2.update(60 * 11);           // 24 frames * 60 = 1440 total: done
    ASSERT_TRUE(q2.empty());
}

TEST(method_modifier_may_requeue_itself) {
    ModifierQueue q;
    int calls = 0;
    std::function<void()> step = [&] {
        ++calls;
        if (calls < 3) {
            q.push_delay(10);
            q.push_method(step);
        }
    };
    q.push_method(step);
    q.update(1);                  // first call, queues delay + method; delay gets the carried 1 ms
    ASSERT_EQ(calls, 1);
    q.update(9);                  // delay finishes exactly: method waits a frame
    ASSERT_EQ(calls, 1);
    q.update(1);
    ASSERT_EQ(calls, 2);
    q.update(11);
    ASSERT_EQ(calls, 3);
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// SceneSplash
// ---------------------------------------------------------------------------

TEST(splash_defaults_hold_2100ms_then_transition) {
    RecordingHost host;
    SplashScene splash(host);
    ASSERT_TRUE(!splash.finished());
    ASSERT_TRUE(splash.cover().visible);
    ASSERT_EQ(splash.cover().alpha, 0.0f);
    splash.update(2099);
    ASSERT_TRUE(!splash.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{0});
    splash.update(2);             // delay done (1 ms carried), method fires
    ASSERT_TRUE(splash.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
    ASSERT_TRUE(host.transitions[0].to == SceneId::Load);
    ASSERT_EQ(host.transitions[0].ms, 500);
    ASSERT_TRUE(!host.transitions[0].wait);
    splash.update(1000);          // only once
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
}

TEST(splash_waits_for_preloading_section) {
    RecordingHost host;
    host.preloaded = false;
    SplashScene splash(host);
    splash.update(5000);
    ASSERT_TRUE(splash.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{0});
    host.preloaded = true;
    splash.update(16);
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
}

TEST(splash_without_gamehouse_animation_finishes_immediately) {
    RecordingHost host;
    SplashScene::Config cfg;
    cfg.show_gamehouse_animation = false;
    SplashScene splash(host, cfg);
    ASSERT_TRUE(splash.finished());
    ASSERT_TRUE(!splash.cover().visible);
    splash.update(1);
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
}

TEST(splash_extra_card_fades_in_holds_fades_out) {
    RecordingHost host;
    SplashScene::Config cfg;
    cfg.cards = {{400, 300}};
    SplashScene splash(host, cfg);
    // GH card: delay 2100, then cover fades to black over 500.
    splash.update(2100);
    ASSERT_EQ(splash.cover().alpha, 0.0f);
    splash.update(250);
    ASSERT_EQ(splash.cover().alpha, 0.5f);
    splash.update(250);
    ASSERT_EQ(splash.cover().alpha, 1.0f);
    // Card centred, fades in 500, holds 1700, fades out 500.
    ASSERT_TRUE(splash.card().visible);
    ASSERT_EQ(splash.card().x, 200.0f);
    ASSERT_EQ(splash.card().y, 150.0f);
    splash.update(1);
    splash.update(249);
    ASSERT_EQ(splash.card().alpha, 0.5f);
    splash.update(250);
    ASSERT_EQ(splash.card().alpha, 1.0f);
    splash.update(1700);
    splash.update(1);
    splash.update(499);
    ASSERT_EQ(splash.card().alpha, 0.0f);
    ASSERT_TRUE(!splash.finished());
    splash.update(1);             // method: no more cards -> finished
    ASSERT_TRUE(splash.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
}

// ---------------------------------------------------------------------------
// LoadScene
// ---------------------------------------------------------------------------

TEST(load_init_plays_music_and_queues_logo) {
    RecordingHost host;
    LoadScene load(host);
    ASSERT_EQ(host.streams.size(), std::size_t{1});
    ASSERT_TRUE(host.streams[0] == SoundId::MusicGameLoad);
    ASSERT_EQ(count(host.sounds, SoundId::MusicLogo), 1);
    ASSERT_TRUE(load.bar().has_clip);
    ASSERT_EQ(load.bar().clip.w, 0.0f);
    ASSERT_EQ(load.logo().frame, 0);
    load.update(500);             // delay over, frame animation starts next frame
    ASSERT_EQ(load.logo().frame, 0);
    load.update(1250);            // 1250 / 100 = frame 12
    ASSERT_EQ(load.logo().frame, 12);
    load.update(1200);            // 2450 >= 2400: last frame
    ASSERT_EQ(load.logo().frame, 23);
}

TEST(load_circles_quadrants_and_phase) {
    RecordingHost host;
    LoadScene load(host);
    ASSERT_EQ(load.circles(0).frame, 19);
    ASSERT_TRUE(load.circles(0).flip == Flip::Vertical);
    ASSERT_EQ(load.circles(0).x, 400.0f);
    ASSERT_EQ(load.circles(0).y, 300.0f);
    ASSERT_TRUE(load.circles(1).flip == Flip::Both);
    ASSERT_TRUE(load.circles(2).flip == Flip::Horizontal);
    ASSERT_EQ(load.circles(3).frame, 22);
    load.update(100);
    ASSERT_EQ(load.circles(0).frame, 20);
    ASSERT_EQ(load.circles(3).frame, 23);
    load.update(3000);            // 31 steps: wraps mod 30
    ASSERT_EQ(load.circles(3).frame, 23);
}

TEST(load_progress_bar_clip_width_formula) {
    ASSERT_EQ(LoadScene::clip_width(0.0f, 0.0f), 0.0f);
    ASSERT_EQ(LoadScene::clip_width(1.0f, 0.0f), 483.0f);
    ASSERT_EQ(LoadScene::clip_width(0.5f, 0.0f), 241.5f);
    ASSERT_EQ(LoadScene::clip_width(0.75f, 0.5f), 241.5f);   // normalised to the first sample
    ASSERT_EQ(LoadScene::clip_width(0.25f, 0.5f), 0.0f);     // never negative
    ASSERT_EQ(LoadScene::clip_width(2.0f, 0.0f), 483.0f);    // never wider than the bar
    ASSERT_EQ(LoadScene::clip_width(1.0f, 1.0f), 483.0f);    // degenerate first sample
}

TEST(load_bar_tracks_progress_from_first_sample) {
    RecordingHost host;
    host.progress = 0.2f;
    LoadScene load(host);
    load.update(16);
    ASSERT_EQ(load.bar().clip.w, 0.0f);
    host.progress = 0.6f;
    load.update(16);
    ASSERT_EQ(load.bar().clip.w, LoadScene::clip_width(0.6f, 0.2f));
    host.done = true;
    load.update(16);
    ASSERT_TRUE(!load.bar().has_clip);
}

TEST(load_holds_3500ms_minimum_then_transitions_once) {
    RecordingHost host;
    host.done = true;
    LoadScene load(host);
    load.update(3499);
    ASSERT_TRUE(!load.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{0});
    load.update(1);
    ASSERT_TRUE(load.finished());
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
    ASSERT_TRUE(host.transitions[0].to == SceneId::Selection);
    ASSERT_EQ(host.transitions[0].ms, 500);
    ASSERT_TRUE(host.transitions[0].wait);
    load.update(1000);
    ASSERT_EQ(host.transitions.size(), std::size_t{1});
}

TEST(load_waits_for_loader_even_after_3500ms) {
    RecordingHost host;
    host.done = false;
    LoadScene load(host);
    load.update(10000);
    ASSERT_TRUE(!load.finished());
    host.done = true;
    load.update(1);
    ASSERT_TRUE(load.finished());
}

// ---------------------------------------------------------------------------
// SelectionScene
// ---------------------------------------------------------------------------

TEST(selection_timers_use_crt_rand_plus_3000) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    const int r1 = tt2::CrtRandom(1).rand();        // 41
    tt2::CrtRandom tmp(1); tmp.rand();
    const int r2 = tmp.rand();                       // 18467
    SelectionScene menu(host, rng);
    ASSERT_EQ(menu.coil_timer_ms(), r1 + 3000);
    ASSERT_EQ(menu.bounce_timer_ms(), r2 + 3000);
    ASSERT_EQ(host.streams.size(), std::size_t{1});
    ASSERT_TRUE(host.streams[0] == SoundId::MusicMenu);
}

TEST(selection_coil_fires_then_rearms_with_7000) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    const int first = menu.coil_timer_ms();          // 3041
    menu.update(first - 1);
    ASSERT_EQ(count(host.sounds, SoundId::LoadingSpring), 0);
    menu.update(1);
    ASSERT_EQ(count(host.sounds, SoundId::LoadingSpring), 1);
    ASSERT_TRUE(menu.coil_timer_ms() >= 7000);
    ASSERT_TRUE(menu.coil_timer_ms() <= 7000 + 32767);
    // 17 frames at 70 ms.
    menu.update(70 * 16);
    ASSERT_EQ(menu.logo_coil().frame, 16);
}

TEST(selection_bounce_plays_two_or_three_passes) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    menu.update(menu.bounce_timer_ms() - 1);
    ASSERT_EQ(count(host.sounds, SoundId::LoadingBounce), 0);
    menu.update(1);               // trigger; the animation gets this 1 ms
    ASSERT_EQ(count(host.sounds, SoundId::LoadingBounce), 1);
    ASSERT_EQ(menu.logo_bounce().frame, 0);
    // The rand() drawn at the trigger (26500) is > 6: three passes = 36 frames * 60 ms.
    menu.update(60 * 25);         // 1501 ms -> frame 25 % 12 = 1
    ASSERT_EQ(menu.logo_bounce().frame, 1);
    menu.update(60 * 11);
    ASSERT_EQ(menu.logo_bounce().frame, 11);
}

TEST(selection_button_layout_matches_dialogs_xml) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    ASSERT_EQ(menu.buttons().size(), std::size_t{12});
    auto* quit = menu.button("BUTTON_QUIT");
    ASSERT_TRUE(quit != nullptr);
    ASSERT_EQ(quit->sprite.x, 641.0f);
    ASSERT_EQ(quit->sprite.y, 539.0f);
    ASSERT_EQ(quit->sprite.sheet.frame_h, 52);
    ASSERT_EQ(quit->hit_threshold, 100);
    auto* timed = menu.button("BUTTON_PLAY");
    ASSERT_EQ(timed->sprite.sheet.frame_w, 134);
    ASSERT_EQ(timed->sprite.sheet.frame_h, 134);
    ASSERT_EQ(timed->hit_threshold, 75);
    auto* untimed = menu.button("BUTTON_PLAY_UNTIMED");
    ASSERT_EQ(untimed->hit_threshold, 0);
    auto* rename = menu.button("BUTTON_CHANGENAME");   // change_name.png is 155x52, four 13 px rows
    ASSERT_EQ(rename->sprite.sheet.frame_w, 155);
    ASSERT_EQ(rename->sprite.sheet.frame_h, 13);
}

TEST(selection_hover_press_release_fires_action) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    auto* quit = menu.button("BUTTON_QUIT");
    const float cx = quit->sprite.x + 10, cy = quit->sprite.y + 10;
    menu.mouse_move(cx, cy, rect_only);
    ASSERT_TRUE(quit->hover);
    ASSERT_TRUE(host.sounds.empty());               // ROADMAP bug #3: hover is silent (0x48d390)
    menu.mouse_move(cx + 1, cy, rect_only);
    ASSERT_TRUE(host.sounds.empty());
    menu.update(0);
    ASSERT_EQ(quit->sprite.frame, 1);
    menu.mouse_down(cx, cy, rect_only);
    menu.update(0);
    ASSERT_EQ(quit->sprite.frame, 2);
    menu.mouse_up(cx, cy, rect_only);
    ASSERT_EQ(host.actions.size(), std::size_t{1});
    ASSERT_EQ(host.actions[0], std::string("BUTTON_QUIT"));
    ASSERT_EQ(count(host.sounds, SoundId::Button), 1);
    menu.mouse_move(0, 0, rect_only);
    menu.update(0);
    ASSERT_EQ(quit->sprite.frame, 0);
}

TEST(selection_pointer_hot_tracks_clickable_hover) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    ASSERT_TRUE(!menu.pointer_over_clickable());
    auto* quit = menu.button("BUTTON_QUIT");
    menu.mouse_move(quit->sprite.x + 10, quit->sprite.y + 10, rect_only);
    ASSERT_TRUE(menu.pointer_over_clickable());
    menu.mouse_move(0, 0, rect_only);
    ASSERT_TRUE(!menu.pointer_over_clickable());
    // A disabled button is not clickable: the pointer stays the arrow.
    auto* wotd = menu.button("BUTTON_PLAY_WOD");
    wotd->enabled = false;
    menu.mouse_move(wotd->sprite.x + 5, wotd->sprite.y + 5, rect_only);
    ASSERT_TRUE(!menu.pointer_over_clickable());
}

// ROADMAP bug #3: no button sets `oversound` in the install XML and the
// GF2 parser (0x48d390) has no default, so hovering is silent everywhere.
TEST(menu_hover_plays_no_sound) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    host.sounds.clear();
    int hovered = 0;
    for (const auto& b : menu.buttons()) {
        const float cx = b.sprite.left() + 1, cy = b.sprite.top() + 1;
        menu.mouse_move(cx, cy, rect_only);
        if (b.hover) ++hovered;                     // the frame still reacts
        menu.mouse_move(-100, -100, rect_only);
    }
    ASSERT_EQ(hovered, 12);
    ASSERT_TRUE(host.sounds.empty());
}

TEST(selection_release_outside_or_masked_does_nothing) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    auto* quit = menu.button("BUTTON_QUIT");
    const float cx = quit->sprite.x + 10, cy = quit->sprite.y + 10;
    menu.mouse_down(cx, cy, rect_only);
    menu.mouse_up(cx, 10, rect_only);               // released elsewhere
    ASSERT_EQ(host.actions.size(), std::size_t{0});
    const SelectionScene::HitTest masked = [](const MenuButton&, float, float) { return false; };
    menu.mouse_move(cx, cy, masked);                // transparent pixel: no hover
    ASSERT_TRUE(!quit->hover);
    menu.mouse_down(cx, cy, masked);
    menu.mouse_up(cx, cy, masked);
    ASSERT_EQ(host.actions.size(), std::size_t{0});
}

TEST(selection_disabled_wotd_plays_disabled_sound) {
    RecordingHost host;
    tt2::CrtRandom rng(1);
    SelectionScene menu(host, rng);
    auto* wotd = menu.button("BUTTON_PLAY_WOD");
    wotd->enabled = false;
    menu.update(0);
    ASSERT_EQ(wotd->sprite.frame, 3);
    const float cx = wotd->sprite.x + 5, cy = wotd->sprite.y + 5;
    menu.mouse_down(cx, cy, rect_only);
    menu.mouse_up(cx, cy, rect_only);
    ASSERT_EQ(host.actions.size(), std::size_t{0});
    ASSERT_EQ(count(host.sounds, SoundId::ButtonWordOfTheDay), 1);
}

// ---------------------------------------------------------------------------
// WelcomeFlow (end to end, headless)
// ---------------------------------------------------------------------------

TEST(flow_runs_splash_load_menu_with_fades) {
    std::vector<SoundId> streams;
    WelcomeFlow::Services s;
    s.play_stream = [&](SoundId id) { streams.push_back(id); };
    s.play_sound = [](SoundId) {};
    WelcomeFlow flow(s, 1);
    ASSERT_TRUE(flow.current() == SceneId::Splash);
    flow.update(2101);                       // splash finished, fade starts
    ASSERT_TRUE(flow.current() == SceneId::Splash);
    ASSERT_TRUE(flow.black_alpha() == 0.0f);
    flow.update(125);
    ASSERT_EQ(flow.black_alpha(), 0.5f);
    flow.update(125);                        // black: switch to Load
    ASSERT_TRUE(flow.current() == SceneId::Load);
    ASSERT_EQ(flow.black_alpha(), 1.0f);
    ASSERT_EQ(streams.size(), std::size_t{1});
    ASSERT_TRUE(streams[0] == SoundId::MusicGameLoad);
    flow.update(250);                        // faded in; LoadScene has run 125 + 250 ms
    ASSERT_EQ(flow.black_alpha(), 0.0f);
    ASSERT_EQ(flow.load()->elapsed_ms(), 375);
    // loader_done defaults to true: 3500 ms hold then fade to the menu.
    for (int i = 0; i < 31; ++i) flow.update(100);   // 3475 ms: still holding
    ASSERT_TRUE(!flow.load()->finished());
    flow.update(100);                        // 3575 ms: finished, fade starts
    ASSERT_TRUE(flow.load()->finished());
    ASSERT_TRUE(flow.current() == SceneId::Load);
    flow.update(250);                        // black: switch to the menu
    ASSERT_TRUE(flow.current() == SceneId::Selection);
    ASSERT_EQ(streams.size(), std::size_t{2});
    ASSERT_TRUE(streams[1] == SoundId::MusicMenu);
    ASSERT_TRUE(flow.load() == nullptr);
    ASSERT_TRUE(flow.splash() == nullptr);
    ASSERT_EQ(flow.draw_list().size(), std::size_t{1 + 12 + 3});
}

TEST(flow_pointer_busy_during_load_only) {
    WelcomeFlow::Services s;
    s.play_stream = [](SoundId) {};
    s.play_sound = [](SoundId) {};
    WelcomeFlow flow(s, 1);
    // Splash is not "loading": the pointer stays the arrow.
    ASSERT_TRUE(!flow.pointer_busy());
    flow.update(2101);                        // splash finished, fade starts
    flow.update(125);
    flow.update(125);                         // black: switch to Load
    ASSERT_TRUE(flow.current() == SceneId::Load);
    ASSERT_TRUE(flow.pointer_busy());         // hourglass while loading
    for (int i = 0; i < 35; ++i) flow.update(100);
    flow.update(250);                         // black: switch to the menu
    ASSERT_TRUE(flow.current() == SceneId::Selection);
    ASSERT_TRUE(!flow.pointer_busy());
}

TEST(flow_menu_action_reaches_services) {
    std::vector<std::string> actions;
    WelcomeFlow::Services s;
    s.menu_action = [&](const std::string& n) { actions.push_back(n); };
    WelcomeFlow flow(s, 1);
    flow.update(2101);
    flow.update(250);
    flow.update(250);
    for (int t = 0; t < 3600; t += 100) flow.update(100);
    flow.update(250);
    ASSERT_TRUE(flow.current() == SceneId::Selection);
    auto* play = flow.selection()->button("BUTTON_PLAY");
    const float cx = play->sprite.x + 60, cy = play->sprite.y + 60;
    flow.selection()->mouse_down(cx, cy, rect_only);
    flow.selection()->mouse_up(cx, cy, rect_only);
    ASSERT_EQ(actions.size(), std::size_t{1});
    ASSERT_EQ(actions[0], std::string("BUTTON_PLAY"));
}
