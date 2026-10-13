#pragma once

// Player profiles, progression and high-score tables.
//
// Reconstructed from TextTwist2.exe (see docs/reverse-engineering/
// profiles.md for every address quoted below). This header is pure:
// no files, no clock, no platform calls. `profile_store.hpp` adds the
// plain-text persistence on top.

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/random.hpp"

namespace tt2 {

// Player list limits. 5 is checked at 0x40ad00 / 0x40aa40, 15 is the
// `maxlength` of the INPUT_NAME box in dialogs.xml.
inline constexpr int MAX_PLAYERS = 5;
inline constexpr std::size_t MAX_PLAYER_NAME = 15;

// 24 trophy slots (ACHIEVEMENT/0..23 at 0x40f3d0), 5 game modes.
inline constexpr std::size_t TROPHY_COUNT = 24;
inline constexpr std::size_t MODE_COUNT = 5;

// Default `MAX` (maximum word length) for a fresh player (0x40f060).
inline constexpr int DEFAULT_MAX_WORD_LENGTH = 8;

// The five high-score tables, keyed exactly as the results dialog
// does at 0x4068d0.
enum class GameMode { Timed = 0, Untimed = 1, Blitz = 2, Crossword = 3, WordOfTheDay = 4 };

inline const char* mode_key(GameMode m) noexcept {
    switch (m) {
        case GameMode::Timed:        return "timed";
        case GameMode::Untimed:      return "untimed";
        case GameMode::Blitz:        return "blitz";
        case GameMode::Crossword:    return "crossword";
        case GameMode::WordOfTheDay: return "wotd";
    }
    return "untimed";
}

// Word-of-the-Day day number as the menu computes it at 0x42f1a0:
// local time (unix seconds minus the timezone offset) in days, minus
// the 2008-09-03 epoch (bag.hpp::WOTD_EPOCH_DAY).
inline std::int32_t wotd_day_from_unix(std::int64_t unix_seconds,
                                       std::int32_t tz_offset_minutes = 0) noexcept {
    const std::int64_t local = unix_seconds - static_cast<std::int64_t>(tz_offset_minutes) * 60;
    return static_cast<std::int32_t>(local / 86'400 - WOTD_EPOCH_DAY);
}

// Number of UTF-8 code points, the way 0x45fa60 counts a name: every
// byte that is not a continuation byte (10xxxxxx) counts.
inline std::size_t utf8_length(std::string_view s) noexcept {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0u) != 0x80u) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Player profile (TextTwist2Player)
// ---------------------------------------------------------------------------

struct PlayerProfile {
    std::string name;
    BagState bag;                              // SEED / INDEX
    std::int32_t wod_day = -1;                 // WOD: last Word-of-the-Day day played
    int max_word_length = DEFAULT_MAX_WORD_LENGTH;  // MAX: 6 / 7 / 8
    std::array<std::int32_t, TROPHY_COUNT> trophies{};  // ACHIEVEMENT/0..23
    std::int32_t twists = 0;                   // TRACKER/TWISTS
    std::int32_t solved = 0;                   // TRACKER/SOLVED
    std::int32_t hard_solved = 0;              // TRACKER/HARD_SOLVED
    std::array<std::int32_t, MODE_COUNT> rounds_completed{};  // TRACKER/0..4 (int)
    std::array<bool, MODE_COUNT> mode_flags{};               // TRACKER/0..4 (bool)
    // Not persisted. Set on a profile that was loaded from storage;
    // drives "Welcome back" instead of "Welcome" (0x42f1a0, +0x2c).
    bool returning = false;

    // 0x40f060: SEED = timeGetTime(), everything else zero / -1 / 8.
    static PlayerProfile create(std::string player_name, std::uint32_t seed_millis) {
        PlayerProfile p;
        p.name = std::move(player_name);
        p.bag = BagState{seed_millis, 0};
        return p;
    }

    bool played_wod_today(std::int32_t today) const noexcept { return wod_day == today; }
    bool has_trophy(std::size_t slot) const noexcept {
        return slot < TROPHY_COUNT && trophies[slot] != 0;
    }
};

// ---------------------------------------------------------------------------
// Player manager
// ---------------------------------------------------------------------------

enum class AddPlayerResult { Added, EmptyName, TooLong, InvalidChars, Exists, Full };

inline const char* to_string(AddPlayerResult r) noexcept {
    switch (r) {
        case AddPlayerResult::Added:        return "Added";
        case AddPlayerResult::EmptyName:    return "EmptyName";
        case AddPlayerResult::TooLong:      return "TooLong";
        case AddPlayerResult::InvalidChars: return "InvalidChars";
        case AddPlayerResult::Exists:       return "Exists";
        case AddPlayerResult::Full:         return "Full";
    }
    return "?";
}

class PlayerManager {
public:
    const std::vector<PlayerProfile>& players() const noexcept { return players_; }
    int count() const noexcept { return static_cast<int>(players_.size()); }
    bool full() const noexcept { return count() >= MAX_PLAYERS; }
    bool empty() const noexcept { return players_.empty(); }

    // 0x460d10: exact, case-sensitive compare.
    bool exists(std::string_view name) const noexcept { return find(name) >= 0; }

    int find(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < players_.size(); ++i) {
            if (players_[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    // Current player, or nullptr. The index survives add/remove.
    PlayerProfile* current() noexcept {
        return current_ >= 0 && current_ < count() ? &players_[static_cast<std::size_t>(current_)] : nullptr;
    }
    const PlayerProfile* current() const noexcept {
        return current_ >= 0 && current_ < count() ? &players_[static_cast<std::size_t>(current_)] : nullptr;
    }
    int current_index() const noexcept { return current_; }

    // Name validation as the name-input OK handler does it (0x4088c0),
    // plus the port's separator rule (no tab / newline).
    static AddPlayerResult validate_name(std::string_view name) noexcept {
        if (name.empty()) return AddPlayerResult::EmptyName;
        if (utf8_length(name) > MAX_PLAYER_NAME) return AddPlayerResult::TooLong;
        for (char c : name) {
            if (c == '\t' || c == '\n' || c == '\r') return AddPlayerResult::InvalidChars;
        }
        return AddPlayerResult::Added;
    }

    // 0x4088c0 → 0x460bb0 + 0x460e60: validate, reject duplicates and
    // a full list, append, make current.
    AddPlayerResult add(std::string name, std::uint32_t seed_millis) {
        const AddPlayerResult v = validate_name(name);
        if (v != AddPlayerResult::Added) return v;
        if (exists(name)) return AddPlayerResult::Exists;
        if (full()) return AddPlayerResult::Full;
        players_.push_back(PlayerProfile::create(std::move(name), seed_millis));
        current_ = count() - 1;
        return AddPlayerResult::Added;
    }

    // Used by the loader: no validation beyond the cap, no reselect.
    bool add_loaded(PlayerProfile p) {
        if (full() || exists(p.name)) return false;
        p.returning = true;
        players_.push_back(std::move(p));
        return true;
    }

    // 0x460e60. Returns false when the name is unknown (current unchanged).
    bool select(std::string_view name) noexcept {
        const int i = find(name);
        if (i < 0) return false;
        current_ = i;
        return true;
    }

    void clear_current() noexcept { current_ = -1; }

    // 0x460c30: drop the player; a removed current player leaves no
    // current player. Returns false when the name is unknown.
    bool remove(std::string_view name) {
        const int i = find(name);
        if (i < 0) return false;
        players_.erase(players_.begin() + i);
        if (current_ == i) current_ = -1;
        else if (current_ > i) --current_;
        return true;
    }

    // The PlayerSelect dialog's selection after a removal (0x40aef0):
    // `old + 1`, clamped to `count - 2`, and -1 when that is negative.
    static int selection_after_remove(int old_selection, int count_after) noexcept {
        int sel = old_selection + 1;
        if (count_after <= sel) sel = count_after - 2;
        return sel < 0 ? -1 : sel;
    }

private:
    std::vector<PlayerProfile> players_;
    int current_ = -1;
};

// ---------------------------------------------------------------------------
// High scores
// ---------------------------------------------------------------------------

inline constexpr int DEFAULT_MAX_ENTRIES = 10;
inline constexpr int DEFAULT_FILL_COUNT = 10;
inline constexpr std::int32_t DEFAULT_FILL_STEP = 1000;
inline constexpr int DEFAULT_FILL_NAME_RETRIES = 10;

struct HighScoreEntry {
    std::string name;
    std::int32_t score = 0;
    std::string extra;           // round number ("Round:") or date ("Date:")
    bool prefilled = true;       // isprefilledscore; the ctor default at 0x456cf0
    std::int32_t rank = -1;      // +0x38 forced-order override, -1 = by score
    std::int32_t uid = 0;        // +0x34 manager counter at creation

    // 0x4572f0.
    bool compare(const HighScoreEntry& other) const noexcept {
        if (rank < 0) return score <= other.score;
        return rank < other.rank;
    }
};

class HighScoreTable {
public:
    bool descending = true;
    int max_entries = DEFAULT_MAX_ENTRIES;
    int last_index = -1;        // `lastscore`: index of the last inserted entry, or -1
    std::vector<HighScoreEntry> entries;

    int size() const noexcept { return static_cast<int>(entries.size()); }

    // 0x457cd0. Returns the index the entry landed on, or -1.
    int insert(HighScoreEntry e) {
        const bool capped = max_entries > 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].compare(e) == descending) {
                entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(i), std::move(e));
                if (capped && size() > max_entries) entries.pop_back();
                last_index = static_cast<int>(i);
                return last_index;
            }
        }
        if (capped && size() >= max_entries) {
            last_index = -1;
            return -1;
        }
        entries.push_back(std::move(e));
        last_index = size() - 1;
        return last_index;
    }

    // Would `insert` place this score? Mirrors the loop without mutating.
    bool qualifies(std::int32_t score) const noexcept {
        HighScoreEntry probe;
        probe.score = score;
        for (const auto& e : entries) {
            if (e.compare(probe) == descending) return true;
        }
        return !(max_entries > 0 && size() >= max_entries);
    }

    // 0x406f70: ten entries, scores 1000..10000, names drawn from the
    // list with the Java LCG, retrying up to 10 times on a repeat.
    void fill_defaults(const std::vector<std::string>& names, Random& rng, std::int32_t uid = 0) {
        std::set<std::int32_t> used;
        std::int32_t score = DEFAULT_FILL_STEP;
        for (int k = 0; k < DEFAULT_FILL_COUNT; ++k, score += DEFAULT_FILL_STEP) {
            std::int32_t pick = 0;
            if (!names.empty()) {
                int tries = DEFAULT_FILL_NAME_RETRIES;
                do {
                    pick = rng.rand(0, static_cast<std::int32_t>(names.size()));
                    if (tries < 1) break;
                    --tries;
                } while (!used.insert(pick).second);
            }
            HighScoreEntry e;
            e.name = names.empty() ? std::string() : names[static_cast<std::size_t>(pick)];
            e.score = score;
            e.prefilled = true;
            e.uid = uid;
            insert(std::move(e));
        }
    }
};

class HighScoreManager {
public:
    // `default_names` is CTEXT_HIGHSCORE_NAMES; `rng` is the global
    // Java LCG (`random` command). Both are non-owning, like the rest
    // of the engine: keep them alive for as long as the manager.
    HighScoreManager(const std::vector<std::string>& default_names, Random& rng)
        : names_(&default_names), rng_(&rng) {}

    std::int32_t uid() const noexcept { return uid_; }
    void set_uid(std::int32_t u) noexcept { uid_ = u; }

    // 0x406e90: create on demand, refill whenever the size is not 10.
    HighScoreTable& table(GameMode mode) { return table(mode_key(mode)); }
    HighScoreTable& table(const std::string& key) {
        HighScoreTable& t = tables_[key];
        if (t.size() != DEFAULT_FILL_COUNT) t.fill_defaults(*names_, *rng_, uid_);
        return t;
    }

    const std::map<std::string, HighScoreTable>& tables() const noexcept { return tables_; }
    std::map<std::string, HighScoreTable>& tables() noexcept { return tables_; }

    // 0x416440: build the entry for a finished round and insert it.
    // Returns the index, or -1 when the score did not make the table.
    int submit(GameMode mode, const std::string& player, std::int32_t score, std::string extra) {
        if (score <= 0) return -1;
        HighScoreEntry e;
        e.name = player;
        e.score = score;
        e.extra = std::move(extra);
        e.prefilled = false;
        e.uid = ++uid_;
        return table(mode).insert(std::move(e));
    }

private:
    const std::vector<std::string>* names_;
    Random* rng_;
    std::int32_t uid_ = 0;
    std::map<std::string, HighScoreTable> tables_;
};

// ---------------------------------------------------------------------------
// Text serialization (the port's own format, see profiles.md §5)
// ---------------------------------------------------------------------------

namespace profile_text {

inline std::string join_ints(const std::int32_t* v, std::size_t n) {
    std::ostringstream os;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) os << ',';
        os << v[i];
    }
    return os.str();
}

inline bool split_ints(std::string_view s, std::int32_t* out, std::size_t n) {
    std::size_t i = 0, pos = 0;
    while (i < n) {
        const std::size_t comma = s.find(',', pos);
        const std::string_view tok = s.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        if (tok.empty()) return false;
        try {
            std::size_t used = 0;
            const long v = std::stol(std::string(tok), &used);
            if (used != tok.size()) return false;
            out[i++] = static_cast<std::int32_t>(v);
        } catch (...) {
            return false;
        }
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return i == n;
}

inline bool parse_int(std::string_view s, std::int64_t& out) {
    if (s.empty()) return false;
    try {
        std::size_t used = 0;
        out = std::stoll(std::string(s), &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace profile_text

// One `[player]` block per profile; `current=` names the selected one.
inline std::string serialize(const PlayerManager& pm) {
    std::ostringstream os;
    os << "format=re-twist-tt2 profiles\nversion=1\n";
    if (const auto* c = pm.current()) os << "current=" << c->name << '\n';
    for (const auto& p : pm.players()) {
        os << "[player]\n"
           << "name=" << p.name << '\n'
           << "seed=" << p.bag.seed << '\n'
           << "index=" << p.bag.index << '\n'
           << "wod=" << p.wod_day << '\n'
           << "max=" << p.max_word_length << '\n'
           << "trophies=" << profile_text::join_ints(p.trophies.data(), p.trophies.size()) << '\n'
           << "twists=" << p.twists << '\n'
           << "solved=" << p.solved << '\n'
           << "hard_solved=" << p.hard_solved << '\n'
           << "rounds=" << profile_text::join_ints(p.rounds_completed.data(), p.rounds_completed.size()) << '\n'
           << "flags=";
        for (std::size_t i = 0; i < p.mode_flags.size(); ++i) os << (i ? "," : "") << (p.mode_flags[i] ? 1 : 0);
        os << '\n';
    }
    return os.str();
}

// Returns false on a malformed document; `pm` is then left untouched.
inline bool parse(std::string_view text, PlayerManager& pm) {
    PlayerManager out;
    std::string current;
    bool in_player = false;
    PlayerProfile cur;
    auto flush = [&]() -> bool {
        if (!in_player) return true;
        if (PlayerManager::validate_name(cur.name) != AddPlayerResult::Added) return false;
        if (!out.add_loaded(cur)) return false;
        cur = PlayerProfile{};
        in_player = false;
        return true;
    };
    std::size_t pos = 0;
    bool header_seen = false;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        if (line == "[player]") {
            if (!flush()) return false;
            in_player = true;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, eq);
        const std::string_view val = line.substr(eq + 1);
        std::int64_t n = 0;
        if (!in_player) {
            if (key == "format") { header_seen = (val == "re-twist-tt2 profiles"); if (!header_seen) return false; }
            else if (key == "version") { if (!profile_text::parse_int(val, n) || n != 1) return false; }
            else if (key == "current") current = std::string(val);
            else return false;
            continue;
        }
        if (key == "name") cur.name = std::string(val);
        else if (key == "seed") { if (!profile_text::parse_int(val, n) || n < 0 || n > 0xFFFFFFFFLL) return false; cur.bag.seed = static_cast<std::uint32_t>(n); }
        else if (key == "index") { if (!profile_text::parse_int(val, n) || n < 0 || n > 0xFFFFFFFFLL) return false; cur.bag.index = static_cast<std::uint32_t>(n); }
        else if (key == "wod") { if (!profile_text::parse_int(val, n)) return false; cur.wod_day = static_cast<std::int32_t>(n); }
        else if (key == "max") { if (!profile_text::parse_int(val, n) || n < 6 || n > 8) return false; cur.max_word_length = static_cast<int>(n); }
        else if (key == "trophies") { if (!profile_text::split_ints(val, cur.trophies.data(), cur.trophies.size())) return false; }
        else if (key == "twists") { if (!profile_text::parse_int(val, n)) return false; cur.twists = static_cast<std::int32_t>(n); }
        else if (key == "solved") { if (!profile_text::parse_int(val, n)) return false; cur.solved = static_cast<std::int32_t>(n); }
        else if (key == "hard_solved") { if (!profile_text::parse_int(val, n)) return false; cur.hard_solved = static_cast<std::int32_t>(n); }
        else if (key == "rounds") { if (!profile_text::split_ints(val, cur.rounds_completed.data(), cur.rounds_completed.size())) return false; }
        else if (key == "flags") {
            std::array<std::int32_t, MODE_COUNT> f{};
            if (!profile_text::split_ints(val, f.data(), f.size())) return false;
            for (std::size_t i = 0; i < MODE_COUNT; ++i) cur.mode_flags[i] = f[i] != 0;
        }
        else return false;
    }
    if (!header_seen) return false;
    if (!flush()) return false;
    if (!current.empty()) out.select(current);  // unknown name → no current (0x4622c0)
    pm = std::move(out);
    return true;
}

// `[table <key>]` blocks; entries are tab-separated so names stay free-form.
inline std::string serialize(const HighScoreManager& hm) {
    std::ostringstream os;
    os << "format=re-twist-tt2 highscores\nversion=1\nuid=" << hm.uid() << '\n';
    for (const auto& [key, t] : hm.tables()) {
        os << "[table " << key << "]\n"
           << "descending=" << (t.descending ? 1 : 0) << '\n'
           << "maxentries=" << t.max_entries << '\n'
           << "lastscore=" << t.last_index << '\n';
        for (const auto& e : t.entries) {
            os << "entry=" << e.score << '\t' << (e.prefilled ? 1 : 0) << '\t' << e.rank << '\t'
               << e.uid << '\t' << e.name << '\t' << e.extra << '\n';
        }
    }
    return os.str();
}

inline bool parse(std::string_view text, HighScoreManager& hm) {
    std::map<std::string, HighScoreTable> tables;
    std::int32_t uid = 0;
    HighScoreTable* cur = nullptr;
    bool header_seen = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        if (line.size() > 8 && line.substr(0, 7) == "[table " && line.back() == ']') {
            const std::string key(line.substr(7, line.size() - 8));
            if (key.empty()) return false;
            cur = &tables[key];
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, eq);
        const std::string_view val = line.substr(eq + 1);
        std::int64_t n = 0;
        if (!cur) {
            if (key == "format") { header_seen = (val == "re-twist-tt2 highscores"); if (!header_seen) return false; }
            else if (key == "version") { if (!profile_text::parse_int(val, n) || n != 1) return false; }
            else if (key == "uid") { if (!profile_text::parse_int(val, n)) return false; uid = static_cast<std::int32_t>(n); }
            else return false;
            continue;
        }
        if (key == "descending") { if (!profile_text::parse_int(val, n)) return false; cur->descending = n != 0; }
        else if (key == "maxentries") { if (!profile_text::parse_int(val, n)) return false; cur->max_entries = static_cast<int>(n); }
        else if (key == "lastscore") { if (!profile_text::parse_int(val, n)) return false; cur->last_index = static_cast<int>(n); }
        else if (key == "entry") {
            std::array<std::string_view, 6> f{};
            std::size_t p = 0;
            for (std::size_t i = 0; i < 6; ++i) {
                const std::size_t tab = val.find('\t', p);
                if (i < 5 && tab == std::string_view::npos) return false;
                f[i] = val.substr(p, i < 5 ? tab - p : std::string_view::npos);
                p = tab + 1;
            }
            HighScoreEntry e;
            std::int64_t score = 0, pre = 0, rank = 0, euid = 0;
            if (!profile_text::parse_int(f[0], score) || !profile_text::parse_int(f[1], pre) ||
                !profile_text::parse_int(f[2], rank) || !profile_text::parse_int(f[3], euid)) return false;
            e.score = static_cast<std::int32_t>(score);
            e.prefilled = pre != 0;
            e.rank = static_cast<std::int32_t>(rank);
            e.uid = static_cast<std::int32_t>(euid);
            e.name = std::string(f[4]);
            e.extra = std::string(f[5]);
            cur->entries.push_back(std::move(e));
        }
        else return false;
    }
    if (!header_seen) return false;
    hm.set_uid(uid);
    hm.tables() = std::move(tables);
    return true;
}

}  // namespace tt2
