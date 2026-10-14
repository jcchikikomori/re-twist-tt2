// DIALOG_HELP: layout, page turns and the crossfade
// (docs/reverse-engineering/help-and-settings.md §2).
#include "test_harness.hpp"

#include <string>

#include "tt2ui/help_dialog.hpp"

using namespace tt2ui;

namespace {
float page_alpha(const Dialog& d, int i) {
    for (const auto& s : d.sprites)
        if (s.name == help_page_name(i)) return s.alpha;
    return -1.0f;
}
bool visible(const Dialog& d, const char* name) { return d.button(name)->sprite.visible; }
}  // namespace

TEST(help_layout_matches_dialog_help) {
    const Dialog d = build_help();
    ASSERT_EQ(d.w, 798);
    ASSERT_EQ(d.h, 595);
    ASSERT_EQ(d.sprites.size(), std::size_t{4});
    ASSERT_EQ(d.sprites[0].name, std::string("HELP_PAGE01"));
    ASSERT_EQ(d.sprites[3].sheet.image_id, std::string("IMAGE_HELP_PAGE04"));
    ASSERT_TRUE(d.sprites[2].x == 36.0f && d.sprites[2].y == 83.0f);
    ASSERT_EQ(d.sprites[1].sheet.frame_w, 727);
    ASSERT_EQ(d.sprites[1].sheet.frame_h, 468);
    ASSERT_TRUE(page_alpha(d, 0) == 1.0f);
    ASSERT_TRUE(page_alpha(d, 1) == 0.0f && page_alpha(d, 3) == 0.0f);
    const TextLabel* header = d.label("LABEL_HEADER");
    ASSERT_EQ(header->text, std::string("HOW TO PLAY"));
    ASSERT_EQ(header->font_id, std::string("FONT_DIALOG_HEADERS"));
    ASSERT_TRUE(header->anchor == Anchor::Center && header->x == 394.0f && header->y == 28.0f);
    const MenuButton* prev = d.button("BUTTON_PREV");
    ASSERT_TRUE(prev->sprite.x == 28.0f && prev->sprite.y == 507.0f && prev->sprite.anchor == Anchor::TopLeft);
    const MenuButton* ok = d.button("BUTTON_OK");
    ASSERT_TRUE(ok->sprite.x == 400.0f && ok->sprite.y == 505.0f && ok->sprite.anchor == Anchor::Top);
    const MenuButton* next = d.button("BUTTON_NEXT");
    ASSERT_TRUE(next->sprite.x == 776.0f && next->sprite.y == 511.0f && next->sprite.anchor == Anchor::TopRight);
    ASSERT_EQ(next->sprite.sheet.frame_h, 50);                     // 119x200, 4 rows
    ASSERT_TRUE(!visible(d, "BUTTON_PREV"));                       // page 0
    ASSERT_TRUE(visible(d, "BUTTON_NEXT"));
}

TEST(help_next_crossfades_linearly_over_200_ms) {
    Dialog d = build_help();
    HelpPager p;
    p.turn(d, +1);
    ASSERT_EQ(p.page, 1);
    ASSERT_TRUE(p.fading());
    p.tick(d, 50);                                                 // t = 0.25
    ASSERT_TRUE(page_alpha(d, 0) == 0.75f);
    ASSERT_TRUE(page_alpha(d, 1) == 0.25f);
    p.tick(d, 50);                                                 // t = 0.5
    ASSERT_TRUE(page_alpha(d, 0) == 0.5f && page_alpha(d, 1) == 0.5f);
    p.tick(d, 99);
    ASSERT_TRUE(p.fading());
    p.tick(d, 1);                                                  // 200 ms
    ASSERT_TRUE(!p.fading());
    ASSERT_TRUE(page_alpha(d, 0) == 0.0f && page_alpha(d, 1) == 1.0f);
    p.tick(d, 500);                                                // idle: nothing moves
    ASSERT_TRUE(page_alpha(d, 1) == 1.0f);
}

TEST(help_buttons_show_only_where_a_page_exists) {
    Dialog d = build_help();
    HelpPager p;
    p.turn(d, +1);
    ASSERT_TRUE(visible(d, "BUTTON_PREV") && visible(d, "BUTTON_NEXT"));
    p.turn(d, +1);
    p.turn(d, +1);
    ASSERT_EQ(p.page, 3);
    ASSERT_TRUE(visible(d, "BUTTON_PREV") && !visible(d, "BUTTON_NEXT"));
    p.turn(d, -1);
    ASSERT_EQ(p.page, 2);
    ASSERT_TRUE(visible(d, "BUTTON_NEXT"));
}

TEST(help_pages_clamp_instead_of_wrapping) {
    Dialog d = build_help();
    HelpPager p;
    p.turn(d, -1);                                                 // before page 1
    ASSERT_EQ(p.page, 0);
    ASSERT_TRUE(!p.fading());
    ASSERT_TRUE(page_alpha(d, 0) == 1.0f);
    for (int i = 0; i < 6; ++i) p.turn(d, +1);
    ASSERT_EQ(p.page, HELP_PAGE_COUNT - 1);                        // after page 4
}

TEST(help_turn_during_a_fade_finishes_it_first) {
    Dialog d = build_help();
    HelpPager p;
    p.turn(d, +1);
    p.tick(d, 100);
    p.turn(d, +1);                                                 // 0 -> 1 snaps, 1 -> 2 starts
    ASSERT_TRUE(page_alpha(d, 0) == 0.0f);
    ASSERT_TRUE(page_alpha(d, 1) == 1.0f && page_alpha(d, 2) == 0.0f);
    p.tick(d, HELP_FADE_MS);
    ASSERT_TRUE(page_alpha(d, 1) == 0.0f && page_alpha(d, 2) == 1.0f);
}

TEST(help_dialog_runs_buttons_by_name) {
    HelpDialog h;
    ASSERT_TRUE(!h.press("BUTTON_PREV"));                          // hidden on page 0: ignored
    ASSERT_EQ(h.pager().page, 0);
    ASSERT_TRUE(!h.press("BUTTON_NEXT"));
    ASSERT_EQ(h.pager().page, 1);
    h.tick(HELP_FADE_MS);
    ASSERT_TRUE(page_alpha(h.dialog(), 1) == 1.0f);
    ASSERT_TRUE(!h.press("BUTTON_PREV"));
    ASSERT_EQ(h.pager().page, 0);
    ASSERT_TRUE(!h.press("BUTTON_NOPE"));                          // unknown: ignored
    ASSERT_TRUE(h.press("BUTTON_OK"));                             // OK closes
}

TEST(help_placeholder_pages_cover_every_image) {
    ASSERT_EQ(static_cast<int>(help_strings::PLACEHOLDER_PAGES.size()), HELP_PAGE_COUNT);
    for (const char* page : help_strings::PLACEHOLDER_PAGES) ASSERT_TRUE(std::string(page).size() > 20);
}
