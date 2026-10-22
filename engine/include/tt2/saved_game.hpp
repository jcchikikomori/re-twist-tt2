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
//   0x4203f0  Crossword save (crossword.md §5.5): ROUND, TIME (always
//             0), SCORE, WORD (the root), the grid and a CROSS_LIST of
//             played words and scored flags; load 0x420fa0.
//
// The text format here is our own (same style as profile_store.hpp);
// the keys mirror the original's names. Crossword stores its placements
// as `xword=` lines instead of the exe's table (D-8, D-10); the format
// stays version 1, because only CROSSWORD saves have them (D-9).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/word_tree.hpp"

namespace tt2 {

// Mode names the original uses as save-file names, CONFIRMED: the three
// classic modes (0x4242a0), Blitz (0x4110f0) and Crossword (0x41a110
// pushes "CROSSWORD" at 0x590254; the string "CRISSCROSS" has no xref,
// crossword.md §3.1, OI-11).
inline const char* save_mode_name(GameMode m) noexcept {
    switch (m) {
        case GameMode::Timed:        return "CLASSIC_TIMED";
        case GameMode::Untimed:      return "CLASSIC_UNTIMED";
        case GameMode::WordOfTheDay: return "WORD_OF_THE_DAY";
        case GameMode::Blitz:        return "BLITZ";
        case GameMode::Crossword:    return "CROSSWORD";
    }
    return "CLASSIC_UNTIMED";
}

// The bounds of a Crossword save, taken from round_crossword.hpp (not
// included here, as the Blitz bounds are not taken from
// round_lightning.hpp): CrosswordGrid's 20 x 11 cell storage, the root
// plus Config::max_words placements, a 5..8 letter root and 3..8 letter
// words (the common list's sub-anagrams).
inline constexpr int SAVED_CROSSWORD_A_CELLS = 20;
inline constexpr int SAVED_CROSSWORD_B_CELLS = 11;
inline constexpr std::size_t SAVED_CROSSWORD_MAX_WORDS = 11;

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
    // Crossword: one per placement in commit order, root first, written
    // as `xword=<answer>,<a>,<b>,<h|v>,<crossing>,<scored>,<played>`.
    // WORD is the root; ROUND, TIME (always 0) and SCORE are used with it.
    struct CrosswordWord {
        std::string answer;       // the placed word
        int a = 0;                // first cell (CrosswordGrid coordinates)
        int b = 0;
        bool horizontal = true;
        int crossing = -1;        // WordPlacement::crossing: -1 for the root
        bool scored = false;      // points paid; a take-back keeps it
        std::string played;       // the player's word, empty while unsolved
    };
    std::vector<CrosswordWord> crossword_words;

    bool blitz() const { return mode == "BLITZ"; }
    bool crossword() const { return mode == save_mode_name(GameMode::Crossword); }

    // A snapshot the scene can rebuild a round from. Only the bounds are
    // checked; whether a Crossword grid fits is CrosswordRound::restore's.
    bool valid() const noexcept {
        if (crossword()) return valid_crossword();
        if (!crossword_words.empty()) return false;   // xword= belongs to CROSSWORD saves
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

private:
    // The data contract (wotd-crossword.md, "Crossword save keys"): 1..11
    // placements; the root in [a-z]{5,8}, equal to WORD, horizontal, with
    // no crossing; every word [a-z]{3,8} and spellable from the root, a in
    // 0..19, b in 0..10, crossing 0..len-1; a played word as long as its
    // answer, spellable from the root and scored; no found= or blitzword=.
    bool valid_crossword() const noexcept {
        if (round < 1 || time_ms < 0 || !found_words.empty() || !blitz_words.empty()) return false;
        if (crossword_words.empty() || crossword_words.size() > SAVED_CROSSWORD_MAX_WORDS) return false;
        if (!lower_case(word, 5, 8)) return false;
        const CrosswordWord& root = crossword_words.front();
        if (root.answer != word || !root.horizontal || root.crossing != -1) return false;
        for (std::size_t i = 0; i < crossword_words.size(); ++i) {
            const CrosswordWord& w = crossword_words[i];
            const int len = static_cast<int>(w.answer.size());
            if (!lower_case(w.answer, 3, 8) || !spellable_from(w.answer, word)) return false;
            if (w.a < 0 || w.a >= SAVED_CROSSWORD_A_CELLS || w.b < 0 || w.b >= SAVED_CROSSWORD_B_CELLS) return false;
            if (i > 0 && (w.crossing < 0 || w.crossing >= len)) return false;
            if (w.played.empty()) continue;
            if (!w.scored || w.played.size() != w.answer.size() || !spellable_from(w.played, word)) return false;
        }
        return true;
    }

    static bool lower_case(const std::string& w, std::size_t min_len, std::size_t max_len) noexcept {
        if (w.size() < min_len || w.size() > max_len) return false;
        for (char c : w) if (c < 'a' || c > 'z') return false;
        return true;
    }
};

namespace saved_game_detail {
// `<answer>,<a>,<b>,<h|v>,<crossing>,<scored 0|1>,<played>`: exactly seven
// fields, `played` may be empty. Numbers must fit an int, so a huge value
// cannot wrap into range; the ranges themselves are valid()'s.
inline bool parse_crossword_word(std::string_view val, SavedGame::CrosswordWord& out) {
    constexpr std::size_t FIELDS = 7;
    std::array<std::string_view, FIELDS> f;
    std::size_t start = 0;
    for (std::size_t i = 0; i < FIELDS; ++i) {
        const std::size_t comma = val.find(',', start);
        const bool last = i + 1 == FIELDS;
        if (last != (comma == std::string_view::npos)) return false;   // too few or too many fields
        f[i] = val.substr(start, last ? std::string_view::npos : comma - start);
        start = comma + 1;
    }
    auto to_int = [](std::string_view s, int& n) {
        std::int64_t v = 0;
        if (!profile_text::parse_int(s, v)) return false;
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
        n = static_cast<int>(v);
        return true;
    };
    SavedGame::CrosswordWord w;
    w.answer = std::string(f[0]);
    if (!to_int(f[1], w.a) || !to_int(f[2], w.b) || !to_int(f[4], w.crossing)) return false;
    if (f[3] != "h" && f[3] != "v") return false;
    if (f[5] != "0" && f[5] != "1") return false;
    w.horizontal = f[3] == "h";
    w.scored     = f[5] == "1";
    w.played     = std::string(f[6]);
    out = std::move(w);
    return true;
}
}  // namespace saved_game_detail

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
    for (const auto& w : g.crossword_words) {
        os << "xword=" << w.answer << ',' << w.a << ',' << w.b << ',' << (w.horizontal ? 'h' : 'v') << ','
           << w.crossing << ',' << (w.scored ? 1 : 0) << ',' << w.played << '\n';
    }
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
        else if (key == "xword") {
            SavedGame::CrosswordWord w;
            if (!saved_game_detail::parse_crossword_word(val, w)) return false;
            g.crossword_words.push_back(std::move(w));
        }
        else return false;
    }
    if (!saw_format || (!saw_word && !g.blitz()) || !g.valid()) return false;
    out = std::move(g);
    return true;
}

}  // namespace tt2
