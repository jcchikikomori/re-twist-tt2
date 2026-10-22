// Classic game scene: word-grid layout, letter rows, guess flow, timer
// and round / game-over transitions (game_scene.hpp).

#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/game_scene.hpp"

using namespace tt2ui;

namespace {

// One rack family: "crate" with its common sub-anagrams. Every pool
// entry (5+ letters) has >= 15 sub-anagrams so the bag accepts it.
tt2::WordTree common() {
    return tt2::WordTree::from_words({
        "crate", "caret", "trace", "react", "carte", "cater",
        "ace", "act", "arc", "are", "art", "ate", "car", "cat", "ear", "eat", "era", "eta",
        "rat", "tar", "tea", "car", "care", "race", "acre", "rate", "tare", "tear",
    });
}
tt2::WordTree full() {
    auto words = common().words();
    words.push_back("cart");      // valid, not common: "uncommon" guess
    words.push_back("tec");       // 3 letters, in full only
    return tt2::WordTree::from_words(words);
}

struct Fixture {
    tt2::WordTree com = common();
    tt2::WordTree ful = full();
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
    std::vector<std::string> names{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    tt2::Random hs_rng{1};
    tt2::HighScoreManager hs{names, hs_rng};
    std::vector<SoundId> sounds;
    std::vector<std::string> urls;
    int saves = 0;
    int exits = 0;
    int discards = 0;
    std::vector<tt2::SavedGame> saved;
    tt2::GameSettings settings;
    std::vector<float> fx_applied, music_applied;
    int settings_commits = 0;
    std::unique_ptr<GameScene> scene;
    // The wall clock Services::now reads (WOTD); tests move it.
    tt2::WallClock clock{};
    tt2::WotdDayRule rule = tt2::WotdDayRule::Original;

    // `tutorial_seen`: the player has had DIALOG_TUTORIAL for every mode,
    // so most tests start straight at the rack. The bool overload keeps
    // the Classic tests as they were (true = Timed, false = Untimed).
    explicit Fixture(bool timed, bool with_hs = true, std::int64_t seed = 0, bool tutorial_seen = true)
        : Fixture(timed ? tt2::GameMode::Timed : tt2::GameMode::Untimed, with_hs, seed, tutorial_seen) {}
    explicit Fixture(tt2::GameMode mode, bool with_hs = true, std::int64_t seed = 0, bool tutorial_seen = true,
                     tt2::WallClock now = tt2::WallClock{}, tt2::WotdDayRule wotd_rule = tt2::WotdDayRule::Original)
        : clock(now), rule(wotd_rule) {
        profile.mode_flags.fill(tutorial_seen);
        scene = std::make_unique<GameScene>(com, ful, profile, with_hs ? &hs : nullptr, mode, services(), seed);
    }
    // The fixture's hooks, for a second scene built by a test.
    GameScene::Services services() {
        GameScene::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_profile_change = [this] { ++saves; };
        s.on_exit = [this] { ++exits; };
        s.save_game = [this](const tt2::SavedGame& g) { saved.push_back(g); };
        s.discard_save = [this] { ++discards; };
        s.open_url = [this](const std::string& u) { urls.push_back(u); };
        s.settings.current = [this] { return settings; };
        s.settings.apply_sound_volume = [this](float v) { fx_applied.push_back(v); };
        s.settings.apply_music_volume = [this](float v) { music_applied.push_back(v); };
        s.settings.commit = [this](const tt2::GameSettings& g) { settings = g; ++settings_commits; };
        s.now = [this] { return clock; };
        s.wotd_rule = rule;
        return s;
    }
    // The skip button the scene shows right now (PASS x2, PASS or GIVE UP).
    std::string skip_button() const {
        for (const char* n : {"BUTTON_Pass_2", "BUTTON_Pass", "BUTTON_GiveUp"})
            if (scene->button(n)->sprite.visible) return n;
        return "";
    }
    // Press it, answer YES, and wait out the 3000 ms reveal.
    void skip() {
        scene->press(skip_button());
        scene->dialog_press("BUTTON_YES");
        scene->update(REVEAL_MS + 20);
    }
    // Let update() pick up the queued popup and report what is on screen.
    std::optional<PopupKind> shown() {
        scene->update(0);
        return scene->popup_kind();
    }
    // Run every popup that is up or waiting to the end.
    void drain_popups() {
        for (int i = 0; i < 32 && (scene->popup_kind() || scene->popups_waiting() > 0); ++i) {
            scene->update(0);
            scene->update(3000);
        }
        scene->update(1);
    }
    bool played(SoundId id) const {
        for (auto s : sounds) if (s == id) return true;
        return false;
    }
    void type(const std::string& w) { for (char c : w) scene->type_letter(c); }
    const SlotView* slot(const std::string& w) const {
        for (const auto& s : scene->slots()) if (s.word == w) return &s;
        return nullptr;
    }
    // Every board word except the rack itself.
    std::vector<std::string> non_bingo_words() const {
        std::vector<std::string> out;
        for (const auto& s : scene->slots()) if (s.word != scene->rack()) out.push_back(s.word);
        return out;
    }
};

std::vector<std::string> words_of_lengths(const std::vector<int>& lens) {
    std::vector<std::string> out;
    for (int l : lens) out.push_back(std::string(static_cast<std::size_t>(l), 'a'));
    return out;
}

}  // namespace

// ---- layout_anagram_table (0x431e50) ---------------------------------

TEST(table_layout_small_board_uses_four_columns) {
    // 10 words: ceil(10/8) = 2 -> clamped to 4 columns, 2.5 rows each:
    // rows 2, 3, 2, 3 (fraction accumulates 0.5, 1.0, 0.5, 1.0).
    auto t = layout_anagram_table(words_of_lengths({3, 3, 3, 3, 3, 3, 4, 4, 4, 4}));
    ASSERT_EQ(t.slots.size(), std::size_t{10});
    int per_col[4] = {0, 0, 0, 0};
    for (const auto& s : t.slots) ++per_col[s.column];
    ASSERT_EQ(per_col[0], 2);
    ASSERT_EQ(per_col[1], 3);
    ASSERT_EQ(per_col[2], 2);
    ASSERT_EQ(per_col[3], 3);
    // Rows are 20 px apart, columns restart at y = 0.
    ASSERT_EQ(t.slots[0].y, 0);
    ASSERT_EQ(t.slots[1].y, 20);
    ASSERT_EQ(t.slots[2].y, 0);
    // Column x: previous x + 3 + longest*20 + 20 with longest = 3 (3-letter words).
    ASSERT_EQ(t.slots[0].x, 0);
    ASSERT_EQ(t.slots[2].x, 83);
    ASSERT_EQ(t.slots[5].x, 166);
    // Slot 6 (column 2) is the first 4-letter word, so column 3 starts
    // 3 + 4*20 + 20 further right and the table is 4 tiles wider.
    ASSERT_EQ(t.slots[7].x, 269);
    ASSERT_EQ(t.width, 269 + 80);
    ASSERT_EQ(t.height, 60);
}

TEST(table_layout_full_board_grows_columns) {
    // 48 words -> 6 columns of 8. The running "longest" lengthens the gaps.
    std::vector<int> lens;
    for (int i = 0; i < 16; ++i) lens.push_back(3);
    for (int i = 0; i < 16; ++i) lens.push_back(4);
    for (int i = 0; i < 16; ++i) lens.push_back(5);
    auto t = layout_anagram_table(words_of_lengths(lens));
    ASSERT_EQ(t.slots.back().column, 5);
    ASSERT_EQ(t.slots[8].x, 83);           // after a 3-letter column
    ASSERT_EQ(t.slots[16].x, 166);
    ASSERT_EQ(t.slots[24].x, 166 + 103);   // after a 4-letter column: 3 + 80 + 20
    ASSERT_EQ(t.slots[40].x, 269 + 103 + 123);
    ASSERT_EQ(t.height, 160);
    ASSERT_EQ(t.width, t.slots[40].x + 100);
    ASSERT_EQ(layout_anagram_table({}).slots.size(), std::size_t{0});
}

TEST(row_geometry_is_centred_on_400) {
    ASSERT_TRUE(row_left(6) == 400.0f - 207.0f);
    ASSERT_TRUE(ball_center_x(6, 0) == 193.0f + 34.5f);
    ASSERT_TRUE(ball_center_x(6, 5) == 193.0f + 5 * 69.0f + 34.5f);
}

// LetterBox (0x43ab90): one 69x69 cell per letter in each row; the
// played row shows the blue socket, the available row the gold dot.
TEST(letter_boxes_sit_under_every_cell_of_both_rows) {
    const auto boxes = letter_boxes(6);
    ASSERT_EQ(boxes.size(), std::size_t{12});
    for (int i = 0; i < 6; ++i) {
        const SpriteState& played = boxes[static_cast<std::size_t>(i)];
        const SpriteState& avail = boxes[static_cast<std::size_t>(6 + i)];
        ASSERT_TRUE(played.x == ball_center_x(6, i));
        ASSERT_TRUE(avail.x == ball_center_x(6, i));
        ASSERT_TRUE(played.y == PLAYED_ROW_Y + BALL_HALF);
        ASSERT_TRUE(avail.y == AVAILABLE_ROW_Y + BALL_HALF);
        ASSERT_EQ(played.frame, LETTERBOX_PLAYED_FRAME);
        ASSERT_EQ(avail.frame, LETTERBOX_AVAILABLE_FRAME);
        ASSERT_TRUE(played.anchor == Anchor::Center);
    }
    // Worked example, 6 letters: the row starts at 400 - 6*69/2 = 193, so
    // the first box spans x 193..262 and y 213..282.
    ASSERT_TRUE(boxes[0].left() == 193.0f);
    ASSERT_TRUE(boxes[0].top() == PLAYED_ROW_Y);
    ASSERT_EQ(boxes[0].sheet.frame_w, LETTERBOX_SIZE);
    ASSERT_EQ(boxes[0].sheet.frame_h, LETTERBOX_SIZE);
    ASSERT_EQ(boxes[0].sheet.image_id, std::string("LetterBox"));
}

TEST(letter_boxes_are_empty_without_letters) {
    ASSERT_TRUE(letter_boxes(0).empty());
    ASSERT_TRUE(letter_boxes(-3).empty());
}

// ---- round start -------------------------------------------------------

static std::string ball_order(const GameScene& s) {
    std::string order(s.balls().size(), ' ');
    for (const auto& b : s.balls()) order[static_cast<std::size_t>(b.slot)] = b.letter;
    return order;
}

// ROADMAP bug #1: the balls used to spell the rack word, giving away the bingo.
TEST(scene_deals_the_rack_scrambled) {
    for (std::int64_t seed = 0; seed < 20; ++seed) {
        Fixture f(false, true, seed);
        auto& s = *f.scene;
        const std::string order = ball_order(s);
        ASSERT_TRUE(order != s.rack());
        std::string a = order, b = s.rack();
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        ASSERT_EQ(a, b);                                   // same letters, new order
    }
}

TEST(scene_starts_round_one_with_rack_and_board) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_EQ(s.round_number(), 1);
    ASSERT_EQ(s.score(), 0);
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.rack().size(), std::size_t{5});
    ASSERT_EQ(s.balls().size(), std::size_t{5});
    ASSERT_TRUE(!s.slots().empty());
    for (const auto& b : s.balls()) {
        ASSERT_TRUE(!b.played);
        ASSERT_TRUE(b.y == AVAILABLE_ROW_Y + BALL_HALF);
    }
    ASSERT_TRUE(s.balls()[0].x == ball_center_x(5, 0));
    // Board slots sit on the centred table.
    ASSERT_TRUE(s.table_x() == 400.0f - s.table().width * 0.5f);
    ASSERT_EQ(s.slots()[0].y, 15);
    // The balls flip in one after another (0x43c940); SOUND_letter_load
    // plays as each flip starts, since 0x43a820 runs the frames and the
    // sound side by side under the ball's delay.
    ASSERT_TRUE(!f.played(SoundId::LetterLoad));
    ASSERT_EQ(s.balls()[0].frame, 0);
    auto loads = [&f] {
        int n = 0;
        for (auto id : f.sounds) if (id == SoundId::LetterLoad) ++n;
        return n;
    };
    s.update(1);
    ASSERT_EQ(loads(), 1);                             // ball 0 at 0 ms
    s.update(BALL_REVEAL_STAGGER_MS - 2);
    ASSERT_EQ(loads(), 1);                             // 119 ms: ball 1 still waits
    s.update(1);
    ASSERT_EQ(loads(), 2);                             // 120 ms: ball 1 starts
    s.update(BALL_REVEAL_STAGGER_MS * 3);
    ASSERT_EQ(loads(), 5);                             // 480 ms: the last ball starts
    s.update(BALL_REVEAL_FRAME_MS * (BALL_IDLE_FRAME + 1));
    ASSERT_EQ(loads(), 5);                             // the flips end without a sound
    for (const auto& b : s.balls()) ASSERT_EQ(b.frame, BALL_IDLE_FRAME);
    ASSERT_EQ(f.saves, 1);                             // bag state persisted
    ASSERT_EQ(f.profile.bag.index, std::uint32_t{1});
    ASSERT_TRUE(s.typed().empty());
    ASSERT_EQ(s.timer_minutes_text(), std::string("2:"));
    ASSERT_EQ(s.timer_seconds_text(), std::string("00"));
    ASSERT_EQ(s.round_text(), std::string("1"));
    ASSERT_TRUE(std::string(s.background_id()) == "gamescene/BACKGROUND");
    ASSERT_TRUE(std::string(Fixture(true).scene->background_id()) == "gamescene/BACKGROUND_TIMED");
}

TEST(buttons_follow_the_layout_and_qualified_flag) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_TRUE(s.button("BUTTON_Twist")->sprite.x == 147.0f);
    ASSERT_TRUE(s.button("BUTTON_Enter")->sprite.y == 407.0f);
    ASSERT_EQ(s.button("BUTTON_NextRound")->sprite.sheet.frame_h, 52);
    ASSERT_TRUE(!s.button("BUTTON_NextRound")->sprite.visible);
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_Pass_2"));   // no miss yet
    ASSERT_EQ(s.button("BUTTON_Pass_2")->sprite.sheet.image_id, std::string("BUTTON_PASS2"));
    f.type(s.rack());
    s.key_return();
    ASSERT_TRUE(s.qualified());
    ASSERT_TRUE(s.button("BUTTON_NextRound")->sprite.visible);
    ASSERT_EQ(f.skip_button(), std::string(""));                // never two at the spot
}

// ---- letters -------------------------------------------------------------

// The boxes belong to the rows, not the balls: playing a letter leaves
// every box where it was.
TEST(scene_letter_boxes_follow_the_rack_not_the_balls) {
    Fixture f(false);
    auto& s = *f.scene;
    const auto before = s.letter_boxes();
    ASSERT_EQ(before.size(), 2 * s.balls().size());
    s.type_letter(s.rack()[0]);
    s.update(1000);
    const auto after = s.letter_boxes();
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        ASSERT_TRUE(after[i].x == before[i].x);
        ASSERT_TRUE(after[i].y == before[i].y);
        ASSERT_EQ(after[i].frame, before[i].frame);
    }
}

// USER-OBSERVED: the available row's gold dot under a ball stays hidden
// while that ball deals in (round-start flip) and shows once it rests.
TEST(available_dots_hide_while_their_ball_deals_in) {
    Fixture f(false);
    auto& s = *f.scene;
    const std::size_t n = s.balls().size();
    auto boxes = s.letter_boxes();
    for (std::size_t i = 0; i < n; ++i) ASSERT_TRUE(boxes[i].visible);       // played row: sockets stay
    for (std::size_t i = n; i < 2 * n; ++i) ASSERT_TRUE(!boxes[i].visible);  // every ball still dealing
    // Ball 0 settles after 12 frames * 60 ms; the last one waits (n-1) * 120 ms more.
    s.update((BALL_IDLE_FRAME + 1) * BALL_REVEAL_FRAME_MS);
    boxes = s.letter_boxes();
    int shown = 0;
    for (std::size_t i = n; i < 2 * n; ++i) if (boxes[i].visible) ++shown;
    ASSERT_EQ(shown, 1);
    s.update(5000);
    boxes = s.letter_boxes();
    for (std::size_t i = n; i < 2 * n; ++i) ASSERT_TRUE(boxes[i].visible);
}

TEST(typing_moves_balls_to_played_row_and_back) {
    Fixture f(false);
    auto& s = *f.scene;
    const std::string rack = s.rack();
    // The rack is dealt scrambled: find the cell that holds rack[2].
    int home = -1;
    for (const auto& b : s.balls()) if (b.letter == rack[2] && (home < 0 || b.slot < home)) home = b.slot;
    ASSERT_TRUE(home >= 0);
    s.type_letter(rack[2]);
    ASSERT_EQ(s.typed(), std::string(1, rack[2]));
    const LetterBall* moved = nullptr;
    for (const auto& b : s.balls()) if (b.played) moved = &b;
    ASSERT_TRUE(moved != nullptr);
    ASSERT_EQ(moved->slot, 0);
    ASSERT_TRUE(moved->target_y == PLAYED_ROW_Y + BALL_HALF);
    ASSERT_TRUE(moved->target_x == ball_center_x(5, 0));
    ASSERT_TRUE(f.played(SoundId::LetterPlace));
    s.update(16);
    ASSERT_TRUE(moved->y < AVAILABLE_ROW_Y + BALL_HALF);   // sliding
    s.update(5000);
    ASSERT_TRUE(moved->y == moved->target_y);
    // Unknown letter / letter not on the rack: nothing happens.
    s.type_letter('z');
    s.type_letter('1');
    ASSERT_EQ(s.typed().size(), std::size_t{1});
    s.backspace();
    ASSERT_TRUE(s.typed().empty());
    ASSERT_TRUE(f.played(SoundId::LetterClear));
    // The ball returned to the first free cell of the rack (its old one here).
    ASSERT_EQ(moved->slot, home);
    s.backspace();                                         // nothing to remove
    ASSERT_TRUE(s.typed().empty());
}

TEST(clicking_balls_places_and_removes_them) {
    Fixture f(false);
    auto& s = *f.scene;
    const auto& b0 = s.balls()[0];
    s.mouse_down(b0.x, b0.y);
    s.mouse_up(b0.x, b0.y);
    ASSERT_TRUE(b0.played);
    const auto& b1 = s.balls()[1];
    s.mouse_up(b1.x, b1.y);
    ASSERT_TRUE(b1.played);
    ASSERT_EQ(s.typed().size(), std::size_t{2});
    // Clicking the FIRST played ball does nothing; the LAST one goes back.
    s.mouse_up(b0.target_x, b0.target_y);
    ASSERT_TRUE(b0.played);
    s.mouse_up(b1.target_x, b1.target_y);
    ASSERT_TRUE(!b1.played);
    ASSERT_EQ(s.typed().size(), std::size_t{1});
    s.mouse_up(1, 1);                                      // empty space
    ASSERT_EQ(s.typed().size(), std::size_t{1});
}

TEST(clear_lastword_and_twist_buttons) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    ASSERT_EQ(s.typed(), std::string("car"));
    s.press("BUTTON_Clear");
    ASSERT_TRUE(s.typed().empty());
    s.press("BUTTON_LastWord");                            // nothing submitted yet
    ASSERT_TRUE(s.typed().empty());
    f.type("car");
    s.press("BUTTON_Enter");
    ASSERT_EQ(s.last_word(), std::string("car"));
    ASSERT_TRUE(s.typed().empty());                        // letters return after Enter
    s.press("BUTTON_LastWord");
    ASSERT_EQ(s.typed(), std::string("car"));
    s.press("BUTTON_Clear");
    s.update(BALL_MOVE_MS);                                // the letters land back in the rack
    // Twist: same multiset of letters in the rack row, counted on the profile.
    const int twists = f.profile.twists;
    s.press("BUTTON_Twist");
    ASSERT_EQ(f.profile.twists, twists + 1);
    ASSERT_TRUE(f.played(SoundId::WordTwist));
    std::string order;
    std::vector<const LetterBall*> row;
    for (const auto& b : s.balls()) row.push_back(&b);
    std::sort(row.begin(), row.end(), [](const LetterBall* a, const LetterBall* b) { return a->slot < b->slot; });
    for (auto* b : row) order.push_back(b->letter);
    std::string sorted_rack = s.rack(), sorted_order = order;
    std::sort(sorted_rack.begin(), sorted_rack.end());
    std::sort(sorted_order.begin(), sorted_order.end());
    ASSERT_EQ(sorted_order, sorted_rack);
    s.update(BALL_RELAYOUT_MS);
    s.key_space();                                         // space twists too
    ASSERT_EQ(f.profile.twists, twists + 2);
}

// The rack order (left to right) of the balls in the available row.
static std::string rack_row(const GameScene& s) {
    std::vector<const LetterBall*> row;
    for (const auto& b : s.balls()) if (!b.played) row.push_back(&b);
    std::sort(row.begin(), row.end(), [](const LetterBall* a, const LetterBall* b) { return a->slot < b->slot; });
    std::string out;
    for (auto* b : row) out.push_back(b->letter);
    return out;
}

// 0x43c280: a held TWIST key only moves the rack once the last slide has
// landed. Key repeats in between leave the balls and the counter alone,
// and (a port deviation from 0x425240) make no sound either.
TEST(twist_waits_for_the_balls_to_land) {
    Fixture f(false);
    auto& s = *f.scene;
    const int twists = f.profile.twists;
    s.key_space();
    ASSERT_EQ(f.profile.twists, twists + 1);
    const std::string moved = rack_row(s);
    int sounds = 0;
    for (auto id : f.sounds) if (id == SoundId::WordTwist) ++sounds;
    ASSERT_EQ(sounds, 1);
    // Key repeats every 33 ms while the 400 ms slide runs: refused.
    for (int t = 33; t < BALL_RELAYOUT_MS; t += 33) {
        s.update(33);
        s.key_space();
    }
    ASSERT_EQ(f.profile.twists, twists + 1);
    ASSERT_EQ(rack_row(s), moved);
    sounds = 0;
    for (auto id : f.sounds) if (id == SoundId::WordTwist) ++sounds;
    ASSERT_EQ(sounds, 1);                                  // the 12 refused repeats are silent
    s.update(BALL_RELAYOUT_MS);                            // landed
    for (const auto& b : s.balls()) ASSERT_TRUE(!b.moving());
    s.key_space();
    ASSERT_EQ(f.profile.twists, twists + 2);
    sounds = 0;
    for (auto id : f.sounds) if (id == SoundId::WordTwist) ++sounds;
    ASSERT_EQ(sounds, 2);
}

// 0x43c370 shuffles again until the order changes, so a twist always
// moves something.
TEST(twist_always_changes_the_order) {
    Fixture f(false);
    auto& s = *f.scene;
    for (int i = 0; i < 20; ++i) {
        const std::string before = rack_row(s);
        s.key_space();
        ASSERT_TRUE(rack_row(s) != before);
        ASSERT_TRUE(rack_row(s) != s.rack());              // never hands over the rack word
        s.update(BALL_RELAYOUT_MS);
    }
}

// 0x43c280: one letter left in the rack row cannot be twisted.
TEST(twist_needs_two_letters_in_the_row) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack().substr(0, s.rack().size() - 1));      // all but one letter played
    s.update(BALL_MOVE_MS);
    const int twists = f.profile.twists;
    s.key_space();
    ASSERT_EQ(f.profile.twists, twists);
    ASSERT_TRUE(!f.played(SoundId::WordTwist));            // a refused press is silent
}

// LetterBall::tick reports the start of the flip once, and a ball that is
// not flipping never reports it.
TEST(letter_ball_reports_the_flip_start_once) {
    LetterBall b;
    b.letter = 'a';
    b.frame = 0;
    b.reveal_delay_ms = 100;
    b.reveal_elapsed_ms = 0;
    ASSERT_TRUE(!b.tick(99));
    ASSERT_TRUE(b.tick(1));                                // the delay ran out
    ASSERT_EQ(b.frame, 0);
    ASSERT_TRUE(!b.tick(BALL_REVEAL_FRAME_MS));
    ASSERT_EQ(b.frame, 1);
    ASSERT_TRUE(!b.tick(BALL_REVEAL_FRAME_MS * BALL_IDLE_FRAME));
    ASSERT_EQ(b.frame, BALL_IDLE_FRAME);
    ASSERT_TRUE(!b.revealing());
    LetterBall idle;
    idle.letter = 'b';
    ASSERT_TRUE(!idle.tick(1000));
    LetterBall now;                                        // no delay: starts on the first tick
    now.letter = 'c';
    now.frame = 0;
    now.reveal_elapsed_ms = 0;
    ASSERT_TRUE(now.tick(16));
    ASSERT_TRUE(!now.tick(16));
}

// 0x424730 -> 0x449c90: the score label is set with sprintf("%d") right
// after the score changes; there is no count-up, so the text matches the
// new total before any time passes.
TEST(score_label_jumps_to_the_new_total) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_EQ(s.score_text(), std::string("0"));
    f.type("car");
    s.key_return();
    ASSERT_EQ(s.score_text(), std::string("90"));          // no update() needed
    s.update(16);
    ASSERT_EQ(s.score_text(), std::string("90"));          // and it stays put
    s.key_return();                                        // recall "car": no score change
    s.key_return();                                        // a repeat word scores nothing
    ASSERT_EQ(s.score(), 90);
    ASSERT_EQ(s.score_text(), std::string("90"));
}

// 0x427560: Return on an empty played row is LAST WORD, so the player
// can submit a word, press Return again to get it back, and extend it
// ("scalar" -> "scalars" in the original; "car" -> "cart" here).
TEST(return_on_an_empty_row_recalls_the_last_word) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();                                        // submit
    ASSERT_EQ(s.score(), 90);
    ASSERT_TRUE(s.typed().empty());
    s.key_return();                                        // recall
    ASSERT_EQ(s.typed(), std::string("car"));
    ASSERT_EQ(s.score(), 90);                              // recalling submits nothing
    f.type("t");
    ASSERT_EQ(s.typed(), std::string("cart"));
    s.key_return();                                        // submit the longer word
    ASSERT_EQ(s.last_word(), std::string("cart"));
    ASSERT_EQ(s.score(), 90 + 160);
    ASSERT_TRUE(s.typed().empty());
}

// 0x427560 calls 0x425000 without checking for a last word, so the
// whoosh still plays; nothing moves and nothing is submitted.
TEST(return_on_an_empty_row_without_a_last_word_only_whooshes) {
    Fixture f(false);
    auto& s = *f.scene;
    f.sounds.clear();
    s.key_return();
    ASSERT_TRUE(s.typed().empty());
    ASSERT_EQ(s.score(), 0);
    ASSERT_EQ(f.sounds.size(), std::size_t{1});
    ASSERT_TRUE(f.sounds[0] == SoundId::WordLast);
}

// 0x425000: SOUND_word_last once, and the balls move silently (0x43bad0
// plays nothing), so recalling "car" is one whoosh, not three clicks.
TEST(last_word_plays_word_last_once_and_no_letter_clicks) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.press("BUTTON_Enter");
    f.sounds.clear();
    s.press("BUTTON_LastWord");
    ASSERT_EQ(s.typed(), std::string("car"));
    ASSERT_EQ(f.sounds.size(), std::size_t{1});
    ASSERT_TRUE(f.sounds[0] == SoundId::WordLast);
    s.press("BUTTON_Clear");
    s.update(BALL_MOVE_MS);
    f.sounds.clear();
    s.key_return();                                        // the Return key: the same sound
    ASSERT_EQ(s.typed(), std::string("car"));
    ASSERT_EQ(f.sounds.size(), std::size_t{1});
    ASSERT_TRUE(f.sounds[0] == SoundId::WordLast);
    f.sounds.clear();
    f.type("t");                                           // typing still clicks
    ASSERT_TRUE(f.played(SoundId::LetterPlace));
}

// The LAST button is off with no last word (0x425710), so a press is
// silent, unlike the Return key above.
TEST(disabled_last_word_button_plays_nothing) {
    Fixture f(false);
    auto& s = *f.scene;
    f.sounds.clear();
    s.press("BUTTON_LastWord");
    ASSERT_TRUE(s.typed().empty());
    ASSERT_TRUE(f.sounds.empty());
}

// 0x427560, engine key 4 (Delete): CLEAR.
TEST(delete_key_clears_the_played_row) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    f.sounds.clear();
    s.key_delete();
    ASSERT_TRUE(s.typed().empty());
    ASSERT_TRUE(f.played(SoundId::LetterClear));
    f.sounds.clear();
    s.key_delete();                                        // nothing to clear: silent
    ASSERT_TRUE(!f.played(SoundId::LetterClear));
    f.type("car");
    s.key_escape();                                        // pause menu open
    s.key_delete();
    ASSERT_EQ(s.typed(), std::string("car"));              // the menu owns the keys
}

// ---- word-appear flip (0x4310f0) -----------------------------------------

// Worked example, 5 letters staggered: letter i starts at i * 100 ms,
// squashes 150 ms, grows 150 ms; the last one settles at 400 + 300 = 700.
TEST(anagram_flip_squashes_then_grows_each_letter_in_turn) {
    ASSERT_EQ(anagram_flip_ms(5, false), 700);
    ASSERT_EQ(anagram_flip_ms(5, true), 300);
    ASSERT_EQ(anagram_flip_ms(0, false), 0);
    AnagramTile t = anagram_tile_at(0, 0, false, false);
    ASSERT_TRUE(t.tile_scale_y == 1.0f && !t.letter_visible);
    t = anagram_tile_at(75, 0, false, false);
    ASSERT_TRUE(t.tile_scale_y == 0.5f && !t.letter_visible);
    t = anagram_tile_at(150, 0, false, false);           // letter set, growing from 0
    ASSERT_TRUE(t.letter_visible && t.tile_scale_y == 0.0f && t.letter_scale_y == 0.0f);
    ASSERT_EQ(t.tile_frame, 0);                            // found words keep the white tile
    t = anagram_tile_at(225, 0, false, false);
    ASSERT_TRUE(t.tile_scale_y == 0.5f && t.letter_scale_y == 0.5f);
    t = anagram_tile_at(5000, 0, false, false);
    ASSERT_TRUE(t.tile_scale_y == 1.0f && t.letter_scale_y == 1.0f && t.letter_visible);
    // Letter 2 has not started at 150 ms; it squashes from 200 ms.
    t = anagram_tile_at(150, 2, false, false);
    ASSERT_TRUE(t.tile_scale_y == 1.0f && !t.letter_visible);
    t = anagram_tile_at(275, 2, false, false);
    ASSERT_TRUE(t.tile_scale_y == 0.5f);
}

TEST(anagram_flip_revealed_words_turn_yellow_and_restored_ones_flip_together) {
    ASSERT_EQ(anagram_tile_at(100, 0, true, false).tile_frame, 0);   // still squashing
    ASSERT_EQ(anagram_tile_at(150, 0, true, false).tile_frame, 1);
    const AnagramTile last = anagram_tile_at(150, 7, false, true);
    ASSERT_TRUE(last.letter_visible);                      // no stagger
    SlotView idle;
    idle.word = "crate";
    ASSERT_TRUE(!idle.tile(0).letter_visible);              // not started: blank white tile
    ASSERT_TRUE(!idle.flipping());
}

TEST(found_word_flips_in_and_the_pause_menu_holds_it) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    const SlotView* v = f.slot("car");
    ASSERT_TRUE(v != nullptr);
    ASSERT_EQ(v->flip_ms, 0);
    ASSERT_TRUE(v->flipping());
    s.update(300);
    ASSERT_TRUE(v->tile(0).letter_visible);
    ASSERT_TRUE(v->tile(0).letter_scale_y == 1.0f);
    ASSERT_TRUE(!v->tile(2).letter_visible);              // letter 2 still squashing
    s.press("BUTTON_Menu");
    s.update(1000);
    ASSERT_EQ(v->flip_ms, 300);                            // frozen while paused
    s.dialog_press("BUTTON_RESUME");
    s.update(1000);
    ASSERT_TRUE(!v->flipping());
    ASSERT_TRUE(v->tile(2).letter_visible);
}

// ---- guesses -------------------------------------------------------------

TEST(guess_outcomes_score_and_commentary) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    ASSERT_EQ(s.score(), 90);
    ASSERT_TRUE(f.slot("car")->found);
    ASSERT_TRUE(f.played(SoundId::WordCorrect));
    ASSERT_EQ(f.profile.solved, 1);
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);      // first correct word: popup 10
    f.drain_popups();
    // Duplicate.
    f.sounds.clear();
    f.type("car");
    s.key_return();
    ASSERT_EQ(s.score(), 90);
    ASSERT_TRUE(f.played(SoundId::WordReject));
    ASSERT_TRUE(f.shown() == PopupKind::AlreadyGuessed);
    // Not a word: queued behind the popup that is up (0x428e20).
    f.type("cra");
    s.key_return();
    ASSERT_EQ(s.popups_waiting(), std::size_t{1});
    s.update(popup_duration_ms(PopupKind::AlreadyGuessed));
    s.update(1);                                           // the clean-up step runs a frame later
    ASSERT_TRUE(f.shown() == PopupKind::NotListed);
    // Valid in the full list but not a board word ("cart"): the
    // original places it in the first empty slot of its length and
    // shows the guessed word there (0x432b20 / 0x431880), flagged
    // "uncommon" on the profile.
    f.type("cart");
    s.key_return();
    ASSERT_EQ(s.score(), 90 + 160);
    ASSERT_TRUE(f.slot("cart") != nullptr);
    ASSERT_TRUE(f.slot("cart")->found);
    ASSERT_EQ(f.profile.hard_solved, 1);
    // The ENTER button with nothing typed is a no-op (0x425b60 calls
    // 0x424730 directly; only the Return key falls back to LAST WORD).
    f.sounds.clear();
    s.press("BUTTON_Enter");
    ASSERT_TRUE(f.sounds.empty());
    ASSERT_TRUE(s.typed().empty());
    ASSERT_EQ(s.score(), 90 + 160);
}

TEST(no_room_comment_depends_on_length_slots) {
    Fixture f(false);
    auto& s = *f.scene;
    // "tec" is valid (full list) but has nowhere to go once every
    // 3-letter slot is taken. Fill ALL 3-letter slots, then a fresh
    // valid 3-letter word gets "enough words of that length".
    std::vector<std::string> threes;
    for (const auto& v : s.slots()) if (v.word.size() == 3) threes.push_back(v.word);
    for (const auto& w : threes) { f.type(w); s.key_return(); }
    f.drain_popups();
    f.sounds.clear();
    f.type("tec");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::EnoughOfLength);
    ASSERT_EQ(s.popup().sheet.image_id, std::string("IMAGE_IMAGE_POPUP_5"));
    ASSERT_TRUE(!f.played(SoundId::WordReject));            // 0x424730 case 2: no sound
}

TEST(uncommon_guess_takes_the_first_empty_slot) {
    Fixture f(false);
    auto& s = *f.scene;
    // "cart" is in the full list only. It fills the first empty
    // 4-letter slot — the "care" slot — which then displays the
    // guessed word.
    f.type("cart");
    s.key_return();
    ASSERT_TRUE(f.slot("cart") != nullptr);
    ASSERT_TRUE(f.slot("cart")->found);
    ASSERT_TRUE(f.slot("care") == nullptr);
    // The covered board word is not a duplicate: "care" fills the
    // next empty 4-letter slot ("race").
    f.type("care");
    s.key_return();
    ASSERT_TRUE(f.slot("care") != nullptr);
    ASSERT_TRUE(f.slot("care")->found);
    // "cart" again is a duplicate.
    f.drain_popups();
    f.sounds.clear();
    f.type("cart");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::AlreadyGuessed);
    ASSERT_TRUE(f.played(SoundId::WordReject));
}

// 0x428ec0, one message popup step by step. Worked example: 150 ms
// fade in, 250 ms grow to 1.2 (smoothstep: half way = 0.6), 100 ms
// settle to 1.0, 900 ms hold, 250 ms fade out = 1650 ms.
TEST(message_popup_fades_grows_settles_holds_and_fades) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("cra");
    s.key_return();
    ASSERT_TRUE(!s.popup_kind());                          // queued, not up yet
    ASSERT_TRUE(f.shown() == PopupKind::NotListed);
    const SpriteState& p = s.popup();
    ASSERT_EQ(p.sheet.image_id, std::string("IMAGE_IMAGE_POPUP_0"));
    ASSERT_TRUE(p.x == POPUP_X && p.y == POPUP_Y && p.anchor == Anchor::Center);
    ASSERT_TRUE(p.alpha == 0.0f && p.scale == 0.0f);
    s.update(POPUP_FADE_IN_MS);
    ASSERT_TRUE(p.alpha == 1.0f && p.scale == 0.0f);       // grows only after the fade
    s.update(POPUP_GROW_MS / 2);
    ASSERT_TRUE(std::fabs(p.scale - 0.6f) < 0.001f);
    s.update(POPUP_GROW_MS / 2);
    ASSERT_TRUE(std::fabs(p.scale - POPUP_OVERSHOOT) < 0.001f);
    s.update(POPUP_SETTLE_MS / 2);
    ASSERT_TRUE(std::fabs(p.scale - 1.1f) < 0.001f);       // linear settle
    s.update(POPUP_SETTLE_MS / 2);
    ASSERT_TRUE(p.scale == 1.0f);
    s.update(POPUP_MESSAGE_HOLD_MS - 1);
    ASSERT_TRUE(p.alpha == 1.0f && p.visible);             // boundary: still holding
    s.update(1);
    s.update(POPUP_FADE_OUT_MS / 2);
    ASSERT_TRUE(std::fabs(p.alpha - 0.5f) < 0.001f);
    s.update(POPUP_FADE_OUT_MS / 2);
    ASSERT_TRUE(p.alpha == 0.0f);
    s.update(1);
    ASSERT_TRUE(!s.popup_kind() && !p.visible);
    ASSERT_EQ(popup_duration_ms(PopupKind::NotListed), 1650);
}

TEST(banner_popup_runs_its_frames_at_50_ms) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::Bingo);
    s.update(POPUP_FADE_IN_MS);
    s.update(POPUP_GROW_MS);
    s.update(POPUP_SETTLE_MS);
    ASSERT_EQ(s.popup().frame, 0);
    s.update(POPUP_BANNER_FRAME_MS * 3);
    ASSERT_EQ(s.popup().frame, 3);
    s.update(POPUP_BANNER_FRAME_MS * 12);
    ASSERT_EQ(s.popup().frame, 14);                        // one pass, rests on the last frame
    ASSERT_EQ(popup_duration_ms(PopupKind::Bingo), 2500);
    ASSERT_EQ(popup_duration_ms(PopupKind::AllWords), 150 + 250 + 100 + 18 * 50 + 1000 + 250);
}

// 0x424730: the first plain correct word queues popup 10 (click a word
// for its definition), once per scene; later words queue nothing.
TEST(first_correct_word_shows_the_definitions_hint_once) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});
    f.type("arc");
    s.key_return();
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});
    f.skip();                                              // round 2: the flag stays set
    f.type("car");
    s.key_return();
    s.update(5000);
    ASSERT_TRUE(!f.shown());
}

// The first bingo shows its banner instead; a repeat bingo can show the hint.
TEST(first_bingo_skips_the_definitions_hint_but_a_repeat_bingo_shows_it) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("crate");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::Bingo);
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});
    f.type("trace");                                       // repeat bingo: no banner
    s.key_return();
    ASSERT_EQ(s.popups_waiting(), std::size_t{1});
    s.update(popup_duration_ms(PopupKind::Bingo));
    s.update(1);                                           // the clean-up step runs a frame later
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);
}

// A rejected word never sets the flag.
TEST(rejected_words_leave_the_definitions_hint_pending) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("cra");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::NotListed);
    s.update(5000);
    f.type("car");
    s.key_return();
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);
}

// 0x425c50 -> 0x42ace0: a new round drops the popups still waiting.
TEST(new_round_drops_waiting_popups) {
    Fixture f(false);
    auto& s = *f.scene;
    for (const char* w : {"cra", "rca", "atc"}) { f.type(w); s.key_return(); }
    ASSERT_EQ(s.popups_waiting(), std::size_t{3});
    f.skip();                                              // PASS, YES, 3 s reveal, round 2
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});
}

TEST(bingo_qualifies_with_popup_and_bonus) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    ASSERT_TRUE(s.qualified());
    ASSERT_EQ(s.score(), 250 + 1000);
    ASSERT_TRUE(f.played(SoundId::WordBingo));
    ASSERT_TRUE(f.shown() == PopupKind::Bingo);
    ASSERT_EQ(s.popup().sheet.image_id, std::string("IMAGE_POPUP_BINGO"));
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});          // the banner only, no extra message
    s.update(popup_duration_ms(PopupKind::Bingo));
    s.update(16);
    ASSERT_TRUE(!s.popup_kind());
    ASSERT_TRUE(!s.popup().visible);
    // A second bingo-length word is just 10*len^2 (no second bonus).
    std::string other;
    for (const auto& v : s.slots()) if (v.word.size() == 5 && v.word != s.rack()) { other = v.word; break; }
    f.sounds.clear();
    f.type(other);
    s.key_return();
    ASSERT_EQ(s.score(), 1250 + 250);
    ASSERT_TRUE(f.played(SoundId::WordBingo));
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);      // no second banner; popup 10 instead
}

TEST(all_words_ends_round_and_next_round_starts) {
    Fixture f(false);
    auto& s = *f.scene;
    std::vector<std::string> words;
    for (const auto& v : s.slots()) words.push_back(v.word);
    for (const auto& w : words) { f.type(w); s.key_return(); }
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::AllWords);
    ASSERT_TRUE(f.played(SoundId::AllWords));
    // Popup 10 (first correct word) came first; BINGO and ALL WORDS
    // wait behind it (one at a time).
    ASSERT_TRUE(f.shown() == PopupKind::Definitions);
    ASSERT_EQ(s.popups_waiting(), std::size_t{2});
    // Counted as DIALOG_NEXTROUND opens (the original: on its OK, 0x425190).
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Untimed)], 0);
    const std::int32_t score = s.score();
    ASSERT_TRUE(score >= 1000 + 100 * static_cast<std::int32_t>(words.size()));
    s.type_letter('c');                                    // input ignored now
    ASSERT_TRUE(s.typed().empty());
    s.update(ALL_WORDS_REVEAL_MS - 10);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);           // 0x424730 -> 0x428840(1000)
    ASSERT_TRUE(!s.dialog_open());
    s.update(20);
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(f.played(SoundId::NextRoundAchieved));
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Untimed)], 1);
    s.dialog_press("BUTTON_OK");
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.score(), score);                           // score carries over
    ASSERT_TRUE(!s.qualified());
    for (const auto& v : s.slots()) ASSERT_TRUE(!v.found && !v.revealed);
}

// DIALOG_NEXTROUND letterballs (0x409100): "Great Job" on slots 1-8,
// character i waits 500 + 200 * i ms (the space counts), then flips.
TEST(next_round_balls_spell_great_job) {
    const auto balls = next_round_balls(NEXT_ROUND_LETTERBALLS);
    ASSERT_EQ(balls.size(), std::size_t{8});
    const std::string letters = "greatjob";
    const int delays[8] = {500, 700, 900, 1100, 1300, 1700, 1900, 2100};
    for (std::size_t i = 0; i < balls.size(); ++i) {
        ASSERT_EQ(balls[i].letter, letters[i]);
        ASSERT_EQ(balls[i].slot, static_cast<int>(i));
        ASSERT_EQ(balls[i].reveal_delay_ms, delays[i]);
        ASSERT_TRUE(balls[i].x == NEXT_ROUND_BALL_XY[i][0] && balls[i].y == NEXT_ROUND_BALL_XY[i][1]);
        ASSERT_EQ(balls[i].frame, 0);
    }
    ASSERT_EQ(balls[0].sheet_frame(), ('g' - 'a') * BALL_FRAMES_PER_LETTER);   // 102, first flip frame
    ASSERT_TRUE(balls[2].x == 192.0f && balls[2].y == -8.0f);                  // slot 3 pokes above the frame
    // SOUND_letter_load goes with the ball appearing at 500 ms.
    LetterBall g = balls[0];
    ASSERT_TRUE(!g.tick(NEXT_ROUND_BALL_DELAY_MS - 1));
    ASSERT_TRUE(g.tick(1));
}

TEST(next_round_balls_skip_spaces_and_stop_at_eight) {
    const auto two = next_round_balls("Hi");
    ASSERT_EQ(two.size(), std::size_t{2});                  // slots 3-8 stay empty
    const auto gap = next_round_balls("a!b");              // '!' takes slot 2 but has no sheet row
    ASSERT_EQ(gap.size(), std::size_t{2});
    ASSERT_EQ(gap[1].slot, 2);
    ASSERT_EQ(gap[1].reveal_delay_ms, 500 + 2 * 200);
    ASSERT_EQ(next_round_balls("abcdefghijk").size(), std::size_t{8});   // no slot "9"
    ASSERT_TRUE(next_round_balls("").empty());
    ASSERT_TRUE(next_round_balls("   ").empty());
}

// ---- DIALOG_NEXTROUND particle burst (0x409100) -----------------------------

TEST(particle_waits_then_flies_out_and_fades) {
    Particle p;
    p.start_x = 10;
    p.start_y = 20;
    p.end_x = 110;                                         // 100 px to the right
    p.end_y = 20;
    p.delay_ms = 500;
    p.fade_ms = 600;
    p.tick(499);                                           // still on its ball
    ASSERT_EQ(p.x(), 10.0f);
    ASSERT_EQ(p.alpha(), 1.0f);
    p.tick(351);                                           // 350 ms in: 350 / 700 of the way
    ASSERT_EQ(p.x(), 60.0f);
    ASSERT_EQ(p.y(), 20.0f);
    ASSERT_TRUE(std::fabs(p.alpha() - (1.0f - 350.0f / 600.0f)) < 1e-6f);
    p.tick(250);                                           // 600 ms in: faded out, not there yet
    ASSERT_EQ(p.alpha(), 0.0f);
    ASSERT_TRUE(p.x() < 110.0f);
    p.tick(1000);                                          // the line stops at its end
    ASSERT_EQ(p.x(), 110.0f);
    ASSERT_EQ(p.alpha(), 0.0f);
}

TEST(each_next_round_ball_bursts_eight_particles) {
    const auto balls = next_round_balls(NEXT_ROUND_LETTERBALLS);
    tt2::Random rng(42);
    const auto ps = next_round_particles(balls, rng);
    ASSERT_EQ(ps.size(), balls.size() * NEXT_ROUND_PARTICLES_PER_BALL);
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const LetterBall& b = balls[i / NEXT_ROUND_PARTICLES_PER_BALL];
        const Particle& p = ps[i];
        ASSERT_EQ(p.start_x, b.x);                         // from the ball's centre
        ASSERT_EQ(p.start_y, b.y);
        ASSERT_EQ(p.delay_ms, b.reveal_delay_ms);          // with the ball (500 + 200 * i)
        ASSERT_TRUE(p.fade_ms >= 600 && p.fade_ms < 690);
        const float d = std::hypot(p.end_x - p.start_x, p.end_y - p.start_y);
        ASSERT_TRUE(d >= 75.0f - 1e-3f && d <= 124.0f + 1e-3f);   // (75 + 0..49) on each axis
    }
    // Same seed, same burst; the draws are angle, rx, ry, fade per particle.
    tt2::Random again(42);
    const auto ps2 = next_round_particles(balls, again);
    ASSERT_EQ(ps2[5].end_x, ps[5].end_x);
    tt2::Random manual(42);
    manual.next_int();
    const double a = static_cast<double>(manual.state() >> 24) / 16777216.0 * 6.283185307179586;
    const int rx = manual.rand(0, 50);
    const int ry = manual.rand(0, 50);
    const int fade = 600 + manual.rand(0, 90);
    ASSERT_EQ(ps[0].end_x, balls[0].x + (75.0f + rx) * static_cast<float>(std::cos(a)));
    ASSERT_EQ(ps[0].end_y, balls[0].y + (75.0f + ry) * static_cast<float>(std::sin(a)));
    ASSERT_EQ(ps[0].fade_ms, fade);
    // No balls, no burst.
    tt2::Random none(1);
    ASSERT_TRUE(next_round_particles({}, none).empty());
}

TEST(next_round_dialog_owns_its_particles) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_TRUE(s.dialog_particles().empty());
    std::vector<std::string> words;
    for (const auto& v : s.slots()) words.push_back(v.word);
    for (const auto& w : words) { f.type(w); s.key_return(); }
    s.update(REVEAL_MS + 16);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_EQ(s.dialog_particles().size(), std::size_t{8 * 8});
    const Particle first = s.dialog_particles()[0];
    s.update(500 + 350);                                   // G's burst is half way out
    ASSERT_TRUE(s.dialog_particles()[0].x() != first.x() || s.dialog_particles()[0].y() != first.y());
    ASSERT_TRUE(s.dialog_particles()[0].alpha() < 1.0f);
    ASSERT_EQ(s.dialog_particles()[63].alpha(), 1.0f);    // B waits until 2100 ms
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_particles().empty());             // gone with the dialog
}

TEST(next_round_dialog_flips_its_balls_with_sound) {
    Fixture f(false);
    auto& s = *f.scene;
    std::vector<std::string> words;
    for (const auto& v : s.slots()) words.push_back(v.word);
    for (const auto& w : words) { f.type(w); s.key_return(); }
    s.update(REVEAL_MS + 16);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_EQ(s.dialog_balls().size(), std::size_t{8});
    f.sounds.clear();
    s.update(500 + 12 * BALL_REVEAL_FRAME_MS);             // G is done, R is still flipping
    ASSERT_EQ(s.dialog_balls()[0].frame, BALL_IDLE_FRAME);
    ASSERT_TRUE(s.dialog_balls()[1].revealing());
    // 1220 ms: G, R, E and A (500, 700, 900, 1100 ms) have each sounded
    // as they started; T waits until 1300 ms.
    ASSERT_EQ(static_cast<int>(std::count(f.sounds.begin(), f.sounds.end(), SoundId::LetterLoad)), 4);
    s.update(2100);                                        // past B's 2100 + 720 ms
    int loads = 0;
    for (auto id : f.sounds) if (id == SoundId::LetterLoad) ++loads;
    ASSERT_EQ(loads, 8);
    for (const auto& b : s.dialog_balls()) ASSERT_EQ(b.frame, BALL_IDLE_FRAME);
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_balls().empty());                 // gone with the dialog
}

TEST(next_round_button_while_qualified_reveals_then_advances) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    s.press("BUTTON_NextRound");
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));
    bool any_revealed = false;
    for (const auto& v : s.slots()) if (v.revealed) any_revealed = true;
    ASSERT_TRUE(any_revealed);
    // 0x4333a0: missed words flip in letter by letter on yellow tiles.
    for (const auto& v : s.slots()) {
        if (!v.revealed) continue;
        ASSERT_EQ(v.flip_ms, 0);
        ASSERT_TRUE(!v.tile(0).letter_visible);
    }
    s.update(1000);
    for (const auto& v : s.slots()) {
        if (!v.revealed) continue;
        ASSERT_EQ(v.tile(0).tile_frame, 1);
        ASSERT_TRUE(v.tile(static_cast<int>(v.word.size()) - 1).letter_visible);
    }
    s.update(REVEAL_MS - 1 - 1000);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(2);
    s.update(1);
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    s.key_return();
    ASSERT_EQ(s.round_number(), 2);
}

// ---- timer ---------------------------------------------------------------

TEST(untimed_clock_never_moves) {
    Fixture f(false);
    f.scene->update(60000);
    ASSERT_EQ(f.scene->time_left_ms(), ROUND_MS);
    ASSERT_TRUE(!f.played(SoundId::ClockCountdown));
}

TEST(timed_clock_counts_down_with_warning_and_time_up) {
    Fixture f(true);
    auto& s = *f.scene;
    s.update(1500);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS - 1500);
    ASSERT_EQ(s.timer_minutes_text(), std::string("1:"));
    ASSERT_EQ(s.timer_seconds_text(), std::string("58"));
    s.update(ROUND_MS - 1500 - WARNING_MS - 500);            // 10.5 s left, no beep yet
    ASSERT_TRUE(!f.played(SoundId::ClockCountdown));
    s.update(600);                                          // crosses 10 s
    ASSERT_TRUE(f.played(SoundId::ClockCountdown));
    f.sounds.clear();
    for (int i = 0; i < 9; ++i) s.update(1000);             // 0.9 s left: 9 more beeps
    int beeps = 0;
    for (auto id : f.sounds) if (id == SoundId::ClockCountdown) ++beeps;
    ASSERT_EQ(beeps, 9);
    s.update(2000);
    ASSERT_EQ(s.time_left_ms(), 0);
    ASSERT_TRUE(f.played(SoundId::ClockTimeUp));
    ASSERT_TRUE(f.played(SoundId::LoseLife));
    ASSERT_TRUE(s.last_round_end() == RoundEnd::TimeUpMissed);
    ASSERT_EQ(s.misses(), 1);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(REVEAL_MS + 20);
    // First miss: 0x425420(1) -> 0x428a80 opens DIALOG_PASSROUND.
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PassRound);
    s.key_return();                                          // OK -> 0x428aa0 -> 0x425190
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS);
    ASSERT_EQ(f.discards, 0);
}

TEST(timed_time_up_miss_asks_round_passed_before_the_next_round) {
    Fixture f(true);
    auto& s = *f.scene;
    s.update(ROUND_MS + 10);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(!s.dialog_open());                           // the reveal runs first
    s.update(REVEAL_MS - 100);
    ASSERT_TRUE(!s.dialog_open());
    f.sounds.clear();
    s.update(200);                                           // 3000 ms after the miss
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PassRound);
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    ASSERT_TRUE(!f.played(SoundId::NextRoundAchieved));      // state 6 plays nothing
    ASSERT_EQ(s.round_number(), 1);
    ASSERT_EQ(s.misses(), 1);
    ASSERT_TRUE(!s.button("BUTTON_NextRound")->sprite.visible);   // the round did not qualify
    const Dialog& d = s.dialog();
    ASSERT_EQ(d.w, 380);
    ASSERT_EQ(d.h, 280);
    ASSERT_EQ(d.label("HEADER")->text, std::string(game_strings::PASS_ROUND_TITLE));
    ASSERT_EQ(d.label("BODY_TEXT")->text, std::string(game_strings::PASS_ROUND_BODY));
    ASSERT_TRUE(d.button("BUTTON_OK") != nullptr);
    s.update(60000);                                         // the dialog waits; no clock
    ASSERT_EQ(s.round_number(), 1);
    s.key_escape();                                          // needs OK, Esc does nothing
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PassRound);
    s.key_return();
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
}

TEST(untimed_pass_never_shows_round_passed) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_Pass_2");
    s.key_return();                                          // PASS? YES
    s.update(REVEAL_MS + 20);
    ASSERT_EQ(s.misses(), 1);
    ASSERT_TRUE(!s.dialog_open());                           // 0x425420(0): straight on
    ASSERT_EQ(s.round_number(), 2);
}

TEST(timed_round_with_bingo_advances_on_time_up) {
    Fixture f(true);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    s.update(ROUND_MS + 10);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::TimeUpQualified);
    ASSERT_EQ(s.misses(), 0);
    // Counted as DIALOG_NEXTROUND opens, after the 3000 ms reveal (the
    // original counts it on that dialog's OK, 0x425190).
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Timed)], 0);
    s.update(REVEAL_MS + 20);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Timed)], 1);
}

TEST(three_misses_end_the_game_with_high_score) {
    Fixture f(true);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();                                        // bingo: 1250 > the lowest default entry
    s.update(ROUND_MS + 10);                               // round 1 qualified
    s.update(REVEAL_MS + 20);
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    s.key_return();                                        // DIALOG_NEXTROUND OK
    for (int i = 0; i < 3; ++i) {                          // then 3 misses
        s.update(ROUND_MS + 10);
        s.update(REVEAL_MS + 20);
        if (i < 2) {                                       // misses 1 and 2: Round Passed
            ASSERT_TRUE(s.dialog_kind() == GameDialog::PassRound);
            s.key_return();
        }
    }
    ASSERT_EQ(s.misses(), 3);
    ASSERT_EQ(s.round_number(), 4);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_EQ(f.discards, 1);                              // the save goes with the game
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Congrats);  // a new high score
    s.key_return();
    // 0x401b50: OK opens DIALOG_HIGHSCORES on the Timed table, MAIN MENU only.
    ASSERT_TRUE(s.dialog_kind() == GameDialog::HighScores);
    ASSERT_TRUE(s.highscore_dialog() != nullptr);
    ASSERT_TRUE(s.highscore_dialog()->mode() == tt2::GameMode::Timed);
    ASSERT_TRUE(!s.dialog().button("BUTTON_OK")->sprite.visible);
    ASSERT_TRUE(f.played(SoundId::HighScoreAchieved));
    const auto& table = f.hs.table(tt2::GameMode::Timed);
    bool found = false;
    for (const auto& e : table.entries) if (e.name == "Mugwump" && e.score == 1250 && e.extra == "4") found = true;
    ASSERT_TRUE(found);
    ASSERT_EQ(f.exits, 0);
    s.key_return();                                        // the dialog takes no keys
    ASSERT_EQ(f.exits, 0);
    s.dialog_press("BUTTON_MAINMENU");
    ASSERT_EQ(f.exits, 1);
    ASSERT_TRUE(s.finished());
    s.dialog_press("BUTTON_MAINMENU");                     // only once
    ASSERT_EQ(f.exits, 1);
}

TEST(pass_asks_then_costs_a_miss_and_moves_on) {
    Fixture f(false, false);
    auto& s = *f.scene;
    const std::string rack = s.rack();
    s.press("BUTTON_Pass_2");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmPass);
    ASSERT_EQ(s.dialog().label("HEADER")->text, std::string("PASS?"));
    ASSERT_TRUE(s.paused());
    s.key_escape();                                        // NO (0x428720): resume, no miss
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(s.misses(), 0);
    ASSERT_EQ(s.rack(), rack);
    s.press("BUTTON_Pass_2");
    s.dialog_press("BUTTON_YES");
    ASSERT_EQ(s.misses(), 1);                              // 0x425420: misses++
    ASSERT_TRUE(f.played(SoundId::LoseLife));
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    // 0x425710 while +0x2f9 is set: setEnabled(0) only, so PASS x2 stays
    // on screen, greyed, through the reveal.
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_Pass_2"));
    ASSERT_TRUE(!s.button("BUTTON_Pass_2")->enabled);
    ASSERT_EQ(f.discards, 0);                              // the game goes on
    s.update(REVEAL_MS + 20);                              // 0x425190: next round by itself
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_Pass"));
}

TEST(two_passes_then_give_up_ends_the_game) {
    Fixture f(false, false);
    auto& s = *f.scene;
    f.skip();
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_Pass"));
    s.press("BUTTON_Pass");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmPass);   // 1 + 1 < 3: still a pass
    s.dialog_press("BUTTON_YES");
    s.update(REVEAL_MS + 20);
    ASSERT_EQ(s.misses(), 2);
    ASSERT_EQ(s.round_number(), 3);
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_GiveUp"));
    f.sounds.clear();
    f.skip();                                              // third miss
    ASSERT_EQ(s.misses(), 3);
    ASSERT_TRUE(f.played(SoundId::LoseLife));
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));        // 0x428c80(.., 1) still reveals
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_EQ(f.discards, 1);                              // 0x461840
}

TEST(hidden_skip_buttons_do_nothing) {
    Fixture f(false, false);
    auto& s = *f.scene;
    s.press("BUTTON_GiveUp");                              // hidden at misses 0
    s.press("BUTTON_Pass");                                // hidden at misses 0
    ASSERT_TRUE(!s.dialog_open());
    f.type(s.rack());
    s.key_return();                                        // qualified: NEXT ROUND takes the spot
    s.press("BUTTON_Pass_2");
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(s.misses(), 0);
}

TEST(resumed_misses_pick_the_skip_button) {
    Fixture f(false, false);
    tt2::SavedGame g = f.scene->snapshot();
    g.round = 4;
    g.fail_count = 1;
    Fixture r(false, false);
    GameScene::Services sv;
    GameScene s(r.com, r.ful, r.profile, nullptr, tt2::GameMode::Untimed, sv, 0, &g);
    ASSERT_EQ(s.misses(), 1);
    ASSERT_TRUE(s.button("BUTTON_Pass")->sprite.visible);
    ASSERT_TRUE(!s.button("BUTTON_Pass_2")->sprite.visible);
    ASSERT_TRUE(!s.button("BUTTON_GiveUp")->sprite.visible);
}

TEST(give_up_and_menu_leave_the_game) {
    Fixture f(false, false);
    auto& s = *f.scene;
    f.skip();                                              // pass 1
    f.skip();                                              // pass 2
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_GiveUp"));
    s.press("BUTTON_GiveUp");                              // asks first
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmGiveUp);
    ASSERT_EQ(s.dialog().label("HEADER")->text, std::string("Give Up?"));
    ASSERT_TRUE(s.paused());
    s.dialog_press("BUTTON_NO");
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    s.press("BUTTON_GiveUp");
    s.key_return();                                        // Return = YES
    ASSERT_TRUE(s.last_round_end() == RoundEnd::Passed);
    ASSERT_EQ(s.misses(), 3);
    s.update(REVEAL_MS + 20);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);   // score 0: no high score
    s.mouse_up(400, 300);                                  // a stray click does nothing
    ASSERT_EQ(f.exits, 0);
    s.dialog_press("BUTTON_OK");                           // MENU
    ASSERT_EQ(f.exits, 1);

    Fixture g(false, false);
    g.scene->press("BUTTON_Menu");                         // the pause menu, not an exit
    ASSERT_EQ(g.exits, 0);
    ASSERT_TRUE(g.scene->dialog_kind() == GameDialog::PauseMenu);
    Fixture h(true, false);
    h.scene->key_escape();                                 // Esc = pause menu
    ASSERT_TRUE(h.scene->dialog_kind() == GameDialog::PauseMenu);
    h.scene->key_escape();                                 // and back
    ASSERT_TRUE(!h.scene->dialog_open());
}

TEST(mouse_buttons_hover_press_and_fire) {
    Fixture f(false);
    auto& s = *f.scene;
    auto* twist = s.button("BUTTON_Twist");
    const float cx = twist->sprite.x + 10, cy = twist->sprite.y + 10;
    s.mouse_move(cx, cy);
    ASSERT_TRUE(twist->hover);
    ASSERT_TRUE(!f.played(SoundId::Button));                // hover is silent; only the click sounds
    s.mouse_down(cx, cy);
    ASSERT_TRUE(twist->pressed);
    s.update(0);
    ASSERT_EQ(twist->sprite.frame, 2);
    s.mouse_up(cx, cy);
    ASSERT_TRUE(f.played(SoundId::Button));
    ASSERT_EQ(f.profile.twists, 1);
    // Hidden GIVE UP / NEXT ROUND cannot be clicked.
    auto* next = s.button("BUTTON_NextRound");
    s.mouse_down(next->sprite.x + 5, next->sprite.y + 5);
    s.mouse_up(next->sprite.x + 5, next->sprite.y + 5);
    ASSERT_EQ(s.round_number(), 1);
}

// 0x425710: the letter buttons follow the two rows. TWIST needs two
// balls left to shuffle, CLEAR and ENTER need played letters (ENTER three
// in the port, one in the original), LAST needs an empty row and a word.
static bool on(GameScene& s, const char* n) { return s.button(n)->enabled; }

TEST(letter_buttons_follow_the_rows) {
    Fixture f(false);
    auto& s = *f.scene;
    // Nothing played, nothing submitted yet: TWIST is the only one.
    ASSERT_TRUE(on(s, "BUTTON_Twist"));
    ASSERT_TRUE(!on(s, "BUTTON_Clear"));
    ASSERT_TRUE(!on(s, "BUTTON_Enter"));
    ASSERT_TRUE(!on(s, "BUTTON_LastWord"));
    f.type("c");                                           // one letter: CLEAR, not ENTER
    ASSERT_TRUE(on(s, "BUTTON_Clear"));
    ASSERT_TRUE(!on(s, "BUTTON_Enter"));
    ASSERT_TRUE(on(s, "BUTTON_Twist"));
    f.type("a");                                           // two letters: still no ENTER
    ASSERT_TRUE(!on(s, "BUTTON_Enter"));
    f.type("r");                                           // three: ENTER
    ASSERT_TRUE(on(s, "BUTTON_Enter"));
    s.press("BUTTON_Clear");                               // back to the start
    ASSERT_TRUE(s.typed().empty());
    ASSERT_TRUE(!on(s, "BUTTON_Clear"));
    ASSERT_TRUE(!on(s, "BUTTON_Enter"));
    f.type("car");
    s.press("BUTTON_Enter");                               // submitted: LAST can recall it
    ASSERT_EQ(s.score(), 90);
    ASSERT_TRUE(on(s, "BUTTON_LastWord"));
    ASSERT_TRUE(!on(s, "BUTTON_Clear"));
    f.type("a");                                           // a letter in the row: no LAST
    ASSERT_TRUE(!on(s, "BUTTON_LastWord"));
    s.backspace();
    ASSERT_TRUE(on(s, "BUTTON_LastWord"));
}

TEST(twist_needs_two_balls_left) {
    Fixture f(false);
    auto& s = *f.scene;
    const std::string rack = s.rack();
    f.type(rack.substr(0, rack.size() - 2));               // two left: can still swap
    ASSERT_TRUE(on(s, "BUTTON_Twist"));
    f.type(rack.substr(rack.size() - 2, 1));               // one left: nothing to shuffle
    ASSERT_TRUE(!on(s, "BUTTON_Twist"));
    f.type(rack.substr(rack.size() - 1));
    ASSERT_TRUE(!on(s, "BUTTON_Twist"));
    ASSERT_TRUE(on(s, "BUTTON_Enter"));
}

TEST(disabled_letter_buttons_ignore_presses) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_LastWord");                            // no last word yet
    ASSERT_TRUE(s.typed().empty());
    f.type("ca");
    f.sounds.clear();
    s.press("BUTTON_Enter");                               // two letters: the button is off
    ASSERT_EQ(s.typed(), std::string("ca"));
    ASSERT_TRUE(f.sounds.empty());
    s.key_return();                                        // the keyboard still submits, as in 0x427560
    ASSERT_TRUE(s.typed().empty());
    ASSERT_TRUE(f.played(SoundId::WordReject));
}

TEST(last_word_does_not_carry_into_the_next_round) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();                                        // bingo
    ASSERT_TRUE(on(s, "BUTTON_LastWord"));
    s.press("BUTTON_NextRound");
    s.update(REVEAL_MS + 16);
    s.dialog_press("BUTTON_OK");                           // round 2
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(s.last_word().empty());
    ASSERT_TRUE(!on(s, "BUTTON_LastWord"));
    s.key_return();                                        // Return on an empty row recalls nothing
    ASSERT_TRUE(s.typed().empty());
}

TEST(letter_buttons_are_off_outside_play) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();                                        // bingo: qualified
    s.press("BUTTON_NextRound");                           // reveal phase
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    for (const char* n : {"BUTTON_Twist", "BUTTON_Clear", "BUTTON_Enter", "BUTTON_LastWord"})
        ASSERT_TRUE(!on(s, n));
    ASSERT_TRUE(on(s, "BUTTON_Menu"));
}

TEST(pointer_hot_tracks_button_hover) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_TRUE(!s.pointer_over_clickable());
    auto* twist = s.button("BUTTON_Twist");
    s.mouse_move(twist->sprite.x + 10, twist->sprite.y + 10);
    ASSERT_TRUE(s.pointer_over_clickable());
    s.mouse_move(0, 0);
    ASSERT_TRUE(!s.pointer_over_clickable());
}

// 0x427a60: over any letter ball the cursor turns to the hand, and that is
// all; the ball keeps its frame, scale and row, and no sound plays.
TEST(pointer_hot_over_letter_balls_without_changing_them) {
    Fixture f(false);
    auto& s = *f.scene;
    const auto& b0 = s.balls()[0];
    const int frame = b0.frame;
    f.sounds.clear();
    s.mouse_move(b0.target_x, b0.target_y);
    ASSERT_TRUE(s.pointer_over_clickable());
    ASSERT_EQ(b0.frame, frame);
    ASSERT_TRUE(!b0.played);
    ASSERT_TRUE(f.sounds.empty());
    s.mouse_up(b0.target_x, b0.target_y);                 // play it: the played row is hot too
    ASSERT_TRUE(b0.played);
    s.mouse_move(b0.target_x, b0.target_y);
    ASSERT_TRUE(s.pointer_over_clickable());
    s.mouse_move(1, 1);                                    // empty space
    ASSERT_TRUE(!s.pointer_over_clickable());
}

// 0x433500: a shown word under the pointer gets the hand cursor, and a
// click opens its dictionary.com page (the original's 0x58feec
// dictionary.reference.com link, updated).
static const SlotView* slot_with(const GameScene& s, bool (*pick)(const SlotView&)) {
    for (const auto& v : s.slots()) if (pick(v)) return &v;
    return nullptr;
}

TEST(clicking_a_found_word_opens_its_definition) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    const SlotView* car = slot_with(s, [](const SlotView& v) { return v.found; });
    ASSERT_TRUE(car != nullptr);
    ASSERT_EQ(car->word, std::string("car"));
    const float lx = car->x + ANAGRAM_TILE_W * 2.5f;       // the last tile, 'r'
    const float ly = car->y + ANAGRAM_TILE_H * 0.5f;
    s.mouse_move(lx, ly);
    ASSERT_TRUE(s.pointer_over_clickable());
    f.sounds.clear();
    s.mouse_up(lx, ly);
    ASSERT_EQ(f.urls.size(), std::size_t{1});
    ASSERT_EQ(f.urls[0], std::string("https://www.dictionary.com/browse/car"));
    ASSERT_TRUE(s.typed().empty());                        // no ball moved
    ASSERT_TRUE(f.sounds.empty());                         // and no sound
}

TEST(hidden_words_and_gaps_open_nothing) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    const SlotView* hidden = slot_with(s, [](const SlotView& v) { return !v.shown(); });
    ASSERT_TRUE(hidden != nullptr);
    const float hx = hidden->x + ANAGRAM_TILE_W * 0.5f, hy = hidden->y + ANAGRAM_TILE_H * 0.5f;
    s.mouse_move(hx, hy);
    ASSERT_TRUE(!s.pointer_over_clickable());              // an unguessed word is not a link
    s.mouse_up(hx, hy);
    const SlotView* car = slot_with(s, [](const SlotView& v) { return v.found; });
    const float right = static_cast<float>(car->x + 3 * ANAGRAM_TILE_W);   // one past 'r'
    s.mouse_up(right, car->y + 5.0f);
    s.mouse_up(car->x + 5.0f, car->y + ANAGRAM_TILE_H);   // just below the tiles
    ASSERT_TRUE(f.urls.empty());
}

TEST(revealed_words_are_links_too) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    s.press("BUTTON_NextRound");                           // reveal the missed words
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    const SlotView* missed = slot_with(s, [](const SlotView& v) { return v.revealed; });
    ASSERT_TRUE(missed != nullptr);
    const std::string word = missed->word;
    s.mouse_up(missed->x + 5.0f, missed->y + 5.0f);
    ASSERT_EQ(f.urls.size(), std::size_t{1});
    ASSERT_EQ(f.urls[0], definition_url(word));
}

TEST(ball_hover_clears_when_a_dialog_opens) {
    Fixture f(false);
    auto& s = *f.scene;
    const auto& b0 = s.balls()[0];
    s.mouse_move(b0.target_x, b0.target_y);
    ASSERT_TRUE(s.pointer_over_clickable());
    s.press("BUTTON_Menu");                                // pause menu
    ASSERT_TRUE(s.dialog_open());
    s.dialog_press("BUTTON_RESUME");
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(!s.pointer_over_clickable());              // stale hover dropped
    ASSERT_EQ(s.ball_at(1, 1), -1);
    ASSERT_EQ(s.ball_at(b0.target_x, b0.target_y), 0);
    ASSERT_EQ(s.ball_at(b0.target_x, b0.target_y - BALL_HALF - 1), -1);   // just above the cell
}

// ---- ball animation (0x43a4c0 / 0x43a820 / 0x43c370) -----------------------

TEST(ball_flip_runs_frames_0_to_11_then_rests_as_a_pearl) {
    LetterBall b;
    b.letter = 'c';
    b.frame = 0;
    b.reveal_delay_ms = 120;
    b.reveal_elapsed_ms = 0;
    ASSERT_TRUE(b.revealing());
    ASSERT_TRUE(!b.tick(100));                             // still waiting
    ASSERT_EQ(b.frame, 0);
    ASSERT_TRUE(b.tick(20 + 3 * BALL_REVEAL_FRAME_MS));    // started (the sound cue), 3 frames in
    ASSERT_EQ(b.frame, 3);
    ASSERT_TRUE(!b.tick(9 * BALL_REVEAL_FRAME_MS));        // last frame reached, no second cue
    ASSERT_EQ(b.frame, BALL_IDLE_FRAME);
    ASSERT_TRUE(!b.revealing());
    ASSERT_TRUE(!b.tick(1000));                            // only once
    ASSERT_EQ(b.sheet_frame(), 2 * BALL_FRAMES_PER_LETTER + BALL_IDLE_FRAME);
}

// 0x43a4c0: ModifierLocationLine with easing 0x4513d0 (smoothstep).
// Worked example: a quarter of the time in, (3 - 0.5) * 0.0625 = 0.15625
// of the way; half way is still the midpoint.
TEST(ball_slides_in_a_straight_line_over_its_duration) {
    LetterBall b;
    b.letter = 'a';
    b.start_x = b.x = 100;
    b.start_y = b.y = 300;
    b.target_x = 200;
    b.target_y = 200;
    b.move_ms = BALL_MOVE_MS;
    b.tick(BALL_MOVE_MS / 4);
    ASSERT_TRUE(b.x == 115.625f && b.y == 284.375f);       // slow start, not 125 / 275
    b.tick(BALL_MOVE_MS / 4);
    ASSERT_TRUE(b.x == 150.0f && b.y == 250.0f);
    b.tick(BALL_MOVE_MS);
    ASSERT_TRUE(b.x == 200.0f && b.y == 200.0f);
}

TEST(placing_takes_300_ms_and_twist_400_ms) {
    Fixture f(false);
    auto& s = *f.scene;
    s.update(5000);                                        // let the flips finish
    s.type_letter(s.rack()[0]);
    const LetterBall* placed = nullptr;
    for (const auto& b : s.balls()) if (b.played) placed = &b;
    ASSERT_EQ(placed->move_ms, BALL_MOVE_MS);
    s.update(BALL_MOVE_MS - 1);
    ASSERT_TRUE(placed->y != placed->target_y);
    s.update(1);
    ASSERT_TRUE(placed->y == placed->target_y);
    s.press("BUTTON_Clear");
    s.update(1000);
    s.press("BUTTON_Twist");
    for (const auto& b : s.balls()) ASSERT_EQ(b.move_ms, BALL_RELAYOUT_MS);
}

// ---- pause menu and confirmations -----------------------------------------

TEST(pause_menu_layout_matches_dialog_menu) {
    const Dialog d = build_pause_menu();
    ASSERT_EQ(d.w, 322);
    ASSERT_EQ(d.h, 390);
    ASSERT_EQ(d.buttons.size(), std::size_t{4});
    ASSERT_TRUE(d.button("BUTTON_RESUME")->sprite.y == 290.0f);
    ASSERT_TRUE(d.button("BUTTON_STOP")->sprite.y == 224.0f);
    ASSERT_EQ(d.button("BUTTON_STOP")->sprite.sheet.image_id, std::string("BUTTON_PAUSE_END"));
    ASSERT_EQ(d.button("BUTTON_HOWTOPLAY")->sprite.sheet.frame_h, 52);
    ASSERT_EQ(d.label("PAUSE_HEADER")->text, std::string("PAUSE MENU"));
}

TEST(yes_no_dialog_grows_with_its_message) {
    const Dialog one = build_yes_no("H", "one line");
    ASSERT_EQ(one.h, YESNO_BUTTON_Y + YESNO_LINE_H + YESNO_BUTTON_H + YESNO_BOTTOM_PAD);   // 214
    ASSERT_TRUE(one.button("BUTTON_YES")->sprite.y == static_cast<float>(YESNO_BUTTON_Y + YESNO_LINE_H));
    const Dialog four = build_yes_no(game_strings::EXIT_HEADER, game_strings::EXIT_BODY);
    const int button_y = YESNO_BUTTON_Y + 4 * YESNO_LINE_H;
    ASSERT_TRUE(four.button("BUTTON_NO")->sprite.y == static_cast<float>(button_y));
    ASSERT_EQ(four.h, button_y + YESNO_BUTTON_H + YESNO_BOTTOM_PAD);
    ASSERT_EQ(four.label("MESSAGE")->line_height, YESNO_LINE_H);
    const Dialog ok = build_yes_no("H", "m", true);
    ASSERT_TRUE(ok.button("BUTTON_OK") != nullptr);
    ASSERT_TRUE(ok.button("BUTTON_YES") == nullptr);
}

// The menu's "Continue the game?" message wraps at 332 px (400 minus the
// 34 px text inset on each side). With the placeholder FONT_SETTINGSTEXT
// at scale 3, "Do you want to continue" (387 px, the original's first
// line at 390) no longer fits, so " continue" moves to the next line.
TEST(yes_no_message_wraps_at_the_label_width) {
    const BitmapFont font = make_placeholder_font(3);
    const LineBreaker wrap = [&font](const std::string& t, int w) { return wrap_text(font, t, w); };
    const Dialog d = build_yes_no(game_strings::SAVEGAME_HEADER, game_strings::SAVEGAME_BODY, false, wrap);
    const std::string& text = d.label("MESSAGE")->text;
    ASSERT_EQ(text.substr(0, text.find('\n')), std::string("Do you want to"));
    std::size_t start = 0;
    while (start <= text.size()) {   // every line fits inside the frame's window
        const std::size_t end = std::min(text.find('\n', start), text.size());
        ASSERT_TRUE(font.measure(text.substr(start, end - start)) <= YESNO_MESSAGE_W);
        start = end + 1;
    }
    const int button_y = YESNO_BUTTON_Y + text_line_count(text) * YESNO_LINE_H;
    ASSERT_TRUE(d.button("BUTTON_YES")->sprite.y == static_cast<float>(button_y));
    ASSERT_EQ(d.h, button_y + YESNO_BUTTON_H + YESNO_BOTTOM_PAD);
    ASSERT_EQ(d.label("MESSAGE")->max_width, YESNO_MESSAGE_W);
}

// Port padding: the message stays inside the frame's window and the
// button pills end above the window's bottom edge, for any width.
TEST(yes_no_content_stays_inside_the_frame_window) {
    for (int width : {400, 600}) {
        const Dialog d = build_yes_no("H", "line one\nline two\nline three", false, {}, width);
        const TextLabel* msg = d.label("MESSAGE");
        ASSERT_EQ(msg->max_width, width - 2 * YESNO_TEXT_INSET);
        ASSERT_TRUE(msg->x - msg->max_width / 2.0f >= 22.0f);               // left window edge
        ASSERT_TRUE(msg->x + msg->max_width / 2.0f <= width - 22.0f);       // right window edge
        const float pill_bottom = d.button("BUTTON_YES")->sprite.y + 43.0f;
        ASSERT_TRUE(pill_bottom + 6.0f <= static_cast<float>(d.h - 35));   // window bottom
    }
}

TEST(yes_no_padding_is_a_port_deviation) {
    // The original MESSAGE is 390 wide with no bottom margin (0x40b470);
    // the port narrows and pads it. Guard the numbers against drift.
    ASSERT_EQ(YESNO_MESSAGE_W, 332);
    ASSERT_TRUE(YESNO_MESSAGE_W < 390);
    ASSERT_EQ(YESNO_BOTTOM_PAD, 34);
}

TEST(yes_no_short_message_is_unchanged_by_the_wrap) {
    const BitmapFont font = make_placeholder_font(3);
    const LineBreaker wrap = [&font](const std::string& t, int w) { return wrap_text(font, t, w); };
    const Dialog d = build_yes_no("H", "one line", false, wrap);
    ASSERT_EQ(d.label("MESSAGE")->text, std::string("one line"));
    ASSERT_EQ(d.h, build_yes_no("H", "one line").h);
}

TEST(scene_yes_no_dialogs_use_the_wrap_service) {
    tt2::WordTree com = common();
    tt2::WordTree ful = full();
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
    profile.mode_flags.fill(true);                         // no tutorial
    int calls = 0;
    GameScene::Services sv;
    sv.play_sound = [](SoundId) {};
    sv.on_profile_change = [] {};
    sv.on_exit = [] {};
    sv.wrap_text = [&calls](const std::string& t, int w) {
        ++calls;
        ASSERT_EQ(w, YESNO_MESSAGE_W);
        return t + "\nX";
    };
    GameScene s(com, ful, profile, nullptr, tt2::GameMode::Untimed, sv, 0);
    s.press("BUTTON_Pass_2");                              // "PASS?" has two own lines too
    ASSERT_EQ(calls, 1);
    ASSERT_EQ(text_line_count(s.dialog().label("MESSAGE")->text), 3);   // 2 own lines + 1 added
}

TEST(pause_menu_freezes_the_clock_and_resumes) {
    Fixture f(true);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    ASSERT_TRUE(s.paused());
    s.update(30000);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS);                 // nothing moved
    s.type_letter(s.rack()[0]);                            // input goes to the dialog
    ASSERT_TRUE(s.typed().empty());
    s.dialog_press("BUTTON_RESUME");
    ASSERT_TRUE(!s.dialog_open());
    s.update(1000);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS - 1000);
    s.focus_lost();                                        // losing focus pauses too
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    s.key_return();                                        // Return = RESUME
    ASSERT_TRUE(!s.dialog_open());
}

// ROADMAP bug #4: DialogMenu's show (0x40aa30) only runs the base dialog
// show; the SOUND_PAUSE + music silence (0x409fa0) belongs to DialogPause.
// Opening and closing the pause menu therefore plays nothing but clicks.
TEST(pause_menu_plays_only_the_button_click) {
    Fixture f(false);
    auto& s = *f.scene;
    s.update(5000);                                        // let the round-start flips finish
    f.sounds.clear();
    s.press("BUTTON_Menu");
    s.update(500);
    s.dialog_press("BUTTON_RESUME");
    s.update(500);
    ASSERT_TRUE(!f.sounds.empty());
    for (auto id : f.sounds) ASSERT_TRUE(id == SoundId::Button);
}

TEST(pause_menu_settings_ok_saves_and_returns) {
    Fixture f(true);
    auto& s = *f.scene;
    s.update(1000);
    const int time_left = s.time_left_ms();
    const std::string rack = s.rack();
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_SETTINGS");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Settings);
    ASSERT_TRUE(s.paused());
    ASSERT_TRUE(s.settings_dialog() != nullptr);
    ASSERT_EQ(s.dialog().w, SETTINGS_W);
    ASSERT_EQ(s.settings_dialog()->max_word_length(), f.profile.max_word_length);
    const int saves = f.saves;
    s.dialog_press("SWITCH_LENGTH_6");
    s.dialog_press("SWITCH_FULLSCREEN");                   // hidden (UX-3): does nothing
    s.update(5000);
    ASSERT_EQ(s.time_left_ms(), time_left);                // the clock stands still
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(s.settings_dialog() == nullptr);
    ASSERT_EQ(f.profile.max_word_length, 6);
    ASSERT_EQ(f.saves, saves + 1);                          // 0x462080
    ASSERT_EQ(f.settings_commits, 1);
    ASSERT_TRUE(!f.settings.fullscreen);                    // the window mode lives in IMPROVEMENTS
    ASSERT_EQ(s.rack(), rack);                              // F13: the current rack stays
}

TEST(pause_menu_settings_cancel_and_escape_change_nothing) {
    Fixture f(false);
    auto& s = *f.scene;
    const int max_len = f.profile.max_word_length;
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_SETTINGS");
    s.dialog_press("SWITCH_LENGTH_8");
    s.dialog_press("BUTTON_CANCEL");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.profile.max_word_length, max_len);
    ASSERT_EQ(f.settings_commits, 0);
    ASSERT_TRUE(!f.music_applied.empty() && f.music_applied.back() == f.settings.music_volume);
    s.dialog_press("BUTTON_SETTINGS");
    s.key_escape();                                         // deviation: Esc = CANCEL
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.settings_commits, 0);
    s.dialog_press("BUTTON_DOES_NOT_EXIST");               // ignored
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
}

TEST(settings_window_mode_change_reaches_the_open_dialog) {
    Fixture f(false);
    auto& s = *f.scene;
    s.window_mode_changed(true);                            // no dialog: harmless
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_SETTINGS");
    s.window_mode_changed(true);
    ASSERT_TRUE(s.settings_dialog()->values().fullscreen);
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(f.settings.fullscreen);                     // OK does not flip it back
}

TEST(settings_slider_drag_in_the_scene_applies_live) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_SETTINGS");
    const SettingsSlider& music = s.settings_dialog()->slider("SLIDER_MUSICVOL");
    const float ox = s.dialog().left(), oy = s.dialog().top();
    s.mouse_down(ox + music.knob_x(), oy + music.knob_y());
    s.mouse_move(ox + 123 + 30, oy + 110);                  // left end
    s.mouse_up(ox + 123 + 30, oy + 110);
    ASSERT_TRUE(f.music_applied.back() == 0.0f);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Settings);   // a drag is not a click
}

TEST(pause_menu_how_to_play_pages_and_returns) {
    Fixture f(true);
    auto& s = *f.scene;
    s.update(1000);
    const int time_left = s.time_left_ms();
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_HOWTOPLAY");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::HowToPlay);
    ASSERT_TRUE(s.paused());
    ASSERT_EQ(s.dialog().w, HELP_W);
    s.dialog_press("BUTTON_PREV");                         // hidden on page 0
    ASSERT_EQ(s.help().page, 0);
    s.dialog_press("BUTTON_NEXT");
    ASSERT_EQ(s.help().page, 1);
    s.update(HELP_FADE_MS);                                 // the fade runs while paused
    ASSERT_TRUE(!s.help().fading());
    ASSERT_EQ(s.time_left_ms(), time_left);                 // the clock does not
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    s.dialog_press("BUTTON_HOWTOPLAY");
    ASSERT_EQ(s.help().page, 0);                            // reopens on page 1
    s.key_escape();
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
}

// 0x408320 / 0x408250 / 0x408180 show their dialog over the pause menu
// without hiding it; NO / OK / CANCEL (0x408c00) close only the top one.
TEST(pause_menu_stays_open_under_its_confirm_settings_and_help) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    ASSERT_TRUE(s.under_dialog() == nullptr);              // nothing stacked yet
    s.dialog_press("BUTTON_STOP");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmExit);
    ASSERT_TRUE(s.under_kind() == GameDialog::PauseMenu);  // the menu is still there
    ASSERT_TRUE(s.under_dialog()->button("BUTTON_RESUME") != nullptr);
    s.dialog_press("BUTTON_NO");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(s.under_dialog() == nullptr);
    s.dialog_press("BUTTON_SETTINGS");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Settings && s.under_kind() == GameDialog::PauseMenu);
    s.dialog_press("BUTTON_CANCEL");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu && s.under_dialog() == nullptr);
    s.dialog_press("BUTTON_HOWTOPLAY");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::HowToPlay && s.under_kind() == GameDialog::PauseMenu);
    s.key_escape();                                        // OK: back to the menu
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu && s.under_dialog() == nullptr);
}

TEST(the_dialog_under_takes_no_input) {
    // Only the top dialog answers: the menu's RESUME does nothing while
    // "Exit Game?" covers it, and the clock stays stopped.
    Fixture f(true);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_STOP");
    const int left = s.time_left_ms();
    s.dialog_press("BUTTON_RESUME");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmExit);
    s.update(1000);
    ASSERT_EQ(s.time_left_ms(), left);
}

TEST(leaving_or_resuming_clears_the_whole_stack) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_STOP");
    s.dialog_press("BUTTON_YES");                          // leave: nothing stays open
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(s.under_dialog() == nullptr);
    Fixture g(false);
    auto& t = *g.scene;
    t.press("BUTTON_Menu");
    t.dialog_press("BUTTON_RESUME");
    ASSERT_TRUE(!t.dialog_open() && t.under_dialog() == nullptr);
}

TEST(stop_asks_then_saves_and_leaves) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();                                        // one found word
    f.type("ca");                                          // half-typed letters are not saved
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_STOP");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmExit);
    ASSERT_EQ(s.dialog().label("HEADER")->text, std::string("Exit Game?"));
    s.key_escape();                                        // NO: back to the menu
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.exits, 0);
    s.dialog_press("BUTTON_STOP");
    s.dialog_press("BUTTON_YES");
    ASSERT_EQ(f.exits, 1);
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    const auto& g = f.saved[0];
    ASSERT_EQ(g.mode, std::string("CLASSIC_UNTIMED"));
    ASSERT_EQ(g.word, s.rack());
    ASSERT_EQ(g.round, 1);
    ASSERT_EQ(g.score, 90);
    ASSERT_EQ(g.fail_count, 0);
    ASSERT_TRUE(!g.qualified);
    ASSERT_EQ(g.found_words.size(), std::size_t{1});
    ASSERT_EQ(g.found_words[0], std::string("car"));
    ASSERT_TRUE(g.valid());
}

TEST(nothing_is_saved_once_the_round_is_over) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    s.press("BUTTON_NextRound");                           // round over, revealing
    ASSERT_TRUE(!s.can_save());
    s.leave();
    ASSERT_TRUE(f.saved.empty());
    s.update(REVEAL_MS + 1);
    s.key_escape();                                        // the round-end dialog needs a choice
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
}

TEST(saved_game_resumes_where_it_left_off) {
    Fixture f(true);
    f.type(f.scene->rack());
    f.scene->key_return();                                 // bingo: qualified
    f.type("car");
    f.scene->key_return();
    f.scene->update(15000);
    const tt2::SavedGame g = f.scene->snapshot();
    ASSERT_TRUE(g.qualified);
    ASSERT_EQ(g.time_ms, ROUND_MS - 15000);

    Fixture r(true);
    GameScene::Services sv;
    int discards = 0;
    sv.discard_save = [&] { ++discards; };
    tt2::SavedGame later = g;
    later.round = 4;
    later.fail_count = 2;
    GameScene resumed(r.com, r.ful, r.profile, nullptr, tt2::GameMode::Timed, sv, 0, &later);
    ASSERT_EQ(discards, 1);                                // 0x42a460 deletes the file
    ASSERT_EQ(resumed.rack(), g.word);
    ASSERT_TRUE(ball_order(resumed) != g.word);           // the resumed rack is scrambled too
    ASSERT_EQ(resumed.round_number(), 4);
    ASSERT_EQ(resumed.misses(), 2);
    ASSERT_EQ(resumed.score(), g.score);
    ASSERT_EQ(resumed.time_left_ms(), ROUND_MS - 15000);
    ASSERT_TRUE(resumed.qualified());
    int found = 0;
    for (const auto& v : resumed.slots()) if (v.found) ++found;
    ASSERT_EQ(found, 2);
    // 0x433090 restores words with every letter flipping at once.
    for (const auto& v : resumed.slots()) {
        if (!v.found) continue;
        ASSERT_TRUE(v.flip_together);
        ASSERT_EQ(v.flip_ms, 0);
    }
    ASSERT_TRUE(resumed.button("BUTTON_NextRound")->sprite.visible);
    ASSERT_TRUE(resumed.phase() == GamePhase::Playing);
    // A second bingo no longer earns the qualifying bonus.
    const std::int32_t before = resumed.score();
    for (char c : resumed.rack()) resumed.type_letter(c);
    resumed.key_return();
    ASSERT_EQ(resumed.score(), before);                    // duplicate: nothing
    // Bag untouched by a resume.
    ASSERT_EQ(r.profile.bag.index, std::uint32_t{1});
}

TEST(unusable_save_starts_a_fresh_game) {
    Fixture f(false);
    GameScene::Services sv;
    int discards = 0;
    sv.discard_save = [&] { ++discards; };
    tt2::SavedGame bad;
    bad.word = "x";                                        // invalid
    GameScene s(f.com, f.ful, f.profile, nullptr, tt2::GameMode::Untimed, sv, 0, &bad);
    ASSERT_EQ(discards, 0);
    ASSERT_EQ(s.round_number(), 1);
    ASSERT_EQ(s.rack().size(), std::size_t{5});
    ASSERT_EQ(std::string(s.save_mode()), std::string("CLASSIC_UNTIMED"));
}

TEST(end_game_play_again_starts_round_one) {
    Fixture f(false, false);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    f.skip();
    f.skip();
    s.press("BUTTON_GiveUp");
    s.dialog_press("BUTTON_YES");
    s.update(REVEAL_MS + 1);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    ASSERT_EQ(s.dialog().label("BODY_TEXT")->line_height, 38);
    s.dialog_press("BUTTON_REPLAY");
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.round_number(), 1);
    ASSERT_EQ(s.score(), 0);
    ASSERT_EQ(s.misses(), 0);
    ASSERT_EQ(f.exits, 0);
}

TEST(dialog_buttons_respond_to_the_mouse) {
    Fixture f(false);
    auto& s = *f.scene;
    s.press("BUTTON_Menu");
    const Dialog& d = s.dialog();
    const MenuButton* resume = d.button("BUTTON_RESUME");
    // Anchor top: the button's centre column is x = 163 in the dialog.
    const float x = d.left() + 163, y = d.top() + 290 + 20;
    s.mouse_move(x, y);
    ASSERT_TRUE(resume->hover);
    ASSERT_TRUE(s.pointer_over_clickable());
    s.mouse_down(x, y);
    s.update(0);
    ASSERT_EQ(resume->sprite.frame, 2);
    s.mouse_up(x, y);
    ASSERT_TRUE(!s.dialog_open());
    s.mouse_move(1, 1);
    ASSERT_TRUE(!s.pointer_over_clickable());
}

TEST(round_end_dialogs_carry_the_sparkle) {
    const Dialog n = build_next_round();
    ASSERT_EQ(n.w, 380);
    ASSERT_EQ(n.h, 320);
    ASSERT_EQ(n.sprites.size(), std::size_t{1});
    ASSERT_TRUE(n.sprites[0].additive);
    ASSERT_EQ(n.sprites[0].tint, 0xFFA800u);
    ASSERT_EQ(n.sprites[0].frame_time_ms, 40);
    const Dialog c = build_congrats();
    ASSERT_EQ(c.sprites[0].frame, 4);
    const Dialog e = build_end_game();
    ASSERT_EQ(e.h, 260);
    ASSERT_EQ(e.button("BUTTON_REPLAY")->sprite.sheet.frame_h, 52);
    ASSERT_EQ(text_line_count("a\nb\nc"), 3);
}

// A centred label centres its whole block: the tutorial's 4 lines sit
// at 107..203, above READ MORE / PLAY (y 235), not from 155 down to 251.
TEST(tutorial_body_is_centred_as_a_block_above_its_buttons) {
    for (int mode = 0; mode < 4; ++mode) {
        const Dialog d = build_tutorial(mode);
        const TextLabel& body = *d.label("BODY_TEXT");
        const int lines = text_line_count(body.text);
        const float first = first_line_y(body, lines, body.line_height);
        const float last = first + static_cast<float>((lines - 1) * body.line_height);
        ASSERT_TRUE((first + last) / 2.0f == body.y);                       // centred on y 155
        ASSERT_TRUE(last + body.line_height / 2.0f <= d.button("BUTTON_OK")->sprite.y);
        ASSERT_TRUE(first - body.line_height / 2.0f >= 52.0f);              // under the header
    }
    const Dialog first_tutorial = build_tutorial(0);   // keep it alive: label() points into it
    const TextLabel& four = *first_tutorial.label("BODY_TEXT");
    ASSERT_TRUE(first_line_y(four, 4, 32) == 107.0f);
}

TEST(first_line_y_moves_only_centred_multi_line_labels) {
    TextLabel l;
    l.y = 100;
    l.anchor = Anchor::Center;
    ASSERT_TRUE(first_line_y(l, 1, 32) == 100.0f);                          // one line: unchanged
    ASSERT_TRUE(first_line_y(l, 3, 20) == 80.0f);
    l.anchor = Anchor::Top;
    ASSERT_TRUE(first_line_y(l, 3, 20) == 100.0f);                          // a top anchor starts at y
}

TEST(first_game_in_a_mode_opens_the_tutorial_once) {
    Fixture f(true, true, 0, false);
    auto& s = *f.scene;
    // 0x425c50: the TRACKER flag for Timed was clear, so it is set and the
    // scene opens DIALOG_TUTORIAL; the other modes keep theirs.
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Tutorial);
    ASSERT_TRUE(f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Timed)]);
    ASSERT_TRUE(!f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Untimed)]);
    ASSERT_TRUE(f.saves > 0);                              // the flag is saved
    const Dialog& d = s.dialog();
    ASSERT_EQ(d.w, 444);
    ASSERT_EQ(d.h, 320);
    ASSERT_EQ(d.label("HEADER")->text, std::string("Timed Mode"));   // TEXT_TUTORIAL_TITLE_0
    ASSERT_EQ(d.label("BODY_TEXT")->font_id, std::string("FONT_TUTORIAL"));
    ASSERT_EQ(d.button("BUTTON_HELP")->sprite.sheet.image_id, std::string("IMAGE_BUTTON_READMORE"));
    ASSERT_EQ(d.button("BUTTON_OK")->sprite.sheet.image_id, std::string("IMAGE_BUTTON_PLAY"));
    // The game waits: no clock, no typing.
    ASSERT_TRUE(s.paused());
    s.update(30000);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS);
    s.key_return();                                        // PLAY
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(!s.paused());
    s.update(1000);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS - 1000);
}

TEST(tutorial_uses_the_untimed_text_in_untimed) {
    Fixture f(false, true, 0, false);
    ASSERT_TRUE(f.scene->dialog_kind() == GameDialog::Tutorial);
    ASSERT_EQ(f.scene->dialog().label("HEADER")->text, std::string("Untimed Mode"));   // TEXT_TUTORIAL_TITLE_1
    ASSERT_EQ(f.scene->dialog().label("BODY_TEXT")->text,
              std::string(game_strings::TUTORIAL_BODIES[1]));
}

TEST(tutorial_read_more_opens_help_and_returns) {
    Fixture f(true, true, 0, false);
    auto& s = *f.scene;
    s.dialog_press("BUTTON_HELP");                         // READ MORE 0x40e1f0
    ASSERT_TRUE(s.dialog_kind() == GameDialog::HowToPlay);
    ASSERT_TRUE(s.under_kind() == GameDialog::Tutorial);   // drawn under the help
    s.dialog_press("BUTTON_OK");                           // the tutorial is still under it
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Tutorial);
    s.key_escape();                                        // Esc does not skip it
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Tutorial);
    s.dialog_press("BUTTON_OK");                           // PLAY
    ASSERT_TRUE(!s.dialog_open());
    // The pause menu's help still goes back to the pause menu.
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_HOWTOPLAY");
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
}

TEST(resumed_game_never_shows_the_tutorial) {
    Fixture f(false);
    tt2::SavedGame g = f.scene->snapshot();
    g.round = 3;
    f.profile.mode_flags.fill(false);
    GameScene::Services sv;
    GameScene s(f.com, f.ful, f.profile, nullptr, tt2::GameMode::Untimed, sv, 0, &g);
    ASSERT_TRUE(!s.dialog_open());                         // 0x42a460 skips 0x425c50
    ASSERT_TRUE(!f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Untimed)]);
}

// 0x4333a0: a guess takes the first empty slot of its length, so a later
// board word found early sits in an earlier slot. The reveal then fills the
// empty slots with the words still missing, never a found word again.
TEST(reveal_never_shows_a_found_word_twice) {
    Fixture f(false);
    auto& s = *f.scene;
    std::vector<std::string> board;
    for (const auto& v : s.slots()) board.push_back(v.word);
    // The last word of the first length with two or more slots.
    std::string late;
    for (std::size_t i = 1; i < board.size(); ++i)
        if (board[i].size() == board[i - 1].size() && board[i] != s.rack()) late = board[i];
    ASSERT_TRUE(!late.empty());
    f.type(late);
    s.key_return();
    f.type(s.rack());                                      // the bingo qualifies
    s.key_return();
    s.press("BUTTON_NextRound");
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    std::vector<std::string> shown;
    for (const auto& v : s.slots()) shown.push_back(v.word);
    std::vector<std::string> sorted_shown = shown, sorted_board = board;
    std::sort(sorted_shown.begin(), sorted_shown.end());
    std::sort(sorted_board.begin(), sorted_board.end());
    ASSERT_TRUE(std::adjacent_find(sorted_shown.begin(), sorted_shown.end()) == sorted_shown.end());
    ASSERT_TRUE(sorted_shown == sorted_board);            // every answer exactly once
    for (std::size_t i = 0; i < shown.size(); ++i)
        ASSERT_EQ(shown[i].size(), board[i].size());       // each slot keeps its length
}

// ---- trophies (0x424730, 0x425240, 0x426860, 0x429510) ----------------

namespace {
// Enter `w` and let the letters fly home so the rack can twist again.
void enter_word(Fixture& f, const std::string& w) {
    f.type(w);
    f.scene->key_return();
    f.scene->update(BALL_MOVE_MS);
}
std::vector<std::string> bingo_words(const Fixture& f) {
    std::vector<std::string> out;
    for (const auto& s : f.scene->slots()) if (s.word.size() == f.scene->rack().size()) out.push_back(s.word);
    return out;
}
std::vector<std::string> short_words(const Fixture& f) {
    std::vector<std::string> out;
    for (const auto& s : f.scene->slots()) if (s.word.size() < f.scene->rack().size()) out.push_back(s.word);
    return out;
}
}  // namespace

TEST(trophy_five_words_without_a_twist_win_quick_draw) {
    Fixture f(false);
    const auto words = short_words(f);
    ASSERT_TRUE(words.size() >= 5);
    for (int i = 0; i < 5; ++i) enter_word(f, words[static_cast<std::size_t>(i)]);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_QUICK_DRAW], tt2::TROPHY_NEW);
    // The word popups have their own queue; the trophy starts at once.
    f.scene->update(0);
    ASSERT_EQ(f.scene->trophy_popup_id(), static_cast<int>(tt2::TROPHY_QUICK_DRAW));
    ASSERT_TRUE(f.played(SoundId::TrophyWon));
    const SpriteState& art = f.scene->trophy_popup();
    ASSERT_EQ(art.sheet.image_id, std::string("IMAGE_TROPHY1"));
    ASSERT_TRUE(std::fabs(art.x - 725.0f) < 0.01f);
    ASSERT_TRUE(std::fabs(art.y - 475.0f) < 0.01f);
    ASSERT_EQ(f.scene->trophy_label().name, std::string("Quick\nDraw"));
    ASSERT_TRUE(std::fabs(f.scene->trophy_label().y - 535.0f) < 0.01f);
    // 200 + 500 + 300 ms in: at rest, 0.5 and 1.0.
    f.scene->update(1000);
    ASSERT_TRUE(std::fabs(f.scene->trophy_popup().scale - 0.5f) < 0.001f);
    ASSERT_TRUE(std::fabs(f.scene->trophy_label().scale - 1.0f) < 0.001f);
    ASSERT_TRUE(std::fabs(f.scene->trophy_popup().alpha - 1.0f) < 0.001f);
    f.scene->update(2500);   // hold 2000 + fade 500 = 3500 ms in all
    f.scene->update(1);
    ASSERT_EQ(f.scene->trophy_popup_id(), -1);
    ASSERT_TRUE(!f.scene->trophy_popup().visible);
}

TEST(trophy_a_twist_breaks_the_no_twist_streak) {
    Fixture f(false);
    const auto words = short_words(f);
    for (int i = 0; i < 4; ++i) enter_word(f, words[static_cast<std::size_t>(i)]);
    f.scene->key_space();
    enter_word(f, words[4]);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_QUICK_DRAW], tt2::TROPHY_NOT_WON);
}

TEST(trophy_bingo_streak_counts_bingo_words_in_a_row) {
    Fixture f(false);
    const auto bingos = bingo_words(f);
    ASSERT_TRUE(bingos.size() >= 5);
    for (int i = 0; i < 5; ++i) enter_word(f, bingos[static_cast<std::size_t>(i)]);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_BINGO_BLASTER], tt2::TROPHY_NEW);
}

TEST(trophy_a_short_word_resets_the_bingo_streak) {
    Fixture f(false);
    const auto bingos = bingo_words(f);
    for (int i = 0; i < 4; ++i) enter_word(f, bingos[static_cast<std::size_t>(i)]);
    enter_word(f, short_words(f)[0]);
    enter_word(f, bingos[4]);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_BINGO_BLASTER], tt2::TROPHY_NOT_WON);
}

TEST(trophy_twist_and_word_counters_award_at_their_thresholds) {
    Fixture f(false);
    f.profile.twists = 999;
    f.profile.solved = 999;
    f.scene->key_space();
    ASSERT_EQ(f.profile.twists, 1000);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_WITH_A_TWIST], tt2::TROPHY_NEW);
    f.scene->update(BALL_MOVE_MS);
    enter_word(f, short_words(f)[0]);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_WORD_NERD], tt2::TROPHY_NEW);
    ASSERT_EQ(f.scene->trophies_waiting() + (f.scene->trophy_popup_id() >= 0 ? 1u : 0u), std::size_t{2});
}

TEST(trophy_every_word_fast_and_untwisted_wins_look_ma_and_speed_speller) {
    Fixture f(false);
    std::vector<std::string> all;
    for (const auto& s : f.scene->slots()) all.push_back(s.word);
    for (const auto& w : all) { f.type(w); f.scene->key_return(); }
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_LOOK_MA], tt2::TROPHY_NEW);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NEW);
}

TEST(trophy_a_slow_twisted_round_wins_neither) {
    Fixture f(false);
    f.scene->update(6000);
    f.scene->key_space();
    f.scene->update(BALL_MOVE_MS);
    std::vector<std::string> all;
    for (const auto& s : f.scene->slots()) all.push_back(s.word);
    for (const auto& w : all) { f.type(w); f.scene->key_return(); }
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_LOOK_MA], tt2::TROPHY_NOT_WON);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NOT_WON);
}

TEST(trophy_popup_waits_while_a_dialog_is_open) {
    Fixture f(false);
    f.profile.twists = 999;
    f.scene->press("BUTTON_Menu");
    f.scene->update(0);
    f.scene->dialog_press("BUTTON_RESUME");
    f.scene->update(BALL_MOVE_MS);
    f.drain_popups();
    f.scene->key_space();
    f.scene->press("BUTTON_Menu");
    f.scene->update(100);
    ASSERT_EQ(f.scene->trophy_popup_id(), -1);
    ASSERT_EQ(f.scene->trophies_waiting(), std::size_t{1});
    f.scene->dialog_press("BUTTON_RESUME");
    f.scene->update(400);   // the pause menu fades out first
    f.scene->update(0);
    ASSERT_EQ(f.scene->trophy_popup_id(), static_cast<int>(tt2::TROPHY_WITH_A_TWIST));
}

// Android: going to the background pauses the round and saves it at once,
// since the app may be killed there (T2.2, docs/plans/android-support.md).
TEST(background_pauses_a_timed_round_and_saves_it_now) {
    Fixture f(true);
    auto& s = *f.scene;
    s.update(5000);
    s.enter_background();
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    ASSERT_EQ(f.saved[0].time_ms, ROUND_MS - 5000);
    ASSERT_EQ(f.saved[0].word, s.rack());
    s.update(60000);                                       // minutes in the background
    ASSERT_EQ(s.time_left_ms(), ROUND_MS - 5000);          // the clock never moved
    s.dialog_press("BUTTON_RESUME");                       // back, and a tap goes on
    s.update(1000);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS - 6000);
}

TEST(background_after_the_round_is_over_saves_nothing) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    s.press("BUTTON_NextRound");                           // round over, revealing
    s.enter_background();
    ASSERT_TRUE(f.saved.empty());
}

// Rounds read letters from key presses, never SDL text input, so no
// on-screen keyboard comes up (Android, Steam Deck).
TEST(letter_for_key_takes_letter_keys_only) {
    ASSERT_EQ(letter_for_key('a', false), 'a');                // SDLK_A
    ASSERT_EQ(letter_for_key('z', false), 'z');                // SDLK_Z
    ASSERT_EQ(letter_for_key('a', true), '\0');                // Ctrl+A and friends type nothing
    ASSERT_EQ(letter_for_key('0', false), '\0');
    ASSERT_EQ(letter_for_key(' ', false), '\0');               // Space is TWIST, handled apart
    ASSERT_EQ(letter_for_key(0x1Bu, false), '\0');             // SDLK_ESCAPE
    ASSERT_EQ(letter_for_key(0x4000011Au, false), '\0');       // SDLK_AC_BACK (Android Back)
    ASSERT_EQ(letter_for_key('A', false), '\0');               // SDL never sends upper case keycodes
}

TEST(key_letters_place_balls_like_typed_text) {
    Fixture f(false);
    auto& s = *f.scene;
    const char first = s.rack()[0];
    s.type_letter(letter_for_key(static_cast<std::uint32_t>(first), false));
    ASSERT_EQ(s.typed(), std::string(1, first));
}

// ---- Word of the Day (game-scene.md §8; design wotd-crossword.md) -----

namespace {

constexpr tt2::GameMode WOTD = tt2::GameMode::WordOfTheDay;

// Manila (UTC+8, no DST; `timezone` = -28800 s = -480 min west). 07:30
// local on 2026-10-23 is 23:30 UTC on 2026-10-22 (unix day 20748):
// Original rule -> word day 6623 (UTC), scene day 6624 (local standard),
// date 2026-10-22; Local rule -> 6624 / 6624 / 2026-10-23.
constexpr tt2::WallClock MANILA_1023_0730{1'792'711'800'000LL, -480, 480};
// 08:30 local on 2026-10-23 (00:30 UTC): word day 6624, scene day 6624.
constexpr tt2::WallClock MANILA_1023_0830{1'792'715'400'000LL, -480, 480};
// Both sides of local midnight: 23:50 on 10-22 (15:50 UTC) and 00:10 on
// 10-23 (16:10 UTC). Word day 6623 for both; scene day 6623, then 6624.
constexpr tt2::WallClock MANILA_1022_2350{1'792'684'200'000LL, -480, 480};
constexpr tt2::WallClock MANILA_1023_0010{1'792'685'400'000LL, -480, 480};
// Berlin in July (CET -60 min west, CEST +120 min): 00:30 CEST on
// 2026-07-15 is 22:30 UTC on 07-14. Original scene day 6523 (still
// 23:30 standard time on 07-14), Local 6524.
constexpr tt2::WallClock BERLIN_0715_0030{1'784'068'200'000LL, -60, 120};

// The fixture dictionary's pool (crate, caret, trace, react, carte, cater)
// shuffled with seed 1337: day 6622 -> crate, 6623 -> carte, 6624 -> cater.
// The WOTD board keeps the first 5 per length in DFS order (0x4262b0).
const std::vector<std::string> CARTE_BOARD = {
    "car", "cat", "act", "ace", "arc",
    "care", "acre", "race", "rate", "tare",
    "carte", "caret", "cater", "crate", "react",
};

void bingo(Fixture& f) {
    f.type(f.scene->rack());
    f.scene->key_return();
}

// Every board word, typed in board order.
void find_all(Fixture& f) {
    std::vector<std::string> words;
    for (const auto& v : f.scene->slots()) words.push_back(v.word);
    for (const auto& w : words) {
        f.type(w);
        f.scene->key_return();
    }
}

std::vector<std::string> board_of(const GameScene& s) {
    std::vector<std::string> out;
    for (const auto& v : s.slots()) out.push_back(v.word);
    return out;
}

}  // namespace

// The timings the WOTD flow uses (game-scene.md §8.5).
TEST(wotd_timing_constants_match_the_exe) {
    ASSERT_EQ(ROUND_MS, 120000);                  // 0x425c50: +0x2fc
    ASSERT_EQ(REVEAL_MS, 3000);                   // 0x428840(3000), 0x428c80(.., 1)
    ASSERT_EQ(ALL_WORDS_REVEAL_MS, 1000);         // 0x424730 -> 0x428840(1000), every mode
    ASSERT_EQ(WOTD_GAME_OVER_DELAY_MS, 3000);     // state 3 -> 0x428c80(.., 0)
    ASSERT_EQ(WOTD_BOARD_PER_LENGTH, std::size_t{5});   // 0x4262b0
}

// AC-W1: the rack is the day's word, the scene is timed from 2:00 on the
// WOTD background, and the board is capped at 5 per length.
TEST(wotd_starts_with_the_word_of_the_day) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    ASSERT_TRUE(s.mode() == WOTD);
    ASSERT_TRUE(s.wotd());
    ASSERT_TRUE(s.timed());
    ASSERT_EQ(s.rack(), std::string("carte"));
    ASSERT_EQ(s.rack(), tt2::Bag::word_of_the_day_for_day(f.com, 6623));
    ASSERT_EQ(std::string(s.background_id()), std::string("gamescene/BACKGROUND_WOTD"));
    ASSERT_TRUE(board_of(s) == CARTE_BOARD);
    ASSERT_EQ(s.scene_day(), 6624);                        // local standard day, not the word day
    ASSERT_EQ(s.time_left_ms(), 120000);                   // 2:00 (0x425c50: +0x2fc)
    s.update(1000);
    ASSERT_EQ(s.time_left_ms(), 119000);                   // 0x426d67: WOTD ticks like Timed
    ASSERT_EQ(std::string(s.save_mode()), std::string("WORD_OF_THE_DAY"));
}

// AC-W1: MAX WORD LENGTH does not apply (0x426008-0x426104 skipped); a
// player set to 6 still gets the day's 8-letter word.
TEST(wotd_ignores_max_word_length) {
    tt2::WordTree tri = tt2::WordTree::from_words({
        "triangle", "integral", "relating", "altering",
        "ant", "art", "ate", "ear", "eat", "era", "gel", "get", "gin", "lag", "lit", "net", "nil", "rag",
        "ran", "rat", "tag", "tan", "tar", "tea", "ten", "tie", "tin",
        "lane", "late", "rate", "rain", "tail", "tile", "gate", "grin", "ring", "line",
    });
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
    p.mode_flags.fill(true);
    p.max_word_length = 6;
    GameScene::Services sv;
    sv.now = [] { return MANILA_1023_0730; };
    GameScene s(tri, tri, p, nullptr, WOTD, sv, 0);
    ASSERT_EQ(s.rack(), std::string("triangle"));
    // 21 three-letter sub-anagrams, 5 kept; no 5-7 letter words.
    const std::vector<std::string> board = {"tin", "tie", "tar", "tan", "tag", "tile", "tail", "ring", "rate",
                                            "rain", "triangle", "relating", "integral", "altering"};
    ASSERT_TRUE(board_of(s) == board);
}

// D-1: the scene plays Timed, Untimed and WOTD; the other modes throw.
TEST(game_scene_rejects_lightning_and_crossword) {
    for (tt2::GameMode m : {tt2::GameMode::Blitz, tt2::GameMode::Crossword}) {
        tt2::WordTree com = common();
        tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
        bool invalid = false;
        try {
            GameScene s(com, com, p, nullptr, m, GameScene::Services{}, 0);
        } catch (const std::invalid_argument&) {
            invalid = true;
        }
        ASSERT_TRUE(invalid);
    }
    Fixture timed(tt2::GameMode::Timed);
    Fixture untimed(tt2::GameMode::Untimed);
    ASSERT_TRUE(timed.scene->mode() == tt2::GameMode::Timed);
    ASSERT_TRUE(untimed.scene->mode() == tt2::GameMode::Untimed);
    ASSERT_TRUE(!untimed.scene->timed());
    ASSERT_EQ(untimed.scene->scene_day(), -1);             // only WOTD keeps a day
    ASSERT_EQ(untimed.scene->snapshot().day, -1);
}

// Error handling: a common list with no 5-8 letter word has no WOTD rack.
TEST(wotd_start_throws_no_rack_on_an_empty_pool) {
    tt2::WordTree small = tt2::WordTree::from_words({"act", "arc", "art", "car", "cat", "rat", "tar", "cart"});
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
    p.mode_flags.fill(true);
    bool no_rack = false;
    try {
        GameScene s(small, small, p, nullptr, WOTD, GameScene::Services{}, 0);
    } catch (const tt2::NoRackError&) {
        no_rack = true;
    }
    ASSERT_TRUE(no_rack);
}

// AC-W5 (§8.4): the spot at (317, 473) shows GIVE UP until the first
// bingo, then DONE; PASS, PASSES: 2 and NEXT ROUND never show.
TEST(wotd_spot_shows_give_up_then_done) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    auto shown = [&](const char* n) { return s.button(n)->sprite.visible; };
    ASSERT_TRUE(shown("BUTTON_GiveUp"));
    ASSERT_TRUE(!shown("BUTTON_WOTD_DONE") && !shown("BUTTON_NextRound"));
    ASSERT_TRUE(!shown("BUTTON_Pass") && !shown("BUTTON_Pass_2"));
    const MenuButton& done = *s.button("BUTTON_WOTD_DONE");
    ASSERT_EQ(done.sprite.sheet.image_id, std::string("BUTTON_Done"));
    ASSERT_TRUE(done.sprite.x == 317.0f && done.sprite.y == 473.0f);
    f.type("car");
    s.key_return();
    ASSERT_TRUE(shown("BUTTON_GiveUp"));                   // a plain word changes nothing
    bingo(f);
    ASSERT_TRUE(shown("BUTTON_WOTD_DONE"));
    ASSERT_TRUE(!shown("BUTTON_GiveUp") && !shown("BUTTON_NextRound"));
    ASSERT_TRUE(!shown("BUTTON_Pass") && !shown("BUTTON_Pass_2"));
    // A click on it runs its command, BUTTON_NextRound (0x425b60).
    s.mouse_move(327, 483);
    s.mouse_down(327, 483);
    s.mouse_up(327, 483);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::NextRound);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    // 0x428840 disables +0x224 and 0x425710 (+0x2f9 set) only disables:
    // DONE stays on screen, greyed (wotd-gaps.md #1).
    ASSERT_TRUE(shown("BUTTON_WOTD_DONE") && !s.button("BUTTON_WOTD_DONE")->enabled);
    ASSERT_TRUE(!shown("BUTTON_GiveUp"));
}

// Negative: Classic never shows DONE; a qualified round shows NEXT ROUND.
TEST(classic_never_shows_wotd_done) {
    Fixture f(true);
    auto& s = *f.scene;
    ASSERT_TRUE(!s.button("BUTTON_WOTD_DONE")->sprite.visible);
    ASSERT_TRUE(s.button("BUTTON_Pass_2")->sprite.visible);
    bingo(f);
    ASSERT_TRUE(s.button("BUTTON_NextRound")->sprite.visible);
    ASSERT_TRUE(!s.button("BUTTON_WOTD_DONE")->sprite.visible);
}

// AC-W5 / AC-W10: DONE reveals for 3000 ms, then state 3 counts the round,
// deletes the save and writes WOD; 3000 ms later the game is over. No
// second round; the qualified, non-ranking card returns to the menu.
TEST(wotd_done_ends_the_game_after_one_round) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    ASSERT_EQ(s.score(), 1250);                            // 10 * 5^2 + 1000
    s.press("BUTTON_WOTD_DONE");
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));
    s.update(2999);                                        // 0x428840(3000)
    ASSERT_EQ(f.profile.rounds_completed[4], 0);           // not before state 3
    ASSERT_EQ(f.profile.wod_day, -1);
    ASSERT_EQ(f.discards, 0);
    s.update(10);
    ASSERT_EQ(f.profile.rounds_completed[4], 1);           // mov eax, 4 @0x426b74
    ASSERT_EQ(f.profile.wod_day, 6624);                    // the scene day, not word day 6623
    ASSERT_EQ(f.discards, 1);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);           // 0x428c80(.., 0) still waits
    s.update(2900);                                        // 2910 of its 3000 ms
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(200);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_EQ(s.dialog().label("BODY_TEXT")->text, std::string("Click to return\nto the main menu!"));
    ASSERT_TRUE(f.played(SoundId::NextRoundAchieved));
    ASSERT_EQ(s.round_number(), 1);
    ASSERT_EQ(s.rack(), std::string("carte"));
    s.dialog_press("BUTTON_OK");                           // 0x409a70 -> +0xdc = 0x427b20
    ASSERT_EQ(f.exits, 1);
    ASSERT_TRUE(f.saved.empty());                          // no save once the game is over
    ASSERT_EQ(s.round_number(), 1);                        // never a second round
    ASSERT_TRUE(!tt2::wotd_button_enabled(f.profile, MANILA_1023_0730));   // the gate is closed today
    ASSERT_TRUE(tt2::wotd_button_enabled(f.profile, {MANILA_1023_0730.unix_ms + 86'400'000LL, -480, 480}));
}

// AC-W5: every slot found queues the ALL WORDS banner, reveals for only
// 1000 ms (0x428840(1000)) and reaches state 3.
TEST(wotd_all_words_reveal_for_one_second) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    find_all(f);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::AllWords);
    ASSERT_TRUE(f.played(SoundId::AllWords));
    s.update(999);                                         // 0x428840(1000)
    ASSERT_EQ(f.profile.wod_day, -1);
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
    s.update(10);
    ASSERT_EQ(f.profile.wod_day, 6624);
    ASSERT_EQ(f.profile.rounds_completed[4], 1);
    s.update(2900);                                        // 2909 of the 3000 ms after state 3
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(200);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
}

// AC-W8: when the first bingo is also the guess that fills every slot, it
// queues popup 1 (ALL WORDS) and not popup 11. The pool is {cabin} on any
// day, and the board is its five 3-letter words plus "cabin".
TEST(wotd_bingo_that_fills_the_board_queues_all_words_not_popup_11) {
    tt2::WordTree cab = tt2::WordTree::from_words({"cabin", "ban", "bin", "cab", "can", "nab"});
    tt2::PlayerProfile p = tt2::PlayerProfile::create("Mugwump", 7u);
    p.mode_flags.fill(true);
    std::vector<SoundId> sounds;
    GameScene::Services sv;
    sv.now = [] { return MANILA_1023_0730; };
    sv.play_sound = [&sounds](SoundId id) { sounds.push_back(id); };
    GameScene s(cab, cab, p, nullptr, WOTD, sv, 0);
    ASSERT_EQ(s.rack(), std::string("cabin"));
    const std::vector<std::string> board = {"cab", "can", "ban", "bin", "nab", "cabin"};   // DFS on the rack
    ASSERT_TRUE(board_of(s) == board);
    for (const char* w : {"ban", "bin", "cab", "can", "nab"}) {
        for (const char* c = w; *c; ++c) s.type_letter(*c);
        s.key_return();
    }
    ASSERT_TRUE(!s.qualified());
    s.update(0);
    ASSERT_TRUE(s.popup_kind() == PopupKind::Definitions);    // popup 10 after the first plain word
    for (int i = 0; i < 4 && (s.popup_kind() || s.popups_waiting() > 0); ++i) s.update(3000);
    ASSERT_TRUE(!s.popup_kind());
    for (char c : std::string("cabin")) s.type_letter(c);
    s.key_return();
    ASSERT_TRUE(s.last_round_end() == RoundEnd::AllWords);
    ASSERT_EQ(s.popups_waiting(), std::size_t{1});            // one popup, not 1 and 11
    s.update(0);
    ASSERT_TRUE(s.popup_kind() == PopupKind::AllWords);
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});
    // 5 * 10*3^2 + (10*5^2 + 1000 bingo + 100 * 6 slots) = 450 + 1850.
    ASSERT_EQ(s.score(), 2300);
    ASSERT_TRUE(std::find(sounds.begin(), sounds.end(), SoundId::AllWords) != sounds.end());
}

// AC-W5: time up after a bingo ends the round (3000 ms reveal), then the game.
TEST(wotd_time_up_after_a_bingo_counts_the_round) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    s.update(120000);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::TimeUpQualified);
    ASSERT_TRUE(f.played(SoundId::ClockTimeUp));
    ASSERT_EQ(f.profile.wod_day, -1);
    s.update(2999);                                        // 0x428840(3000)
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
    ASSERT_EQ(f.profile.wod_day, -1);
    s.update(11);
    ASSERT_EQ(f.profile.rounds_completed[4], 1);
    ASSERT_EQ(f.profile.wod_day, 6624);
    s.update(2900);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(200);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
}

// AC-W5 / AC-W10: time up without a bingo deletes the save and writes WOD
// at once, reveals for 3000 ms and ends the game: no miss, no count, and
// the end card has no REPLAY.
TEST(wotd_time_up_without_a_bingo_ends_the_game) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    s.update(120000);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::TimeUpMissed);
    ASSERT_EQ(f.profile.wod_day, 6624);                    // 0x427cc0 before the reveal
    ASSERT_EQ(f.discards, 1);
    ASSERT_EQ(s.misses(), 0);                              // 0x425420 is never called
    ASSERT_TRUE(!f.played(SoundId::LoseLife));
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));
    s.update(2999);                                        // 0x428c80(.., 1): reveal, 3000 ms
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(11);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    ASSERT_EQ(s.dialog().label("BODY_TEXT")->text, std::string("Sorry, you didn't find the\nWord of The Day."));
    ASSERT_TRUE(!s.dialog().button("BUTTON_REPLAY")->sprite.visible);
    ASSERT_TRUE(s.dialog().button("BUTTON_OK")->sprite.x == 192.0f);
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
    s.dialog_press("BUTTON_REPLAY");                       // hidden and disabled: nothing
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    ASSERT_EQ(f.exits, 0);
    s.key_return();                                        // Return = MENU
    ASSERT_EQ(f.exits, 1);
}

// AC-W5 / AC-W10: GIVE UP always asks "Give Up?" (no lives, never PASS),
// and YES ends the game at once without counting the round.
TEST(wotd_give_up_always_asks_give_up_and_ends_the_game) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    ASSERT_EQ(s.misses(), 0);
    s.press("BUTTON_GiveUp");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::ConfirmGiveUp);
    ASSERT_EQ(s.dialog().label("HEADER")->text, std::string("Give Up?"));
    s.dialog_press("BUTTON_NO");
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(f.profile.wod_day, -1);
    s.press("BUTTON_GiveUp");
    s.dialog_press("BUTTON_YES");                          // 0x428610 -> 0x4253a0
    ASSERT_TRUE(s.last_round_end() == RoundEnd::Passed);
    ASSERT_EQ(f.profile.wod_day, 6624);
    ASSERT_EQ(f.discards, 1);
    ASSERT_EQ(s.misses(), 0);
    ASSERT_TRUE(!f.played(SoundId::LoseLife));
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);           // 0x428c80(.., 1) reveals
    ASSERT_TRUE(f.played(SoundId::WordPassReveal));
    s.update(2999);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(11);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);   // never DIALOG_PASSROUND
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
}

// AC-W5: PASS and PASSES: 2 are hidden in WOTD (0x425710), and pressing
// them by name does nothing.
TEST(wotd_pass_buttons_do_nothing) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    s.press("BUTTON_Pass_2");
    s.press("BUTTON_Pass");
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.misses(), 0);
}

// AC-W5 / AC-W6: a ranking score opens DIALOG_CONGRATS and stores the UTC
// date at game over (07:30 on 10-23 in Manila -> 2026-10-22).
TEST(wotd_high_score_stores_the_utc_date) {
    Fixture f(WOTD, true, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    s.press("BUTTON_WOTD_DONE");
    s.update(3010);
    s.update(3010);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Congrats);
    ASSERT_TRUE(f.played(SoundId::HighScoreAchieved));
    const auto& table = f.hs.table(WOTD).entries;
    int mine = 0;
    for (const auto& e : table)
        if (e.name == "Mugwump") {
            ++mine;
            ASSERT_EQ(e.score, 1250);
            ASSERT_EQ(e.extra, std::string("2026-10-22"));
        }
    ASSERT_EQ(mine, 1);
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::HighScores);
    ASSERT_TRUE(s.highscore_dialog()->mode() == WOTD);
}

// AC-W6: the date is read at game over, not at round start.
TEST(wotd_high_score_date_is_taken_at_game_over) {
    Fixture f(WOTD, true, 0, true, MANILA_1022_2350);      // 15:50 UTC on 10-22
    auto& s = *f.scene;
    bingo(f);
    f.clock = MANILA_1023_0830;                            // 00:30 UTC on 10-23
    s.press("BUTTON_WOTD_DONE");
    s.update(3010);
    s.update(3010);
    std::string extra;
    for (const auto& e : f.hs.table(WOTD).entries) if (e.name == "Mugwump") extra = e.extra;
    ASSERT_EQ(extra, std::string("2026-10-23"));
    ASSERT_EQ(f.profile.wod_day, 6623);                    // WOD keeps the start day (§8.6)
}

// AC-W6 negatives: a score of 0 is not submitted; a non-ranking score adds
// no row and the qualified game gets the "back to the menu" card.
TEST(wotd_high_score_negatives) {
    Fixture zero(WOTD, true, 0, true, MANILA_1023_0730);
    zero.scene->update(120000);
    zero.scene->update(3010);
    ASSERT_TRUE(zero.scene->dialog_kind() == GameDialog::EndGame);
    for (const auto& e : zero.hs.table(WOTD).entries) ASSERT_TRUE(e.name != "Mugwump");

    Fixture f(WOTD, true, 0, true, MANILA_1023_0730);
    for (int i = 0; i < 10; ++i) f.hs.submit(WOTD, "Pro", 50000, "2026-10-21");
    bingo(f);
    f.scene->press("BUTTON_WOTD_DONE");
    f.scene->update(3010);
    f.scene->update(3010);
    ASSERT_TRUE(f.scene->dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(!f.played(SoundId::HighScoreAchieved));
    for (const auto& e : f.hs.table(WOTD).entries) ASSERT_TRUE(e.name != "Mugwump");
}

// AC-W7 / AC-W4: leaving mid-round saves WORD_OF_THE_DAY with DAY = the
// scene day (not the word day) and writes no WOD, so the gate stays open.
TEST(wotd_leave_saves_the_scene_day_without_wod) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    f.type("car");
    s.key_return();
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_STOP");
    s.dialog_press("BUTTON_YES");
    ASSERT_EQ(f.exits, 1);
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    const tt2::SavedGame& g = f.saved[0];
    ASSERT_EQ(g.mode, std::string("WORD_OF_THE_DAY"));
    ASSERT_EQ(g.word, std::string("carte"));
    ASSERT_EQ(g.day, 6624);
    ASSERT_EQ(f.profile.wod_day, -1);
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
    ASSERT_TRUE(tt2::wotd_button_enabled(f.profile, MANILA_1023_0730));
}

// AC-W7: a same-day save resumes its word, words, time and score, even when
// the UTC word day has moved on (08:30 in Manila picks "cater").
TEST(wotd_same_day_save_resumes_without_a_new_pick) {
    Fixture f(WOTD, false, 0, false, MANILA_1023_0730);    // first WOTD game: tutorial
    auto& first = *f.scene;
    ASSERT_TRUE(first.dialog_kind() == GameDialog::Tutorial);
    first.dialog_press("BUTTON_OK");                       // PLAY
    f.type("car");
    first.key_return();
    first.update(15000);
    first.press("BUTTON_Menu");
    first.dialog_press("BUTTON_STOP");
    first.dialog_press("BUTTON_YES");                      // 0x427b20: save and leave
    ASSERT_EQ(f.saved.size(), std::size_t{1});
    f.clock = MANILA_1023_0830;
    ASSERT_EQ(tt2::Bag::word_of_the_day_for_day(f.com, 6624), std::string("cater"));
    ASSERT_TRUE(!wotd_save_is_stale(f.saved[0], f.clock));
    GameScene s(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &f.saved[0]);
    ASSERT_TRUE(!s.dialog_open());                         // AC-W9: no tutorial on a resume
    ASSERT_EQ(s.rack(), std::string("carte"));
    ASSERT_EQ(s.scene_day(), 6624);
    ASSERT_EQ(s.score(), 90);                              // "car": 10 * 3^2
    ASSERT_EQ(s.time_left_ms(), 105000);
    ASSERT_TRUE(s.slots()[0].found);
    ASSERT_EQ(f.discards, 1);                              // 0x42a460 deletes the file
}

// AC-W7: across local midnight in UTC+8 the save is another day's: it is
// deleted and a new game starts (same UTC word, but nothing carried over).
TEST(wotd_other_day_save_starts_a_new_game) {
    Fixture f(WOTD, false, 0, true, MANILA_1022_2350);
    ASSERT_EQ(f.scene->scene_day(), 6623);
    f.type("car");
    f.scene->key_return();
    f.scene->update(15000);
    const tt2::SavedGame g = f.scene->snapshot();
    ASSERT_EQ(g.day, 6623);
    f.clock = MANILA_1023_0010;
    ASSERT_TRUE(wotd_save_is_stale(g, f.clock));
    ASSERT_TRUE(!wotd_save_is_stale(g, MANILA_1022_2350));
    GameScene s(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &g);
    ASSERT_EQ(f.discards, 1);
    ASSERT_EQ(s.scene_day(), 6624);
    ASSERT_EQ(s.rack(), std::string("carte"));             // word day 6623 until 08:00 local
    ASSERT_EQ(s.score(), 0);
    ASSERT_EQ(s.time_left_ms(), 120000);
    for (const auto& v : s.slots()) ASSERT_TRUE(!v.found);
    // The same save at 08:30 (00:30 UTC): stale too, and the new game
    // takes the new UTC word day's word.
    f.clock = MANILA_1023_0830;
    ASSERT_TRUE(wotd_save_is_stale(g, f.clock));
    GameScene later(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &g);
    ASSERT_EQ(f.discards, 2);
    ASSERT_EQ(later.rack(), std::string("cater"));
    ASSERT_EQ(later.score(), 0);
}

// DEVIATION (D6, user decision 2026-10-22): the port hides the UI bar's
// "ROUND:" caption and number in WOTD. The exe shows "ROUND: 1" there
// (0x426360 sets LABEL_ROUND with no mode check, game-scene.md §8.10);
// the round number itself is unchanged, only the label is hidden.
TEST(wotd_hides_the_round_label) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    ASSERT_TRUE(!f.scene->round_visible());
    ASSERT_EQ(f.scene->round_number(), 1);
    ASSERT_EQ(f.scene->round_text(), std::string("1"));
    ASSERT_TRUE(f.scene->timed());                         // SCORE and TIME still show
}

TEST(classic_modes_show_the_round_label) {
    ASSERT_TRUE(Fixture(tt2::GameMode::Timed).scene->round_visible());
    ASSERT_TRUE(Fixture(tt2::GameMode::Untimed).scene->round_visible());
}

// A resumed WOTD game hides it too; a resumed Classic game keeps it.
TEST(resumed_wotd_hides_the_round_label) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    f.type("car");
    f.scene->key_return();
    const tt2::SavedGame g = f.scene->snapshot();
    ASSERT_TRUE(!wotd_save_is_stale(g, f.clock));
    GameScene resumed(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &g);
    ASSERT_EQ(f.discards, 1);                              // it really resumed, no new pick
    ASSERT_EQ(resumed.score(), 90);
    ASSERT_TRUE(!resumed.round_visible());

    Fixture c(tt2::GameMode::Untimed);
    tt2::SavedGame classic = c.scene->snapshot();
    classic.round = 3;
    GameScene untimed(c.com, c.ful, c.profile, nullptr, tt2::GameMode::Untimed, c.services(), 0, &classic);
    ASSERT_EQ(untimed.round_number(), 3);
    ASSERT_TRUE(untimed.round_visible());
}

// AC-W8: the first bingo queues popup 11 instead of the BINGO banner, with
// the same +1000 and sound; a repeat bingo queues neither. Timed keeps 2.
TEST(wotd_first_bingo_queues_popup_11) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    ASSERT_EQ(s.score(), 1250);
    ASSERT_TRUE(f.played(SoundId::WordBingo));
    ASSERT_TRUE(f.shown() == PopupKind::WordOfTheDay);
    ASSERT_EQ(s.popup().sheet.image_id, std::string("IMAGE_IMAGE_POPUP_11"));
    ASSERT_EQ(s.popups_waiting(), std::size_t{0});         // 11 replaces 2, not both
    f.drain_popups();
    f.type("crate");                                       // a second bingo
    s.key_return();
    ASSERT_EQ(s.score(), 1500);
    for (int i = 0; i < 4; ++i) {
        const auto k = f.shown();
        ASSERT_TRUE(k != PopupKind::WordOfTheDay && k != PopupKind::Bingo);
        s.update(3000);
    }
    ASSERT_TRUE(!popup_is_banner(PopupKind::WordOfTheDay));
    ASSERT_EQ(popup_duration_ms(PopupKind::WordOfTheDay), 1650);   // message timing (0x428ec0 @0x42934c)

    Fixture t(true);
    bingo(t);
    ASSERT_TRUE(t.shown() == PopupKind::Bingo);
    Fixture u(false);
    bingo(u);
    ASSERT_TRUE(u.shown() == PopupKind::Bingo);
}

// AC-W9: the first WOTD game shows the "Word of the Day" tutorial once.
TEST(wotd_tutorial_shows_once) {
    Fixture f(WOTD, false, 0, false, MANILA_1023_0730);
    ASSERT_TRUE(f.scene->dialog_kind() == GameDialog::Tutorial);
    ASSERT_EQ(f.scene->dialog().label("HEADER")->text, std::string("Word of the Day"));
    ASSERT_TRUE(f.profile.mode_flags[4]);
    ASSERT_TRUE(!f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Timed)]);
    GameScene again(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0);
    ASSERT_TRUE(!again.dialog_open());
}

// AC-W10: state 3 counts the round with count_round(.., WordOfTheDay) and
// pushes its trophy; with the four other modes played once that is Well
// Rounded. State 3 sets the block flag first, so the popup waits.
TEST(wotd_round_end_counts_the_round_and_pushes_its_trophy) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    f.profile.rounds_completed = {1, 1, 1, 1, 0};
    bingo(f);
    f.scene->press("BUTTON_WOTD_DONE");
    f.scene->update(0);                                    // Speed Speller shows during the reveal
    f.scene->update(3010);
    ASSERT_EQ(f.profile.rounds_completed[4], 1);
    ASSERT_TRUE(f.profile.has_trophy(tt2::TROPHY_WELL_ROUNDED));
    ASSERT_EQ(f.scene->trophies_waiting(), std::size_t{1});
}

// AC-W10 negatives: leaving during the reveal, before state 3, counts
// nothing, saves nothing and writes no WOD (§8.6 INFERRED edge).
TEST(wotd_leaving_before_state_3_counts_nothing) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    s.press("BUTTON_WOTD_DONE");
    s.update(1000);
    s.press("BUTTON_Menu");
    s.dialog_press("BUTTON_STOP");
    s.dialog_press("BUTTON_YES");
    ASSERT_EQ(f.exits, 1);
    s.update(6000);
    ASSERT_TRUE(f.saved.empty());
    ASSERT_EQ(f.profile.wod_day, -1);
    ASSERT_EQ(f.profile.rounds_completed[4], 0);
}

// AC-W11: the Local rule moves the word, the scene day and the date to the
// local calendar day (Manila 07:30 on 10-23 -> word day 6624, 2026-10-23).
TEST(wotd_local_rule_uses_the_local_calendar_day) {
    Fixture f(WOTD, true, 0, true, MANILA_1023_0730, tt2::WotdDayRule::Local);
    auto& s = *f.scene;
    ASSERT_EQ(s.rack(), std::string("cater"));
    ASSERT_EQ(s.scene_day(), 6624);
    bingo(f);
    s.press("BUTTON_WOTD_DONE");
    s.update(3010);
    s.update(3010);
    std::string extra;
    for (const auto& e : f.hs.table(WOTD).entries) if (e.name == "Mugwump") extra = e.extra;
    ASSERT_EQ(extra, std::string("2026-10-23"));
    ASSERT_EQ(f.profile.wod_day, 6624);
}

// AC-W11 known edge: switching the rule can turn a same-day save into
// another day's (Berlin, 00:30 CEST: Original says 07-14, Local 07-15), and
// it is then deleted on the next start.
TEST(wotd_rule_switch_turns_a_same_day_save_stale) {
    Fixture f(WOTD, false, 0, true, BERLIN_0715_0030);
    ASSERT_EQ(f.scene->scene_day(), 6523);
    f.type("car");
    f.scene->key_return();
    const tt2::SavedGame original = f.scene->snapshot();
    GameScene same(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &original);
    ASSERT_EQ(same.score(), 90);                           // same rule: resumed
    ASSERT_EQ(f.discards, 1);
    f.rule = tt2::WotdDayRule::Local;
    ASSERT_TRUE(wotd_save_is_stale(original, BERLIN_0715_0030, tt2::WotdDayRule::Local));
    GameScene local(f.com, f.ful, f.profile, nullptr, WOTD, f.services(), 0, &original);
    ASSERT_EQ(f.discards, 2);                              // deleted, new game
    ASSERT_EQ(local.score(), 0);
    ASSERT_EQ(local.scene_day(), 6524);
    // And back: a Local save is another day's under Original.
    const tt2::SavedGame from_local = local.snapshot();
    ASSERT_TRUE(wotd_save_is_stale(from_local, BERLIN_0715_0030, tt2::WotdDayRule::Original));
    ASSERT_TRUE(!wotd_save_is_stale(from_local, BERLIN_0715_0030, tt2::WotdDayRule::Local));
    // The Local game ends on the Local day.
    local.press("BUTTON_GiveUp");
    local.dialog_press("BUTTON_YES");
    ASSERT_EQ(f.profile.wod_day, 6524);
}

// The two WOTD end cards; the default cards keep their text and buttons.
TEST(wotd_end_cards_differ_from_classic) {
    const Dialog n = build_next_round(true);
    ASSERT_EQ(n.label("BODY_TEXT")->text, std::string("Click to return\nto the main menu!"));
    ASSERT_EQ(build_next_round().label("BODY_TEXT")->text, std::string("Click to continue\nto the next round!"));
    const Dialog e = build_end_game(true);
    ASSERT_EQ(e.label("BODY_TEXT")->text, std::string("Sorry, you didn't find the\nWord of The Day."));
    ASSERT_TRUE(!e.button("BUTTON_REPLAY")->sprite.visible && !e.button("BUTTON_REPLAY")->enabled);
    ASSERT_TRUE(e.button("BUTTON_OK")->sprite.x == END_GAME_WOTD_MENU_X);
    const Dialog c = build_end_game();
    ASSERT_EQ(c.label("BODY_TEXT")->text, std::string("Sorry, you didn't qualify\nfor the next round."));
    ASSERT_TRUE(c.button("BUTTON_REPLAY")->sprite.visible && c.button("BUTTON_REPLAY")->enabled);
    ASSERT_TRUE(c.button("BUTTON_OK")->sprite.x == 109.0f);
}

// ---- Main menu: start errors and the stale-day check (T2.4) ------------

// Error handling (design wotd-crossword.md): a scene that cannot start shows
// one generic, port-only message with a single OK; stderr has the cause.
TEST(start_failed_message_is_generic_and_ok_only) {
    ASSERT_EQ(std::string(game_strings::START_FAILED_HEADER), std::string("Sorry"));
    ASSERT_EQ(std::string(game_strings::START_FAILED_BODY),
              std::string("This game could not be started.\nPlease try again."));
    const Dialog d = build_yes_no(game_strings::START_FAILED_HEADER, game_strings::START_FAILED_BODY, true);
    ASSERT_EQ(d.label("HEADER")->text, std::string("Sorry"));
    ASSERT_EQ(d.label("MESSAGE")->text, std::string("This game could not be started.\nPlease try again."));
    ASSERT_EQ(d.buttons.size(), std::size_t{1});
    ASSERT_TRUE(d.button("BUTTON_OK") != nullptr);
    ASSERT_TRUE(d.button("BUTTON_YES") == nullptr && d.button("BUTTON_NO") == nullptr);
}

namespace {
tt2::SavedGame saved(const char* mode, std::int32_t day) {
    tt2::SavedGame g;
    g.mode = mode;
    g.word = "carte";
    g.day = day;
    return g;
}
}  // namespace

// AC-W7: the menu runs 0x42a460's day check before "Continue the game?".
// A same-day WOTD save goes on to the prompt; another day's is dropped.
TEST(menu_discards_another_days_wotd_save_before_the_prompt) {
    const tt2::SavedGame today = saved("WORD_OF_THE_DAY", 6624);
    const tt2::SavedGame yesterday = saved("WORD_OF_THE_DAY", 6623);
    ASSERT_TRUE(!menu_discards_saved_game(WOTD, today, MANILA_1023_0730));       // prompt path
    ASSERT_TRUE(menu_discards_saved_game(WOTD, yesterday, MANILA_1023_0730));    // delete path
    // Both sides of local midnight in UTC+8 (the UTC word day is 6623 for both).
    ASSERT_TRUE(!menu_discards_saved_game(WOTD, yesterday, MANILA_1022_2350));
    ASSERT_TRUE(menu_discards_saved_game(WOTD, yesterday, MANILA_1023_0010));
}

// AC-W11: the rule decides which day "today" is.
TEST(menu_discards_wotd_save_by_the_chosen_day_rule) {
    const tt2::SavedGame original_day = saved("WORD_OF_THE_DAY", 6523);
    ASSERT_TRUE(!menu_discards_saved_game(WOTD, original_day, BERLIN_0715_0030, tt2::WotdDayRule::Original));
    ASSERT_TRUE(menu_discards_saved_game(WOTD, original_day, BERLIN_0715_0030, tt2::WotdDayRule::Local));
}

// Negative: the other modes keep no day (DAY = -1), so their saves always
// reach the prompt, at any clock.
TEST(menu_never_discards_classic_or_lightning_saves) {
    ASSERT_TRUE(!menu_discards_saved_game(tt2::GameMode::Timed, saved("TIMED", -1), MANILA_1023_0730));
    ASSERT_TRUE(!menu_discards_saved_game(tt2::GameMode::Untimed, saved("UNTIMED", -1), MANILA_1023_0010));
    ASSERT_TRUE(!menu_discards_saved_game(tt2::GameMode::Blitz, saved("BLITZ", -1), BERLIN_0715_0030,
                                          tt2::WotdDayRule::Local));
}

// ---- W5 audit against the exe (docs/plans/wotd-gaps.md) --------------------

// Gap 1, 0x425710: while +0x260 or +0x2f9 is set the refresh only calls
// setEnabled(0) (vtable +0xb8) on +0x210..+0x230, never setVisible. NEXT
// ROUND stays on screen with its disabled frame through the reveal and
// behind DIALOG_NEXTROUND.
TEST(next_round_stays_on_screen_greyed_through_the_reveal_and_the_dialog) {
    Fixture f(false);
    auto& s = *f.scene;
    bingo(f);
    const MenuButton& next = *s.button("BUTTON_NextRound");
    ASSERT_TRUE(next.sprite.visible && next.enabled);
    s.press("BUTTON_NextRound");
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(16);
    ASSERT_TRUE(next.sprite.visible && !next.enabled);
    ASSERT_EQ(next.sprite.frame, 3);                       // MenuButton: disabled frame
    ASSERT_EQ(f.skip_button(), std::string(""));           // PASS does not come back
    s.press("BUTTON_NextRound");                           // greyed: nothing happens
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(REVEAL_MS);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(next.sprite.visible && !next.enabled);     // behind the dialog
    s.dialog_press("BUTTON_OK");
    // Negative: the new round's refresh (both flags clear) sets visibility
    // again: not qualified, so PASS x2 and no NEXT ROUND.
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(!next.sprite.visible);
    ASSERT_EQ(f.skip_button(), std::string("BUTTON_Pass_2"));
    ASSERT_TRUE(s.button("BUTTON_Pass_2")->enabled);
}

// Gap 1 in WOTD: DONE stays on screen, greyed, through the reveal, state 3
// and behind the end card (frames: the exe never hides +0x224 there).
TEST(wotd_done_stays_greyed_behind_the_end_card) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    bingo(f);
    s.press("BUTTON_WOTD_DONE");
    s.update(REVEAL_MS + 10);
    s.update(WOTD_GAME_OVER_DELAY_MS + 10);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    const MenuButton& done = *s.button("BUTTON_WOTD_DONE");
    ASSERT_TRUE(done.sprite.visible && !done.enabled);
    ASSERT_TRUE(!s.button("BUTTON_GiveUp")->sprite.visible);
}

// Gap 1, GIVE UP -> YES: GIVE UP stays, greyed; DONE never appears.
TEST(wotd_give_up_stays_greyed_after_yes) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    s.press("BUTTON_GiveUp");
    s.dialog_press("BUTTON_YES");
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    const MenuButton& give_up = *s.button("BUTTON_GiveUp");
    ASSERT_TRUE(give_up.sprite.visible && !give_up.enabled);
    ASSERT_TRUE(!s.button("BUTTON_WOTD_DONE")->sprite.visible);
    s.update(REVEAL_MS + 10);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(give_up.sprite.visible && !give_up.enabled);
}

// Gap 2, 0x424730 @0x42493c: finding every word passes 1000 to 0x428840
// with no mode test, so a Classic all-words reveal is 1 s too.
TEST(classic_timed_all_words_reveal_for_one_second) {
    Fixture f(true);
    auto& s = *f.scene;
    find_all(f);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::AllWords);
    s.update(ALL_WORDS_REVEAL_MS - 10);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(!s.dialog_open());
    s.update(20);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
}

// Gap 2 negative: NEXT ROUND passes 3000 (0x425b60 -> 0x428840(3000)).
TEST(classic_next_round_reveal_holds_three_seconds) {
    Fixture f(false);
    auto& s = *f.scene;
    bingo(f);
    s.press("BUTTON_NextRound");
    s.update(ALL_WORDS_REVEAL_MS + 10);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(REVEAL_MS - ALL_WORDS_REVEAL_MS - 30);        // 2980 ms in
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    s.update(30);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
}

// Gap 3, 0x428840 @0x42888b: every caller runs the Speed Speller check,
// NEXT ROUND too.
TEST(next_round_within_five_seconds_wins_speed_speller) {
    Fixture f(false);
    bingo(f);
    f.scene->update(4900);
    f.scene->press("BUTTON_NextRound");
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NEW);
}

TEST(next_round_after_five_seconds_wins_no_speed_speller) {
    Fixture f(false);
    bingo(f);
    f.scene->update(5100);
    f.scene->press("BUTTON_NextRound");
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NOT_WON);
}

TEST(wotd_done_within_five_seconds_wins_speed_speller) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    bingo(f);
    f.scene->update(4900);
    f.scene->press("BUTTON_WOTD_DONE");
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NEW);
}

TEST(wotd_done_after_five_seconds_wins_no_speed_speller) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    bingo(f);
    f.scene->update(5100);
    f.scene->press("BUTTON_WOTD_DONE");
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_SPEED_SPELLER], tt2::TROPHY_NOT_WON);
}

// Gap 4, 0x426860 @0x426cd9: trophies dequeue whenever +0x260 is 0, and
// 0x428840 sets only +0x2f9 / +0x264. A trophy won as the round ends
// (here Speed Speller, won by NEXT ROUND itself) shows during the reveal.
TEST(trophy_shows_during_the_next_round_reveal) {
    Fixture f(false);
    auto& s = *f.scene;
    bingo(f);
    s.press("BUTTON_NextRound");
    s.update(0);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_EQ(s.trophy_popup_id(), static_cast<int>(tt2::TROPHY_SPEED_SPELLER));
    ASSERT_TRUE(f.played(SoundId::TrophyWon));
}

// Gap 4: the time-up check runs only with +0x260 clear (0x426d24) and
// 0x425420 keeps it clear while lives are left, so a trophy waiting at a
// time-up miss shows during that reveal too.
TEST(trophy_shows_during_a_time_up_miss_reveal) {
    Fixture f(true);
    auto& s = *f.scene;
    f.profile.twists = 999;
    f.profile.solved = 999;
    s.update(ROUND_MS - 2000);
    s.key_space();                                         // With a Twist
    s.update(BALL_MOVE_MS);                                // its popup starts (3500 ms)
    enter_word(f, short_words(f)[0]);                      // Word Nerd waits behind it
    ASSERT_EQ(s.trophies_waiting(), std::size_t{1});
    s.update(1400);                                        // time up, not qualified
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_TRUE(s.last_round_end() == RoundEnd::TimeUpMissed);
    s.update(1600);                                        // With a Twist is done
    s.update(0);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_EQ(s.trophy_popup_id(), static_cast<int>(tt2::TROPHY_WORD_NERD));
}

// Gap 4 negative: a pass comes from its YES (0x4253a0 leaves +0x260 at 1),
// so a waiting trophy holds through the reveal and shows in the next round.
TEST(trophy_waits_through_a_pass_reveal) {
    Fixture f(false);
    auto& s = *f.scene;
    f.profile.solved = 999;
    f.type(short_words(f)[0]);
    s.key_return();                                        // Word Nerd, not shown yet
    s.press("BUTTON_Pass_2");
    s.dialog_press("BUTTON_YES");
    s.update(0);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);
    ASSERT_EQ(s.trophy_popup_id(), -1);
    ASSERT_EQ(s.trophies_waiting(), std::size_t{1});
    s.update(REVEAL_MS + 20);                              // 0x425190: the next round
    s.update(0);
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_EQ(s.trophy_popup_id(), static_cast<int>(tt2::TROPHY_WORD_NERD));
}

// Gap 4 negative in WOTD: GIVE UP's reveal holds it too, and the end card
// follows, so it never shows (the award stays).
TEST(wotd_give_up_reveal_holds_trophies) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    f.profile.solved = 999;
    f.type("car");
    s.key_return();                                        // Word Nerd, not shown yet
    s.press("BUTTON_GiveUp");
    s.dialog_press("BUTTON_YES");
    s.update(0);
    ASSERT_EQ(s.trophy_popup_id(), -1);
    s.update(REVEAL_MS + 10);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    s.update(5000);
    ASSERT_EQ(s.trophy_popup_id(), -1);
    ASSERT_EQ(s.trophies_waiting(), std::size_t{1});
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_WORD_NERD], tt2::TROPHY_NEW);
}

// Answer (a): a trophy won before state 3 shows during the reveal; state 3
// sets +0x260 (0x426b5c) and nothing clears it before the scene goes
// (0x427d10, the end card, its OK -> 0x427b20 -> 0x427b40 all leave it),
// so the round's own trophy, here Well Rounded, is won but never shown.
// The trophy room shows it as new (slot 1).
TEST(wotd_round_trophy_is_won_but_never_shown) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    f.profile.rounds_completed = {1, 1, 1, 1, 0};
    bingo(f);
    s.press("BUTTON_WOTD_DONE");
    s.update(0);
    ASSERT_EQ(s.trophy_popup_id(), static_cast<int>(tt2::TROPHY_SPEED_SPELLER));   // during the reveal
    s.update(REVEAL_MS + 10);                              // state 3
    ASSERT_TRUE(f.profile.has_trophy(tt2::TROPHY_WELL_ROUNDED));
    ASSERT_EQ(s.trophies_waiting(), std::size_t{1});
    s.update(1000);                                        // Speed Speller's 3500 ms are over
    s.update(0);
    ASSERT_EQ(s.trophy_popup_id(), -1);                    // +0x260 holds Well Rounded
    s.update(WOTD_GAME_OVER_DELAY_MS);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    s.update(5000);
    ASSERT_EQ(s.trophy_popup_id(), -1);
    ASSERT_EQ(s.trophies_waiting(), std::size_t{1});
    s.dialog_press("BUTTON_OK");
    ASSERT_EQ(f.exits, 1);
    ASSERT_EQ(f.profile.trophies[tt2::TROPHY_WELL_ROUNDED], tt2::TROPHY_NEW);
}

// Gap 5, 0x432850: "uncommon" is a miss in the table's list at +0x180,
// which 0x432330 fills with the board's words only. "rat" is common, but
// the WOTD cap of 5 per length leaves it off the carte board, so it takes
// the first free 3-letter slot as an uncommon word (Human Dictionary).
TEST(wotd_common_word_off_the_board_counts_as_uncommon) {
    Fixture f(WOTD, false, 0, true, MANILA_1023_0730);
    auto& s = *f.scene;
    ASSERT_TRUE(f.com.contains("rat"));
    ASSERT_TRUE(f.slot("rat") == nullptr);
    f.type("rat");
    s.key_return();
    ASSERT_TRUE(f.slot("rat") != nullptr && f.slot("rat")->found);
    ASSERT_EQ(f.profile.hard_solved, 1);
    f.type("cat");                                         // a board word is not uncommon
    s.key_return();
    ASSERT_EQ(f.profile.hard_solved, 1);
}

// Gap 5 negative: Classic keeps every common word of this rack (27 < 48),
// so "rat" is a board word there; a full-only word is still uncommon.
TEST(classic_board_word_is_not_uncommon) {
    Fixture f(false);
    auto& s = *f.scene;
    ASSERT_TRUE(f.slot("rat") != nullptr);
    f.type("rat");
    s.key_return();
    ASSERT_EQ(f.profile.hard_solved, 0);
    f.type("cart");
    s.key_return();
    ASSERT_EQ(f.profile.hard_solved, 1);
}
