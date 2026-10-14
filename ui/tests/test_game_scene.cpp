// Classic game scene: word-grid layout, letter rows, guess flow, timer
// and round / game-over transitions (game_scene.hpp).

#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
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
    int discards = 0;
    std::vector<tt2::SavedGame> saved;
    tt2::GameSettings settings;
    std::vector<float> fx_applied, music_applied;
    int settings_commits = 0;
    std::unique_ptr<GameScene> scene;

    explicit Fixture(bool timed, bool with_hs = true, std::int64_t seed = 0) {
        GameScene::Services s;
        s.play_sound = [this](SoundId id) { sounds.push_back(id); };
        s.on_profile_change = [this] { ++saves; };
        s.on_exit = [this] { ++exits; };
        s.save_game = [this](const tt2::SavedGame& g) { saved.push_back(g); };
        s.discard_save = [this] { ++discards; };
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
    // The balls flip in one after another (0x43c940); each flip ends with
    // SOUND_letter_load on the resting pearl frame.
    ASSERT_TRUE(!f.played(SoundId::LetterLoad));
    ASSERT_EQ(s.balls()[0].frame, 0);
    s.update(BALL_REVEAL_FRAME_MS * (BALL_IDLE_FRAME + 1));
    int loads = 0;
    for (auto id : f.sounds) if (id == SoundId::LetterLoad) ++loads;
    ASSERT_EQ(loads, 1);
    s.update(BALL_REVEAL_STAGGER_MS * 4);
    loads = 0;
    for (auto id : f.sounds) if (id == SoundId::LetterLoad) ++loads;
    ASSERT_EQ(loads, 5);
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
    // Enter with nothing typed is a no-op.
    f.sounds.clear();
    s.key_return();
    ASSERT_TRUE(f.sounds.empty());
    ASSERT_EQ(s.score(), 90 + 160);
    // Commentary fades in, holds, fades out.
    s.update(COMMENT_FADE_MS);
    ASSERT_TRUE(s.commentary().alpha > 0.9f);
    s.update(COMMENT_SHOW_MS + COMMENT_FADE_MS + 50);
    ASSERT_TRUE(s.commentary().alpha < 0.05f);
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
    f.sounds.clear();
    f.type("tec");
    s.key_return();
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::EnoughOfLength));
    ASSERT_TRUE(f.played(SoundId::WordReject));
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
    f.sounds.clear();
    f.type("cart");
    s.key_return();
    ASSERT_EQ(s.commentary().frame, static_cast<int>(Comment::AlreadyGuessed));
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
    ASSERT_EQ(static_cast<int>(std::count(f.sounds.begin(), f.sounds.end(), SoundId::LetterLoad)), 1);
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
    // First miss: 0x425190 starts the next round by itself, no dialog.
    ASSERT_TRUE(s.phase() == GamePhase::Playing);
    ASSERT_TRUE(!s.dialog_open());
    ASSERT_EQ(s.round_number(), 2);
    ASSERT_EQ(s.time_left_ms(), ROUND_MS);
    ASSERT_EQ(f.discards, 0);
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
    }
    ASSERT_EQ(s.misses(), 3);
    ASSERT_EQ(s.round_number(), 4);
    ASSERT_TRUE(s.phase() == GamePhase::GameOver);
    ASSERT_EQ(f.discards, 1);                              // the save goes with the game
    ASSERT_TRUE(s.game_over_sprite().alpha == 1.0f);
    ASSERT_TRUE(s.dialog_kind() == GameDialog::Congrats);  // a new high score
    s.key_return();
    ASSERT_TRUE(s.dialog_kind() == GameDialog::EndGame);
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
    ASSERT_TRUE(!b.tick(20 + 3 * BALL_REVEAL_FRAME_MS));   // 3 frames in
    ASSERT_EQ(b.frame, 3);
    ASSERT_TRUE(b.tick(9 * BALL_REVEAL_FRAME_MS));         // last frame reached
    ASSERT_EQ(b.frame, BALL_IDLE_FRAME);
    ASSERT_TRUE(!b.revealing());
    ASSERT_TRUE(!b.tick(1000));                            // only once
    ASSERT_EQ(b.sheet_frame(), 2 * BALL_FRAMES_PER_LETTER + BALL_IDLE_FRAME);
}

TEST(ball_slides_in_a_straight_line_over_its_duration) {
    LetterBall b;
    b.letter = 'a';
    b.start_x = b.x = 100;
    b.start_y = b.y = 300;
    b.target_x = 200;
    b.target_y = 200;
    b.move_ms = BALL_MOVE_MS;
    b.tick(BALL_MOVE_MS / 2);
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
    ASSERT_EQ(one.h, YESNO_MIN_H);
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

// The menu's "Continue the game?" message wraps at 390 px. With the
// placeholder FONT_SETTINGSTEXT (scale 3: glyph 15 + 3, space 9 + 3; '('
// and ')' have no glyph) the first line "Do you want to continue" is
// 19 * 18 + 4 * 12 - 3 = 387 <= 390, and adding " your" would make 471.
TEST(yes_no_message_wraps_at_the_label_width) {
    const BitmapFont font = make_placeholder_font(3);
    const LineBreaker wrap = [&font](const std::string& t, int w) { return wrap_text(font, t, w); };
    const Dialog d = build_yes_no(game_strings::SAVEGAME_HEADER, game_strings::SAVEGAME_BODY, false, wrap);
    ASSERT_EQ(d.label("MESSAGE")->text,
              std::string("Do you want to continue\nyour last played game?\n(Select 'No' to start\nagain.)"));
    const int button_y = YESNO_BUTTON_Y + 4 * YESNO_LINE_H;   // 100 + 120 = 220
    ASSERT_TRUE(d.button("BUTTON_YES")->sprite.y == static_cast<float>(button_y));
    ASSERT_EQ(d.h, button_y + YESNO_BUTTON_H);                // 270
    ASSERT_EQ(d.label("MESSAGE")->max_width, YESNO_MESSAGE_W);
}

TEST(yes_no_short_message_is_unchanged_by_the_wrap) {
    const BitmapFont font = make_placeholder_font(3);
    const LineBreaker wrap = [&font](const std::string& t, int w) { return wrap_text(font, t, w); };
    const Dialog d = build_yes_no("H", "one line", false, wrap);
    ASSERT_EQ(d.label("MESSAGE")->text, std::string("one line"));
    ASSERT_EQ(d.h, YESNO_MIN_H);
}

TEST(scene_yes_no_dialogs_use_the_wrap_service) {
    tt2::WordTree com = common();
    tt2::WordTree ful = full();
    tt2::PlayerProfile profile = tt2::PlayerProfile::create("Mugwump", 7u);
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
    s.dialog_press("SWITCH_FULLSCREEN");
    s.update(5000);
    ASSERT_EQ(s.time_left_ms(), time_left);                // the clock stands still
    s.dialog_press("BUTTON_OK");
    ASSERT_TRUE(s.dialog_kind() == GameDialog::PauseMenu);
    ASSERT_TRUE(s.settings_dialog() == nullptr);
    ASSERT_EQ(f.profile.max_word_length, 6);
    ASSERT_EQ(f.saves, saves + 1);                          // 0x462080
    ASSERT_EQ(f.settings_commits, 1);
    ASSERT_TRUE(f.settings.fullscreen);
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
