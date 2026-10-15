// Saved classic games: text codec (saved_game.hpp) and the per-player
// files kept by ProfileStore.

#include <filesystem>
#include <string>

#include "test_harness.hpp"
#include "tt2/profile_store.hpp"
#include "tt2/saved_game.hpp"

using tt2::GameMode;
using tt2::ProfileStore;
using tt2::SavedGame;

namespace {
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
