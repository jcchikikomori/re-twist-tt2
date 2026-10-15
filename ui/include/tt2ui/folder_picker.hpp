// The native "choose your Text Twist 2 folder" dialog, headless part.
//
// The dialog itself is tinyfiledialogs (third_party/, zlib), wrapped by
// ui/src/folder_picker.cpp. Off a real display, or when the only backend
// it finds is a console one, it would read from the terminal and block,
// so the game shows a `tt2 --import DIR` hint instead.
#pragma once

#include <string>

namespace tt2ui {

struct PickResult {
    enum class Kind {
        Picked,        // `path` holds the folder (UTF-8)
        Cancelled,     // the player closed the dialog
        Unavailable,   // no graphic dialog on this system
    };
    Kind kind = Kind::Unavailable;
    std::string path;
};

// `video_driver` is SDL_GetCurrentVideoDriver(); `backend` is what
// tinyfiledialogs reports for a "tinyfd_query" (tinyfd_response).
inline bool picker_available(const std::string& video_driver, const std::string& backend) {
    if (video_driver.empty() || video_driver == "dummy" || video_driver == "offscreen" ||
        video_driver == "evdev" || video_driver == "KMSDRM")
        return false;
    // Console backends read from stdin: never from a game window.
    return !backend.empty() && backend != "dialog" && backend != "whiptail" && backend != "basicinput" &&
           backend != "no_solution";
}

}  // namespace tt2ui
