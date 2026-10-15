// Creates the game's SDL renderer by walking tt2ui::backend_chain().
#pragma once

#include <string>

#include "tt2ui/render_backend.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace tt2ui {

struct RendererPick {
    SDL_Renderer* renderer = nullptr;   // null: every backend failed
    RenderCaps caps;
    std::string log;                    // "opengl (vulkan failed: ...)"
};

// Tries each backend for `pref` in order on `window` and keeps the first that
// starts. SDL may recreate the window's surface for Vulkan or OpenGL, so the
// window must not be drawn to before this.
RendererPick create_renderer(SDL_Window* window, tt2::RendererPref pref);

}  // namespace tt2ui
