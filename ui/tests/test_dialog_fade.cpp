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
    ASSERT_TRUE(!f.closing_above());                          // a replace closes underneath
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

TEST(hand_off_push_keeps_the_dialog_below_at_full_alpha) {
    // Pause menu (1) open, then "Exit Game?" (2) pushed over it (0x408320).
    DialogFade under, top;
    top.update(1, 300);
    const HandOff h = hand_off(under, 1, top, 2);
    ASSERT_TRUE(!h.under_closing);
    ASSERT_TRUE(!h.top_closing);
    ASSERT_TRUE(!under.update(1, 16));
    ASSERT_TRUE(!top.update(2, 16));
    ASSERT_EQ(under.open_id(), 1);
    ASSERT_TRUE(std::fabs(under.open_alpha() - 1.0f) < 1e-5f);   // never dips
    ASSERT_TRUE(std::fabs(under.backdrop() - 1.0f) < 1e-5f);
    ASSERT_TRUE(!top.closing());                                  // the menu does not fade out of the top slot
    ASSERT_TRUE(std::fabs(top.open_alpha() - 0.08f) < 1e-5f);    // only the confirm fades in
}

TEST(hand_off_pop_fades_out_only_the_top_dialog) {
    DialogFade under, top;
    top.update(1, 300);
    hand_off(under, 1, top, 2);
    under.update(1, 300);
    top.update(2, 300);
    // NO (0x408c00) hides the confirm; the menu comes back to the top slot.
    const HandOff h = hand_off(under, 0, top, 1);
    ASSERT_TRUE(h.top_closing);
    ASSERT_TRUE(!h.under_closing);
    under.update(0, 16);
    top.update(1, 16);
    ASSERT_TRUE(!under.visible());                                // gone without a fade
    ASSERT_EQ(top.open_id(), 1);
    ASSERT_TRUE(std::fabs(top.open_alpha() - 1.0f) < 1e-5f);     // no fade back in
    ASSERT_EQ(top.closing_id(), 2);
    ASSERT_TRUE(std::fabs(top.closing_alpha() - 0.92f) < 1e-5f);
    ASSERT_TRUE(top.closing_above());                             // drawn over the menu, own backdrop
    ASSERT_TRUE(std::fabs(top.backdrop() - 1.0f) < 1e-5f);        // the menu's backdrop stays put
}

TEST(hand_off_push_keeps_a_half_faded_dialog_where_it_was) {
    DialogFade under, top;
    top.update(1, 100);                                           // half way in
    hand_off(under, 1, top, 2);
    ASSERT_TRUE(std::fabs(under.open_alpha() - 0.5f) < 1e-5f);
    under.update(1, 50);
    ASSERT_TRUE(std::fabs(under.open_alpha() - 0.75f) < 1e-5f);  // finishes its own fade-in
}

TEST(hand_off_ignores_a_plain_replace) {
    // The top dialog swapped for another, nothing under it: a normal crossfade.
    DialogFade under, top;
    top.update(1, 300);
    const HandOff h = hand_off(under, 0, top, 2);
    ASSERT_TRUE(!h.under_closing);
    ASSERT_TRUE(!h.top_closing);
    ASSERT_TRUE(!under.visible());
    ASSERT_EQ(top.open_id(), 1);                                  // untouched until update()
    ASSERT_TRUE(top.update(2, 16));
    ASSERT_EQ(top.closing_id(), 1);
}

TEST(hand_off_ignores_steady_and_empty_stacks) {
    DialogFade under, top;
    HandOff h = hand_off(under, 0, top, 0);
    ASSERT_TRUE(!h.under_closing && !h.top_closing);
    ASSERT_TRUE(!under.visible() && !top.visible());
    under.update(1, 300);
    top.update(2, 300);
    h = hand_off(under, 1, top, 2);                               // nothing moved
    ASSERT_TRUE(!h.under_closing && !h.top_closing);
    ASSERT_EQ(under.open_id(), 1);
    ASSERT_EQ(top.open_id(), 2);
}

TEST(dialog_fade_adopt_and_drop) {
    DialogFade f;
    ASSERT_TRUE(!f.adopt(3, 2.0f));                               // clamped to 1
    ASSERT_TRUE(std::fabs(f.open_alpha() - 1.0f) < 1e-5f);
    ASSERT_TRUE(!f.adopt(3, 0.5f));                               // same dialog: nothing closes
    ASSERT_TRUE(f.adopt(4, 1.0f));                                // a different one: 3 starts closing
    ASSERT_EQ(f.closing_id(), 3);
    f.drop();
    ASSERT_EQ(f.open_id(), 0);
    ASSERT_TRUE(f.closing());                                     // drop() leaves the closing fade alone
}
