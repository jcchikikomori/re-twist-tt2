// Saved classic games: text codec (saved_game.hpp) and the per-player
// files kept by ProfileStore.

#include <filesystem>
#include <stdexcept>
#include <string>

#include "test_harness.hpp"
#include "tt2/profile_store.hpp"
#include "tt2/saved_game.hpp"

using tt2::GameMode;
using tt2::ProfileStore;
using tt2::SavedGame;

namespace {
// Version-1 saves exactly as serialize() wrote them before Crossword had
// a save format (D-9: the format stays version 1, the new keys appear in
// CROSSWORD saves only).
const std::string OLD_UNTIMED =
    "format=re-twist-tt2 savegame\nversion=1\n"
    "mode=CLASSIC_UNTIMED\nword=crate\nround=3\ntime=0\nlongestword=1\nscore=1230\nday=-1\nfailcount=1\n"
    "found=caret\nfound=ace\nfound=car\n";
const std::string OLD_TIMED =
    "format=re-twist-tt2 savegame\nversion=1\n"
    "mode=CLASSIC_TIMED\nword=planet\nround=2\ntime=85000\nlongestword=0\nscore=410\nday=-1\nfailcount=2\n"
    "found=plan\nfound=ten\n";
const std::string OLD_WOTD =
    "format=re-twist-tt2 savegame\nversion=1\n"
    "mode=WORD_OF_THE_DAY\nword=garden\nround=1\ntime=61000\nlongestword=1\nscore=1360\nday=6624\nfailcount=0\n"
    "found=garden\nfound=danger\n";
const std::string OLD_LIGHTNING =
    "format=re-twist-tt2 savegame\nversion=1\n"
    "mode=BLITZ\nword=\nround=4\ntime=41000\nlongestword=0\nscore=2210\nday=-1\nfailcount=0\n"
    "blitzword=crate,1\nblitzword=traces,0\n";

// A Crossword save on the hand-made grid of test_round_crossword.cpp
// (planet / pane / tale / ten): the root solved, "pant" played in pane's
// slot (another valid word), tale solved and taken back (scored, empty).
const std::string CROSSWORD_SAVE =
    "format=re-twist-tt2 savegame\nversion=1\n"
    "mode=CROSSWORD\nword=planet\nround=2\ntime=0\nlongestword=0\nscore=680\nday=-1\nfailcount=0\n"
    "xword=planet,6,5,h,-1,1,planet\n"
    "xword=pane,8,4,v,1,1,pant\n"
    "xword=tale,5,7,h,3,1,\n"
    "xword=ten,11,5,v,0,0,\n";

// `text` with its first `from` replaced by `to`. A fixture typo throws, so
// a variant can never silently equal the accepted text.
std::string with(const std::string& text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("fixture text not found: " + from);
    std::string out = text;
    out.replace(at, from.size(), to);
    return out;
}
bool parses(const std::string& text) {
    SavedGame out;
    return tt2::parse(text, out);
}


SavedGame sample() {
    SavedGame g;
    g.mode = tt2::save_mode_name(GameMode::Untimed);
    g.word = "crate";
    g.round = 3;
    g.time_ms = 85000;
    g.qualified = true;
    g.score = 1230;
    g.day = -1;
    g.fail_count = 1;
    g.found_words = {"caret", "ace", "car"};
    return g;
}
std::filesystem::path scratch_dir() {
    const auto p = std::filesystem::temp_directory_path() / "re-twist-tt2-savegame-test";
    std::filesystem::remove_all(p);
    return p;
}
}  // namespace

TEST(save_mode_names_match_the_original_files) {
    ASSERT_EQ(std::string(tt2::save_mode_name(GameMode::Timed)), std::string("CLASSIC_TIMED"));
    ASSERT_EQ(std::string(tt2::save_mode_name(GameMode::Untimed)), std::string("CLASSIC_UNTIMED"));
    ASSERT_EQ(std::string(tt2::save_mode_name(GameMode::WordOfTheDay)), std::string("WORD_OF_THE_DAY"));
}

TEST(saved_game_round_trips_through_text) {
    const SavedGame g = sample();
    const std::string text = tt2::serialize(g);
    ASSERT_TRUE(text.rfind("format=re-twist-tt2 savegame\nversion=1\n", 0) == 0);
    ASSERT_TRUE(text.find("word=crate\n") != std::string::npos);
    ASSERT_TRUE(text.find("longestword=1\n") != std::string::npos);
    ASSERT_TRUE(text.find("failcount=1\n") != std::string::npos);
    ASSERT_TRUE(text.find("found=caret\nfound=ace\nfound=car\n") != std::string::npos);
    SavedGame back;
    ASSERT_TRUE(tt2::parse(text, back));
    ASSERT_EQ(back.mode, g.mode);
    ASSERT_EQ(back.word, g.word);
    ASSERT_EQ(back.round, 3);
    ASSERT_EQ(back.time_ms, 85000);
    ASSERT_TRUE(back.qualified);
    ASSERT_EQ(back.score, 1230);
    ASSERT_EQ(back.day, -1);
    ASSERT_EQ(back.fail_count, 1);
    ASSERT_EQ(back.found_words.size(), std::size_t{3});
    ASSERT_EQ(back.found_words[1], std::string("ace"));
    // CRLF and blank lines are tolerated.
    std::string crlf;
    for (char c : text) { if (c == '\n') crlf += "\r\n\r\n"; else crlf.push_back(c); }
    SavedGame again;
    ASSERT_TRUE(tt2::parse(crlf, again));
    ASSERT_EQ(again.word, std::string("crate"));
}

TEST(saved_game_rejects_malformed_text) {
    SavedGame out;
    out.word = "keep";
    ASSERT_TRUE(!tt2::parse("", out));                                   // no format line
    ASSERT_TRUE(!tt2::parse("word=crate\n", out));                       // format must come first
    ASSERT_TRUE(!tt2::parse("format=something else\nword=crate\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nversion=2\nword=crate\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nround=1\n", out));         // no word
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=CRATE\n", out));      // upper case
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=ab\n", out));         // too short
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nround=0\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nfailcount=3\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nlongestword=2\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nscore=abc\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nbogus=1\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nno equals sign\n", out));
    ASSERT_TRUE(!tt2::parse("format=re-twist-tt2 savegame\nword=crate\nfound=crates\n", out));  // longer than the rack
    ASSERT_EQ(out.word, std::string("keep"));                            // untouched on failure
    // The minimal valid document.
    ASSERT_TRUE(tt2::parse("format=re-twist-tt2 savegame\nword=crate\n", out));
    ASSERT_EQ(out.round, 1);
    ASSERT_TRUE(out.found_words.empty());
}

TEST(saved_game_validity_rules) {
    SavedGame g = sample();
    ASSERT_TRUE(g.valid());
    g.found_words.push_back("");
    ASSERT_TRUE(!g.valid());
    g = sample();
    g.time_ms = -1;
    ASSERT_TRUE(!g.valid());
    g = sample();
    g.word = "abcdefghi";
    ASSERT_TRUE(!g.valid());
}

TEST(store_encodes_player_names_into_file_names) {
    ASSERT_EQ(ProfileStore::encode_file_name("Mugwump"), std::string("Mugwump"));
    ASSERT_EQ(ProfileStore::encode_file_name("a b/c"), std::string("a%20b%2Fc"));
    ASSERT_EQ(ProfileStore::encode_file_name(".."), std::string("%2E%2E"));
    ASSERT_EQ(ProfileStore::encode_file_name(""), std::string("_"));
    ProfileStore store("/tmp/x");
    const auto p = store.saved_game_path("A B", "CLASSIC_UNTIMED");
    ASSERT_EQ(p.filename().string(), std::string("CLASSIC_UNTIMED.txt"));
    ASSERT_EQ(p.parent_path().filename().string(), std::string("A%20B"));
    ASSERT_EQ(p.parent_path().parent_path().filename().string(), std::string("savegames"));
}

TEST(store_saves_loads_and_removes_saved_games) {
    const auto dir = scratch_dir();
    ProfileStore store(dir);
    const std::string mode = tt2::save_mode_name(GameMode::Untimed);
    SavedGame out;
    ASSERT_TRUE(!store.has_saved_game("Mugwump", mode));
    ASSERT_TRUE(!store.load_saved_game("Mugwump", mode, out));          // missing: false, untouched
    ASSERT_TRUE(store.remove_saved_game("Mugwump", mode));               // missing: still fine
    ASSERT_TRUE(store.save_saved_game("Mugwump", mode, sample()));       // directories are created
    ASSERT_TRUE(store.has_saved_game("Mugwump", mode));
    ASSERT_TRUE(!store.has_saved_game("Mugwump", "CLASSIC_TIMED"));      // one file per mode
    ASSERT_TRUE(!store.has_saved_game("Other", mode));                   // and per player
    ASSERT_TRUE(store.load_saved_game("Mugwump", mode, out));
    ASSERT_EQ(out.word, std::string("crate"));
    ASSERT_EQ(out.found_words.size(), std::size_t{3});
    // A corrupt file is reported as "no save" without touching `out`.
    {
        std::ofstream bad(store.saved_game_path("Mugwump", mode), std::ios::trunc);
        bad << "garbage\n";
    }
    SavedGame keep;
    keep.word = "keep";
    ASSERT_TRUE(!store.load_saved_game("Mugwump", mode, keep));
    ASSERT_EQ(keep.word, std::string("keep"));
    ASSERT_TRUE(store.remove_saved_game("Mugwump", mode));
    ASSERT_TRUE(!store.has_saved_game("Mugwump", mode));
    std::filesystem::remove_all(dir);
}

TEST(store_removes_every_saved_game_of_one_player) {
    const auto dir = scratch_dir();
    ProfileStore store(dir);
    ASSERT_TRUE(store.remove_saved_games("Nobody"));                    // missing: still fine
    ASSERT_TRUE(store.save_saved_game("A B", "CLASSIC_UNTIMED", sample()));
    ASSERT_TRUE(store.save_saved_game("A B", "CLASSIC_TIMED", sample()));
    ASSERT_TRUE(store.save_saved_game("Other", "CLASSIC_TIMED", sample()));
    ASSERT_TRUE(store.remove_saved_games("A B"));
    ASSERT_TRUE(!store.has_saved_game("A B", "CLASSIC_UNTIMED"));
    ASSERT_TRUE(!store.has_saved_game("A B", "CLASSIC_TIMED"));
    ASSERT_TRUE(!std::filesystem::exists(store.saved_game_path("A B", "CLASSIC_TIMED").parent_path()));
    ASSERT_TRUE(store.has_saved_game("Other", "CLASSIC_TIMED"));        // other players keep theirs
    std::filesystem::remove_all(dir);
}

// --- Old saves (D-9) ---------------------------------------------------------

TEST(old_version_1_saves_of_four_modes_parse_unchanged) {
    SavedGame untimed;
    ASSERT_TRUE(tt2::parse(OLD_UNTIMED, untimed));
    ASSERT_EQ(untimed.mode, std::string("CLASSIC_UNTIMED"));
    ASSERT_EQ(untimed.word, std::string("crate"));
    ASSERT_EQ(untimed.round, 3);
    ASSERT_TRUE(untimed.qualified);
    ASSERT_EQ(untimed.score, 1230);
    ASSERT_EQ(untimed.fail_count, 1);
    ASSERT_EQ(untimed.found_words.size(), std::size_t{3});
    ASSERT_EQ(tt2::serialize(untimed), OLD_UNTIMED);

    SavedGame timed;
    ASSERT_TRUE(tt2::parse(OLD_TIMED, timed));
    ASSERT_EQ(timed.mode, std::string("CLASSIC_TIMED"));
    ASSERT_EQ(timed.time_ms, 85000);
    ASSERT_EQ(timed.fail_count, 2);
    ASSERT_EQ(timed.found_words[1], std::string("ten"));
    ASSERT_EQ(tt2::serialize(timed), OLD_TIMED);

    SavedGame wotd;
    ASSERT_TRUE(tt2::parse(OLD_WOTD, wotd));
    ASSERT_EQ(wotd.mode, std::string("WORD_OF_THE_DAY"));
    ASSERT_EQ(wotd.word, std::string("garden"));
    ASSERT_EQ(wotd.day, 6624);
    ASSERT_EQ(wotd.score, 1360);
    ASSERT_EQ(tt2::serialize(wotd), OLD_WOTD);

    SavedGame lightning;
    ASSERT_TRUE(tt2::parse(OLD_LIGHTNING, lightning));
    ASSERT_TRUE(lightning.blitz());
    ASSERT_EQ(lightning.round, 4);
    ASSERT_EQ(lightning.time_ms, 41000);
    ASSERT_EQ(lightning.blitz_words.size(), std::size_t{2});
    ASSERT_TRUE(lightning.blitz_words[0].completed);
    ASSERT_EQ(tt2::serialize(lightning), OLD_LIGHTNING);

    for (const SavedGame* g : {&untimed, &timed, &wotd, &lightning}) {
        ASSERT_TRUE(!g->crossword());
        ASSERT_TRUE(g->crossword_words.empty());
    }
}

TEST(saves_of_other_modes_reject_crossword_lines) {
    const std::string xword = "xword=crate,5,5,h,-1,0,\n";
    ASSERT_TRUE(!parses(OLD_UNTIMED + xword));
    ASSERT_TRUE(!parses(OLD_TIMED + xword));
    ASSERT_TRUE(!parses(OLD_WOTD + xword));
    ASSERT_TRUE(!parses(OLD_LIGHTNING + xword));
}

// --- Crossword (C6) -----------------------------------------------------------

TEST(crossword_save_tag_is_the_exes) {
    // 0x41a110 pushes "CROSSWORD" (0x590254); "CRISSCROSS" has no xref (OI-11).
    ASSERT_EQ(std::string(tt2::save_mode_name(GameMode::Crossword)), std::string("CROSSWORD"));
    ASSERT_EQ(std::string(tt2::save_mode_name(GameMode::Blitz)), std::string("BLITZ"));
    SavedGame g;
    g.mode = "CROSSWORD";
    ASSERT_TRUE(g.crossword());
    ASSERT_TRUE(!g.blitz());
    g.mode = "CRISSCROSS";
    ASSERT_TRUE(!g.crossword());
    g.mode = "BLITZ";
    ASSERT_TRUE(!g.crossword());
}

TEST(crossword_save_text_is_accepted_field_by_field) {
    SavedGame g;
    ASSERT_TRUE(tt2::parse(CROSSWORD_SAVE, g));
    ASSERT_TRUE(g.crossword());
    ASSERT_EQ(g.word, std::string("planet"));
    ASSERT_EQ(g.round, 2);
    ASSERT_EQ(g.time_ms, 0);
    ASSERT_EQ(g.score, 680);
    ASSERT_TRUE(g.found_words.empty());
    ASSERT_EQ(g.crossword_words.size(), std::size_t{4});

    const SavedGame::CrosswordWord& root = g.crossword_words[0];
    ASSERT_EQ(root.answer, std::string("planet"));
    ASSERT_EQ(root.a, 6);
    ASSERT_EQ(root.b, 5);
    ASSERT_TRUE(root.horizontal);
    ASSERT_EQ(root.crossing, -1);
    ASSERT_TRUE(root.scored);
    ASSERT_EQ(root.played, std::string("planet"));

    const SavedGame::CrosswordWord& pane = g.crossword_words[1];
    ASSERT_EQ(pane.answer, std::string("pane"));
    ASSERT_EQ(pane.a, 8);
    ASSERT_EQ(pane.b, 4);
    ASSERT_TRUE(!pane.horizontal);
    ASSERT_EQ(pane.crossing, 1);
    ASSERT_EQ(pane.played, std::string("pant"));   // differs from the answer

    ASSERT_TRUE(g.crossword_words[2].scored);       // taken back: scored, no word
    ASSERT_EQ(g.crossword_words[2].played, std::string(""));
    ASSERT_TRUE(!g.crossword_words[3].scored);
    ASSERT_EQ(g.crossword_words[3].crossing, 0);

    // serialize() writes the same bytes: WORD is the root, the xword
    // lines come in commit order, root first.
    ASSERT_EQ(tt2::serialize(g), CROSSWORD_SAVE);
}

TEST(crossword_save_rejects_a_missing_or_wrong_root) {
    ASSERT_TRUE(parses(CROSSWORD_SAVE));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "word=planet\n", "")));                    // no word=
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "word=planet\n", "word=plane\n")));        // xword[0] is not word=
    // xword[0] is not word=, though every other rule holds for "plane".
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=planet,6,5,h,-1,1,planet", "xword=plane,6,5,h,-1,1,plane")));
    // xword[0] not the root: the first two placements swapped.
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=planet,6,5,h,-1,1,planet\nxword=pane,8,4,v,1,1,pant\n",
                             "xword=pane,8,4,v,1,1,pant\nxword=planet,6,5,h,-1,1,planet\n")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "planet,6,5,h,", "planet,6,5,v,")));       // root vertical
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "planet,6,5,h,-1,", "planet,6,5,h,2,")));  // root with a crossing
    // A root of 4 or 9 letters.
    ASSERT_TRUE(!parses("format=re-twist-tt2 savegame\nmode=CROSSWORD\nword=plan\nxword=plan,7,5,h,-1,0,\n"));
    ASSERT_TRUE(!parses("format=re-twist-tt2 savegame\nmode=CROSSWORD\nword=planetary\n"
                        "xword=planetary,4,5,h,-1,0,\n"));
    ASSERT_TRUE(parses("format=re-twist-tt2 savegame\nmode=CROSSWORD\nword=plane\nxword=plane,6,5,h,-1,0,\n"));
}

TEST(crossword_save_rejects_bad_coordinates_and_crossings) {
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,", "pane,20,4,")));      // a past 0..19
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,", "pane,-1,4,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,", "pane,8,11,")));      // b past 0..10
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,", "pane,8,-1,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,", "pane,4294967304,4,")));   // wraps to 8 as an int
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,v,1,", "pane,8,4,v,-1,")));  // a word crosses nothing
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "pane,8,4,v,1,", "pane,8,4,v,4,")));   // past "pane"
    // The extremes are bounds only; whether the word fits is restore()'s check.
    ASSERT_TRUE(parses(with(CROSSWORD_SAVE, "pane,8,4,v,1,", "pane,19,10,v,3,")));
    ASSERT_TRUE(parses(with(CROSSWORD_SAVE, "pane,8,4,v,1,", "pane,0,0,v,0,")));
}

TEST(crossword_save_rejects_words_the_root_cannot_spell) {
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=pane,8,4,v,1,1,pant", "xword=tent,8,4,v,1,0,")));  // one 't'
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=pane,8,4,v,1,1,pant", "xword=zeta,8,4,v,1,0,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=ten,", "xword=te,")));                 // 2 letters
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=pane,", "xword=Pane,")));              // upper case
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, ",1,pant\n", ",1,tent\n")));                  // played: one 't'
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, ",1,pant\n", ",1,pan\n")));                   // played: wrong length
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, ",1,pant\n", ",1,PANT\n")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, ",1,pant\n", ",0,pant\n")));                  // played, never scored
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=ten,11,5,v,0,0,", "xword=planetary,11,5,v,0,0,")));  // 9 letters
}

TEST(crossword_save_rejects_lines_of_other_modes) {
    ASSERT_TRUE(!parses(CROSSWORD_SAVE + "found=plan\n"));
    ASSERT_TRUE(!parses(CROSSWORD_SAVE + "blitzword=planet,0\n"));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "round=2\n", "round=0\n")));
}

TEST(crossword_save_rejects_malformed_xword_lines) {
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=ten,11,5,v,0,0,\n", "xword=ten,11,5,v,0,0\n")));    // 6 fields
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=ten,11,5,v,0,0,\n", "xword=ten,11,5,v,0,0,,\n")));  // 8 fields
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "xword=ten,11,5,v,0,0,\n", "xword=\n")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "ten,11,5,", "ten,x,5,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "ten,11,5,", "ten,11,,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "ten,11,5,v,", "ten,11,5,d,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "ten,11,5,v,0,0,", "ten,11,5,v,0,2,")));
    ASSERT_TRUE(!parses(with(CROSSWORD_SAVE, "ten,11,5,v,0,", "ten,11,5,v,z,")));
}

TEST(crossword_save_holds_one_to_eleven_placements) {
    const std::string head = "format=re-twist-tt2 savegame\nversion=1\nmode=CROSSWORD\nword=planet\n";
    const std::string root = "xword=planet,6,5,h,-1,0,\n";
    std::string words;   // the bound only; restore() rejects this grid
    for (int i = 0; i < 10; ++i) words += "xword=pane,8,4,v,1,0,\n";
    ASSERT_TRUE(!parses(head));                                  // no placement
    ASSERT_TRUE(parses(head + root));                            // the root alone
    ASSERT_TRUE(parses(head + root + words));                    // 11: the root and Config::max_words
    ASSERT_TRUE(!parses(head + root + words + "xword=ten,11,5,v,0,0,\n"));   // 12
}
