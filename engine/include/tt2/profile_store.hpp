#pragma once

// Plain-text persistence for `profile.hpp`: two files in a directory the
// front end chooses (the original uses the registry plus per-player XML
// under the common AppData folder; profiles.md §0.1 and §5).

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "tt2/profile.hpp"

namespace tt2 {

// Application folder name used under the platform's per-user data root.
inline constexpr const char* SAVE_DIR_NAME = "re-twist-tt2";

// Per-user data root following each platform's convention:
//
//   Windows  %APPDATA%\re-twist-tt2            (Roaming; falls back to %LOCALAPPDATA%, %USERPROFILE%)
//   macOS    ~/Library/Application Support/re-twist-tt2
//   Linux    $XDG_DATA_HOME/re-twist-tt2       (defaults to ~/.local/share)
//
// `getenv_fn` is injectable so tests can drive every branch; the
// default reads the real environment. Returns an empty path when no
// usable root variable is set.
template <typename GetEnv>
std::filesystem::path default_save_dir_with(GetEnv getenv_fn) {
    auto env = [&](const char* name) -> std::string {
        const char* v = getenv_fn(name);
        return v ? std::string(v) : std::string();
    };
#if defined(_WIN32)
    for (const char* var : {"APPDATA", "LOCALAPPDATA", "USERPROFILE"}) {
        const std::string root = env(var);
        if (!root.empty()) return std::filesystem::path(root) / SAVE_DIR_NAME;
    }
    return {};
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    if (home.empty()) return {};
    return std::filesystem::path(home) / "Library" / "Application Support" / SAVE_DIR_NAME;
#else
    const std::string xdg = env("XDG_DATA_HOME");
    if (!xdg.empty()) return std::filesystem::path(xdg) / SAVE_DIR_NAME;
    const std::string home = env("HOME");
    if (home.empty()) return {};
    return std::filesystem::path(home) / ".local" / "share" / SAVE_DIR_NAME;
#endif
}

inline std::filesystem::path default_save_dir() {
    return default_save_dir_with([](const char* n) { return std::getenv(n); });
}

class ProfileStore {
public:
    static constexpr const char* PROFILES_FILE = "profiles.txt";
    static constexpr const char* HIGHSCORES_FILE = "highscores.txt";

    explicit ProfileStore(std::filesystem::path dir) : dir_(std::move(dir)) {}

    const std::filesystem::path& dir() const noexcept { return dir_; }
    std::filesystem::path profiles_path() const { return dir_ / PROFILES_FILE; }
    std::filesystem::path highscores_path() const { return dir_ / HIGHSCORES_FILE; }

    // A missing file is not an error: the manager is left empty and
    // `true` is returned. A present but malformed file returns false and
    // leaves the manager untouched.
    bool load(PlayerManager& pm) const {
        std::string text;
        if (!read_file(profiles_path(), text)) return true;
        return parse(text, pm);
    }
    bool load(HighScoreManager& hm) const {
        std::string text;
        if (!read_file(highscores_path(), text)) return true;
        return parse(text, hm);
    }

    bool save(const PlayerManager& pm) const { return write_file(profiles_path(), serialize(pm)); }
    bool save(const HighScoreManager& hm) const { return write_file(highscores_path(), serialize(hm)); }

private:
    static bool read_file(const std::filesystem::path& p, std::string& out) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    bool write_file(const std::filesystem::path& p, const std::string& text) const {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        if (ec) return false;
        // Write next to the target and rename, so a crash never leaves a
        // half-written profile behind.
        const std::filesystem::path tmp = p.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << text;
            if (!out) return false;
        }
        std::filesystem::rename(tmp, p, ec);
        return !ec;
    }

    std::filesystem::path dir_;
};

}  // namespace tt2
