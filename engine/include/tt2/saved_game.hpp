// A saved classic game: the snapshot the original writes when the
// player leaves a running round and reads back when the same mode is
// started again (docs/reverse-engineering/game-scene.md §7).
//
//   0x4242a0  scene start: file "<mode>" under the player's savegames
//             folder exists -> load it (0x42a460), else round 1.
//   0x427b20  scene leave: round still running -> save (0x429dd0).
//   0x429dd0  keys written: WORD, ROUND, TIME, LONGESTWORD, SCORE,
//             DAY, FAILCOUNT and a WORD_LIST of ANAGRAM/WORD entries,
//             one per found slot.
//   0x42a460  load: round 1 is rebuilt from WORD, then ROUND / TIME /
//             LONGESTWORD / SCORE / DAY / FAILCOUNT overwrite the
//             counters and every WORD_LIST entry is placed as a guess;
//             the file is deleted afterwards (0x461840).
//
// The text format here is our own (same style as profile_store.hpp);
// the keys mirror the original's names.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "tt2/profile.hpp"

namespace tt2 {

// Mode names the original uses as save-file names (0x4242a0,
// CONFIRMED for the three classic modes and Blitz (0x4110f0); Crossword
// INFERRED).
inline const char* save_mode_name(GameMode m) noexcept {
    switch (m) {
        case GameMode::Timed:        return "CLASSIC_TIMED";
        case GameMode::Untimed:      return "CLASSIC_UNTIMED";
        case GameMode::WordOfTheDay: return "WORD_OF_THE_DAY";
        case GameMode::Blitz:        return "BLITZ";
        case GameMode::Crossword:    return "CRISSCROSS";
    }
    return "CLASSIC_UNTIMED";
}

struct SavedGame {
    std::string mode;                      // save_mode_name()
    std::string word;                      // WORD: the rack (lower case)
    int round = 1;                         // ROUND
    int time_ms = 0;                       // TIME: milliseconds left (0 in Untimed)
    bool qualified = false;                // LONGESTWORD
    std::int32_t score = 0;                // SCORE
    std::int32_t day = -1;                 // DAY: Word-of-the-Day number, -1 otherwise
    int fail_count = 0;                    // FAILCOUNT: misses so far (0..2)
    std::vector<std::string> found_words;  // WORD_LIST/ANAGRAM/WORD, board order
    // Blitz (0x416af0): GAME_LIST/GAME_DATA, one per word: WORD and COMPLETED.
    // Only ROUND, TIME and SCORE of the keys above are used with it.
    struct BlitzWord {
        std::string word;
        bool completed = false;
    };
    std::vector<BlitzWord> blitz_words;

    bool blitz() const { return mode == "BLITZ"; }

    // A snapshot the scene can rebuild a round from.
    bool valid() const noexcept {
        if (blitz()) {
            if (round < 1 || time_ms < 0 || blitz_words.empty() || blitz_words.size() > 10) return false;
            for (const auto& w : blitz_words) {
                if (w.word.size() < 5 || w.word.size() > 8) return false;
                for (char c : w.word) if (c < 'a' || c > 'z') return false;
            }
            return true;
        }
        if (word.size() < 3 || word.size() > 8) return false;
        for (char c : word) if (c < 'a' || c > 'z') return false;
        if (round < 1 || time_ms < 0 || fail_count < 0 || fail_count > 2) return false;
        for (const auto& w : found_words) {
            if (w.empty() || w.size() > word.size()) return false;
            for (char c : w) if (c < 'a' || c > 'z') return false;
        }
        return true;
    }
};

inline std::string serialize(const SavedGame& g) {
    std::ostringstream os;
    os << "format=re-twist-tt2 savegame\nversion=1\n"
       << "mode=" << g.mode << '\n'
       << "word=" << g.word << '\n'
       << "round=" << g.round << '\n'
       << "time=" << g.time_ms << '\n'
       << "longestword=" << (g.qualified ? 1 : 0) << '\n'
       << "score=" << g.score << '\n'
       << "day=" << g.day << '\n'
       << "failcount=" << g.fail_count << '\n';
    for (const auto& w : g.found_words) os << "found=" << w << '\n';
    for (const auto& w : g.blitz_words) os << "blitzword=" << w.word << ',' << (w.completed ? 1 : 0) << '\n';
    return os.str();
}

// Returns false on a malformed document; `out` is then left untouched.
inline bool parse(std::string_view text, SavedGame& out) {
    SavedGame g;
    bool saw_format = false, saw_word = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, eq), val = line.substr(eq + 1);
        std::int64_t n = 0;
        if (key == "format") { if (val != "re-twist-tt2 savegame") return false; saw_format = true; }
        else if (!saw_format) return false;
        else if (key == "version") { if (!profile_text::parse_int(val, n) || n != 1) return false; }
        else if (key == "mode") g.mode = std::string(val);
        else if (key == "word") { g.word = std::string(val); saw_word = true; }
        else if (key == "round") { if (!profile_text::parse_int(val, n)) return false; g.round = static_cast<int>(n); }
        else if (key == "time") { if (!profile_text::parse_int(val, n)) return false; g.time_ms = static_cast<int>(n); }
        else if (key == "longestword") { if (!profile_text::parse_int(val, n) || (n != 0 && n != 1)) return false; g.qualified = n == 1; }
        else if (key == "score") { if (!profile_text::parse_int(val, n)) return false; g.score = static_cast<std::int32_t>(n); }
        else if (key == "day") { if (!profile_text::parse_int(val, n)) return false; g.day = static_cast<std::int32_t>(n); }
        else if (key == "failcount") { if (!profile_text::parse_int(val, n)) return false; g.fail_count = static_cast<int>(n); }
        else if (key == "found") g.found_words.emplace_back(val);
        else if (key == "blitzword") {
            const std::size_t comma = val.rfind(',');
            if (comma == std::string_view::npos) return false;
            const std::string_view flag = val.substr(comma + 1);
            if (flag != "0" && flag != "1") return false;
            g.blitz_words.push_back(SavedGame::BlitzWord{std::string(val.substr(0, comma)), flag == "1"});
        }
        else return false;
    }
    if (!saw_format || (!saw_word && !g.blitz()) || !g.valid()) return false;
    out = std::move(g);
    return true;
}

}  // namespace tt2
