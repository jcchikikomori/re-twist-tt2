// Which SDL renderer to try, in order (deviation UX-1). Kept free of SDL so
// the order is unit-tested; ui/src/render_backend.cpp does the trying.
//
// CONFIRMED (display-and-timing.md §2): the original renders with Direct3D 9
// only. The port prefers Vulkan, then OpenGL, then SDL's software renderer
// (docs/design/improve-ui-ux.md). Direct3D (direct3d, direct3d11,
// direct3d12) and SDL's GPU renderer are never used, nor GLES (Q12). The
// software renderer is the last resort and switches the visual
// enhancements off (RenderCaps::enhancements).
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "tt2/improvements.hpp"

namespace tt2ui {

enum class Backend { Vulkan, OpenGL, Software };

// SDL's name for the backend (SDL_CreateRenderer, SDL_GetRenderDriver).
inline const char* sdl_driver_name(Backend b) {
    switch (b) {
        case Backend::Vulkan: return "vulkan";
        case Backend::OpenGL: return "opengl";
        case Backend::Software: return "software";
    }
    return "software";
}

// The backends to try for `pref`, in order, keeping only those SDL lists in
// `sdl_drivers` (SDL_GetRenderDriver). Software is always last and always
// there: SDL builds it in, and something has to draw.
inline std::vector<Backend> backend_chain(tt2::RendererPref pref, const std::vector<std::string>& sdl_drivers) {
    std::vector<Backend> wanted;
    switch (pref) {
        case tt2::RendererPref::Auto:
        case tt2::RendererPref::Vulkan: wanted = {Backend::Vulkan, Backend::OpenGL}; break;
        case tt2::RendererPref::OpenGL: wanted = {Backend::OpenGL}; break;
        case tt2::RendererPref::Software: break;
    }
    std::vector<Backend> chain;
    for (Backend b : wanted)
        if (std::find(sdl_drivers.begin(), sdl_drivers.end(), sdl_driver_name(b)) != sdl_drivers.end())
            chain.push_back(b);
    chain.push_back(Backend::Software);
    return chain;
}

// What the renderer that started can do. Later phases (filtering, hi-res
// dialog layers, the upscaler) read `enhancements`.
struct RenderCaps {
    Backend backend = Backend::Software;
    bool enhancements = false;   // off on the software renderer
    int max_texture_size = 0;    // 0: unknown

    static RenderCaps of(Backend b, int max_texture_size) {
        return RenderCaps{b, b != Backend::Software, max_texture_size};
    }
};

// For the IMPROVEMENTS row: what the player would see named.
inline const char* backend_title(Backend b) {
    switch (b) {
        case Backend::Vulkan: return "Vulkan";
        case Backend::OpenGL: return "OpenGL";
        case Backend::Software: return "Software";
    }
    return "Software";
}

}  // namespace tt2ui
