// Which SDL renderer to try, in order (deviation UX-1). Kept free of SDL so
// the order is unit-tested; ui/src/render_backend.cpp does the trying.
//
// CONFIRMED (display-and-timing.md §2): the original renders with Direct3D 9
// only. The port prefers Vulkan, then OpenGL, then SDL's software renderer
// (docs/design/improve-ui-ux.md). Direct3D (direct3d, direct3d11,
// direct3d12) and SDL's GPU renderer are never used. The software renderer
// is the last resort and switches the visual enhancements off
// (RenderCaps::enhancements).
//
// Deviation (decision D2, docs/design/android-support.md): Android has no
// desktop OpenGL and most devices support OpenGL ES, so there the order is
// OpenGL ES, then Vulkan, then software. Desktop never uses OpenGL ES (the
// earlier "no GLES" rule, Q12), so the desktop chain is exactly what it was.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "tt2/improvements.hpp"

namespace tt2ui {

enum class Backend { Vulkan, OpenGL, OpenGLES, Software };

enum class Platform { Desktop, Android };

#ifdef __ANDROID__
inline constexpr Platform THIS_PLATFORM = Platform::Android;
#else
inline constexpr Platform THIS_PLATFORM = Platform::Desktop;
#endif

// What RendererPref::OpenGL means: desktop OpenGL, or OpenGL ES on Android.
inline Backend opengl_backend(Platform platform) {
    return platform == Platform::Android ? Backend::OpenGLES : Backend::OpenGL;
}

// SDL's name for the backend (SDL_CreateRenderer, SDL_GetRenderDriver).
inline const char* sdl_driver_name(Backend b) {
    switch (b) {
        case Backend::Vulkan: return "vulkan";
        case Backend::OpenGL: return "opengl";
        case Backend::OpenGLES: return "opengles2";
        case Backend::Software: return "software";
    }
    return "software";
}

// The backends to try for `pref`, in order, keeping only those SDL lists in
// `sdl_drivers` (SDL_GetRenderDriver). Software is always last and always
// there: SDL builds it in, and something has to draw.
inline std::vector<Backend> backend_chain(tt2::RendererPref pref, const std::vector<std::string>& sdl_drivers,
                                          Platform platform = Platform::Desktop) {
    const Backend gl = opengl_backend(platform);
    std::vector<Backend> wanted;
    switch (pref) {
        case tt2::RendererPref::Auto:
            wanted = platform == Platform::Android ? std::vector<Backend>{gl, Backend::Vulkan}
                                                   : std::vector<Backend>{Backend::Vulkan, gl};
            break;
        case tt2::RendererPref::Vulkan: wanted = {Backend::Vulkan, gl}; break;
        case tt2::RendererPref::OpenGL: wanted = {gl}; break;
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
        case Backend::OpenGLES: return "OpenGL ES";
        case Backend::Software: return "Software";
    }
    return "Software";
}

}  // namespace tt2ui
