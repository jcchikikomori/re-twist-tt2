// Where the dictionaries live at run time.
//
// A development build reads them from the source tree (TT2_DATA_DIR, an
// absolute path baked in by CMake). A packaged build runs somewhere else,
// so the executable also looks next to itself:
//   - Windows zip:          <exe dir>/data
//   - AppImage / install:   <prefix>/bin/../share/tt2/data
// The first folder that holds a dictionary wins; with none, the source
// tree is returned so the error message names the expected place.
#pragma once

#include <functional>
#include <string>
#include <vector>

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

}  // namespace tt2ui
