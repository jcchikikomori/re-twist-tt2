// Applies the IMPROVEMENTS display mode and size to the SDL window
// (deviation UX-5); the choices themselves live in tt2ui/display.hpp.
#pragma once

#include <string>

#include "tt2/improvements.hpp"
#include "tt2ui/display.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace tt2ui {

// The sizes and modes the display under `window` allows.
DisplayInfo query_display(SDL_Window* window);

struct DisplayApplied {
    tt2::WindowMode mode = tt2::WindowMode::Windowed;   // what is showing (Native may fall back)
    bool native_failed = false;
    std::string log;   // "windowed 1280x720", "native 800x600 @ 60 Hz", "borderless 1920x1080 (native ...)"
};

// Windowed: the saved size, or the closest that fits, centred. Borderless:
// the desktop size. Native: the exact display mode at its highest refresh
// rate, else Borderless.
DisplayApplied apply_display(SDL_Window* window, const tt2::Improvements& imp, const DisplayInfo& info);

// Compares SDL's letterbox rectangle with fit_4x3() for the current output.
// Returns "output WxH, game X,Y WxH" plus a warning when they differ.
std::string check_presentation(SDL_Renderer* renderer);

}  // namespace tt2ui
