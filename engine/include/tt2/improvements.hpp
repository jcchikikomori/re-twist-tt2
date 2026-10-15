#pragma once

// The port's own display options (docs/design/improve-ui-ux.md), saved in
// improvements.txt next to settings.txt. Nothing here exists in the
// original game; every field is a deliberate deviation (UX-4 and later).
//
// The file is shared by builds of different ages, so its rules differ from
// settings.txt in one place: an unknown key is ignored, so a file written
// by a newer build still loads in an older one. A known key with a bad
// value still rejects the whole file.

#include <sstream>
#include <string>
#include <string_view>

#include "tt2/settings.hpp"

namespace tt2 {

enum class WindowMode {
    Windowed,     // an 800x600 window
    Borderless,   // desktop-sized, no border (SDL2's FULLSCREEN_DESKTOP)
};

inline const char* to_string(WindowMode m) {
    switch (m) {
        case WindowMode::Windowed: return "windowed";
        case WindowMode::Borderless: return "borderless";
    }
    return "windowed";
}

inline bool parse_window_mode(std::string_view s, WindowMode& out) {
    for (WindowMode m : {WindowMode::Windowed, WindowMode::Borderless})
        if (s == to_string(m)) {
            out = m;
            return true;
        }
    return false;
}

struct Improvements {
    WindowMode window = WindowMode::Windowed;   // GameSettings' default: windowed first run

    // A 0.2.x settings.txt only knows fullscreen on / off; its fullscreen
    // was SDL's desktop-sized borderless window.
    static Improvements from_legacy(const GameSettings& g) {
        Improvements imp;
        imp.window = g.fullscreen ? WindowMode::Borderless : WindowMode::Windowed;
        return imp;
    }
    // What settings.txt keeps mirroring, so a 0.2.x build opened later
    // still starts in the same kind of window.
    bool legacy_fullscreen() const { return window != WindowMode::Windowed; }

    friend bool operator==(const Improvements& a, const Improvements& b) { return a.window == b.window; }
    friend bool operator!=(const Improvements& a, const Improvements& b) { return !(a == b); }
};

inline std::string serialize(const Improvements& imp) {
    std::ostringstream os;
    os << "format=re-twist-tt2 improvements\nversion=1\n"
       << "window=" << to_string(imp.window) << '\n';
    return os.str();
}

// Returns false on a malformed document; `out` is then left untouched.
// A missing key keeps its current value; an unknown key is skipped.
inline bool parse(std::string_view text, Improvements& out) {
    Improvements imp = out;
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
            header_seen = val == "re-twist-tt2 improvements";
            if (!header_seen) return false;
        } else if (!header_seen) {
            return false;   // the header comes first
        } else if (key == "version") {
            if (val != "1") return false;
        } else if (key == "window") {
            if (!parse_window_mode(val, imp.window)) return false;
        }
        // Any other key belongs to a newer build: skip it.
    }
    if (!header_seen) return false;
    out = imp;
    return true;
}

}  // namespace tt2
