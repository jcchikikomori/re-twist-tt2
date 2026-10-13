// Player profiles, player manager, high scores and their text format.
// Rules quoted by address live in docs/reverse-engineering/profiles.md.

#include "test_harness.hpp"
#include "tt2/profile.hpp"
#include "tt2/profile_store.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <random>

using namespace tt2;

namespace {

const std::vector<std::string>& names() {
    static const std::vector<std::string> n = {
        "Susan", "Karen", "Linda", "Julie", "Helen", "Sarah", "Carol", "Jane",
        "Janet", "Sandra", "Angela", "Ann", "Sharon", "Lisa", "Alison", "Sue",
        "Anne", "Mary", "Tracey", "Barbara", "Amanda", "Wendy", "Claire", "Emma",
        "Joanne", "Donna", "Nicola"};
    return n;
}

std::filesystem::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = std::filesystem::temp_directory_path() /
                   ("tt2_profile_test_" + std::string(tag) + "_" + std::to_string(rd()));
    std::filesystem::remove_all(p);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// PlayerProfile
// ---------------------------------------------------------------------------

TEST(profile_defaults_match_0x40f060) {
    const PlayerProfile p = PlayerProfile::create("Ann", 123456u);
    ASSERT_EQ(p.name, std::string("Ann"));
    ASSERT_EQ(p.bag.seed, 123456u);
    ASSERT_EQ(p.bag.index, 0u);
    ASSERT_EQ(p.wod_day, -1);
    ASSERT_EQ(p.max_word_length, 8);
    ASSERT_EQ(p.twists, 0);
    ASSERT_EQ(p.solved, 0);
    ASSERT_EQ(p.hard_solved, 0);
    for (auto t : p.trophies) ASSERT_EQ(t, 0);
    for (auto r : p.rounds_completed) ASSERT_EQ(r, 0);
    for (auto f : p.mode_flags) ASSERT_TRUE(!f);
    ASSERT_TRUE(!p.returning);
    ASSERT_TRUE(!p.has_trophy(0));
    ASSERT_TRUE(!p.has_trophy(99));
}

TEST(profile_wod_today_and_day_number) {
    // 2008-09-03 00:00 UTC is unix day 14125 → day 0.
    ASSERT_EQ(wotd_day_from_unix(14125LL * 86400), 0);
    ASSERT_EQ(wotd_day_from_unix(14125LL * 86400 + 86399), 0);
    ASSERT_EQ(wotd_day_from_unix(14126LL * 86400), 1);
    // A +480 minute zone (UTC+8) moves local midnight 8 h earlier... the
    // menu subtracts `tz_minutes * 60` where tz is minutes *west* of UTC.
    ASSERT_EQ(wotd_day_from_unix(14126LL * 86400, 480), 0);
    ASSERT_EQ(wotd_day_from_unix(14126LL * 86400 + 480 * 60, 480), 1);
    PlayerProfile p = PlayerProfile::create("x", 1);
    ASSERT_TRUE(!p.played_wod_today(5));
    p.wod_day = 5;
    ASSERT_TRUE(p.played_wod_today(5));
    ASSERT_TRUE(!p.played_wod_today(6));
}

TEST(utf8_length_counts_code_points) {
    ASSERT_EQ(utf8_length(""), 0u);
    ASSERT_EQ(utf8_length("abc"), 3u);
    ASSERT_EQ(utf8_length("caf\xC3\xA9"), 4u);         // café
    ASSERT_EQ(utf8_length("\xE2\x82\xAC"), 1u);          // €
}

// ---------------------------------------------------------------------------
// PlayerManager
// ---------------------------------------------------------------------------

TEST(player_manager_add_makes_current) {
    PlayerManager pm;
    ASSERT_TRUE(pm.empty());
    ASSERT_TRUE(pm.current() == nullptr);
    ASSERT_EQ(to_string(pm.add("Ann", 7)), to_string(AddPlayerResult::Added));
    ASSERT_EQ(pm.count(), 1);
    ASSERT_TRUE(pm.current() != nullptr);
    ASSERT_EQ(pm.current()->name, std::string("Ann"));
    ASSERT_EQ(pm.current()->bag.seed, 7u);
    ASSERT_EQ(to_string(pm.add("Bob", 8)), to_string(AddPlayerResult::Added));
    ASSERT_EQ(pm.current()->name, std::string("Bob"));
    ASSERT_EQ(pm.current_index(), 1);
}

TEST(player_manager_rejects_bad_names) {
    PlayerManager pm;
    ASSERT_EQ(to_string(pm.add("", 1)), to_string(AddPlayerResult::EmptyName));
    ASSERT_EQ(to_string(pm.add("1234567890123456", 1)), to_string(AddPlayerResult::TooLong));
    ASSERT_EQ(to_string(pm.add("123456789012345", 1)), to_string(AddPlayerResult::Added));
    ASSERT_EQ(to_string(pm.add("a\tb", 1)), to_string(AddPlayerResult::InvalidChars));
    ASSERT_EQ(to_string(pm.add("a\nb", 1)), to_string(AddPlayerResult::InvalidChars));
    // 15 code points of 2 bytes each is still 15 characters (0x45fa60).
    std::string accented;
    for (int i = 0; i < 15; ++i) accented += "\xC3\xA9";
    ASSERT_EQ(to_string(pm.add(accented, 1)), to_string(AddPlayerResult::Added));
    ASSERT_EQ(pm.count(), 2);
}

TEST(player_manager_duplicate_is_exact_case_sensitive) {
    PlayerManager pm;
    pm.add("Ann", 1);
    ASSERT_EQ(to_string(pm.add("Ann", 2)), to_string(AddPlayerResult::Exists));
    ASSERT_EQ(to_string(pm.add("ann", 2)), to_string(AddPlayerResult::Added));
    ASSERT_EQ(to_string(pm.add("Ann ", 2)), to_string(AddPlayerResult::Added));
    ASSERT_TRUE(pm.exists("Ann"));
    ASSERT_TRUE(!pm.exists("ANN"));
    ASSERT_EQ(pm.count(), 3);
    // Duplicate attempt must not change the current player.
    ASSERT_EQ(pm.current()->name, std::string("Ann "));
}

TEST(player_manager_caps_at_five) {
    PlayerManager pm;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        ASSERT_EQ(to_string(pm.add("P" + std::to_string(i), 1)), to_string(AddPlayerResult::Added));
    }
    ASSERT_TRUE(pm.full());
    ASSERT_EQ(to_string(pm.add("Six", 1)), to_string(AddPlayerResult::Full));
    ASSERT_EQ(pm.count(), 5);
    // Exists is checked before Full (0x4088c0 checks the name first).
    ASSERT_EQ(to_string(pm.add("P0", 1)), to_string(AddPlayerResult::Exists));
    ASSERT_TRUE(pm.remove("P2"));
    ASSERT_TRUE(!pm.full());
    ASSERT_EQ(to_string(pm.add("Six", 1)), to_string(AddPlayerResult::Added));
}

TEST(player_manager_select_and_remove) {
    PlayerManager pm;
    pm.add("A", 1);
    pm.add("B", 1);
    pm.add("C", 1);
    ASSERT_TRUE(pm.select("A"));
    ASSERT_EQ(pm.current()->name, std::string("A"));
    ASSERT_TRUE(!pm.select("Z"));
    ASSERT_EQ(pm.current()->name, std::string("A"));
    // Removing someone else keeps the current pointer valid.
    pm.select("C");
    ASSERT_TRUE(pm.remove("A"));
    ASSERT_EQ(pm.current()->name, std::string("C"));
    ASSERT_EQ(pm.current_index(), 1);
    // Removing the current player leaves none (0x460c30).
    ASSERT_TRUE(pm.remove("C"));
    ASSERT_TRUE(pm.current() == nullptr);
    ASSERT_EQ(pm.count(), 1);
    ASSERT_TRUE(!pm.remove("C"));
    pm.clear_current();
    ASSERT_TRUE(pm.current() == nullptr);
}

TEST(player_manager_selection_after_remove_matches_0x40aef0) {
    // old selection 0 of 3 → after removal count 2 → 1
    ASSERT_EQ(PlayerManager::selection_after_remove(0, 2), 1);
    // old 1 of 3 → count 2 → sel 2 >= 2 → 0
    ASSERT_EQ(PlayerManager::selection_after_remove(1, 2), 0);
    // last of 2 → count 1 → sel 2 >= 1 → -1 → none
    ASSERT_EQ(PlayerManager::selection_after_remove(1, 1), -1);
    // only player removed → count 0 → none
    ASSERT_EQ(PlayerManager::selection_after_remove(0, 0), -1);
}

// ---------------------------------------------------------------------------
// High scores
// ---------------------------------------------------------------------------

TEST(highscore_compare_matches_0x4572f0) {
    HighScoreEntry a, b;
    a.score = 100; b.score = 200;
    ASSERT_TRUE(a.compare(b));      // 100 <= 200
    ASSERT_TRUE(!b.compare(a));
    b.score = 100;
    ASSERT_TRUE(a.compare(b));      // ties compare true
    a.rank = 2; b.rank = 5;
    ASSERT_TRUE(a.compare(b));      // forced order ignores score
    b.rank = 1;
    ASSERT_TRUE(!a.compare(b));
}

TEST(highscore_insert_orders_descending_and_trims) {
    HighScoreTable t;
    t.max_entries = 3;
    HighScoreEntry e;
    e.score = 50;  ASSERT_EQ(t.insert(e), 0);
    e.score = 100; ASSERT_EQ(t.insert(e), 0);
    e.score = 75;  ASSERT_EQ(t.insert(e), 1);
    ASSERT_EQ(t.size(), 3);
    ASSERT_EQ(t.entries[0].score, 100);
    ASSERT_EQ(t.entries[1].score, 75);
    ASSERT_EQ(t.entries[2].score, 50);
    ASSERT_EQ(t.last_index, 1);
    // Too low for a full table → -1 and nothing changes.
    e.score = 10;
    ASSERT_EQ(t.insert(e), -1);
    ASSERT_EQ(t.last_index, -1);
    ASSERT_EQ(t.size(), 3);
    ASSERT_TRUE(!t.qualifies(10));
    ASSERT_TRUE(t.qualifies(50));
    // Equal to the lowest → goes above it, lowest is dropped.
    e.score = 50;
    ASSERT_EQ(t.insert(e), 2);
    ASSERT_EQ(t.size(), 3);
    ASSERT_EQ(t.entries[2].score, 50);
    // A tie with the top entry lands on top.
    e.score = 100;
    ASSERT_EQ(t.insert(e), 0);
    ASSERT_EQ(t.entries[1].score, 100);
    ASSERT_EQ(t.entries[2].score, 75);
}

TEST(highscore_insert_uncapped_and_ascending) {
    HighScoreTable t;
    t.max_entries = 0;  // uncapped
    HighScoreEntry e;
    for (int s : {5, 1, 3, 9}) { e.score = s; t.insert(e); }
    ASSERT_EQ(t.size(), 4);
    ASSERT_EQ(t.entries[0].score, 9);
    ASSERT_EQ(t.entries[3].score, 1);
    ASSERT_TRUE(t.qualifies(-1000));
    HighScoreTable asc;
    asc.descending = false;
    for (int s : {5, 1, 3}) { e.score = s; asc.insert(e); }
    ASSERT_EQ(asc.entries[0].score, 1);
    ASSERT_EQ(asc.entries[2].score, 5);
}

TEST(highscore_fill_defaults_ten_entries_1000_to_10000) {
    Random rng(42);
    HighScoreTable t;
    t.fill_defaults(names(), rng);
    ASSERT_EQ(t.size(), 10);
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(t.entries[static_cast<std::size_t>(i)].score, 10000 - 1000 * i);
        ASSERT_TRUE(t.entries[static_cast<std::size_t>(i)].prefilled);
        ASSERT_TRUE(!t.entries[static_cast<std::size_t>(i)].name.empty());
    }
    // Deterministic for a given seed, and 10 distinct names when the
    // list has 27 candidates and 10 retries.
    Random rng2(42);
    HighScoreTable t2;
    t2.fill_defaults(names(), rng2);
    std::set<std::string> distinct;
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(t.entries[static_cast<std::size_t>(i)].name, t2.entries[static_cast<std::size_t>(i)].name);
        distinct.insert(t.entries[static_cast<std::size_t>(i)].name);
    }
    ASSERT_EQ(distinct.size(), 10u);
    // Empty name list: still ten entries, blank names, no throw.
    HighScoreTable t3;
    const std::vector<std::string> none;
    t3.fill_defaults(none, rng);
    ASSERT_EQ(t3.size(), 10);
    ASSERT_TRUE(t3.entries[0].name.empty());
}

TEST(highscore_manager_creates_and_fills_tables) {
    Random rng(1);
    HighScoreManager hm(names(), rng);
    HighScoreTable& t = hm.table(GameMode::Timed);
    ASSERT_EQ(t.size(), 10);
    ASSERT_EQ(t.max_entries, 10);
    ASSERT_TRUE(t.descending);
    ASSERT_EQ(hm.tables().size(), 1u);
    ASSERT_EQ(hm.tables().count("timed"), 1u);
    // Same key → same table, no refill while it holds 10 entries.
    const std::string first = t.entries[0].name;
    ASSERT_EQ(hm.table("timed").entries[0].name, first);
    // Loaded table with the wrong size is refilled on access (0x406e90).
    hm.tables()["blitz"].entries.clear();
    ASSERT_EQ(hm.table(GameMode::Blitz).size(), 10);
    ASSERT_EQ(std::string(mode_key(GameMode::WordOfTheDay)), std::string("wotd"));
    ASSERT_EQ(std::string(mode_key(GameMode::Crossword)), std::string("crossword"));
    ASSERT_EQ(std::string(mode_key(GameMode::Untimed)), std::string("untimed"));
}

TEST(highscore_manager_submit_matches_0x416440) {
    Random rng(1);
    HighScoreManager hm(names(), rng);
    ASSERT_EQ(hm.submit(GameMode::Untimed, "Ann", 0, "3"), -1);     // score 0 → nothing
    ASSERT_EQ(hm.submit(GameMode::Untimed, "Ann", 500, "3"), -1);   // below 1000
    ASSERT_EQ(hm.uid(), 1);
    const int idx = hm.submit(GameMode::Untimed, "Ann", 4500, "7");
    ASSERT_EQ(idx, 6);
    const HighScoreTable& t = hm.table(GameMode::Untimed);
    ASSERT_EQ(t.entries[6].name, std::string("Ann"));
    ASSERT_EQ(t.entries[6].extra, std::string("7"));
    ASSERT_TRUE(!t.entries[6].prefilled);
    ASSERT_EQ(t.entries[6].uid, 2);
    ASSERT_EQ(t.last_index, 6);
    ASSERT_EQ(t.size(), 10);
    ASSERT_EQ(hm.submit(GameMode::Untimed, "Ann", 20000, "9"), 0);
}

// ---------------------------------------------------------------------------
// Text format
// ---------------------------------------------------------------------------

TEST(profiles_text_round_trip) {
    PlayerManager pm;
    pm.add("Ann", 11);
    pm.add("Bo b", 22);
    pm.select("Ann");
    PlayerProfile& a = *pm.current();
    a.bag.index = 17;
    a.wod_day = 6600;
    a.max_word_length = 6;
    a.trophies[3] = 1;
    a.trophies[23] = 2;
    a.twists = 1234;
    a.solved = 55;
    a.hard_solved = 6;
    a.rounds_completed = {1, 2, 3, 4, 5};
    a.mode_flags = {true, false, true, false, true};

    const std::string text = serialize(pm);
    PlayerManager back;
    ASSERT_TRUE(parse(text, back));
    ASSERT_EQ(back.count(), 2);
    ASSERT_TRUE(back.current() != nullptr);
    ASSERT_EQ(back.current()->name, std::string("Ann"));
    const PlayerProfile& b = *back.current();
    ASSERT_EQ(b.bag.seed, 11u);
    ASSERT_EQ(b.bag.index, 17u);
    ASSERT_EQ(b.wod_day, 6600);
    ASSERT_EQ(b.max_word_length, 6);
    ASSERT_EQ(b.trophies[3], 1);
    ASSERT_EQ(b.trophies[23], 2);
    ASSERT_EQ(b.trophies[0], 0);
    ASSERT_EQ(b.twists, 1234);
    ASSERT_EQ(b.solved, 55);
    ASSERT_EQ(b.hard_solved, 6);
    ASSERT_EQ(b.rounds_completed[4], 5);
    ASSERT_TRUE(b.mode_flags[0] && !b.mode_flags[1] && b.mode_flags[4]);
    ASSERT_TRUE(b.returning);                 // loaded → "Welcome back"
    ASSERT_EQ(back.players()[1].name, std::string("Bo b"));
    ASSERT_EQ(serialize(back), text);         // stable
}

TEST(profiles_text_no_current_and_unknown_current) {
    PlayerManager pm;
    pm.add("Ann", 1);
    pm.clear_current();
    PlayerManager back;
    ASSERT_TRUE(parse(serialize(pm), back));
    ASSERT_TRUE(back.current() == nullptr);
    // currentplayer naming nobody known → no current (0x4622c0).
    const std::string text = "format=re-twist-tt2 profiles\nversion=1\ncurrent=Zed\n[player]\nname=Ann\n";
    ASSERT_TRUE(parse(text, back));
    ASSERT_EQ(back.count(), 1);
    ASSERT_TRUE(back.current() == nullptr);
}

TEST(profiles_text_rejects_garbage) {
    PlayerManager pm;
    pm.add("Keep", 1);
    const char* bad[] = {
        "",                                                        // no header
        "format=something else\nversion=1\n",
        "format=re-twist-tt2 profiles\nversion=2\n",
        "format=re-twist-tt2 profiles\nversion=1\nbogus=1\n",
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=\n",            // empty name
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\nseed=x\n",
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\nmax=9\n",
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\ntrophies=1,2\n",
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\nno equals sign\n",
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\n[player]\nname=A\n",  // duplicate
        "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=A\n[player]\nname=B\n[player]\nname=C\n"
        "[player]\nname=D\n[player]\nname=E\n[player]\nname=F\n",                     // six players
    };
    for (const char* text : bad) {
        ASSERT_TRUE(!parse(text, pm));
        ASSERT_EQ(pm.count(), 1);                 // untouched on failure
        ASSERT_EQ(pm.current()->name, std::string("Keep"));
    }
    // Comments, blank lines and CRLF are fine.
    PlayerManager ok;
    ASSERT_TRUE(parse("# c\r\nformat=re-twist-tt2 profiles\r\n\r\nversion=1\r\n[player]\r\nname=A\r\n", ok));
    ASSERT_EQ(ok.count(), 1);
}

TEST(highscores_text_round_trip) {
    Random rng(3);
    HighScoreManager hm(names(), rng);
    hm.submit(GameMode::Timed, "Ann Lee", 7777, "12");
    hm.submit(GameMode::WordOfTheDay, "Bob", 3000, "2026-10-13");
    hm.tables()["custom"].max_entries = 0;
    hm.tables()["custom"].descending = false;
    const std::string text = serialize(hm);

    Random rng2(99);
    HighScoreManager back(names(), rng2);
    ASSERT_TRUE(parse(text, back));
    ASSERT_EQ(back.uid(), hm.uid());
    ASSERT_EQ(back.tables().size(), 3u);
    const HighScoreTable& t = back.tables().at("timed");
    ASSERT_EQ(t.size(), 10);
    ASSERT_EQ(t.last_index, hm.tables().at("timed").last_index);
    const HighScoreEntry& e = t.entries[static_cast<std::size_t>(t.last_index)];
    ASSERT_EQ(e.name, std::string("Ann Lee"));
    ASSERT_EQ(e.score, 7777);
    ASSERT_EQ(e.extra, std::string("12"));
    ASSERT_TRUE(!e.prefilled);
    ASSERT_EQ(e.uid, 1);
    ASSERT_TRUE(t.entries[0].prefilled);
    ASSERT_EQ(back.tables().at("wotd").entries[static_cast<std::size_t>(back.tables().at("wotd").last_index)].extra,
              std::string("2026-10-13"));
    ASSERT_TRUE(!back.tables().at("custom").descending);
    ASSERT_EQ(back.tables().at("custom").max_entries, 0);
    ASSERT_EQ(serialize(back), text);
    // The loaded tables are used as-is (size 10 → no refill touches rng2).
    ASSERT_EQ(back.table(GameMode::Timed).entries[0].name, hm.table(GameMode::Timed).entries[0].name);
}

TEST(highscores_text_rejects_garbage) {
    Random rng(3);
    HighScoreManager hm(names(), rng);
    hm.table(GameMode::Timed);
    const char* bad[] = {
        "",
        "format=nope\nversion=1\n",
        "format=re-twist-tt2 highscores\nversion=1\nuid=x\n",
        "format=re-twist-tt2 highscores\nversion=1\n[table ]\n",
        "format=re-twist-tt2 highscores\nversion=1\n[table timed]\nentry=1\t2\n",   // too few fields
        "format=re-twist-tt2 highscores\nversion=1\n[table timed]\nentry=a\t1\t-1\t0\tn\t\n",
        "format=re-twist-tt2 highscores\nversion=1\n[table timed]\nwhat=1\n",
        "format=re-twist-tt2 highscores\nversion=1\nentry=1\t1\t-1\t0\tn\t\n",         // entry outside a table
    };
    for (const char* text : bad) {
        ASSERT_TRUE(!parse(text, hm));
        ASSERT_EQ(hm.tables().size(), 1u);
        ASSERT_EQ(hm.tables().at("timed").size(), 10);
    }
}

// ---------------------------------------------------------------------------
// ProfileStore
// ---------------------------------------------------------------------------

TEST(profile_store_save_and_load) {
    const auto dir = temp_dir("store");
    ProfileStore store(dir / "nested");          // directory is created on save
    PlayerManager pm;
    ASSERT_TRUE(store.load(pm));                 // missing file → ok, empty
    ASSERT_TRUE(pm.empty());

    pm.add("Ann", 5);
    pm.add("Bob", 6);
    ASSERT_TRUE(store.save(pm));
    ASSERT_TRUE(std::filesystem::exists(store.profiles_path()));
    ASSERT_TRUE(!std::filesystem::exists(store.profiles_path().string() + ".tmp"));

    Random rng(9);
    HighScoreManager hm(names(), rng);
    hm.submit(GameMode::Blitz, "Bob", 5555, "4");
    ASSERT_TRUE(store.save(hm));

    PlayerManager pm2;
    ASSERT_TRUE(store.load(pm2));
    ASSERT_EQ(pm2.count(), 2);
    ASSERT_EQ(pm2.current()->name, std::string("Bob"));
    ASSERT_TRUE(pm2.current()->returning);

    Random rng2(9);
    HighScoreManager hm2(names(), rng2);
    ASSERT_TRUE(store.load(hm2));
    ASSERT_EQ(hm2.tables().at("blitz").entries[static_cast<std::size_t>(hm2.tables().at("blitz").last_index)].score, 5555);

    // Corrupt file → load fails and leaves the manager untouched.
    {
        std::ofstream out(store.profiles_path(), std::ios::binary | std::ios::trunc);
        out << "format=re-twist-tt2 profiles\nversion=1\n[player]\nname=\n";
    }
    ASSERT_TRUE(!store.load(pm2));
    ASSERT_EQ(pm2.count(), 2);

    std::filesystem::remove_all(dir);
}

TEST(default_save_dir_follows_platform_convention) {
    using Env = std::map<std::string, std::string>;
    auto make = [](Env e) {
        return [e = std::move(e)](const char* n) -> const char* {
            auto it = e.find(n);
            return it == e.end() ? nullptr : it->second.c_str();
        };
    };
    const std::string app = SAVE_DIR_NAME;
#if defined(_WIN32)
    ASSERT_EQ(default_save_dir_with(make({{"APPDATA", "C:\\U\\Roaming"}, {"LOCALAPPDATA", "C:\\U\\Local"}})).string(),
              (std::filesystem::path("C:\\U\\Roaming") / app).string());
    ASSERT_EQ(default_save_dir_with(make({{"LOCALAPPDATA", "C:\\U\\Local"}})).string(),
              (std::filesystem::path("C:\\U\\Local") / app).string());
    ASSERT_EQ(default_save_dir_with(make({{"USERPROFILE", "C:\\U"}})).string(),
              (std::filesystem::path("C:\\U") / app).string());
#elif defined(__APPLE__)
    ASSERT_EQ(default_save_dir_with(make({{"HOME", "/Users/me"}})).string(),
              std::string("/Users/me/Library/Application Support/") + app);
#else
    ASSERT_EQ(default_save_dir_with(make({{"XDG_DATA_HOME", "/data"}, {"HOME", "/home/me"}})).string(),
              std::string("/data/") + app);
    ASSERT_EQ(default_save_dir_with(make({{"HOME", "/home/me"}})).string(),
              std::string("/home/me/.local/share/") + app);
#endif
    // Nothing set → empty, so the caller can fall back (e.g. to cwd).
    ASSERT_TRUE(default_save_dir_with(make({})).empty());
    // The real environment on a dev box yields a non-empty path.
    ASSERT_TRUE(!default_save_dir().empty());
}

TEST(profile_store_save_fails_on_unwritable_dir) {
    const auto dir = temp_dir("file_as_dir");
    std::filesystem::create_directories(dir);
    // A regular file where the directory should be: create_directories fails.
    { std::ofstream f(dir / "blocker"); f << "x"; }
    ProfileStore store(dir / "blocker");
    PlayerManager pm;
    pm.add("Ann", 1);
    ASSERT_TRUE(!store.save(pm));
    std::filesystem::remove_all(dir);
}
