// Classic game scene: word-grid layout, letter rows, guess flow, timer
// and round / game-over transitions (game_scene.hpp).

#include "test_harness.hpp"

#include <memory>
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
    int saves = 0;
    int exits = 0;
    std::unique_ptr<GameScene> scene;

    explicit Fixture(bool timed, bool with_hs = true) {
        GameScene::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_profile_change = [this] { ++saves; };
        s.on_exit = [this] { ++exits; };
        scene = std::make_unique<GameScene>(com, ful, profile, with_hs ? &hs : nullptr, timed, s, 0);
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

// ---- round start -------------------------------------------------------

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
    ASSERT_TRUE(f.played(SoundId::LetterLoad));
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
    ASSERT_TRUE(s.button("BUTTON_GiveUp")->sprite.visible);
    f.type(s.rack());
    s.key_return();
    ASSERT_TRUE(s.qualified());
    ASSERT_TRUE(s.button("BUTTON_NextRound")->sprite.visible);
    ASSERT_TRUE(!s.button("BUTTON_GiveUp")->sprite.visible);
}

// ---- letters -------------------------------------------------------------

TEST(typing_moves_balls_to_played_row_and_back) {
    Fixture f(false);
    auto& s = *f.scene;
    const std::string rack = s.rack();
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
    ASSERT_EQ(moved->slot, 2);
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
    s.key_space();                                         // space twists too
    ASSERT_EQ(f.profile.twists, twists + 2);
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
    // Duplicate.
    f.sounds.clear();
    f.type("car");
    s.key_return();
    ASSERT_EQ(s.score(), 90);
    ASSERT_TRUE(f.played(SoundId::WordReject));
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::AlreadyGuessed));
    // Not a word.
    f.type("cra");
    s.key_return();
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::NotListed));
    // Valid in the full list but not a board word: the engine reports
    // "no room" (see GuessProcessor), shown as "enough words of that length".
    f.type("cart");
    s.key_return();
    ASSERT_EQ(s.score(), 90);
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::EnoughOfLength));
    // Enter with nothing typed is a no-op.
    f.sounds.clear();
    s.key_return();
    ASSERT_TRUE(f.sounds.empty());
    ASSERT_EQ(s.score(), 90);
    // Commentary fades in, holds, fades out.
    s.update(COMMENT_FADE_MS);
    ASSERT_TRUE(s.commentary().alpha > 0.9f);
    s.update(COMMENT_SHOW_MS + COMMENT_FADE_MS + 50);
    ASSERT_TRUE(s.commentary().alpha < 0.05f);
}

TEST(no_room_comment_depends_on_length_slots) {
    Fixture f(false);
    auto& s = *f.scene;
    // "tec" is valid (full list) but every 3-letter slot is still open ->
    // it fills one. Fill ALL 3-letter slots, then a fresh valid 3-letter
    // word gets "enough words of that length".
    std::vector<std::string> threes;
    for (const auto& v : s.slots()) if (v.word.size() == 3) threes.push_back(v.word);
    for (const auto& w : threes) { f.type(w); s.key_return(); }
    f.sounds.clear();
    f.type("tec");
    s.key_return();
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::EnoughOfLength));
    ASSERT_TRUE(f.played(SoundId::WordReject));
}

TEST(bingo_qualifies_with_popup_and_bonus) {
    Fixture f(false);
    auto& s = *f.scene;
    f.type(s.rack());
    s.key_return();
    ASSERT_TRUE(s.qualified());
    ASSERT_EQ(s.score(), 250 + 1000);
    ASSERT_TRUE(f.played(SoundId::WordBingo));
    ASSERT_TRUE(s.popup_bingo().visible);
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::Bingo));
    s.update(POPUP_FRAME_MS * 15 + 100);
    s.update(16);
    ASSERT_TRUE(!s.popup_bingo().visible);
    // A second bingo-length word is just 10*len^2 (no second bonus).
    std::string other;
    for (const auto& v : s.slots()) if (v.word.size() == 5 && v.word != s.rack()) { other = v.word; break; }
    f.type(other);
    s.key_return();
    ASSERT_EQ(s.score(), 1250 + 250);
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
    ASSERT_TRUE(s.popup_all_words().visible);
    ASSERT_EQ(f.profile.rounds_completed[static_cast<std::size_t>(tt2::GameMode::Untimed)], 1);
    const std::int32_t score = s.score();
    ASSERT_TRUE(score >= 1000 + 100 * static_cast<std::int32_t>(words.size()));
    s.type_letter('c');                                    // input ignored now
    ASSERT_TRUE(s.typed().empty());
    s.update(16);                                          // no reveal delay: all found
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
    ASSERT_TRUE(s.button("BUTTON_NextRound")->sprite.visible);
    s.press("BUTTON_NextRound");
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_EQ(s.score(), score);                           // score carries over
    ASSERT_TRUE(!s.qualified());
    for (const auto& v : s.slots()) ASSERT_TRUE(!v.found && !v.revealed);
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
    s.update(REVEAL_MS - 1);
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
    ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);       // first miss: play on
    s.key_return();
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS);
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
    for (int i = 0; i < 4; ++i) {
        s.update(ROUND_MS + 10);                           // round 1 qualified, then 3 misses
        s.update(REVEAL_MS + 20);
        if (i < 3) {
            ASSERT_TRUE(s.phase() == GamePhase::AwaitNext);
            s.key_return();
        }
    }
    ASSERT_EQ(s.misses(), 3);
    ASSERT_EQ(s.round_number(), 4);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    s.update(600);
    ASSERT_TRUE(s.game_over_sprite().alpha == 1.0f);
    ASSERT_EQ(s.game_over_sprite().frame, 0);
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

TEST(give_up_and_menu_leave_the_game) {
    Fixture f(false, false);
    auto& s = *f.scene;
    s.press("BUTTON_GiveUp");
    ASSERT_TRUE(s.last_round_end() == RoundEnd::GaveUp);
    s.update(REVEAL_MS + 20);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    s.mouse_up(400, 300);
    ASSERT_EQ(f.exits, 1);

    Fixture g(false, false);
    g.scene->press("BUTTON_Menu");
    ASSERT_EQ(g.exits, 1);
    Fixture h(true, false);
    h.scene->key_escape();                                 // Esc = give up
    ASSERT_TRUE(h.scene->last_round_end() == RoundEnd::GaveUp);
}

TEST(mouse_buttons_hover_press_and_fire) {
    Fixture f(false);
    auto& s = *f.scene;
    auto* twist = s.button("BUTTON_Twist");
    const float cx = twist->sprite.x + 10, cy = twist->sprite.y + 10;
    s.mouse_move(cx, cy);
    ASSERT_TRUE(twist->hover);
    ASSERT_TRUE(f.played(SoundId::ButtonRollover));
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
