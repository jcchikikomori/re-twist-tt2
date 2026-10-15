// What the renderer does with the IMPROVEMENTS choices (render_options.hpp; deviation UX-8).
#include "test_harness.hpp"

#include "tt2ui/render_options.hpp"

using namespace tt2ui;
using tt2::TextureFilter;

namespace {
tt2::Improvements with_filter(TextureFilter f) {
    tt2::Improvements imp;
    imp.filter = f;
    return imp;
}
}  // namespace

TEST(software_forces_sharp_and_800x600_layers) {
    const RenderCaps sw = RenderCaps::of(Backend::Software, 0);
    for (TextureFilter f : {TextureFilter::Sharp, TextureFilter::Smooth, TextureFilter::PixelArt}) {
        const EffectiveOptions o = effective(with_filter(f), sw);
        ASSERT_TRUE(o.filter == TextureFilter::Sharp);
        ASSERT_TRUE(!o.hires_layers);
    }
}

TEST(software_keeps_the_stored_choice) {
    // The file says smooth; this launch is software: the effect is Sharp,
    // but the Improvements value itself is untouched for the next GPU launch.
    const tt2::Improvements imp = with_filter(TextureFilter::Smooth);
    ASSERT_TRUE(effective(imp, RenderCaps::of(Backend::Software, 0)).filter == TextureFilter::Sharp);
    ASSERT_TRUE(imp.filter == TextureFilter::Smooth);
}

TEST(gpu_backends_keep_each_choice) {
    for (Backend b : {Backend::Vulkan, Backend::OpenGL})
        for (TextureFilter f : {TextureFilter::Sharp, TextureFilter::Smooth, TextureFilter::PixelArt}) {
            const EffectiveOptions o = effective(with_filter(f), RenderCaps::of(b, 16384));
            ASSERT_TRUE(o.filter == f);
            ASSERT_TRUE(o.hires_layers);
        }
}

TEST(pixelart_without_support_is_smooth) {
    ASSERT_TRUE(filter_with_support(TextureFilter::PixelArt, false) == TextureFilter::Smooth);
    ASSERT_TRUE(filter_with_support(TextureFilter::PixelArt, true) == TextureFilter::PixelArt);
    ASSERT_TRUE(filter_with_support(TextureFilter::Sharp, false) == TextureFilter::Sharp);
    ASSERT_TRUE(filter_with_support(TextureFilter::Smooth, false) == TextureFilter::Smooth);
}

TEST(layer_size_matches_the_game_rectangle) {
    ASSERT_TRUE(layer_size(1920, 1080, true, 16384) == (LayerSize{1440, 1080}));
    ASSERT_TRUE(layer_size(1280, 800, true, 16384) == (LayerSize{1066, 800}));   // Steam Deck
    ASSERT_TRUE(layer_size(2560, 1440, true, 0) == (LayerSize{1920, 1440}));     // unknown limit: trusted
}

TEST(layer_size_at_800x600_is_the_canvas) {
    // Nothing changes at scale 1, so 800x600 golden frames still match.
    ASSERT_TRUE(layer_size(800, 600, true, 16384) == (LayerSize{800, 600}));
    ASSERT_TRUE(layer_size(1920, 1080, false, 16384) == (LayerSize{800, 600}));   // software
}

TEST(layer_size_never_shrinks_or_exceeds_the_gpu) {
    ASSERT_TRUE(layer_size(640, 480, true, 16384) == (LayerSize{800, 600}));      // below 1x
    ASSERT_TRUE(layer_size(0, 0, true, 16384) == (LayerSize{800, 600}));
    ASSERT_TRUE(layer_size(3840, 2160, true, 2048) == (LayerSize{800, 600}));     // 2880x2160 > 2048
}
