// Tooltips (tooltip.hpp): timing, placement and the dialogs.xml table.
#include "test_harness.hpp"

#include <string>

#include "tt2ui/tooltip.hpp"

using namespace tt2ui;

TEST(tooltip_shows_after_one_second_on_the_same_widget) {
    TooltipTracker t;
    t.update("Game/BUTTON_Twist", "Scramble the letters", 100, 100, 16);   // arrives: timers start
    ASSERT_TRUE(!t.visible());
    t.update("Game/BUTTON_Twist", "Scramble the letters", 100, 100, 999);
    ASSERT_TRUE(!t.visible());                                              // 1 ms left
    t.update("Game/BUTTON_Twist", "Scramble the letters", 120, 110, 1);
    ASSERT_TRUE(t.visible());
    ASSERT_EQ(t.text(), std::string("Scramble the letters"));
    ASSERT_TRUE(t.anchor_x() == 120.0f && t.anchor_y() == 110.0f);        // placed where the cursor is then
    t.update("Game/BUTTON_Twist", "Scramble the letters", 140, 130, 16);
    ASSERT_TRUE(t.anchor_x() == 120.0f);                                    // and stays put
}

TEST(tooltip_hides_after_ten_seconds_until_another_widget) {
    TooltipTracker t;
    t.update("a", "A", 0, 0, 0);
    t.update("a", "A", 0, 0, TOOLTIP_DELAY_MS);
    ASSERT_TRUE(t.visible());
    t.update("a", "A", 0, 0, TOOLTIP_SHOW_MS - 1);
    ASSERT_TRUE(t.visible());
    t.update("a", "A", 0, 0, 16);
    ASSERT_TRUE(!t.visible());
    t.update("a", "A", 0, 0, 5000);
    ASSERT_TRUE(!t.visible());                 // the same widget never shows it again
    t.update("b", "B", 0, 0, 16);              // another one starts over
    t.update("b", "B", 0, 0, TOOLTIP_DELAY_MS);
    ASSERT_TRUE(t.visible());
    ASSERT_EQ(t.text(), std::string("B"));
}

TEST(tooltip_moving_off_or_onto_another_widget_resets_the_delay) {
    TooltipTracker t;
    t.update("a", "A", 0, 0, 0);
    t.update("a", "A", 0, 0, 900);
    t.update("", "", 0, 0, 16);                // off every widget
    ASSERT_TRUE(!t.visible());
    t.update("a", "A", 0, 0, 0);
    t.update("a", "A", 0, 0, 900);
    ASSERT_TRUE(!t.visible());                 // 900 ms again, not 1800
    t.update("a", "A", 0, 0, TOOLTIP_DELAY_MS);
    ASSERT_TRUE(t.visible());
    t.update("b", "", 0, 0, 16);               // a widget with no text counts as none
    ASSERT_TRUE(!t.visible());
}

TEST(tooltip_place_goes_under_the_cursor) {
    const TooltipPlace p = tooltip_place(100, 100, 120, 20, 800, 600);
    ASSERT_TRUE(p.x == 100.0f && p.y == 124.0f);
}

TEST(tooltip_place_moves_aside_near_the_bottom) {
    // 590 + 24 + 23 > 600: 16 right, back to the cursor's y, then as low as fits.
    const TooltipPlace p = tooltip_place(100, 590, 120, 20, 800, 600);
    ASSERT_TRUE(p.x == 116.0f);
    ASSERT_TRUE(p.y == 577.0f);                // 600 - (20 + 3)
    const TooltipPlace q = tooltip_place(100, 560, 120, 20, 800, 600);
    ASSERT_TRUE(q.x == 116.0f && q.y == 560.0f);
}

TEST(tooltip_place_flips_at_the_right_edge) {
    // Not moved: against the right edge. 800 - (120 + 3) = 677.
    const TooltipPlace p = tooltip_place(750, 100, 120, 20, 800, 600);
    ASSERT_TRUE(p.x == 677.0f && p.y == 124.0f);
    // Moved aside too: to the cursor's left, 2 px clear. 750 - 123 - 2 = 625.
    const TooltipPlace q = tooltip_place(750, 590, 120, 20, 800, 600);
    ASSERT_TRUE(q.x == 625.0f && q.y == 577.0f);
}

TEST(tooltip_text_follows_dialogs_xml) {
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Game, "BUTTON_Twist")), std::string("Scramble the letters"));
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Game, "BUTTON_Pass_2")), std::string("End this game"));
    // The same name differs by surface.
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Selection, "BUTTON_HOWTOPLAY")),
              std::string("View the game instructions"));
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::PauseMenu, "BUTTON_STOP")), std::string("Exit this level"));
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::PlayerSelect, "NAME_3")),
              std::string("Click on a name to\nselect a player"));
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::RoundDialog, "BUTTON_OK")),
              std::string("Play as the selected player"));   // sic, as in dialogs.xml
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Settings, "SWITCH_LENGTH_7")),
              std::string(tooltip_strings::SETTINGS_FULLSCREEN));
}

TEST(tooltip_text_is_empty_for_widgets_without_one) {
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Selection, "BUTTON_IMPROVEMENTS")), std::string());
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Selection, "BUTTON_PLAY_UNTIMED")), std::string());
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Game, "BUTTON_STOP")), std::string());
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::Settings, "BUTTON_OK")), std::string());
    ASSERT_EQ(std::string(tooltip_text(TooltipSurface::PlayerSelect, "NAM")), std::string());
}
