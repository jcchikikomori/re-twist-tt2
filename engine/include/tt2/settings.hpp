#pragma once

// Global game settings: the volumes and the window mode. The max word
// length is per player and lives in profile.hpp (PlayerProfile::max_word_length).
//
// The original keeps these in the registry, group "GameSettngs" (sic),
// through GF2::SettingsManager (0x466300; load 0x465a80, save 0x465830).
// Defaults come from the TextTwist2GameSettings factory 0x40e720
// (docs/reverse-engineering/help-and-settings.md §3).

#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace tt2 {

struct GameSettings {
    // Deliberate deviation: 0x40e720 defaults to fullscreen=1. A first
    // run here opens windowed; the Settings switch turns fullscreen on.
    bool fullscreen = false;
    float sound_volume = 0.5f;   // "soundvolume", 0x40e720: 0.5
    float music_volume = 0.5f;   // "musicvolume", 0x40e720: 0.5

    // The volume setters 0x463db0 / 0x463e30 clamp to [0, 1].
    static bool valid_volume(float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }

    friend bool operator==(const GameSettings& a, const GameSettings& b) {
        return a.fullscreen == b.fullscreen && a.sound_volume == b.sound_volume && a.music_volume == b.music_volume;
    }
    friend bool operator!=(const GameSettings& a, const GameSettings& b) { return !(a == b); }
};

namespace settings_text {

// Locale-independent float text: "0.5", never "0,5".
inline std::string format_float(float v) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << v;
    return os.str();
}

inline bool parse_float(std::string_view s, float& out) {
    if (s.empty()) return false;
    std::istringstream is{std::string(s)};
    is.imbue(std::locale::classic());
    float v = 0.0f;
    is >> v;
    if (is.fail() || is.peek() != std::char_traits<char>::eof()) return false;
    out = v;
    return true;
}

}  // namespace settings_text

inline std::string serialize(const GameSettings& s) {
    std::ostringstream os;
    os << "format=re-twist-tt2 settings\nversion=1\n"
       << "fullscreen=" << (s.fullscreen ? 1 : 0) << '\n'
       << "soundvolume=" << settings_text::format_float(s.sound_volume) << '\n'
       << "musicvolume=" << settings_text::format_float(s.music_volume) << '\n';
    return os.str();
}

// Returns false on a malformed document; `out` is then left untouched.
// A missing key keeps its current value, as 0x465a80 only overwrites
// the values the registry holds.
inline bool parse(std::string_view text, GameSettings& out) {
    GameSettings s = out;
    bool header_seen = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, eq);
        const std::string_view val = line.substr(eq + 1);
        if (key == "format") {
            header_seen = val == "re-twist-tt2 settings";
            if (!header_seen) return false;
        } else if (!header_seen) {
            return false;   // the header comes first
        } else if (key == "version") {
            if (val != "1") return false;
        } else if (key == "fullscreen") {
            if (val != "0" && val != "1") return false;
            s.fullscreen = val == "1";
        } else if (key == "soundvolume" || key == "musicvolume") {
            float v = 0.0f;
            if (!settings_text::parse_float(val, v) || !GameSettings::valid_volume(v)) return false;
            (key == "soundvolume" ? s.sound_volume : s.music_volume) = v;
        } else {
            return false;
        }
    }
    if (!header_seen) return false;
    out = s;
    return true;
}

}  // namespace tt2
