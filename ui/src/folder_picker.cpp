#include "folder_picker.hpp"

#include <SDL.h>

#include <filesystem>
#include <system_error>

#include "tinyfiledialogs.h"

namespace tt2ui {

PickResult pick_install_folder() {
    tinyfd_forceConsole = 0;
    tinyfd_allowCursesDialogs = 0;
#ifdef _WIN32
    tinyfd_winUtf8 = 1;   // paths come back as UTF-8, like the rest of the game
#endif
    const char* driver = SDL_GetCurrentVideoDriver();
    tinyfd_selectFolderDialog("tinyfd_query", "");   // fills tinyfd_response, shows nothing
    PickResult r;
    if (!picker_available(driver ? driver : "", tinyfd_response)) return r;
    // Constant strings only: tinyfiledialogs builds a shell command from them.
    const char* start = "";
#ifdef _WIN32
    // Open where GameHouse installs usually live.
    for (const char* dir : {"C:\\Program Files (x86)\\GameHouse\\Text Twist 2\\",
                            "C:\\Program Files (x86)\\Games\\Text Twist 2\\"}) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) {
            start = dir;
            break;
        }
    }
#endif
    const char* path = tinyfd_selectFolderDialog("Choose your Text Twist 2 folder", start);
    if (!path || !*path) {
        r.kind = PickResult::Kind::Cancelled;
        return r;
    }
    r.kind = PickResult::Kind::Picked;
    r.path = path;
    return r;
}

}  // namespace tt2ui
