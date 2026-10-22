// The two popups every game scene shares: the message popup (0x428ec0)
// and the trophy popup (0x429510). Classic (game_scene.hpp) and Lightning
// (blitz_scene.hpp) both run them from their update; only the message
// popup's anchor differs (0x423a20: (400, 150); 0x410f00: (400, 55)).
// SDL-free, like the rest of the scene model.
#pragma once

#include <deque>
#include <optional>

#include "tt2ui/modifier.hpp"
#include "tt2ui/sprite.hpp"
#include "tt2ui/trophy_room.hpp"
#include "tt2ui/welcome_flow.hpp"

namespace tt2ui {

// ---------------------------------------------------------------------------
// Message popups (0x428ec0, game-scene.md §5)
// ---------------------------------------------------------------------------
// Every message and banner in the scene is one popup, queued by
// 0x428e20(kind) and shown one at a time. Kind 2 is IMAGE_POPUP_BINGO,
// kind 1 IMAGE_POPUP_ALLWORDS, any other kind IMAGE_IMAGE_POPUP_<kind>.
// The COMMENTS (tt_commentary.png) and GAME_OVER (tt_gameover.PNG)
// sprites in dialogs.xml are never looked up by the exe and stay at
// alpha 0, so the port does not draw them.
enum class PopupKind {
    NotListed = 0,       // image_popup_0: the word isn't in the dictionary
    AllWords = 1,        // IMAGE_POPUP_ALLWORDS banner
    Bingo = 2,           // IMAGE_POPUP_BINGO banner
    AlreadyGuessed = 3,  // image_popup_3
    EnoughOfLength = 5,  // image_popup_5
    ExtraTime = 6,       // image_popup_6: Lightning's +8 s (0x4116e0)
    NoneOfLength = 8,    // image_popup_8
    Definitions = 10,    // image_popup_10: click entered words for definitions
    // image_popup_11: "You found the WORD OF THE DAY!". The first bingo of a
    // Word of the Day round queues it instead of the BINGO banner (0x424730
    // @0x424aa1 -> 0x428e20(0xb), game-scene.md §8.5).
    WordOfTheDay = 11,
};

constexpr float POPUP_X = 400.0f;          // 0x423a20: +0x34c = [0x59b984]
constexpr float POPUP_Y = 150.0f;          // 0x423a20: +0x350 = [0x59b980]
constexpr int POPUP_FADE_IN_MS = 150;      // ModifierAlpha(0 -> 1, 0x96)
constexpr int POPUP_GROW_MS = 250;         // ModifierScale(0 -> 1.2, 0xfa, smoothstep 0x4513d0)
constexpr float POPUP_OVERSHOOT = 1.2f;    // [0x59b998]
constexpr int POPUP_SETTLE_MS = 100;       // ModifierScale(1.2 -> 1, 0x64, linear)
constexpr int POPUP_BANNER_FRAME_MS = 50;  // ModifierFrame(one pass, 0x32)
constexpr int POPUP_BANNER_HOLD_MS = 1000; // ModifierDelay(0x3e8) after the frames
constexpr int POPUP_MESSAGE_HOLD_MS = 900; // ModifierDelay(0x384)
constexpr int POPUP_FADE_OUT_MS = 250;     // ModifierAlpha(1 -> 0, 0xfa)

inline bool popup_is_banner(PopupKind k) { return k == PopupKind::Bingo || k == PopupKind::AllWords; }

// Banner sheets from resources.xml; the message sizes are the PNG sizes.
inline SheetSpec popup_sheet(PopupKind k) {
    switch (k) {
        case PopupKind::Bingo: return sheet("IMAGE_POPUP_BINGO", 1900, 420, 5, 3, 15);
        case PopupKind::AllWords: return sheet("IMAGE_POPUP_ALLWORDS", 2280, 420, 6, 3, 18);
        case PopupKind::NotListed: return sheet("IMAGE_IMAGE_POPUP_0", 254, 97);
        case PopupKind::AlreadyGuessed: return sheet("IMAGE_IMAGE_POPUP_3", 295, 97);
        case PopupKind::EnoughOfLength: return sheet("IMAGE_IMAGE_POPUP_5", 339, 103);
        case PopupKind::ExtraTime: return sheet("IMAGE_IMAGE_POPUP_6", 324, 107);
        case PopupKind::NoneOfLength: return sheet("IMAGE_IMAGE_POPUP_8", 301, 102);
        case PopupKind::Definitions: return sheet("IMAGE_IMAGE_POPUP_10", 326, 121);
        // A message, not a banner: 0x428ec0 @0x42934c runs the banner chain
        // only for kinds 2 and 1 (game-scene.md §8.5, W5 audit).
        case PopupKind::WordOfTheDay: return sheet("IMAGE_IMAGE_POPUP_11", 324, 107);
    }
    return SheetSpec{};
}

// Fade in, grow to 1.2 and settle, hold (a banner runs its frames
// first), fade out. Worked example: a message is on screen for
// 150 + 250 + 100 + 900 + 250 = 1650 ms; the 15-frame bingo banner for
// 150 + 250 + 100 + 15*50 + 1000 + 250 = 2500 ms.
inline int popup_duration_ms(PopupKind k) {
    const int frames = popup_is_banner(k) ? popup_sheet(k).frames * POPUP_BANNER_FRAME_MS + POPUP_BANNER_HOLD_MS
                                          : POPUP_MESSAGE_HOLD_MS;
    return POPUP_FADE_IN_MS + POPUP_GROW_MS + POPUP_SETTLE_MS + frames + POPUP_FADE_OUT_MS;
}

// The queue (+0x334) and the popup on screen (+0x348). It holds pointers
// to its own sprite, so it stays where it was built (no copies).
class MessagePopups {
public:
    MessagePopups(float x = POPUP_X, float y = POPUP_Y) : x_(x), y_(y) { sprite_.visible = false; }
    MessagePopups(const MessagePopups&) = delete;
    MessagePopups& operator=(const MessagePopups&) = delete;

    // 0x428e20: add to the queue; update() shows it when the screen is free.
    void push(PopupKind k) { waiting_.push_back(k); }
    // 0x425c50 -> 0x42ace0: a new round drops the queued ones.
    void drop_waiting() { waiting_.clear(); }
    // 0x426860: when no popup is up, the next queued one starts.
    void update(int dt_ms) {
        if (!kind_ && !waiting_.empty()) {
            const PopupKind next = waiting_.front();
            waiting_.pop_front();
            start(next);
        }
        queue_.update(dt_ms);
    }

    const SpriteState& sprite() const { return sprite_; }
    std::optional<PopupKind> kind() const { return kind_; }
    std::size_t waiting() const { return waiting_.size(); }

private:
    // 0x428ec0: centred on the anchor, alpha 0 and scale 0, then the
    // modifier chain (each step starts when the previous one ends).
    void start(PopupKind k) {
        kind_ = k;
        sprite_ = tt2ui::sprite("POPUP", popup_sheet(k), x_, y_);
        sprite_.anchor = Anchor::Center;
        sprite_.alpha = 0.0f;
        sprite_.scale = 0.0f;
        queue_.clear();
        queue_.push_alpha(&sprite_, 0.0f, 1.0f, POPUP_FADE_IN_MS);
        queue_.push_scale(&sprite_, 0.0f, POPUP_OVERSHOOT, POPUP_GROW_MS, Easing::Smoothstep);
        queue_.push_scale(&sprite_, POPUP_OVERSHOOT, 1.0f, POPUP_SETTLE_MS);
        if (popup_is_banner(k)) {
            queue_.push_frames(&sprite_, 0, -1, POPUP_BANNER_FRAME_MS);
            queue_.push_delay(POPUP_BANNER_HOLD_MS);
        } else {
            queue_.push_delay(POPUP_MESSAGE_HOLD_MS);
        }
        queue_.push_alpha(&sprite_, 1.0f, 0.0f, POPUP_FADE_OUT_MS);
        // 0x4235c0 clears the "popup up" flag (+0x348).
        queue_.push_method([this] {
            sprite_.visible = false;
            kind_.reset();
        });
    }

    float x_, y_;
    SpriteState sprite_;
    std::optional<PopupKind> kind_;
    std::deque<PopupKind> waiting_;
    ModifierQueue queue_;
};

// ---------------------------------------------------------------------------
// Trophy popup (0x429510, trophies.md §1)
// ---------------------------------------------------------------------------
// IMAGE_TROPHY<id+1> at (725, 475) and the title at (725, 535), both grow
// in, hold 2 s and fade out (3.5 s); SOUND_game_trophy_won as it starts.
constexpr float TROPHY_POPUP_X = 725.0f, TROPHY_POPUP_Y = 475.0f, TROPHY_LABEL_Y = 535.0f;
constexpr float TROPHY_LABEL_OVERSHOOT = 1.2f;
constexpr int TROPHY_POPUP_HOLD_MS = 2000;
constexpr int TROPHY_POPUP_FADE_OUT_MS = 500;

class TrophyPopups {
public:
    TrophyPopups() {
        sprite_.visible = false;
        label_.visible = false;
    }
    TrophyPopups(const TrophyPopups&) = delete;
    TrophyPopups& operator=(const TrophyPopups&) = delete;

    // 0x429480: award sites push their result; -1 (nothing won) is dropped
    // here instead of at 0x429510.
    void push(int id) {
        if (id >= 0) waiting_.push_back(id);
    }
    // The next trophy starts when `free` (the scene decides: no dialog and
    // no round-end step pending) and none is up. True on the frame one
    // starts, so the scene plays SOUND_game_trophy_won.
    bool update(int dt_ms, bool free) {
        bool started = false;
        if (id_ < 0 && !waiting_.empty() && free) {
            const int next = waiting_.front();
            waiting_.pop_front();
            start(next);
            started = true;
        }
        queue_.update(dt_ms);
        label_queue_.update(dt_ms);
        return started;
    }

    const SpriteState& sprite() const { return sprite_; }
    const SpriteState& label() const { return label_; }
    int id() const { return id_; }
    std::size_t waiting() const { return waiting_.size(); }

private:
    void start(int id) {
        id_ = id;
        sprite_ = trophy_sprite(id, TROPHY_POPUP_X, TROPHY_POPUP_Y);
        sprite_.alpha = 0.0f;
        sprite_.scale = TROPHY_START_SCALE;
        label_ = SpriteState{};
        label_.name = trophy_title(id);
        label_.anchor = Anchor::Center;
        label_.x = TROPHY_POPUP_X;
        label_.y = TROPHY_LABEL_Y;
        label_.alpha = 0.0f;
        label_.scale = 0.0f;
        queue_.clear();
        label_queue_.clear();
        push_trophy_reveal(queue_, &sprite_, TROPHY_REST_SCALE, TROPHY_OVERSHOOT);
        queue_.push_delay(TROPHY_POPUP_HOLD_MS);
        queue_.push_alpha(&sprite_, 1.0f, 0.0f, TROPHY_POPUP_FADE_OUT_MS);
        // 0x4235d0 clears the busy flag (+0x368).
        queue_.push_method([this] {
            sprite_.visible = false;
            label_.visible = false;
            id_ = -1;
        });
        push_trophy_reveal(label_queue_, &label_, 1.0f, TROPHY_LABEL_OVERSHOOT);
        label_queue_.push_delay(TROPHY_POPUP_HOLD_MS);
        label_queue_.push_alpha(&label_, 1.0f, 0.0f, TROPHY_POPUP_FADE_OUT_MS);
    }

    std::deque<int> waiting_;   // +0x354
    int id_ = -1;               // +0x368 busy, and which trophy
    SpriteState sprite_, label_;
    ModifierQueue queue_, label_queue_;
};

}  // namespace tt2ui
