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

    // `tutorial_seen`: the player has had DIALOG_TUTORIAL for every mode,
    // so most tests start straight at the rack.
    explicit Fixture(bool timed, bool with_hs = true, std::int64_t seed = 0, bool tutorial_seen = true) {
        profile.mode_flags.fill(tutorial_seen);
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
        scene = std::make_unique<GameScene>(com, ful, profile, with_hs ? &hs : nullptr, timed, s, seed);
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
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Untimed)], 1);
    const std::int32_t score = s.score();
    ASSERT_TRUE(score >= 1000 + 100 * static_cast<std::int32_t>(words.size()));
    s.type_letter('c');                                    // input ignored now
    ASSERT_TRUE(s.typed().empty());
    s.update(16);
    ASSERT_TRUE(s.phase() == GamePhase::Reveal);           // 0x428840: always 3 s
    s.update(REVEAL_MS);
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::NextRound);
    ASSERT_TRUE(f.played(SoundId::NextRoundAchieved));
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
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
    ASSERT_TRUE(f.played(SoundId::HighScoreAchieved));
    const auto& table = f.hs.table(tt2::GameMode::Timed);
    bool found = false;
    for (const auto& e : table.entries) if (e.name == "Mugwump" && e.score == 1250 && e.extra == "4") found = true;
    ASSERT_TRUE(found);
    ASSERT_EQ(f.exits, 0);
    s.key_return();
    ASSERT_EQ(f.exits, 1);
    ASSERT_TRUE(s.finished());
    s.key_return();                                        // only once
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
    ASSERT_EQ(f.skip_button(), std::string(""));           // hidden during the reveal
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
    GameScene s(r.com, r.ful, r.profile, nullptr, false, sv, 0, &g);
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
    GameScene s(com, ful, profile, nullptr, false, sv, 0);
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
    GameScene resumed(r.com, r.ful, r.profile, nullptr, true, sv, 0, &later);
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
    GameScene s(f.com, f.ful, f.profile, nullptr, false, sv, 0, &bad);
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
    GameScene s(f.com, f.ful, f.profile, nullptr, false, sv, 0, &g);
    ASSERT_TRUE(!s.dialog_open());                         // 0x42a460 skips 0x425c50
    ASSERT_TRUE(!f.profile.mode_flags[static_cast<std::size_t>(tt2::GameMode::Untimed)]);
}
