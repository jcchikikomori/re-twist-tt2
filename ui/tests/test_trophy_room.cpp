// SCENE_TROPHY: grid, reveal of new trophies, hover text and sparkle,
// and the way back to the menu (trophy_room.hpp).
#include "test_harness.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/trophies.hpp"
#include "tt2ui/trophy_room.hpp"

using namespace tt2ui;

namespace {
struct Room {
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
    std::vector<SoundId> sounds;
    int saves = 0;
    int exits = 0;
    std::unique_ptr<TrophyRoom> room;

    void open() {
        TrophyRoom::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_profile_change = [this] { ++saves; };
        s.on_exit = [this] { ++exits; };
        room = std::make_unique<TrophyRoom>(profile, s);
    }
    const SpriteState& trophy(int i) const { return room->trophies()[static_cast<std::size_t>(i)]; }
    void hover(int i) { room->mouse_move(trophy(i).x, trophy(i).y, nullptr); }
};
}  // namespace

TEST(trophy_room_lays_out_the_grid_from_dialogs_xml) {
    Room r;
    r.open();
    ASSERT_EQ(r.room->trophies().size(), std::size_t{24});
    ASSERT_TRUE(std::fabs(r.trophy(0).x - 87.0f) < 0.01f && std::fabs(r.trophy(0).y - 171.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(r.trophy(5).x - 703.0f) < 0.01f && std::fabs(r.trophy(5).y - 171.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(r.trophy(23).x - 703.0f) < 0.01f && std::fabs(r.trophy(23).y - 489.0f) < 0.01f);
    ASSERT_EQ(r.trophy(17).sheet.image_id, std::string("IMAGE_TROPHY18"));
    ASSERT_EQ(r.room->label(), std::string(trophy_strings::DEFAULT));
}

TEST(trophy_room_hides_unwon_and_shows_seen_trophies) {
    Room r;
    r.profile.trophies[3] = tt2::TROPHY_SEEN;
    r.open();
    ASSERT_TRUE(!r.trophy(0).visible);
    ASSERT_TRUE(r.trophy(3).visible);
    ASSERT_TRUE(std::fabs(r.trophy(3).alpha - 1.0f) < 0.001f);
    ASSERT_TRUE(std::fabs(r.trophy(3).scale - 0.5f) < 0.001f);
    ASSERT_EQ(r.saves, 0);   // nothing new: nothing to save
}

TEST(trophy_room_reveals_new_trophies_and_marks_them_seen) {
    Room r;
    r.profile.trophies[7] = tt2::TROPHY_NEW;
    r.profile.trophies[8] = tt2::TROPHY_NEW;
    r.open();
    ASSERT_EQ(r.profile.trophies[7], tt2::TROPHY_SEEN);
    ASSERT_EQ(r.profile.trophies[8], tt2::TROPHY_SEEN);
    ASSERT_EQ(r.saves, 1);
    ASSERT_TRUE(std::fabs(r.trophy(7).alpha) < 0.001f);
    r.room->update(200);   // faded in, still tiny
    ASSERT_TRUE(std::fabs(r.trophy(7).alpha - 1.0f) < 0.001f);
    ASSERT_TRUE(std::fabs(r.trophy(7).scale - 0.05f) < 0.001f);
    r.room->update(500);   // grown to 0.6, both at once
    ASSERT_TRUE(std::fabs(r.trophy(7).scale - 0.6f) < 0.001f);
    ASSERT_TRUE(std::fabs(r.trophy(8).scale - 0.6f) < 0.001f);
    r.room->update(300);
    ASSERT_TRUE(std::fabs(r.trophy(7).scale - 0.5f) < 0.001f);
}

TEST(trophy_room_hover_shows_descriptions_and_the_sparkle_on_won_ones) {
    Room r;
    r.profile.trophies[2] = tt2::TROPHY_SEEN;
    r.open();
    r.hover(2);
    ASSERT_EQ(r.room->hovered(), 2);
    ASSERT_EQ(r.room->label(), std::string(trophy_strings::DESCRIPTIONS[2]));
    ASSERT_TRUE(r.room->sparkle_sprite().visible);
    ASSERT_TRUE(std::fabs(r.room->sparkle_sprite().x - r.trophy(2).x) < 0.01f);
    // Unwon: its description, no sparkle.
    r.hover(4);
    ASSERT_EQ(r.room->label(), std::string(trophy_strings::DESCRIPTIONS[4]));
    ASSERT_TRUE(!r.room->sparkle_sprite().visible);
    // Nothing under the cursor: the default text.
    r.room->mouse_move(400, 30, nullptr);
    ASSERT_EQ(r.room->hovered(), -1);
    ASSERT_EQ(r.room->label(), std::string(trophy_strings::DEFAULT));
}

TEST(trophy_room_keeps_human_dictionary_secret_until_won) {
    Room r;
    r.open();
    r.hover(tt2::TROPHY_HUMAN_DICTIONARY);
    ASSERT_EQ(r.room->label(), std::string(trophy_strings::HIDDEN));
    Room won;
    won.profile.trophies[tt2::TROPHY_HUMAN_DICTIONARY] = tt2::TROPHY_SEEN;
    won.open();
    won.hover(tt2::TROPHY_HUMAN_DICTIONARY);
    ASSERT_EQ(won.room->label(), std::string(trophy_strings::DESCRIPTIONS[tt2::TROPHY_HUMAN_DICTIONARY]));
}

TEST(trophy_room_menu_button_goes_back) {
    Room r;
    r.open();
    MenuButton& menu = r.room->buttons()[0];
    const float x = menu.sprite.x, y = menu.sprite.y + 10;
    r.room->mouse_down(x, y, nullptr);
    r.room->mouse_up(x, y, nullptr);
    ASSERT_EQ(r.exits, 1);
    ASSERT_EQ(r.sounds.size(), std::size_t{1});
    // A release away from the button does nothing.
    r.room->mouse_down(x, y, nullptr);
    r.room->mouse_up(10, 10, nullptr);
    ASSERT_EQ(r.exits, 1);
    r.room->press("BUTTON_OTHER");
    ASSERT_EQ(r.exits, 1);
}

TEST(trophy_titles_match_text_xml) {
    ASSERT_EQ(std::string(trophy_title(0)), std::string("Quick\nDraw"));
    ASSERT_EQ(std::string(trophy_title(23)), std::string("Human\nDictionary"));
    ASSERT_EQ(std::string(trophy_title(24)), std::string());
    ASSERT_EQ(std::string(trophy_title(-1)), std::string());
}
