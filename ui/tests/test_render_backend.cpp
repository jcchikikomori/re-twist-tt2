// The renderer order (render_backend.hpp; deviation UX-1).
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "tt2ui/render_backend.hpp"

using namespace tt2ui;
using tt2::RendererPref;

namespace {
// What SDL 3.4 lists on Linux and on Windows.
const std::vector<std::string> kLinux = {"vulkan", "opengl", "opengles2", "gpu", "software"};
const std::vector<std::string> kWindows = {"direct3d11", "direct3d12", "direct3d", "vulkan", "opengl", "gpu",
                                           "software"};
bool same(const std::vector<Backend>& a, const std::vector<Backend>& b) { return a == b; }
}  // namespace

TEST(backend_chain_auto_tries_vulkan_then_opengl_then_software) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, kLinux), {Backend::Vulkan, Backend::OpenGL, Backend::Software}));
}

TEST(backend_chain_vulkan_pref_is_the_auto_order) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Vulkan, kLinux), {Backend::Vulkan, Backend::OpenGL, Backend::Software}));
}

TEST(backend_chain_opengl_pref_skips_vulkan) {
    ASSERT_TRUE(same(backend_chain(RendererPref::OpenGL, kLinux), {Backend::OpenGL, Backend::Software}));
}

TEST(backend_chain_software_pref_is_software_only) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Software, kLinux), {Backend::Software}));
}

TEST(backend_chain_drops_backends_sdl_does_not_list) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {"opengl", "software"}), {Backend::OpenGL, Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::OpenGL, {"vulkan", "software"}), {Backend::Software}));
}

TEST(backend_chain_never_uses_direct3d_or_gpu) {
    // Windows lists Direct3D first; the chain still starts at Vulkan.
    const auto chain = backend_chain(RendererPref::Auto, kWindows);
    ASSERT_TRUE(same(chain, {Backend::Vulkan, Backend::OpenGL, Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {"direct3d11", "gpu"}), {Backend::Software}));
}

TEST(backend_chain_ends_at_software_even_with_no_drivers) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {}), {Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Vulkan, {"metal", "VULKAN"}), {Backend::Software}));   // exact names only
}

TEST(render_caps_turn_enhancements_off_on_software) {
    ASSERT_TRUE(!RenderCaps::of(Backend::Software, 16384).enhancements);
    ASSERT_TRUE(RenderCaps::of(Backend::Vulkan, 16384).enhancements);
    ASSERT_TRUE(RenderCaps::of(Backend::OpenGL, 8192).enhancements);
    ASSERT_EQ(RenderCaps::of(Backend::OpenGL, 8192).max_texture_size, 8192);
}

TEST(backend_names_match_sdl) {
    ASSERT_EQ(std::string(sdl_driver_name(Backend::Vulkan)), std::string("vulkan"));
    ASSERT_EQ(std::string(sdl_driver_name(Backend::OpenGL)), std::string("opengl"));
    ASSERT_EQ(std::string(sdl_driver_name(Backend::Software)), std::string("software"));   // SDL_SOFTWARE_RENDERER
}

// Android (decision D2): OpenGL ES first, then Vulkan, then software.
namespace {
// What SDL 3.4 lists on Android.
const std::vector<std::string> kAndroid = {"vulkan", "opengles2", "gpu", "software"};
}  // namespace

TEST(backend_chain_android_auto_tries_gles_then_vulkan_then_software) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, kAndroid, Platform::Android),
                     {Backend::OpenGLES, Backend::Vulkan, Backend::Software}));
}

TEST(backend_chain_android_without_gles_falls_to_vulkan) {
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {"vulkan", "software"}, Platform::Android),
                     {Backend::Vulkan, Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {}, Platform::Android), {Backend::Software}));
}

TEST(backend_chain_android_opengl_pref_means_gles) {
    ASSERT_TRUE(same(backend_chain(RendererPref::OpenGL, kAndroid, Platform::Android),
                     {Backend::OpenGLES, Backend::Software}));
    // A desktop improvements.txt copied over ("renderer=opengl") never asks for desktop OpenGL.
    ASSERT_TRUE(same(backend_chain(RendererPref::OpenGL, {"opengl", "software"}, Platform::Android),
                     {Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Vulkan, kAndroid, Platform::Android),
                     {Backend::Vulkan, Backend::OpenGLES, Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Software, kAndroid, Platform::Android), {Backend::Software}));
}

TEST(backend_chain_desktop_never_uses_gles) {
    // Linux lists opengles2 too; the desktop chain (the default) skips it.
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, kLinux, Platform::Desktop),
                     {Backend::Vulkan, Backend::OpenGL, Backend::Software}));
    ASSERT_TRUE(same(backend_chain(RendererPref::Auto, {"opengles2", "software"}), {Backend::Software}));
    ASSERT_TRUE(opengl_backend(Platform::Desktop) == Backend::OpenGL);
    ASSERT_TRUE(opengl_backend(Platform::Android) == Backend::OpenGLES);
}

TEST(gles_names_match_sdl_and_turn_enhancements_on) {
    ASSERT_EQ(std::string(sdl_driver_name(Backend::OpenGLES)), std::string("opengles2"));
    ASSERT_EQ(std::string(backend_title(Backend::OpenGLES)), std::string("OpenGL ES"));
    ASSERT_TRUE(RenderCaps::of(Backend::OpenGLES, 4096).enhancements);
}
