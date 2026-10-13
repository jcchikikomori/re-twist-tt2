// GF2 "modifiers": small time-based actions chained on a queue.
//
// The original engine (see docs/reverse-engineering/welcome-flow.md §0)
// has ModifierDelay, ModifierAlpha, ModifierFrame and ModifierMethod.
// A queue runs them one after another; each modifier has its own
// target sprite, so one queue can fade one sprite, wait, then animate
// another. Leftover time from a modifier that finishes mid-frame is
// carried into the next one.
#pragma once

#include <deque>
#include <functional>
#include <memory>

#include "tt2ui/sprite.hpp"

namespace tt2ui {

class Modifier {
public:
    virtual ~Modifier() = default;
    // Advance by `dt` ms. Returns the unused milliseconds (>= 0) when
    // the modifier has finished, or -1 while it is still running.
    virtual int update(int dt) = 0;
};

// ModifierDelay (0x450b10): wait `ms`.
class DelayModifier : public Modifier {
public:
    explicit DelayModifier(int ms) : ms_(ms) {}
    int update(int dt) override {
        elapsed_ += dt;
        if (elapsed_ >= ms_) return elapsed_ - ms_;
        return -1;
    }
private:
    int ms_;
    int elapsed_ = 0;
};

// ModifierAlpha (0x451030): linear alpha from -> to over `ms`.
class AlphaModifier : public Modifier {
public:
    AlphaModifier(SpriteState* target, float from, float to, int ms)
        : target_(target), from_(from), to_(to), ms_(ms) {}
    int update(int dt) override {
        elapsed_ += dt;
        if (ms_ <= 0 || elapsed_ >= ms_) {
            target_->alpha = to_;
            return ms_ <= 0 ? dt : elapsed_ - ms_;
        }
        target_->alpha = from_ + (to_ - from_) * (static_cast<float>(elapsed_) / ms_);
        return -1;
    }
private:
    SpriteState* target_;
    float from_, to_;
    int ms_;
    int elapsed_ = 0;
};

// ModifierFrame (0x4793c0 / 0x479420): step frames first..last at
// `ms_per_frame`, `passes` times. last == -1 means the sheet's last
// frame.
class FrameModifier : public Modifier {
public:
    FrameModifier(SpriteState* target, int first, int last, int ms_per_frame, int passes = 1)
        : target_(target), first_(first), last_(last), ms_(ms_per_frame), passes_(passes) {}
    int update(int dt) override {
        const int last = last_ < 0 ? target_->sheet.frames - 1 : last_;
        const int count = last - first_ + 1;
        const int total = count * ms_ * passes_;
        elapsed_ += dt;
        if (count <= 0 || ms_ <= 0 || elapsed_ >= total) {
            target_->frame = last;
            return elapsed_ - total > 0 ? elapsed_ - total : 0;
        }
        target_->frame = first_ + (elapsed_ / ms_) % count;
        return -1;
    }
private:
    SpriteState* target_;
    int first_, last_, ms_, passes_;
    int elapsed_ = 0;
};

// ModifierMethod (0x42c030 / 0x4643c0): call back once.
class MethodModifier : public Modifier {
public:
    explicit MethodModifier(std::function<void()> fn) : fn_(std::move(fn)) {}
    int update(int dt) override {
        fn_();
        return dt;
    }
private:
    std::function<void()> fn_;
};

class ModifierQueue {
public:
    void push(std::unique_ptr<Modifier> m) { items_.push_back(std::move(m)); }
    void push_delay(int ms) { push(std::make_unique<DelayModifier>(ms)); }
    void push_alpha(SpriteState* t, float from, float to, int ms) {
        push(std::make_unique<AlphaModifier>(t, from, to, ms));
    }
    void push_frames(SpriteState* t, int first, int last, int ms_per_frame, int passes = 1) {
        push(std::make_unique<FrameModifier>(t, first, last, ms_per_frame, passes));
    }
    void push_method(std::function<void()> fn) {
        push(std::make_unique<MethodModifier>(std::move(fn)));
    }

    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }

    // Run the head modifier; pop it when finished and carry the
    // leftover time into the next one. A MethodModifier may push new
    // modifiers while running, which is why the loop re-reads the
    // front each iteration.
    void update(int dt) {
        int remaining = dt;
        while (!items_.empty() && remaining >= 0) {
            auto* head = items_.front().get();
            const int left = head->update(remaining);
            if (left < 0) return;
            items_.pop_front();
            remaining = left;
            if (remaining == 0) {
                // Finished exactly on the boundary: start the next one
                // on the next frame, like the original.
                return;
            }
        }
    }

private:
    std::deque<std::unique_ptr<Modifier>> items_;
};

}  // namespace tt2ui
