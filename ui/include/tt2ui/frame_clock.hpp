// Frame timing for high refresh rate screens (deviation UX-7), free of SDL so
// it is unit-tested.
//
// CONFIRMED (display-and-timing.md §4): the original runs a 16 ms Sleep cap
// (about 62.5 fps) with vsync on, measures each frame with timeGetTime() in
// whole milliseconds, and clamps the step to 100 ms (0x4924c0), losing the
// rest. Every model in the port also steps in whole milliseconds (int dt_ms).
//
// The port drops the cap. FrameClock reads a nanosecond clock and keeps the
// fraction of a millisecond each frame leaves over, so at 240 Hz (4.1667 ms
// per frame) the steps run 4, 4, 4, 5, ... and add up to the real time
// instead of drifting. The 100 ms clamp stays, and what it cuts is dropped,
// as in the original. FramePacer sleeps the rest of a frame for the
// optional frame limit.
#pragma once

#include <cstdint>

namespace tt2ui {

inline constexpr int FRAME_DT_MAX_MS = 100;   // 0x4924c0
inline constexpr std::uint64_t NS_PER_MS = 1000000;
inline constexpr std::uint64_t NS_PER_S = 1000000000;

// Nanoseconds between frame starts at `fps` (0: unlimited; the choices are
// tt2::FRAME_LIMITS).
inline constexpr std::uint64_t frame_budget_ns(int fps) {
    return fps > 0 ? NS_PER_S / static_cast<std::uint64_t>(fps) : 0;
}

class FrameClock {
public:
    // The milliseconds of game time since the last call, `speed` times the
    // real time (--speed). The first call returns 0; so does a clock that
    // went backwards.
    int advance(std::uint64_t now_ns, int speed = 1) {
        if (!started_ || now_ns < last_ns_) {
            started_ = true;
            last_ns_ = now_ns;
            return 0;
        }
        carry_ns_ += (now_ns - last_ns_) * static_cast<std::uint64_t>(speed < 1 ? 1 : speed);
        last_ns_ = now_ns;
        std::uint64_t ms = carry_ns_ / NS_PER_MS;
        carry_ns_ %= NS_PER_MS;
        if (ms > static_cast<std::uint64_t>(FRAME_DT_MAX_MS)) {
            ms = FRAME_DT_MAX_MS;   // 0x4924c0: the rest is lost
            carry_ns_ = 0;
        }
        return static_cast<int>(ms);
    }

private:
    bool started_ = false;
    std::uint64_t last_ns_ = 0;
    std::uint64_t carry_ns_ = 0;
};

// How long to wait so frames start `budget_ns` apart. Each frame aims at the
// previous target plus one budget, so the rate holds on average; a frame
// that ran more than a whole budget late restarts the schedule instead of
// rushing to catch up.
class FramePacer {
public:
    std::uint64_t wait_ns(std::uint64_t now_ns, std::uint64_t budget_ns) {
        if (budget_ns == 0 || !started_) {
            started_ = budget_ns != 0;
            next_ns_ = now_ns;
            return 0;
        }
        const std::uint64_t target = next_ns_ + budget_ns;
        if (now_ns >= target) {
            next_ns_ = now_ns - target < budget_ns ? target : now_ns;
            return 0;
        }
        next_ns_ = target;
        return target - now_ns;
    }

private:
    bool started_ = false;
    std::uint64_t next_ns_ = 0;
};

}  // namespace tt2ui
