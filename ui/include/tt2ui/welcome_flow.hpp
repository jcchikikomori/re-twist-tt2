// The welcome flow: SceneSplash -> LoadScene -> SelectionScene.
//
// Every number here is from docs/reverse-engineering/welcome-flow.md
// (addresses in comments). The scenes are pure logic: they own sprite
// states and modifier queues and talk to the outside world through
// `FlowHost`, so the renderer and the unit tests are interchangeable.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tt2/random.hpp"
#include "tt2ui/modifier.hpp"
#include "tt2ui/sprite.hpp"

namespace tt2ui {

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 600;

enum class SceneId { Splash, Load, Selection };

// Sound / stream ids from resources.xml.
enum class SoundId {
    MusicGameLoad,      // Stream  MUSIC_game_load   (music/music_game_load.ogg)
    MusicMenu,          // Stream  MUSIC_menu        (music/music_menu.ogg)
    MusicLogo,          // Sound   MUSIC_LOGO        (music/music_logo_animation.ogg)
    LoadingSpring,      // Sound   SOUND_loading_spring (sound/loading_tee_sound.ogg)
    LoadingBounce,      // Sound   SOUND_loading_bounce (sound/loading_two_sound.ogg)
    Button,             // Sound   SOUND_BUTTON
    ButtonWordOfTheDay, // Sound   SOUND_button_wordoftheday (disabled WOTD click)
    // Game scene (sound/*.ogg, resources.xml section "default").
    LetterPlace,        // SOUND_letter_place
    LetterClear,        // SOUND_letter_clear
    LetterLoad,         // SOUND_letter_load
    WordCorrect,        // SOUND_word_correct
    WordReject,         // SOUND_word_reject
    WordBingo,          // SOUND_word_bingo
    WordLast,           // SOUND_word_last
    WordTwist,          // SOUND_word_twist
    WordPassReveal,     // SOUND_word_pass_reveal
    AllWords,           // SOUND_game_all_words_submitted
    ClockCountdown,     // SOUND_game_clock_countdown
    ClockTimeUp,        // SOUND_game_clock_timeup
    LoseLife,           // SOUND_game_lose_life
    NextRoundAchieved,  // SOUND_game_next_round_acheived
    HighScoreAchieved,  // SOUND_game_high_score_achieved
    MusicInGame1,       // Stream  MUSIC_in-game_1 (music/music_in-game_1.ogg)
};

// What the scenes need from the application.
class FlowHost {
public:
    virtual ~FlowHost() = default;
    virtual void play_sound(SoundId id) = 0;
    virtual void play_stream(SoundId id) = 0;
    // FadeBlackTransition: `ms` long; `wait` false means the next scene
    // may start loading while the fade runs (TRANSITION_SPLASH).
    virtual void transition(SceneId to, int ms, bool wait) = 0;
    // Resource loader state for LoadScene (sections default + sceneselection).
    virtual bool loader_done() = 0;
    virtual float loader_progress() = 0;   // 0..1
    // Section "preloading" finished loading (SceneSplash waits for it).
    virtual bool preloading_done() = 0;
    // Main-menu button pressed. `name` is the dialogs.xml component name.
    virtual void menu_action(const std::string& name) = 0;
};

// ---------------------------------------------------------------------------
// Helpers to build sprite states from the resources.xml / dialogs.xml data.
// ---------------------------------------------------------------------------
inline SheetSpec sheet(const char* image_id, int w, int h, int cols = 1, int rows = 1, int frames = 1) {
    SheetSpec s;
    s.image_id = image_id;
    s.columns = cols;
    s.rows = rows;
    s.frames = frames;
    s.frame_w = w / cols;
    s.frame_h = h / rows;
    return s;
}

inline SpriteState sprite(const char* name, SheetSpec spec, float x, float y) {
    SpriteState s;
    s.name = name;
    s.sheet = std::move(spec);
    s.x = x;
    s.y = y;
    return s;
}

// ---------------------------------------------------------------------------
// SceneSplash (0x42c180 ctor, 0x42c1f0 init, 0x42c760 step, 0x42ce20 update)
// ---------------------------------------------------------------------------
class SplashScene {
public:
    struct Config {
        bool show_gamehouse_animation = true;   // SHOW_GAMEHOUSE_ANIMATION
        int display_time_ms = 1700;             // SPLASH_DISPLAY_TIME
        int fade_time_ms = 500;                 // SPLASH_FADE_TIME
        // Extra `splash/splash<N>` cards (w, h). The shipped install has none.
        std::vector<std::pair<int, int>> cards;
    };
    static constexpr int GAMEHOUSE_EXTRA_MS = 400;     // 0x42cb20: + 0x190
    static constexpr int TRANSITION_MS = 500;           // TRANSITION_SPLASH
    static constexpr bool TRANSITION_WAIT = false;

    explicit SplashScene(FlowHost& host) : SplashScene(host, Config{}) {}
    SplashScene(FlowHost& host, Config cfg) : host_(host), cfg_(std::move(cfg)) {
        background_ = sprite("BACKGROUND", sheet("splash/BACKGROUND", SCREEN_W, SCREEN_H), 0, 0);
        cover_ = sprite("COVER", sheet("", SCREEN_W, SCREEN_H), 0, 0);  // image-less 800x600 sprite
        cover_.visible = false;
        card_ = sprite("SPLASH_CARD", sheet("SPLASH_CARD", 1, 1), 0, 0);
        card_.visible = false;
        step();
    }

    void update(int dt_ms) {
        queue_.update(dt_ms);
        // 0x42ce20: once the sequence is finished and "preloading" is
        // loaded, request the transition exactly once.
        if (finished_ && !transition_requested_ && host_.preloading_done()) {
            transition_requested_ = true;
            host_.transition(SceneId::Load, TRANSITION_MS, TRANSITION_WAIT);
        }
    }

    bool finished() const { return finished_; }
    bool transition_requested() const { return transition_requested_; }
    const SpriteState& background() const { return background_; }
    const SpriteState& cover() const { return cover_; }
    const SpriteState& card() const { return card_; }
    std::vector<const SpriteState*> draw_list() const { return {&background_, &card_, &cover_}; }

private:
    // One call per card (0x42c760). Re-queued through a MethodModifier.
    void step() {
        const bool have_card = card_index_ < cfg_.cards.size();
        if (show_gh_) {
            cover_.visible = true;
            cover_.alpha = 0.0f;
            queue_.push_delay(cfg_.display_time_ms + GAMEHOUSE_EXTRA_MS);
            if (have_card) queue_.push_alpha(&cover_, 0.0f, 1.0f, cfg_.fade_time_ms);
        }
        if (have_card) {
            const auto [w, h] = cfg_.cards[card_index_];
            card_.sheet = sheet("SPLASH_CARD", w, h);
            card_.x = static_cast<float>((SCREEN_W - w) / 2);   // 0x42cc0b: (800 - w) * 0.5
            card_.y = static_cast<float>((SCREEN_H - h) / 2);
            card_.alpha = 0.0f;
            card_.visible = true;
            queue_.push_alpha(&card_, 0.0f, 1.0f, cfg_.fade_time_ms);
            queue_.push_delay(cfg_.display_time_ms);
            queue_.push_alpha(&card_, 1.0f, 0.0f, cfg_.fade_time_ms);
            ++card_index_;
        }
        finished_ = !show_gh_ && !have_card;
        show_gh_ = false;
        if (!finished_) queue_.push_method([this] { step(); });
    }

    FlowHost& host_;
    Config cfg_;
    bool show_gh_ = cfg_.show_gamehouse_animation;
    std::size_t card_index_ = 0;
    bool finished_ = false;
    bool transition_requested_ = false;
    SpriteState background_, cover_, card_;
    ModifierQueue queue_;
};

// ---------------------------------------------------------------------------
// LoadScene (0x42af80 ctor, 0x42b080 init, 0x42bd70 update, 0x42bf30 next)
// ---------------------------------------------------------------------------
class LoadScene {
public:
    static constexpr int MIN_HOLD_MS = 3500;        // 0x42bdc7: elapsed < 0xdac
    static constexpr int LOGO_DELAY_MS = 500;       // ModifierDelay(500)
    static constexpr int LOGO_FRAME_MS = 100;       // ModifierFrame(100)
    static constexpr int CIRCLES_FRAME_MS = 100;    // frametime="100"
    static constexpr int TRANSITION_MS = 500;       // TRANSITION_DEFAULT
    static constexpr int BAR_W = 483;
    static constexpr int BAR_H = 69;

    explicit LoadScene(FlowHost& host) : host_(host) {
        background_ = sprite("BACKGROUND", sheet("preloading/BACKGROUND", SCREEN_W, SCREEN_H), 0, 0);
        const SheetSpec circles = sheet("ANI_CIRCLES", 6000, 600, 15, 2, 30);
        auto quadrant = [&](const char* name, float x, float y, Flip f, int start, Anchor a) {
            SpriteState s = sprite(name, circles, x, y);
            s.flip = f;
            s.frame = start;
            s.anchor = a;
            s.frame_time_ms = CIRCLES_FRAME_MS;
            return s;
        };
        circles_[0] = quadrant("CIRCLES_BRIGHT", 400, 300, Flip::Vertical, 19, Anchor::TopLeft);
        circles_[1] = quadrant("CIRCLES_BLEFT", 0, 300, Flip::Both, 0, Anchor::TopLeft);
        circles_[2] = quadrant("CIRCLES_LEFT", 0, 0, Flip::Horizontal, 0, Anchor::TopLeft);
        circles_[3] = quadrant("CIRCLES_RIGHT", 400, 0, Flip::None, 22, Anchor::TopLeft);
        spring_ = sprite("SPRING", sheet("IMAGE_SPRING", SCREEN_W, SCREEN_H), 0, 0);
        bar_empty_ = sprite("LOADING_BAR_EMPTY", sheet("IMAGE_LOADINGBAR_EMPTY", BAR_W, BAR_H), 159, 445);
        bar_ = sprite("PROGRESS_BAR", sheet("LOADING_BAR", BAR_W, BAR_H), 159, 445);
        bar_.has_clip = true;                        // 0x42b414..: zero-width clip
        bar_.clip = Rect{0, 0, 0, static_cast<float>(BAR_H)};
        click_text_ = sprite("CLICK_TEXT", sheet("CLICK_HERE", 400, 20), 320, 440);
        click_text_.anchor = Anchor::Top;
        click_text_.visible = false;                 // never shown in this build
        logo_ = sprite("LOGO", sheet("IMAGE_LOGO_ANIM", 5700, 864, 12, 2, 24), 161, 0);
        branding_ = sprite("GH_BRANDING", sheet("IMAGE_GH_BRANDING", 313, 70), 8, 521);

        // Init (0x42b080).
        host_.play_stream(SoundId::MusicGameLoad);
        logo_queue_.push_delay(LOGO_DELAY_MS);
        logo_queue_.push_frames(&logo_, 0, -1, LOGO_FRAME_MS);
        host_.play_sound(SoundId::MusicLogo);
    }

    // 0x42bd70. Deviation: the original calls Sleep(100) while loading;
    // we do not block the thread.
    void update(int dt_ms) {
        if (finished_) return;
        for (auto& c : circles_) c.tick_auto_frames(dt_ms);
        logo_queue_.update(dt_ms);
        elapsed_ms_ += dt_ms;
        if (host_.loader_done()) {
            bar_.has_clip = false;                   // fully lit bar
            if (elapsed_ms_ < MIN_HOLD_MS) return;
            finished_ = true;
            host_.transition(SceneId::Selection, TRANSITION_MS, true);   // 0x42bf30
            return;
        }
        const float p = host_.loader_progress();
        if (first_progress_ < 0.0f) first_progress_ = p;
        bar_.has_clip = true;
        bar_.clip = Rect{0, 0, clip_width(p, first_progress_), static_cast<float>(BAR_H)};
    }

    // width = 483 * (p - p0) / (1 - p0); p0 is the first sample seen.
    static float clip_width(float p, float p0) {
        if (p0 >= 1.0f) return static_cast<float>(BAR_W);
        float w = BAR_W * ((p - p0) / (1.0f - p0));
        if (w < 0.0f) w = 0.0f;
        if (w > BAR_W) w = static_cast<float>(BAR_W);
        return w;
    }

    bool finished() const { return finished_; }
    int elapsed_ms() const { return elapsed_ms_; }
    const SpriteState& logo() const { return logo_; }
    const SpriteState& bar() const { return bar_; }
    const SpriteState& circles(int i) const { return circles_[i]; }
    std::vector<const SpriteState*> draw_list() const {
        return {&background_, &circles_[0], &circles_[1], &circles_[2], &circles_[3],
                &spring_, &bar_empty_, &bar_, &click_text_, &logo_, &branding_};
    }

private:
    FlowHost& host_;
    int elapsed_ms_ = 0;
    bool finished_ = false;
    float first_progress_ = -1.0f;   // 0x42af80: [0x7b] = -1.0f
    SpriteState background_, circles_[4], spring_, bar_empty_, bar_, click_text_, logo_, branding_;
    ModifierQueue logo_queue_;
};

// ---------------------------------------------------------------------------
// SelectionScene (0x42d220 init, 0x42d9b0 enter, 0x42dd30 update)
// ---------------------------------------------------------------------------
struct MenuButton {
    SpriteState sprite;      // 4-row sheet: normal / hover / pressed / disabled
    int hit_threshold = 0;   // hitmaskthreshold (alpha, 0..255)
    bool enabled = true;
    bool hover = false;
    bool pressed = false;
    SoundId disabled_sound = SoundId::Button;
    bool has_disabled_sound = false;

    int frame_for_state() const {
        if (!enabled) return 3;
        if (pressed) return 2;
        if (hover) return 1;
        return 0;
    }
    bool contains(float px, float py) const {
        const float l = sprite.left(), t = sprite.top();
        return px >= l && py >= t && px < l + sprite.sheet.frame_w && py < t + sprite.sheet.frame_h;
    }
};

class SelectionScene {
public:
    static constexpr int FIRST_TIMER_BASE_MS = 3000;   // rand() + 3000
    static constexpr int RETRIGGER_BASE_MS = 7000;     // rand() + 7000
    static constexpr int COIL_FRAME_MS = 70;           // 0x46
    static constexpr int BOUNCE_FRAME_MS = 60;         // 0x3c
    static constexpr int BOUNCE_THIRD_PASS_THRESHOLD = 6;   // rand() > 6

    SelectionScene(FlowHost& host, tt2::CrtRandom& rng) : host_(host), rng_(rng) {
        background_ = sprite("BACKGROUND", sheet("sceneselection/BACKGROUND", SCREEN_W, SCREEN_H), 0, 0);
        logo_bounce_ = sprite("LOGO_BOUNCE", sheet("IMAGE_LOGO_BOUNCE", 1728, 252, 12, 1, 12), 513, 43);
        logo_coil_ = sprite("LOGO_COIL", sheet("IMAGE_LOGO_COIL", 3298, 253, 17, 1, 17), 128, 64);
        logo_static_ = sprite("LOGO_STATIC", sheet("IMAGE_LOGO_STATIC", 254, 204), 288, 84);

        auto add = [&](const char* name, const char* image, int w, int h, float x, float y, int threshold) {
            MenuButton b;
            b.sprite = sprite(name, sheet(image, w, h, 1, 4, 4), x, y);
            b.hit_threshold = threshold;
            buttons_.push_back(b);
        };
        add("BUTTON_CHANGENAME", "BUTTON_CHANGENAME", 155, 52, 324, 49, 0);   // 4 rows of 13 px
        add("BUTTON_PLAY_UNTIMED", "IMAGE_BUTTON_UNTIMED", 160, 536, 200, 290, 0);
        add("BUTTON_PLAY", "IMAGE_BUTTON_TIMED", 134, 536, 451, 290, 75);
        add("BUTTON_PLAY_WOD", "IMAGE_BUTTON_WOTD", 157, 596, 85, 378, 75);
        add("BUTTON_BLITZ", "IMAGE_BUTTON_LIGHTNING", 170, 536, 320, 393, 75);
        add("BUTTON_CRISSCROSS", "IMAGE_BUTTON_CROSSWORD", 206, 536, 528, 393, 75);
        add("BUTTON_GH", "BUTTON_GH", 115, 400, 653, 210, 0);
        add("BUTTON_SETTINGS", "BUTTON_SETTINGS", 157, 208, 3, 539, 100);
        add("BUTTON_HOWTOPLAY", "BUTTON_HOWTOPLAY", 157, 208, 163, 539, 100);
        add("BUTTON_HIGHSCORES", "BUTTON_HIGHSCORES", 157, 208, 323, 539, 100);
        add("BUTTON_TROPHY", "IMAGE_BUTTON_TROPHIES", 157, 208, 482, 539, 100);
        add("BUTTON_QUIT", "BUTTON_QUIT", 157, 208, 641, 539, 100);
        for (auto& b : buttons_) {
            if (b.sprite.name == "BUTTON_PLAY_WOD") {
                b.has_disabled_sound = true;
                b.disabled_sound = SoundId::ButtonWordOfTheDay;
            }
        }

        // 0x42d220: timers.
        coil_timer_ms_ = rng_.rand() + FIRST_TIMER_BASE_MS;
        bounce_timer_ms_ = rng_.rand() + FIRST_TIMER_BASE_MS;
        // 0x42d9b0: enter.
        host_.play_stream(SoundId::MusicMenu);
    }

    // 0x42dd30.
    void update(int dt_ms) {
        coil_timer_ms_ -= dt_ms;
        bounce_timer_ms_ -= dt_ms;
        if (coil_timer_ms_ <= 0) {
            host_.play_sound(SoundId::LoadingSpring);
            coil_queue_.push_frames(&logo_coil_, 0, -1, COIL_FRAME_MS);
            coil_timer_ms_ = rng_.rand() + RETRIGGER_BASE_MS;
        }
        if (bounce_timer_ms_ <= 0) {
            host_.play_sound(SoundId::LoadingBounce);
            int passes = 2;
            if (rng_.rand() > BOUNCE_THIRD_PASS_THRESHOLD) passes = 3;
            bounce_queue_.push_frames(&logo_bounce_, 0, -1, BOUNCE_FRAME_MS, passes);
            bounce_timer_ms_ = rng_.rand() + RETRIGGER_BASE_MS;
        }
        coil_queue_.update(dt_ms);
        bounce_queue_.update(dt_ms);
        for (auto& b : buttons_) b.sprite.frame = b.frame_for_state();
    }

    // Mouse handling. `hit` decides whether the pixel under the cursor
    // counts (the renderer supplies the alpha-mask test; tests pass
    // a rectangle-only predicate).
    using HitTest = std::function<bool(const MenuButton&, float, float)>;

    // True while the pointer sits on a clickable button. The SDL
    // front end switches to the hand cursor for this; disabled
    // buttons keep the arrow.
    bool pointer_over_clickable() const {
        for (const auto& b : buttons_)
            if (b.hover && b.enabled) return true;
        return false;
    }

    // Hover only changes the frame: the GF2 button parser (0x48d390) sets a
    // hover sound only from an `oversound` attribute, which no button in
    // the install XML has (welcome-flow.md §3).
    void mouse_move(float x, float y, const HitTest& hit) {
        for (auto& b : buttons_) {
            const bool over = b.contains(x, y) && hit(b, x, y);
            b.hover = over;
            if (!over) b.pressed = false;
        }
    }
    void mouse_down(float x, float y, const HitTest& hit) {
        for (auto& b : buttons_) b.pressed = b.contains(x, y) && hit(b, x, y);
    }
    void mouse_up(float x, float y, const HitTest& hit) {
        for (auto& b : buttons_) {
            const bool over = b.contains(x, y) && hit(b, x, y);
            if (b.pressed && over) {
                if (!b.enabled) {
                    if (b.has_disabled_sound) host_.play_sound(b.disabled_sound);
                } else {
                    host_.play_sound(SoundId::Button);
                    host_.menu_action(b.sprite.name);
                }
            }
            b.pressed = false;
        }
    }

    int coil_timer_ms() const { return coil_timer_ms_; }
    int bounce_timer_ms() const { return bounce_timer_ms_; }
    const SpriteState& logo_coil() const { return logo_coil_; }
    const SpriteState& logo_bounce() const { return logo_bounce_; }
    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    MenuButton* button(const std::string& name) {
        for (auto& b : buttons_) if (b.sprite.name == name) return &b;
        return nullptr;
    }
    std::vector<const SpriteState*> draw_list() const {
        std::vector<const SpriteState*> out{&background_};
        for (const auto& b : buttons_) out.push_back(&b.sprite);
        out.push_back(&logo_bounce_);
        out.push_back(&logo_coil_);
        out.push_back(&logo_static_);
        return out;
    }

private:
    FlowHost& host_;
    tt2::CrtRandom& rng_;
    int coil_timer_ms_ = 0;
    int bounce_timer_ms_ = 0;
    SpriteState background_, logo_bounce_, logo_coil_, logo_static_;
    std::vector<MenuButton> buttons_;
    ModifierQueue coil_queue_, bounce_queue_;
};

// ---------------------------------------------------------------------------
// Flow controller: owns the current scene and the fade-to-black.
// ---------------------------------------------------------------------------
class WelcomeFlow : public FlowHost {
public:
    // The application plugs in the real services here.
    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void(SoundId)> play_stream;
        std::function<bool()> loader_done = [] { return true; };
        std::function<float()> loader_progress = [] { return 1.0f; };
        std::function<bool()> preloading_done = [] { return true; };
        std::function<void(const std::string&)> menu_action;
    };

    explicit WelcomeFlow(Services s, std::uint32_t crt_seed = tt2::CrtRandom::DEFAULT_SEED)
        : services_(std::move(s)), rng_(crt_seed) {
        splash_ = std::make_unique<SplashScene>(*this);
        current_ = SceneId::Splash;
    }

    void update(int dt_ms) {
        // Fade: `fade_out_left` counts down to black, then the scene
        // switches and `fade_in_left` counts up from black. INFERRED
        // split of FadeBlackTransition's `time` (half out, half in).
        if (fade_out_left_ > 0) {
            fade_out_left_ -= dt_ms;
            if (fade_out_left_ <= 0) {
                fade_out_left_ = 0;
                switch_to(pending_);
                fade_in_left_ = fade_half_ms_;
            }
            if (!pending_wait_) update_current(dt_ms);   // wait="false": keep running
            return;
        }
        if (fade_in_left_ > 0) {
            fade_in_left_ -= dt_ms;
            if (fade_in_left_ < 0) fade_in_left_ = 0;
        }
        update_current(dt_ms);
    }

    // 0 = no black overlay, 1 = fully black.
    float black_alpha() const {
        if (fade_out_left_ > 0) return 1.0f - static_cast<float>(fade_out_left_) / fade_half_ms_;
        if (fade_in_left_ > 0) return static_cast<float>(fade_in_left_) / fade_half_ms_;
        return 0.0f;
    }

    SceneId current() const { return current_; }

    // The load screen holds the pointer in the "wait" (hourglass)
    // state until the loading sequence is done (design brief §2).
    bool pointer_busy() const { return current_ == SceneId::Load; }
    SplashScene* splash() { return splash_.get(); }
    LoadScene* load() { return load_.get(); }
    SelectionScene* selection() { return selection_.get(); }
    tt2::CrtRandom& rng() { return rng_; }

    std::vector<const SpriteState*> draw_list() const {
        switch (current_) {
            case SceneId::Splash: return splash_->draw_list();
            case SceneId::Load: return load_->draw_list();
            case SceneId::Selection: return selection_->draw_list();
        }
        return {};
    }

    // FlowHost
    void play_sound(SoundId id) override { if (services_.play_sound) services_.play_sound(id); }
    void play_stream(SoundId id) override { if (services_.play_stream) services_.play_stream(id); }
    void transition(SceneId to, int ms, bool wait) override {
        pending_ = to;
        pending_wait_ = wait;
        fade_half_ms_ = ms > 1 ? ms / 2 : 1;
        fade_out_left_ = fade_half_ms_;
    }
    bool loader_done() override { return services_.loader_done(); }
    float loader_progress() override { return services_.loader_progress(); }
    bool preloading_done() override { return services_.preloading_done(); }
    void menu_action(const std::string& name) override {
        if (services_.menu_action) services_.menu_action(name);
    }

private:
    void update_current(int dt_ms) {
        switch (current_) {
            case SceneId::Splash: splash_->update(dt_ms); break;
            case SceneId::Load: load_->update(dt_ms); break;
            case SceneId::Selection: selection_->update(dt_ms); break;
        }
    }
    void switch_to(SceneId to) {
        current_ = to;
        switch (to) {
            case SceneId::Splash: break;
            case SceneId::Load:
                splash_.reset();                      // 0x42b080 unloads "splash"
                load_ = std::make_unique<LoadScene>(*this);
                break;
            case SceneId::Selection:
                load_.reset();
                selection_ = std::make_unique<SelectionScene>(*this, rng_);
                break;
        }
    }

    Services services_;
    tt2::CrtRandom rng_;
    SceneId current_ = SceneId::Splash;
    SceneId pending_ = SceneId::Splash;
    bool pending_wait_ = true;
    int fade_half_ms_ = 250;
    int fade_out_left_ = 0;
    int fade_in_left_ = 0;
    std::unique_ptr<SplashScene> splash_;
    std::unique_ptr<LoadScene> load_;
    std::unique_ptr<SelectionScene> selection_;
};

}  // namespace tt2ui
