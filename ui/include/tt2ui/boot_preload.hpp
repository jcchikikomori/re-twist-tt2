// What the boot preloads, and how far it has got (deviation from the port's
// first lazy loader; the original's sections, docs/reverse-engineering/
// welcome-flow.md §0).
//
// The original preloads section "preloading" (the loading screen) before the
// first scene, and loads "default" + "sceneselection" in the background while
// the splash and loading screens run; the loading bar shows that load. The
// port loaded every file the first time it was drawn or played. LoadScene
// starts MUSIC_LOGO and MUSIC_game_load as it is built, and its first draw then
// decoded ANI_CIRCLES (6000x600) and IMAGE_LOGO_ANIM (5700x864) on the main
// thread: about 150 ms on a desktop, 1-2 s on a low-end tablet. The step is
// clamped to 100 ms (frame_clock.hpp), so the logo animation fell that far
// behind its music.
//
// The groups here are taken from the scenes themselves, so a sprite added to
// a scene is preloaded without touching this file:
//   PRELOAD_LOADING    what LoadScene draws and plays; loaded during the
//                      splash (SplashScene waits for it:
//                      FlowHost::preloading_done)
//   PRELOAD_MENU       what SelectionScene draws and plays, and the fonts the
//                      host draws over it; loaded during the loading screen,
//                      whose bar shows the progress (loader_progress)
//   PRELOAD_CROSSWORD  every image the Crossword scene draws
//                      (CrosswordScene::art); queued when the scene starts,
//                      so what shows later (the 572 x 1080 highlight sheet's
//                      red frames, popups 4 / 7 / 9, the sheen) decodes on the
//                      workers instead of in the frame it first shows. Nothing
//                      waits for it.
// The host decodes on worker threads and makes one texture per frame
// (ui/src/main.cpp, Assets::preload); anything not loaded yet when it's first
// needed still loads on the spot, as before.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

struct PreloadSet {
    std::vector<SheetSpec> images;      // one per image id, first spec seen
    std::vector<SoundId> sounds;        // FlowHost::play_sound, predecoded
    std::vector<SoundId> streams;       // FlowHost::play_stream
    std::vector<std::string> fonts;     // resources.xml font ids
};

inline constexpr int PRELOAD_LOADING = 0;
inline constexpr int PRELOAD_MENU = 1;
inline constexpr int PRELOAD_CROSSWORD = 2;

namespace preload_detail {

// A FlowHost that only notes what a scene asks for.
class RecordingHost : public FlowHost {
public:
    explicit RecordingHost(PreloadSet& out) : out_(out) {}
    void play_sound(SoundId id) override { add(out_.sounds, id); }
    void play_stream(SoundId id) override { add(out_.streams, id); }
    void transition(SceneId, int, bool) override {}
    bool loader_done() override { return false; }
    float loader_progress() override { return 0.0f; }
    bool preloading_done() override { return false; }
    void menu_action(const std::string&) override {}

    static void add(std::vector<SoundId>& v, SoundId id) {
        if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
    }

private:
    PreloadSet& out_;
};

// Hidden sprites never draw (CLICK_TEXT is never shown in this build), so
// they are never loaded either.
inline void add_images(PreloadSet& set, const std::vector<const SpriteState*>& sprites) {
    for (const SpriteState* s : sprites) {
        if (!s->visible || s->sheet.image_id.empty()) continue;
        const bool seen = std::any_of(set.images.begin(), set.images.end(),
                                      [&](const SheetSpec& x) { return x.image_id == s->sheet.image_id; });
        if (!seen) set.images.push_back(s->sheet);
    }
}

}  // namespace preload_detail

// Section "preloading": the loading screen's art, MUSIC_game_load and
// MUSIC_LOGO (LoadScene's constructor starts both).
inline PreloadSet preload_loading_screen() {
    PreloadSet set;
    preload_detail::RecordingHost host(set);
    const LoadScene scene(host);
    preload_detail::add_images(set, scene.draw_list());
    return set;
}

// Sections "sceneselection" + the parts of "default" the menu uses: the menu's
// art and music, the sounds its timers and buttons play, and the fonts the
// host draws over it.
inline PreloadSet preload_menu() {
    PreloadSet set;
    preload_detail::RecordingHost host(set);
    tt2::CrtRandom rng;
    const SelectionScene scene(host, rng);
    preload_detail::add_images(set, scene.draw_list());
    // Played later, by the menu's timers (the logo's spring and bounce) and
    // its buttons, so building the scene doesn't record them.
    for (SoundId id : {SoundId::LoadingSpring, SoundId::LoadingBounce, SoundId::Button, SoundId::ButtonWordOfTheDay})
        preload_detail::RecordingHost::add(set.sounds, id);
    // LABEL_PLAYERNAME, the menu's tooltips, and the profile dialogs' text.
    set.fonts = {"FONT_FONT_UIBAR", "FONT_TOOLTIP", "FONT_SETTINGSTEXT"};
    return set;
}

// A play scene's images, as its art() lists them (CrosswordScene::art for
// PRELOAD_CROSSWORD): each id once, the first spec seen, images only.
inline PreloadSet preload_scene_art(const std::vector<SheetSpec>& art) {
    PreloadSet set;
    for (const SheetSpec& s : art) {
        if (s.image_id.empty()) continue;
        const bool seen = std::any_of(set.images.begin(), set.images.end(),
                                      [&](const SheetSpec& x) { return x.image_id == s.image_id; });
        if (!seen) set.images.push_back(s);
    }
    return set;
}

// How far a group has got: `done` of `total` files.
struct PreloadCount {
    std::size_t total = 0;
    std::size_t done = 0;
    bool finished() const { return done >= total; }
    // 0..1; an empty group is finished.
    float progress() const { return total == 0 ? 1.0f : static_cast<float>(done) / static_cast<float>(total); }
};

inline std::size_t preload_file_count(const PreloadSet& set) {
    return set.images.size() + set.sounds.size() + set.streams.size() + set.fonts.size();
}

}  // namespace tt2ui
