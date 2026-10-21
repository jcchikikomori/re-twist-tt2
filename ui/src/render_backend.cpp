#include "render_backend_sdl.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace tt2ui {

RendererPick create_renderer(SDL_Window* window, tt2::RendererPref pref) {
    std::vector<std::string> drivers;
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
        if (const char* name = SDL_GetRenderDriver(i)) drivers.emplace_back(name);
    RendererPick pick;
    std::string failures;
    for (Backend b : backend_chain(pref, drivers, THIS_PLATFORM)) {
        SDL_Renderer* r = SDL_CreateRenderer(window, sdl_driver_name(b));
        if (!r) {
            failures += std::string(failures.empty() ? "" : ", ") + sdl_driver_name(b) + " failed: " + SDL_GetError();
            continue;
        }
        const SDL_PropertiesID props = SDL_GetRendererProperties(r);
        const int max_tex = static_cast<int>(SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
        pick.renderer = r;
        pick.caps = RenderCaps::of(b, max_tex);
        pick.log = sdl_driver_name(b);
        break;
    }
    if (!failures.empty()) pick.log += (pick.renderer ? " (" : "none (") + failures + ")";
    return pick;
}

}  // namespace tt2ui
