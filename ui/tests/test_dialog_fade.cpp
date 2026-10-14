// Dialog show / hide fades and the backdrop colour (dialog_fade.hpp).
#include "test_harness.hpp"

#include <cmath>

#include "tt2ui/dialog_fade.hpp"

using namespace tt2ui;

TEST(darken_color_is_dark_blue_at_half_alpha) {
    // Color(0.5, 0, 0.25, 0.6) in A, R, G, B order: 0.25 * 255 + 0.5 = 64.25 -> 64,
    // 0.6 * 255 + 0.5 = 153.5 -> 153, 0.5 * 255 + 0.5 = 128.
    const Rgba8 c = darken_color(1.0f);
    ASSERT_EQ(int{c.r}, 0);
    ASSERT_EQ(int{c.g}, 64);
    ASSERT_EQ(int{c.b}, 153);
    ASSERT_EQ(int{c.a}, 128);
}

TEST(darken_color_alpha_follows_the_fade) {
    ASSERT_EQ(int{darken_color(0.0f).a}, 0);
    ASSERT_EQ(int{darken_color(0.5f).a}, 64);    // 0.25 * 255 + 0.5 = 64.25
    ASSERT_EQ(int{darken_color(0.5f).b}, 153);   // the colour itself never fades
}

TEST(color_byte_clamps_out_of_range_values) {
    ASSERT_EQ(int{color_byte(-1.0f)}, 0);
    ASSERT_EQ(int{color_byte(2.0f)}, 255);
}

TEST(dialog_fade_opens_linearly_over_200_ms) {
    DialogFade f;
    ASSERT_TRUE(!f.update(3, 50));
    ASSERT_EQ(f.open_id(), 3);
    ASSERT_TRUE(std::fabs(f.open_alpha() - 0.25f) < 1e-5f);
    f.update(3, 100);
    ASSERT_TRUE(std::fabs(f.open_alpha() - 0.75f) < 1e-5f);
    f.update(3, 100);
    ASSERT_TRUE(std::fabs(f.open_alpha() - 1.0f) < 1e-5f);   // clamped, never above 1
    ASSERT_TRUE(std::fabs(f.backdrop() - 1.0f) < 1e-5f);
}

TEST(dialog_fade_closes_from_the_alpha_it_had) {
    DialogFade f;
    f.update(3, 100);                 // half way in
    ASSERT_TRUE(f.update(0, 0));      // closed at 0.5: starts closing from there
    ASSERT_TRUE(f.closing());
    ASSERT_EQ(f.closing_id(), 3);
    ASSERT_TRUE(std::fabs(f.closing_alpha() - 0.5f) < 1e-5f);
    f.update(0, 60);
    ASSERT_TRUE(std::fabs(f.closing_alpha() - 0.2f) < 1e-5f);
    f.update(0, 60);
    ASSERT_TRUE(!f.closing());
    ASSERT_TRUE(!f.visible());
    ASSERT_TRUE(std::fabs(f.backdrop() - 0.0f) < 1e-5f);
}

TEST(dialog_fade_swaps_one_dialog_for_another) {
    DialogFade f;
    f.update(1, 300);                 // fully open
    ASSERT_TRUE(f.update(2, 50));     // replaced: 1 fades out while 2 fades in
    ASSERT_EQ(f.open_id(), 2);
    ASSERT_EQ(f.closing_id(), 1);
    ASSERT_TRUE(std::fabs(f.open_alpha() - 0.25f) < 1e-5f);
    ASSERT_TRUE(std::fabs(f.closing_alpha() - 0.75f) < 1e-5f);
    ASSERT_TRUE(std::fabs(f.backdrop() - 0.75f) < 1e-5f);   // one backdrop, the stronger fade
}

TEST(dialog_fade_without_a_dialog_draws_nothing) {
    DialogFade f;
    ASSERT_TRUE(!f.update(0, 16));
    ASSERT_TRUE(!f.visible());
    ASSERT_TRUE(!f.closing());
    ASSERT_TRUE(std::fabs(f.open_alpha() - 0.0f) < 1e-5f);
    ASSERT_TRUE(std::fabs(f.backdrop() - 0.0f) < 1e-5f);
}

TEST(dialog_fade_same_id_keeps_fading_in) {
    DialogFade f;
    f.update(4, 40);
    ASSERT_TRUE(!f.update(4, 40));    // same dialog: no restart, no close
    ASSERT_TRUE(std::fabs(f.open_alpha() - 0.4f) < 1e-5f);
    ASSERT_TRUE(!f.closing());
}
