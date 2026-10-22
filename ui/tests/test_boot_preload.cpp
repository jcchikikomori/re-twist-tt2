// Headless tests for what the boot preloads (boot_preload.hpp).

#include "test_harness.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "tt2ui/boot_preload.hpp"
#include "tt2ui/welcome_flow.hpp"

using namespace tt2ui;

namespace {

bool has_image(const PreloadSet& set, const std::string& id) {
    return std::any_of(set.images.begin(), set.images.end(), [&](const SheetSpec& s) { return s.image_id == id; });
}
bool has(const std::vector<SoundId>& v, SoundId id) { return std::find(v.begin(), v.end(), id) != v.end(); }

// Every visible sprite a scene draws must be in its group.
void covers(const PreloadSet& set, const std::vector<const SpriteState*>& sprites) {
    for (const SpriteState* s : sprites)
        if (s->visible && !s->sheet.image_id.empty()) ASSERT_TRUE(has_image(set, s->sheet.image_id));
}

// A host for the real scenes: sounds are only counted.
struct NullHost : FlowHost {
    void play_sound(SoundId) override {}
    void play_stream(SoundId) override {}
    void transition(SceneId, int, bool) override {}
    bool loader_done() override { return true; }
    float loader_progress() override { return 1.0f; }
    bool preloading_done() override { return true; }
    void menu_action(const std::string&) override {}
};

}  // namespace

TEST(preload_loading_screen_has_its_big_sheets_and_both_tracks) {
    const PreloadSet set = preload_loading_screen();
    // The two sheets whose first draw stalled the frame (~14 MB and ~20 MB of pixels).
    ASSERT_TRUE(has_image(set, "ANI_CIRCLES"));
    ASSERT_TRUE(has_image(set, "IMAGE_LOGO_ANIM"));
    ASSERT_TRUE(has_image(set, "preloading/BACKGROUND"));
    ASSERT_TRUE(has_image(set, "IMAGE_GH_BRANDING"));
    // LoadScene's constructor starts both, so they must be ready before it is built.
    ASSERT_TRUE(has(set.streams, SoundId::MusicGameLoad));
    ASSERT_TRUE(has(set.sounds, SoundId::MusicLogo));
    ASSERT_TRUE(set.fonts.empty());
}

TEST(preload_loading_screen_covers_everything_the_scene_draws) {
    const PreloadSet set = preload_loading_screen();
    NullHost host;
    const LoadScene scene(host);
    covers(set, scene.draw_list());
    // The four circle quadrants share one sheet: listed once.
    ASSERT_EQ(std::count_if(set.images.begin(), set.images.end(),
                            [](const SheetSpec& s) { return s.image_id == "ANI_CIRCLES"; }),
              1);
    // CLICK_TEXT is never shown in this build, so it is never loaded.
    ASSERT_TRUE(!has_image(set, "CLICK_HERE"));
    // The spec comes along, so the host can warn about a wrongly sized file.
    const auto logo = std::find_if(set.images.begin(), set.images.end(),
                                   [](const SheetSpec& s) { return s.image_id == "IMAGE_LOGO_ANIM"; });
    ASSERT_EQ(logo->sheet_w(), 5700);
    ASSERT_EQ(logo->sheet_h(), 864);
}

TEST(preload_menu_covers_the_menu_its_sounds_and_fonts) {
    const PreloadSet set = preload_menu();
    NullHost host;
    tt2::CrtRandom rng;
    const SelectionScene scene(host, rng);
    covers(set, scene.draw_list());
    ASSERT_TRUE(has_image(set, "IMAGE_LOGO_COIL") && has_image(set, "IMAGE_BUTTON_TIMED"));
    ASSERT_TRUE(has(set.streams, SoundId::MusicMenu));   // the scene's enter
    for (SoundId id : {SoundId::LoadingSpring, SoundId::LoadingBounce, SoundId::Button, SoundId::ButtonWordOfTheDay})
        ASSERT_TRUE(has(set.sounds, id));
    ASSERT_TRUE(std::find(set.fonts.begin(), set.fonts.end(), "FONT_FONT_UIBAR") != set.fonts.end());
}

TEST(preload_groups_do_not_repeat_the_loading_screen) {
    // The menu group loads during the loading screen; anything it shared with
    // that screen would already be in memory. Today they share nothing.
    const PreloadSet loading = preload_loading_screen();
    const PreloadSet menu = preload_menu();
    for (const SheetSpec& s : menu.images) ASSERT_TRUE(!has_image(loading, s.image_id));
    for (SoundId id : menu.streams) ASSERT_TRUE(!has(loading.streams, id));
    ASSERT_EQ(preload_file_count(loading), loading.images.size() + 2);
    ASSERT_EQ(preload_file_count(menu), menu.images.size() + 1 + 4 + 3);
}

TEST(preload_lists_ignore_what_the_scene_does_not_draw) {
    PreloadSet set;
    SpriteState hidden = sprite("H", sheet("IMAGE_HIDDEN", 10, 10), 0, 0);
    hidden.visible = false;
    const SpriteState cover = sprite("COVER", sheet("", 800, 600), 0, 0);   // image-less, like the splash cover
    const SpriteState a = sprite("A", sheet("IMAGE_A", 10, 10), 0, 0);
    const SpriteState a2 = sprite("A2", sheet("IMAGE_A", 10, 10), 5, 5);
    preload_detail::add_images(set, {&hidden, &cover, &a, &a2});
    ASSERT_EQ(set.images.size(), 1u);
    ASSERT_EQ(set.images[0].image_id, std::string("IMAGE_A"));
}

// PRELOAD_CROSSWORD takes a scene's art() list: each image id once, the
// first spec kept (for the size warning); nothing without an id, and no
// sounds, streams or fonts.
TEST(preload_scene_art_lists_each_image_once) {
    const PreloadSet set = preload_scene_art(
        {sheet("XwordBox", 69, 138, 1, 2, 2), sheet("", 800, 600), sheet("IMAGE_OVERLAY", 800, 600),
         sheet("XwordBox", 10, 10)});
    ASSERT_EQ(set.images.size(), 2u);
    ASSERT_EQ(set.images[0].image_id, std::string("XwordBox"));
    ASSERT_EQ(set.images[0].frame_h, 69);
    ASSERT_EQ(set.images[1].image_id, std::string("IMAGE_OVERLAY"));
    ASSERT_EQ(preload_file_count(set), 2u);
    ASSERT_TRUE(preload_scene_art({}).images.empty());
    ASSERT_TRUE(PRELOAD_CROSSWORD != PRELOAD_LOADING && PRELOAD_CROSSWORD != PRELOAD_MENU);
}

TEST(preload_count_reports_progress_and_the_end) {
    PreloadCount c;
    ASSERT_TRUE(c.finished());                  // nothing queued: never holds the splash
    ASSERT_TRUE(c.progress() == 1.0f);
    c.total = 4;
    ASSERT_TRUE(!c.finished());
    ASSERT_TRUE(c.progress() == 0.0f);
    c.done = 1;
    ASSERT_TRUE(c.progress() == 0.25f);
    c.done = 4;
    ASSERT_TRUE(c.finished() && c.progress() == 1.0f);
}

// The splash waits for the loading screen's group, and the loading screen's
// bar follows the menu group (the hooks the host now answers truthfully).
TEST(preload_splash_holds_until_the_loading_group_is_in) {
    PreloadCount loading{3, 0}, menu{5, 0};
    WelcomeFlow::Services s;
    s.preloading_done = [&] { return loading.finished(); };
    s.loader_done = [&] { return menu.finished(); };
    s.loader_progress = [&] { return menu.progress(); };
    WelcomeFlow flow(s);
    for (int t = 0; t < 4000; t += 10) flow.update(10);
    ASSERT_TRUE(flow.current() == SceneId::Splash);   // 2100 ms passed long ago
    loading.done = 3;
    for (int t = 0; t < 600; t += 10) flow.update(10);
    ASSERT_TRUE(flow.current() == SceneId::Load);
    // The bar starts from the first sample, then fills with the menu group.
    menu.done = 1;
    flow.update(10);
    menu.done = 3;
    flow.update(10);
    ASSERT_TRUE(flow.load()->bar().has_clip && flow.load()->bar().clip.w > 0.0f &&
                flow.load()->bar().clip.w < LoadScene::BAR_W);
    // Held past 3500 ms until the menu group is in, then on to the menu.
    for (int t = 0; t < 5000; t += 10) flow.update(10);
    ASSERT_TRUE(flow.current() == SceneId::Load);
    menu.done = 5;
    for (int t = 0; t < 1100; t += 10) flow.update(10);
    ASSERT_TRUE(flow.current() == SceneId::Selection);
}
