#include "display_sdl.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

namespace tt2ui {

namespace {

// An exact `size` mode at the desktop's refresh rate, as the original keeps
// it (FullScreen_RefreshRateInHz = 0, display-and-timing.md §3); failing
// that, the fastest one (SDL lists each size's modes fastest first).
bool find_native_mode(SDL_DisplayID display, const tt2::Resolution& size, SDL_DisplayMode& out) {
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes) return false;
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    const SDL_DisplayMode* fastest = nullptr;
    const SDL_DisplayMode* same_rate = nullptr;
    for (int i = 0; i < count; ++i) {
        const SDL_DisplayMode* m = modes[i];
        if (m->w != size.w || m->h != size.h || m->pixel_density != 1.0f) continue;
        if (!fastest) fastest = m;
        if (!same_rate && desktop && std::fabs(m->refresh_rate - desktop->refresh_rate) < 0.5f) same_rate = m;
    }
    const SDL_DisplayMode* pick = same_rate ? same_rate : fastest;
    if (pick) out = *pick;
    SDL_free(modes);
    return pick != nullptr;
}

SDL_DisplayID display_of(SDL_Window* window) {
    const SDL_DisplayID id = SDL_GetDisplayForWindow(window);
    return id ? id : SDL_GetPrimaryDisplay();
}

}  // namespace

DisplayInfo query_display(SDL_Window* window) {
    DisplayInfo info;
    const SDL_DisplayID id = display_of(window);
    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(id)) info.desktop = {desktop->w, desktop->h};
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(id, &usable)) {
        // The frame around a window takes room too; a fullscreen or hidden
        // window reports none, which only matters at the very edge.
        int top = 0, left = 0, bottom = 0, right = 0;
        if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right)) top = left = bottom = right = 0;
        info.windowed = available_resolutions(usable.w - left - right, usable.h - top - bottom);
    }
    for (const tt2::Resolution& r : tt2::RESOLUTIONS) {
        SDL_DisplayMode mode;
        if (find_native_mode(id, r, mode)) info.native.push_back(r);
    }
    return info;
}

DisplayApplied apply_display(SDL_Window* window, const tt2::Improvements& imp, const DisplayInfo& info) {
    DisplayApplied out;
    const SDL_DisplayID id = display_of(window);
    std::string native_error;
    if (imp.window == tt2::WindowMode::Native) {
        // Deviation UX-5: the original's only fullscreen is an exclusive
        // 800x600 mode (display-and-timing.md §3); this is the same switch
        // at the chosen size.
        SDL_DisplayMode mode;
        if (!find_native_mode(id, imp.resolution, mode))
            native_error = "no " + tt2::to_string(imp.resolution) + " display mode";
        else if (!SDL_SetWindowFullscreenMode(window, &mode) || !SDL_SetWindowFullscreen(window, true))
            native_error = SDL_GetError();
        if (native_error.empty()) {
            out.mode = tt2::WindowMode::Native;
            char rate[32];
            std::snprintf(rate, sizeof rate, " @ %.0f Hz", static_cast<double>(mode.refresh_rate));
            out.log = "native " + tt2::to_string(imp.resolution) + rate;
            SDL_SyncWindow(window);
            return out;
        }
        out.native_failed = true;
    }
    if (imp.window != tt2::WindowMode::Windowed) {
        // Borderless, or Native's fallback: the desktop-sized window SDL3
        // calls fullscreen with no display mode.
        out.mode = tt2::WindowMode::Borderless;
        if (!SDL_SetWindowFullscreenMode(window, nullptr) || !SDL_SetWindowFullscreen(window, true))
            std::fprintf(stderr, "warning: borderless: %s\n", SDL_GetError());
        out.log = "borderless " + tt2::to_string(info.desktop);
        if (out.native_failed) out.log += " (native failed: " + native_error + ")";
        SDL_SyncWindow(window);
        return out;
    }
    const tt2::Resolution size = pick_resolution(info.windowed, imp.resolution);
    if (!SDL_SetWindowFullscreen(window, false)) std::fprintf(stderr, "warning: windowed: %s\n", SDL_GetError());
    if (!SDL_SetWindowSize(window, size.w, size.h)) std::fprintf(stderr, "warning: window size: %s\n", SDL_GetError());
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    out.mode = tt2::WindowMode::Windowed;
    out.log = "windowed " + tt2::to_string(size);
    if (size != imp.resolution) out.log += " (" + tt2::to_string(imp.resolution) + " does not fit)";
    SDL_SyncWindow(window);
    return out;
}

std::string check_presentation(SDL_Renderer* renderer) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer, &w, &h);   // pixels, not window points
    SDL_FRect sdl{};
    SDL_GetRenderLogicalPresentationRect(renderer, &sdl);
    const Fit4x3 ours = fit_4x3(w, h);
    char buf[200];
    std::snprintf(buf, sizeof buf, "output %dx%d, game %g,%g %gx%g", w, h, static_cast<double>(sdl.x),
                  static_cast<double>(sdl.y), static_cast<double>(sdl.w), static_cast<double>(sdl.h));
    std::string log = buf;
    const auto off = [](float a, float b) { return std::fabs(a - b) > 0.01f; };
    if (off(sdl.x, ours.game.x) || off(sdl.y, ours.game.y) || off(sdl.w, ours.game.w) || off(sdl.h, ours.game.h)) {
        std::snprintf(buf, sizeof buf, " (warning: fit_4x3 says %g,%g %gx%g)", static_cast<double>(ours.game.x),
                      static_cast<double>(ours.game.y), static_cast<double>(ours.game.w),
                      static_cast<double>(ours.game.h));
        log += buf;
    }
    return log;
}

}  // namespace tt2ui
