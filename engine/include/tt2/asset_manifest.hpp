#pragma once

// The original-asset manifest: which files the importer copies out of the
// player's own Text Twist 2 install, and the SHA-256 each one must have.
// The manifest holds relative paths and hashes only, never the files.
//
// Format (sha256sum-compatible, so `sha256sum -c` inside an install checks
// it too; coreutils warns about the # lines but still checks every file):
//
//   # re-twist-tt2 original-asset manifest
//   # version=1
//   <64 lowercase hex>  images/gui/button_ok.png
//
// Every rule here decides from the path alone, before any file is opened.

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "tt2/sha256.hpp"

namespace tt2 {

// The folders scripts/import-original.sh copies, in its order.
inline constexpr const char* IMPORT_ROOTS[] = {
    "images/splash",     "images/loadscene", "images/selectionscene", "images/gui", "images/gamescene",
    "images/trophyscene", "fonts",           "music",                 "sound",
};
// The script's "is this an install?" check.
inline constexpr const char* INSTALL_PROBE = "properties/resources.xml";

namespace asset_manifest_detail {
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

inline bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;   // upper case too: one canonical form
}
}  // namespace asset_manifest_detail

// Never imported: code, the dictionaries, DRM / licensing files, and the
// caches Windows and macOS leave in folders (they differ per machine, so
// they could never match a hash). Case-insensitive, by name only.
inline bool is_excluded(std::string_view relative_path) {
    namespace d = asset_manifest_detail;
    const std::size_t slash = relative_path.find_last_of('/');
    const std::string name = d::lower(slash == std::string_view::npos ? relative_path : relative_path.substr(slash + 1));
    if (name == "texttwist2_radrm.dll" || name == "thumbs.db" || name == "desktop.ini" || name == ".ds_store")
        return true;
    for (const char* ext : {".nfo", ".dll", ".exe", ".tree"})
        if (d::ends_with(name, ext)) return true;
    return false;
}

// A relative path with forward slashes that stays inside its folder: no
// leading '/', no drive letter or ':' at all, no backslash, no empty, "."
// or ".." segment, and no control characters.
inline bool is_safe_relative(std::string_view p) {
    if (p.empty() || p.front() == '/' || p.back() == '/') return false;
    for (char c : p)
        if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
    std::size_t start = 0;
    while (start <= p.size()) {
        std::size_t end = p.find('/', start);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view seg = p.substr(start, end - start);
        if (seg.empty() || seg == "." || seg == "..") return false;
        start = end + 1;
    }
    return true;
}

// True when the path sits inside one of IMPORT_ROOTS.
inline bool under_import_root(std::string_view p) {
    for (const char* root : IMPORT_ROOTS) {
        const std::string_view r(root);
        if (p.size() > r.size() + 1 && p.substr(0, r.size()) == r && p[r.size()] == '/') return true;
    }
    return false;
}

inline bool parse_hex_digest(std::string_view s, Sha256::Digest& out) {
    if (s.size() != 64) return false;
    Sha256::Digest d{};
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = asset_manifest_detail::hex_value(s[i * 2]);
        const int lo = asset_manifest_detail::hex_value(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        d[i] = static_cast<std::uint8_t>(hi << 4 | lo);
    }
    out = d;
    return true;
}

struct AssetManifest {
    static constexpr const char* TITLE = "# re-twist-tt2 original-asset manifest";
    static constexpr const char* VERSION = "# version=1";

    struct Entry {
        std::string path;   // relative, forward slashes, under an IMPORT_ROOTS folder
        Sha256::Digest sha256{};

        friend bool operator==(const Entry& a, const Entry& b) { return a.path == b.path && a.sha256 == b.sha256; }
        friend bool operator!=(const Entry& a, const Entry& b) { return !(a == b); }
    };
    std::vector<Entry> entries;

    friend bool operator==(const AssetManifest& a, const AssetManifest& b) { return a.entries == b.entries; }
    friend bool operator!=(const AssetManifest& a, const AssetManifest& b) { return !(a == b); }

    std::string serialize() const {
        std::string out = std::string(TITLE) + "\n" + VERSION + "\n";
        for (const Entry& e : entries) out += to_hex(e.sha256) + "  " + e.path + "\n";
        return out;
    }

    // All or nothing: on any bad line `out` is left untouched. The title
    // must be the first line and the version comment must come before the
    // first entry. Paths are unique ignoring case (Windows can't hold two
    // files that differ only by case).
    static bool parse(std::string_view text, AssetManifest& out) {
        AssetManifest m;
        std::unordered_set<std::string> seen;
        bool first = true;
        bool version_seen = false;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t nl = text.find('\n', pos);
            std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
            pos = nl == std::string_view::npos ? text.size() : nl + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (first) {
                if (line != TITLE) return false;
                first = false;
                continue;
            }
            if (line.empty()) continue;
            if (line.front() == '#') {
                if (line == VERSION) {
                    version_seen = true;
                } else if (line.substr(0, 10) == "# version=") {
                    return false;   // a version this build doesn't know
                }
                continue;
            }
            if (!version_seen) return false;
            if (line.size() < 67 || line.substr(64, 2) != "  ") return false;
            Entry e;
            if (!parse_hex_digest(line.substr(0, 64), e.sha256)) return false;
            e.path = std::string(line.substr(66));
            if (!is_safe_relative(e.path) || !under_import_root(e.path) || is_excluded(e.path)) return false;
            if (!seen.insert(asset_manifest_detail::lower(e.path)).second) return false;
            m.entries.push_back(std::move(e));
        }
        if (first || !version_seen) return false;
        out = std::move(m);
        return true;
    }
};

}  // namespace tt2
