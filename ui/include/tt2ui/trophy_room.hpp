// SCENE_TROPHY (TrophyScene) and the trophy texts and art the game
// scene's trophy popup shares with it, headless.
//
// CONFIRMED (REA, docs/reverse-engineering/trophies.md):
//   - dialogs.xml SCENE_TROPHY: 24 sprites TROPHY_0..23 (IMAGE_TROPHY1..24,
//     241x241 drawn at scale 0.5, centred) on a 6 x 4 grid, LABEL_TROPHY
//     (FONT_SETTINGSTEXT, centred at x 400, top 65, line height 22), the
//     header "TROPHIES", two ribbons, the SPARKLE burst and BUTTON_Menu.
//   - Enter (0x42fb20): MUSIC_trophy_room, then 0x42fe30 for each trophy:
//     not won = hidden; seen = shown; new = the popup's reveal (alpha 0->1
//     200 ms, scale 0.05->0.6 500 ms smoothstep, 0.6->0.5 300 ms), after
//     which the slot is marked seen.
//   - Hover (0x430000): the label shows the hovered trophy's description,
//     won or not, except an unwon Human Dictionary, which shows
//     HIDDEN_ACHIEVEMENT. The sparkle moves to a hovered trophy that is
//     won and hides otherwise; nothing hovered = the default text.
//   - The room shows no trophy names; FONT_TROPHYNAMES is the popup's.
#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/trophies.hpp"
#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/modifier.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

namespace trophy_strings {
// text.xml TEXT_TROPHY_TITLES (TEXT_GAMETROPHY_DESC01..24).
inline constexpr std::array<const char*, tt2::TROPHY_COUNT> TITLES = {
    "Quick\nDraw", "Straight\nShooter", "Bull’s-Eye", "Brainstorm", "Rolling\nThunder",
    "Lightning\nReflexes", "Nick\nof Time", "Speed\nSpeller", "Puzzle\nPro", "Perfect\nPuzzler",
    "Look Ma,\nNo Twisting!", "Bingo\nBlaster", "Bingo\nBoss", "Bonkers for\nBingos!", "With a\nTwist",
    "Dizzy", "Twisted\nTwister", "Word\nNerd", "Wordsmith", "SUPER\nSPELLER", "Well\nRounded",
    "Text Twist\nManiac", "Text Twist\nFanatic", "Human\nDictionary",
};
// text.xml TEXT_TROPHY_DESCRIPTIONS (TEXT_TROPHY_DESCRIPTION_01..24).
inline constexpr std::array<const char*, tt2::TROPHY_COUNT> DESCRIPTIONS = {
    "Quick Draw: Solve 5 words without twisting in\nUntimed, Timed or Word of the Day.",
    "Straight Shooter: Solve 10 words without twisting in\nUntimed, Timed or Word of the Day.",
    "Bull’s-eye: Solve 20 words without twisting in\nUntimed, Timed or Word of the Day.",
    "Brainstorm: Complete a Lightning round with no twisting.",
    "Rolling Thunder: Complete a Lightning round with no passing.",
    "Lightning Reflexes: Complete a Lightning\nround with no hints given.",
    "Nick of Time: Complete a Lightning round\nwith the timer at 3 seconds or less.",
    "Speed Speller: Complete a round in less than 5 seconds in\nUntimed, Timed or Word of the Day.",
    "Puzzle Pro: Solve a Crossword with no twisting.",
    "Perfect Puzzler: Solve a Crossword with no mistakes.",
    "Look Ma, No Twisting!: Solve all words in one round with no twisting\nin Untimed, Timed or Word of the Day.",
    "Bingo Blaster: Complete only bingo words for\n5 levels in a row in Untimed or Timed.",
    "Bingo Boss: Complete only bingo words for\n10 levels in a row in Untimed or Timed.",
    "Bonkers for Bingos!: Complete only bingo words for\n20 levels in a row in Untimed or Timed.",
    "With a Twist: Twist 1,000 times.",
    "Dizzy: Twist 5,000 times.",
    "Twisted Twister: Twist 10,000 times.",
    "Word Nerd: Spell 1,000 words.",
    "Wordsmith: Spell 5,000 words.",
    "SUPER SPELLER: Spell 10,000 words.",
    "Well Rounded: Complete a round in all game modes.",
    "Text Twist Maniac: Complete 10 rounds in all game modes.",
    "Text Twist Fanatic: Complete 30 rounds in all game modes.",
    "Human Dictionary: Spell 500 uncommon words.",
};
inline constexpr const char* DEFAULT = "Roll-over the trophies to read their description.";   // TEXT_TROPHY_DESCRIPTION_DEFAULT
inline constexpr const char* HIDDEN = "Secret Trophy!\nPlay more Text Twist and maybe you'll find it!";   // HIDDEN_ACHIEVEMENT
inline constexpr const char* HEADER = "TROPHIES";
}  // namespace trophy_strings

inline const char* trophy_title(int id) {
    return id >= 0 && static_cast<std::size_t>(id) < tt2::TROPHY_COUNT ? trophy_strings::TITLES[static_cast<std::size_t>(id)] : "";
}

// IMAGE_TROPHY<id + 1>: 241 x 241, one frame (resources.xml, section trophyscene).
inline constexpr int TROPHY_IMAGE_SIZE = 241;
inline std::string trophy_image_id(int id) { return "IMAGE_TROPHY" + std::to_string(id + 1); }
inline SpriteState trophy_sprite(int id, float x, float y) {
    const std::string image = trophy_image_id(id);
    SpriteState s = sprite(("TROPHY_" + std::to_string(id)).c_str(),
                           sheet(image.c_str(), TROPHY_IMAGE_SIZE, TROPHY_IMAGE_SIZE), x, y);
    s.anchor = Anchor::Center;
    return s;
}

// The reveal both the popup (0x429510) and the room (0x42fe30) play.
inline constexpr int TROPHY_FADE_IN_MS = 200;
inline constexpr int TROPHY_GROW_MS = 500;
inline constexpr int TROPHY_SETTLE_MS = 300;
inline constexpr float TROPHY_START_SCALE = 0.05f;
inline constexpr float TROPHY_REST_SCALE = 0.5f;       // the room's scalex / scaley
inline constexpr float TROPHY_OVERSHOOT = 0.6f;
inline void push_trophy_reveal(ModifierQueue& q, SpriteState* s, float rest, float overshoot) {
    q.push_alpha(s, 0.0f, 1.0f, TROPHY_FADE_IN_MS);
    q.push_scale(s, TROPHY_START_SCALE, overshoot, TROPHY_GROW_MS, Easing::Smoothstep);
    q.push_scale(s, overshoot, rest, TROPHY_SETTLE_MS);
}

class TrophyRoom {
public:
    struct Services {
        std::function<void(SoundId)> play_sound;
        std::function<void()> on_profile_change;   // a new trophy was marked seen: save
        std::function<void()> on_exit;             // BUTTON_Menu: back to the main menu
    };
    using HitTest = std::function<bool(const MenuButton&, float, float)>;

    // Grid (dialogs.xml): TROPHY_0 top left, row by row.
    static constexpr std::array<float, 6> COLUMN_X = {87, 211, 334, 457, 580, 703};
    static constexpr std::array<float, 4> ROW_Y = {171, 277, 383, 489};
    static constexpr float LABEL_X = 400, LABEL_Y = 65;
    static constexpr int LABEL_LINE_H = 22;
    static constexpr float HEADER_X = 400, HEADER_Y = 9;

    // The reveals point into trophies_: the room never moves.
    TrophyRoom(const TrophyRoom&) = delete;
    TrophyRoom& operator=(const TrophyRoom&) = delete;

    TrophyRoom(tt2::PlayerProfile& profile, Services services)
        : profile_(profile), services_(std::move(services)) {
        for (int i = 0; i < static_cast<int>(tt2::TROPHY_COUNT); ++i) {
            SpriteState s = trophy_sprite(i, COLUMN_X[static_cast<std::size_t>(i % 6)], ROW_Y[static_cast<std::size_t>(i / 6)]);
            s.scale = TROPHY_REST_SCALE;
            trophies_.push_back(s);
        }
        sparkle_ = sparkle(0, 0, 9);   // currentframe="9", visible="false"
        sparkle_.visible = false;
        ribbons_.push_back(sprite("RIBBON2", sheet("IMAGE_TROPHY_RIBBON", 60, 60), 210, 8));
        ribbons_.back().flip = Flip::Horizontal;
        ribbons_.back().anchor = Anchor::Top;
        ribbons_.push_back(sprite("RIBBON", sheet("IMAGE_TROPHY_RIBBON", 60, 60), 528, 8));
        ribbons_.back().anchor = Anchor::Top;
        MenuButton menu;
        menu.sprite = sprite("BUTTON_Menu", sheet("BUTTON_PAUSE_END", 181, 208, 1, 4, 4), 400, 547);
        menu.sprite.anchor = Anchor::Top;
        menu.hit_threshold = 0;
        buttons_.push_back(menu);
        enter();
    }

    // ---- state for the renderer -------------------------------------
    const std::vector<SpriteState>& trophies() const { return trophies_; }
    const SpriteState& sparkle_sprite() const { return sparkle_; }
    const std::vector<SpriteState>& ribbons() const { return ribbons_; }
    std::vector<MenuButton>& buttons() { return buttons_; }
    const std::vector<MenuButton>& buttons() const { return buttons_; }
    const std::string& label() const { return label_; }
    int hovered() const { return hovered_; }
    const char* background_id() const { return "trophyscene/BACKGROUND"; }
    // The hand cursor: over the button, or over a trophy (INFERRED: 0x430000
    // bumps the hand-cursor count on each trophy it enters).
    bool pointer_over_clickable() const {
        if (hovered_ >= 0) return true;
        for (const auto& b : buttons_) if (b.hover) return true;
        return false;
    }

    // ---- input ------------------------------------------------------
    // 0x430000: the trophy under the cursor decides the label and sparkle.
    // Unwon trophies are hidden but still hit (INFERRED: the hit test does
    // not look at visibility), so their descriptions show too.
    void mouse_move(float x, float y, const HitTest& hit) {
        hovered_ = -1;
        for (int i = 0; i < static_cast<int>(trophies_.size()); ++i)
            if (over_trophy(i, x, y)) { hovered_ = i; break; }
        if (hovered_ < 0) {
            label_ = trophy_strings::DEFAULT;
            sparkle_.visible = false;
        } else {
            const bool won = profile_.trophies[static_cast<std::size_t>(hovered_)] != tt2::TROPHY_NOT_WON;
            label_ = (!won && hovered_ == tt2::TROPHY_HUMAN_DICTIONARY)
                         ? trophy_strings::HIDDEN
                         : trophy_strings::DESCRIPTIONS[static_cast<std::size_t>(hovered_)];
            sparkle_.visible = won;
            sparkle_.x = trophies_[static_cast<std::size_t>(hovered_)].x;
            sparkle_.y = trophies_[static_cast<std::size_t>(hovered_)].y;
        }
        for (auto& b : buttons_) {
            const bool over = b.contains(x, y) && (!hit || hit(b, x, y));
            b.hover = over;
            if (!over) b.pressed = false;
        }
    }
    void mouse_down(float x, float y, const HitTest& hit) {
        for (auto& b : buttons_) b.pressed = b.contains(x, y) && (!hit || hit(b, x, y));
    }
    void mouse_up(float x, float y, const HitTest& hit) {
        for (auto& b : buttons_) {
            const bool over = b.contains(x, y) && (!hit || hit(b, x, y));
            const bool fire = b.pressed && over;
            b.pressed = false;
            if (fire) press(b.sprite.name);
        }
    }
    // 0x42fcb0: back to the main menu (TRANSITION_DEFAULT).
    void press(const std::string& name) {
        if (name != "BUTTON_Menu") return;
        if (services_.play_sound) services_.play_sound(SoundId::Button);
        if (services_.on_exit) services_.on_exit();
    }

    void update(int dt_ms) {
        for (auto& q : reveals_) q.update(dt_ms);
        sparkle_.tick_auto_frames(dt_ms);
        for (auto& b : buttons_) b.sprite.frame = b.frame_for_state();
    }

private:
    // 0x42fe30 for every slot; all new trophies reveal together.
    void enter() {
        label_ = trophy_strings::DEFAULT;
        bool changed = false;
        for (int i = 0; i < static_cast<int>(trophies_.size()); ++i) {
            SpriteState& s = trophies_[static_cast<std::size_t>(i)];
            std::int32_t& slot = profile_.trophies[static_cast<std::size_t>(i)];
            if (slot == tt2::TROPHY_NOT_WON) {
                s.visible = false;
                s.alpha = 0.0f;
            } else if (slot == tt2::TROPHY_NEW) {
                s.alpha = 0.0f;
                s.scale = TROPHY_START_SCALE;
                reveals_.emplace_back();
                push_trophy_reveal(reveals_.back(), &s, TROPHY_REST_SCALE, TROPHY_OVERSHOOT);
                // DEVIATION: the original writes 2 when the reveal ends; it is
                // written now so leaving mid-reveal still counts as seen.
                slot = tt2::TROPHY_SEEN;
                changed = true;
            }
        }
        if (changed && services_.on_profile_change) services_.on_profile_change();
    }

    bool over_trophy(int i, float x, float y) const {
        const SpriteState& s = trophies_[static_cast<std::size_t>(i)];
        const float half = TROPHY_IMAGE_SIZE * TROPHY_REST_SCALE * 0.5f;
        return x >= s.x - half && x < s.x + half && y >= s.y - half && y < s.y + half;
    }

    tt2::PlayerProfile& profile_;
    Services services_;
    std::vector<SpriteState> trophies_;
    std::vector<SpriteState> ribbons_;
    SpriteState sparkle_;
    std::vector<MenuButton> buttons_;
    std::string label_;
    int hovered_ = -1;
    std::deque<ModifierQueue> reveals_;    // one chain per new trophy, run side by side
};

}  // namespace tt2ui
