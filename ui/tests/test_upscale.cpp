// The texture upscaler (upscale.hpp: Super-xBR 2x; deviation UX-9).
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "tt2ui/upscale.hpp"

using namespace tt2ui;

namespace {
using Img = std::vector<std::uint8_t>;
Img fill(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    Img px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (std::size_t k = 0; k < px.size(); k += 4) {
        px[k] = r;
        px[k + 1] = g;
        px[k + 2] = b;
        px[k + 3] = a;
    }
    return px;
}
void set(Img& px, int w, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::uint8_t* p = &px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}
const std::uint8_t* at(const Img& px, int w, int x, int y) {
    return &px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
}
// Without the CRs a Windows checkout adds (core.autocrlf), so the lines
// compare the same on every runner.
std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}
}  // namespace

TEST(upscale_doubles_a_uniform_image_exactly) {
    const Img in = fill(5, 3, 0x20, 0x90, 0xF0, 255);
    const Img out = super_xbr2x(in.data(), 5, 3);
    ASSERT_EQ(out.size(), std::size_t{10} * 6 * 4);
    for (std::size_t k = 0; k < out.size(); k += 4)
        ASSERT_TRUE(out[k] == 0x20 && out[k + 1] == 0x90 && out[k + 2] == 0xF0 && out[k + 3] == 255);
}

TEST(upscale_one_pixel_becomes_four_of_the_same) {
    const Img in = fill(1, 1, 200, 10, 30, 128);
    const Img out = super_xbr2x(in.data(), 1, 1);
    ASSERT_EQ(out.size(), std::size_t{16});
    for (std::size_t k = 0; k < 16; k += 4)
        ASSERT_TRUE(out[k] == 200 && out[k + 1] == 10 && out[k + 2] == 30 && out[k + 3] == 128);
}

TEST(upscale_of_nothing_is_empty) {
    const Img in = fill(2, 2, 0, 0, 0, 255);
    ASSERT_TRUE(super_xbr2x(in.data(), 0, 2).empty());
    ASSERT_TRUE(super_xbr2x(in.data(), 2, -1).empty());
    ASSERT_TRUE(super_xbr2x(nullptr, 2, 2).empty());
}

TEST(upscale_never_rings_past_the_source_colours) {
    // A hard black / white edge: every output channel stays within 0..255 of
    // the two (anti-ringing), and pixels away from the edge stay exact.
    const int w = 8, h = 6;
    Img in = fill(w, h, 0, 0, 0, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 4; x < w; ++x) set(in, w, x, y, 255, 255, 255, 255);
    const Img out = super_xbr2x(in.data(), w, h);
    for (int y = 0; y < 2 * h; ++y) {
        ASSERT_TRUE(at(out, 2 * w, 2, y)[0] == 0);                       // far left: black
        ASSERT_TRUE(at(out, 2 * w, 13, y)[0] == 255);                    // far right: white
        for (int x = 0; x < 2 * w; ++x) {
            const std::uint8_t* p = at(out, 2 * w, x, y);
            ASSERT_TRUE(p[0] == p[1] && p[1] == p[2] && p[3] == 255);   // still grey, still opaque
        }
    }
}

TEST(upscale_smooths_a_diagonal_staircase) {
    // A 45-degree edge: some output pixels take in-between values that plain
    // pixel doubling never makes.
    const int n = 8;
    Img in = fill(n, n, 0, 0, 0, 255);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (x > y) set(in, n, x, y, 255, 255, 255, 255);
    const Img out = super_xbr2x(in.data(), n, n);
    int between = 0;
    for (std::size_t k = 0; k < out.size(); k += 4) between += out[k] > 0 && out[k] < 255;
    ASSERT_TRUE(between > 0);
}

TEST(upscale_keeps_transparent_edges_clean) {
    // Opaque red on transparent black: premultiplied filtering means no pixel
    // that shows at all turns darker than red, and the far corner stays clear.
    const int w = 8, h = 8;
    Img in = fill(w, h, 0, 0, 0, 0);
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x) set(in, w, x, y, 255, 0, 0, 255);
    const Img out = super_xbr2x(in.data(), w, h);
    bool soft_edge = false;
    for (std::size_t k = 0; k < out.size(); k += 4) {
        if (out[k + 3] == 0) continue;
        ASSERT_TRUE(out[k] == 255 && out[k + 1] == 0 && out[k + 2] == 0);
        soft_edge = soft_edge || out[k + 3] < 255;
    }
    ASSERT_TRUE(soft_edge);                                              // the outline is antialiased
    ASSERT_EQ(static_cast<int>(at(out, 2 * w, 0, 0)[3]), 0);
    ASSERT_EQ(static_cast<int>(at(out, 2 * w, 8, 8)[3]), 255);           // the inside stays solid
}

TEST(upscale_does_not_merge_colours_that_differ_only_in_alpha) {
    // Transparent white beside opaque white: alpha counts in the edge test,
    // so the opaque side keeps full alpha away from the boundary.
    const int w = 8, h = 4;
    Img in = fill(w, h, 255, 255, 255, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 4; x < w; ++x) set(in, w, x, y, 255, 255, 255, 255);
    const Img out = super_xbr2x(in.data(), w, h);
    for (int y = 0; y < 2 * h; ++y) {
        ASSERT_EQ(static_cast<int>(at(out, 2 * w, 2, y)[3]), 0);
        ASSERT_EQ(static_cast<int>(at(out, 2 * w, 13, y)[3]), 255);
    }
}

TEST(upscale_is_deterministic) {
    Img in = fill(9, 7, 10, 20, 30, 255);
    for (int k = 0; k < 9 * 7; ++k)
        set(in, 9, k % 9, k / 9, static_cast<std::uint8_t>(k * 37), static_cast<std::uint8_t>(k * 11),
            static_cast<std::uint8_t>(k * 5), static_cast<std::uint8_t>(255 - k));
    ASSERT_TRUE(super_xbr2x(in.data(), 9, 7) == super_xbr2x(in.data(), 9, 7));
}

TEST(can_upscale_respects_the_gpu_and_the_size_cap) {
    ASSERT_TRUE(can_upscale(800, 600, 16384));
    ASSERT_TRUE(can_upscale(1500, 1000, 4096));
    ASSERT_TRUE(can_upscale(800, 600, 0));                               // unknown limit: trusted
    ASSERT_TRUE(!can_upscale(1500, 1000, 2048));                         // 3000 > 2048
    ASSERT_TRUE(!can_upscale(5700, 864, 16384));                         // 4.9 Mpx: over the cap
    ASSERT_TRUE(!can_upscale(2000, 1001, 0));                            // 2,002,000 px
    ASSERT_TRUE(!can_upscale(0, 600, 16384));
}

// MIT asks for the notice in every copy: the port's header must keep
// third_party/super-xbr/LICENSE (which the packages install) line for line.
TEST(upscale_header_keeps_the_super_xbr_notice) {
    const std::string root = std::string(TT2_SOURCE_DATA_DIR) + "/..";
    const std::string license = slurp(root + "/third_party/super-xbr/LICENSE");
    const std::string header = slurp(root + "/ui/include/tt2ui/upscale.hpp");
    ASSERT_TRUE(license.find("Copyright (c) 2015 Hyllian") == 0);
    ASSERT_TRUE(!header.empty());
    std::istringstream lines(license);
    int checked = 0;
    for (std::string line; std::getline(lines, line);) {
        if (line.empty()) continue;
        ASSERT_TRUE(header.find("//   " + line + "\n") != std::string::npos);
        ++checked;
    }
    ASSERT_TRUE(checked >= 15);   // a missing or truncated LICENSE fails, not passes
    ASSERT_TRUE(header.find("//   Copyright (c) 2016 Hyllian") == std::string::npos);   // exact lines only
}
