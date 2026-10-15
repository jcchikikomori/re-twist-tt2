// Frame timing (frame_clock.hpp; deviation UX-7, display-and-timing.md §4).
#include "test_harness.hpp"

#include <cstdint>

#include "tt2ui/frame_clock.hpp"

using namespace tt2ui;

namespace {
// Steps a clock through `frames` frames of `period_ns` each and sums the ms.
int run(FrameClock& c, std::uint64_t start, std::uint64_t period_ns, int frames, int speed = 1) {
    int total = 0;
    for (int i = 1; i <= frames; ++i) total += c.advance(start + period_ns * static_cast<std::uint64_t>(i), speed);
    return total;
}
}  // namespace

TEST(frame_clock_first_call_returns_zero) {
    FrameClock c;
    ASSERT_EQ(c.advance(5'000'000'000ULL), 0);
}

TEST(frame_clock_240hz_adds_up_to_real_time) {
    // 24 frames of 1/240 s are exactly 100 ms: steps 4, 4, 4, 5, ...
    FrameClock c;
    c.advance(0);
    ASSERT_EQ(c.advance(4'166'666), 4);
    ASSERT_EQ(c.advance(8'333'333), 4);
    ASSERT_EQ(c.advance(12'500'000), 4);
    ASSERT_EQ(c.advance(16'666'666), 4);
    ASSERT_EQ(c.advance(20'833'333), 4);
    ASSERT_EQ(c.advance(25'000'000), 5);                     // the carry pays out
    FrameClock d;
    d.advance(0);
    ASSERT_EQ(run(d, 0, NS_PER_S / 240, 240), 999);           // 240 x 4166666 ns = 999.99984 ms: only the last fraction is unpaid
}

TEST(frame_clock_144hz_has_no_drift_over_a_minute) {
    FrameClock c;
    c.advance(0);
    const int total = run(c, 0, NS_PER_S / 144, 144 * 60);
    ASSERT_TRUE(total >= 59'999 && total <= 60'000);         // 6,944,444 ns frames lose < 1 ms in a minute
}

TEST(frame_clock_speed_multiplies_the_step) {
    FrameClock c;
    c.advance(0);
    ASSERT_EQ(c.advance(10 * NS_PER_MS, 4), 40);              // --speed 4
    ASSERT_EQ(c.advance(10 * NS_PER_MS + 250'000, 4), 1);      // 0.25 ms x 4 = 1 ms
}

TEST(frame_clock_clamps_a_long_frame_and_drops_the_rest) {
    FrameClock c;
    c.advance(0);
    ASSERT_EQ(c.advance(250 * NS_PER_MS + 600'000), FRAME_DT_MAX_MS);   // 250.6 ms -> 100
    ASSERT_EQ(c.advance(266 * NS_PER_MS + 600'000), 16);             // not inflated by the lost 150.6 ms
    FrameClock fast;
    fast.advance(0);
    ASSERT_EQ(fast.advance(16 * NS_PER_MS, 20), FRAME_DT_MAX_MS);    // --speed 20: clamped like SDL2's 320
}

TEST(frame_clock_ignores_a_clock_going_backwards) {
    FrameClock c;
    c.advance(1'000 * NS_PER_MS);
    ASSERT_EQ(c.advance(900 * NS_PER_MS), 0);
    ASSERT_EQ(c.advance(916 * NS_PER_MS), 16);                // and carries on from there
}

TEST(frame_budget_matches_the_limits) {
    ASSERT_TRUE(frame_budget_ns(60) == 16'666'666ULL);
    ASSERT_TRUE(frame_budget_ns(240) == 4'166'666ULL);
    ASSERT_TRUE(frame_budget_ns(0) == 0ULL);
}

TEST(frame_pacer_waits_out_the_rest_of_the_frame) {
    FramePacer p;
    const std::uint64_t b = frame_budget_ns(60);
    ASSERT_TRUE(p.wait_ns(0, b) == 0ULL);                     // first frame sets the schedule
    ASSERT_TRUE(p.wait_ns(4 * NS_PER_MS, b) == b - 4 * NS_PER_MS);   // a 4 ms frame sleeps 12.67 ms
    // The next frame aims at 2 budgets from the start, not "now + budget".
    ASSERT_TRUE(p.wait_ns(b + 5 * NS_PER_MS, b) == b - 5 * NS_PER_MS);
}

TEST(frame_pacer_does_not_rush_after_a_long_stall) {
    FramePacer p;
    const std::uint64_t b = frame_budget_ns(120);
    p.wait_ns(0, b);
    ASSERT_TRUE(p.wait_ns(500 * NS_PER_MS, b) == 0ULL);       // a stall: no sleep...
    ASSERT_TRUE(p.wait_ns(500 * NS_PER_MS + NS_PER_MS, b) == b - NS_PER_MS);   // ...and a fresh schedule
}

TEST(frame_pacer_never_waits_without_a_limit) {
    FramePacer p;
    ASSERT_TRUE(p.wait_ns(0, 0) == 0ULL);
    ASSERT_TRUE(p.wait_ns(1, 0) == 0ULL);
    ASSERT_TRUE(p.wait_ns(2, frame_budget_ns(60)) == 0ULL);   // a limit turned on restarts the schedule
}
