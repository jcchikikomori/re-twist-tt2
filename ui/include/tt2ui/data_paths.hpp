// Where the dictionaries live at run time.
//
// A development build reads them from the source tree (TT2_DATA_DIR, an
// absolute path baked in by CMake). A packaged build runs somewhere else,
// so the executable also looks next to itself:
//   - Windows zip:          <exe dir>/data
//   - AppImage / install:   <prefix>/bin/../share/tt2/data
// The first folder that holds a dictionary wins; with none, the source
// tree is returned so the error message names the expected place.
//
// Where the art comes from (OP-8, docs/design/import-assets.md): the
// player's own install is imported into a per-user folder, by default
// SDL_GetPrefPath(PREF_ORG, PREF_APP):
//   - Linux / Steam Deck:  ~/.local/share/re-twist-tt2/imported-assets/
//   - Windows:             %APPDATA%/re-twist-tt2/imported-assets/
// --asset-dir DIR replaces that folder (tests and CI never touch it).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "tt2ui/asset_import.hpp"

namespace tt2ui {

inline constexpr const char* INSTALLED_DATA_DIR = "../share/tt2/data";   // CMake TT2_DATA_INSTALL_DIR (Unix)
inline constexpr const char* BUNDLED_DATA_DIR = "data";                  // CMake TT2_DATA_INSTALL_DIR (Windows)

// `exe_dir` is SDL_GetBasePath(): the executable's folder with a trailing
// separator, or empty when SDL cannot tell.
inline std::vector<std::string> data_dir_candidates(const std::string& exe_dir, const std::string& compiled_dir) {
    std::vector<std::string> out;
    if (!exe_dir.empty()) {
        out.push_back(exe_dir + BUNDLED_DATA_DIR);
        out.push_back(exe_dir + INSTALLED_DATA_DIR);
    }
    out.push_back(compiled_dir);
    return out;
}

inline std::string pick_data_dir(const std::vector<std::string>& candidates,
                                 const std::function<bool(const std::string&)>& has_dictionary) {
    for (const auto& dir : candidates)
        if (has_dictionary(dir)) return dir;
    return candidates.empty() ? std::string() : candidates.back();
}

inline constexpr const char* PREF_ORG = "re-twist-tt2";
inline constexpr const char* PREF_APP = "imported-assets";

// --asset-dir wins; otherwise ask SDL. `pref_path` is called lazily:
// SDL_GetPrefPath creates its folder, so it must never run when
// --asset-dir is given. Empty when SDL can't find a home folder.
inline std::string resolve_user_asset_dir(const std::string& flag, const std::function<std::string()>& pref_path) {
    return flag.empty() ? pref_path() : flag;
}

enum class AssetSource {
    Flag,        // --assets DIR
    Env,         // TT2_ASSETS
    User,        // the per-user import folder
    Developer,   // <source tree>/imported-assets (the old import-original.sh target)
};

inline const char* to_string(AssetSource s) {
    switch (s) {
    case AssetSource::Flag: return "--assets";
    case AssetSource::Env: return "TT2_ASSETS";
    case AssetSource::User: return "user import";
    case AssetSource::Developer: return "source tree";
    }
    return "?";
}

struct AssetDirInputs {
    std::string flag;            // --assets DIR
    std::string env;             // TT2_ASSETS
    std::string user_dir;        // resolve_user_asset_dir(); empty when unknown
    ImportStatus user_status = ImportStatus::NotImported;
    std::string developer_dir;   // empty unless the source tree's folder exists
    bool user_dir_given = false; // --asset-dir named the folder: the source tree's folder never replaces it
    bool can_import = true;      // the bundled manifest loaded
};

struct AssetDirChoice {
    std::string dir;   // empty: no folder, placeholders only
    AssetSource source = AssetSource::User;
    bool prompt = false;   // ask the player to import (the launch warning)
};

// --assets > TT2_ASSETS > the user import (Imported, or Stale: the old
// files still load, but the player is asked to import again) > the source
// tree's folder (unless --asset-dir named the user folder) > the user
// folder (nothing in it yet: placeholders).
inline AssetDirChoice choose_assets_dir(const AssetDirInputs& in) {
    if (!in.flag.empty()) return {in.flag, AssetSource::Flag, false};
    if (!in.env.empty()) return {in.env, AssetSource::Env, false};
    const bool can_prompt = in.can_import && !in.user_dir.empty();
    if (in.user_status == ImportStatus::Imported) return {in.user_dir, AssetSource::User, false};
    if (in.user_status == ImportStatus::Stale) return {in.user_dir, AssetSource::User, can_prompt};
    if (!in.developer_dir.empty() && !in.user_dir_given) return {in.developer_dir, AssetSource::Developer, false};
    return {in.user_dir, AssetSource::User, can_prompt};
}

}  // namespace tt2ui
