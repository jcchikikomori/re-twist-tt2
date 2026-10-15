// Side bars beside the 4:3 game (side_fill.hpp, palette.hpp; deviation UX-6).
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "tt2ui/palette.hpp"
#include "tt2ui/side_fill.hpp"

using namespace tt2ui;

namespace {
Rgb at(const std::vector<std::uint8_t>& px, int w, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
    return {px[i], px[i + 1], px[i + 2]};
}
}  // namespace

TEST(palette_matches_the_placeholder_generator) {
    std::ifstream in(std::string(TT2_SOURCE_DATA_DIR) + "/../tools/gen_placeholders.py");
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string py = ss.str();
    const std::map<std::string, Rgb> ours = {
        {"ROYAL", palette::ROYAL}, {"ROYAL_DARK", palette::ROYAL_DARK}, {"NAVY", palette::NAVY},
        {"DEEP", palette::DEEP},   {"GOLD", palette::GOLD},             {"YELLOW", palette::YELLOW},
        {"ORANGE", palette::ORANGE}, {"CREAM", palette::CREAM},         {"WHITE", palette::WHITE},
        {"CYAN", palette::CYAN},   {"SKY", palette::SKY},
    };
    const std::regex line(R"(^([A-Z_]+) = \(0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2})\))",
                          std::regex::multiline);
    int found = 0;
    for (std::sregex_iterator it(py.begin(), py.end(), line), end; it != end; ++it) {
        const auto m = ours.find((*it)[1].str());
        if (m == ours.end()) continue;
        const Rgb want{static_cast<std::uint8_t>(std::stoi((*it)[2].str(), nullptr, 16)),
                       static_cast<std::uint8_t>(std::stoi((*it)[3].str(), nullptr, 16)),
                       static_cast<std::uint8_t>(std::stoi((*it)[4].str(), nullptr, 16))};
        ASSERT_TRUE(m->second == want);
        ++found;
    }
    ASSERT_EQ(found, static_cast<int>(ours.size()));   // every colour still exists in the generator
}

TEST(side_fill_is_deterministic_and_opaque) {
    const std::vector<std::uint8_t> a = side_fill_rgba(240, 1080, Side::Left);
    ASSERT_EQ(a.size(), std::size_t{240} * 1080 * 4);
    ASSERT_TRUE(a == side_fill_rgba(240, 1080, Side::Left));
    for (std::size_t i = 3; i < a.size(); i += 4) ASSERT_EQ(static_cast<int>(a[i]), 255);
}

TEST(side_fill_uses_only_the_palette_range) {
    // Away from the gold line every pixel lies between DEEP, ROYAL_DARK and
    // ROYAL: red 0, green 0x10..0x30, blue 0x40..0xB0.
    const int w = 107, h = 800;   // a Steam Deck bar
    const std::vector<std::uint8_t> px = side_fill_rgba(w, h, Side::Left);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w - SIDE_FILL_EDGE_PX; ++x) {
            const Rgb c = at(px, w, x, y);
            ASSERT_TRUE(c.r == 0 && c.g >= 0x10 && c.g <= 0x30 && c.b >= 0x40 && c.b <= 0xB0);
        }
    ASSERT_TRUE(at(px, w, 0, 0).b < at(px, w, 0, h - 1).b);   // darker at the top
}

TEST(side_fill_has_a_gold_line_against_the_game) {
    const int w = 240, h = 1080;
    const std::vector<std::uint8_t> left = side_fill_rgba(w, h, Side::Left);
    const std::vector<std::uint8_t> right = side_fill_rgba(w, h, Side::Right);
    for (int y = 0; y < h; y += 37) {
        ASSERT_TRUE(at(left, w, w - 1, y) == palette::GOLD && at(left, w, w - 2, y) == palette::GOLD);
        ASSERT_TRUE(at(left, w, w - 3, y) != palette::GOLD);
        ASSERT_TRUE(at(right, w, 0, y) == palette::GOLD && at(right, w, 1, y) == palette::GOLD);
        ASSERT_TRUE(at(right, w, 2, y) != palette::GOLD);
    }
}

TEST(side_fill_right_is_the_left_mirrored) {
    const int w = 160, h = 720;
    const std::vector<std::uint8_t> left = side_fill_rgba(w, h, Side::Left);
    const std::vector<std::uint8_t> right = side_fill_rgba(w, h, Side::Right);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) ASSERT_TRUE(at(left, w, x, y) == at(right, w, w - 1 - x, y));
}

TEST(side_fill_rings_are_faint) {
    // Each pixel sits between its row's gradient colour and at most 22 % of
    // the way to ROYAL.
    const int w = 240, h = 1080;
    const std::vector<std::uint8_t> px = side_fill_rgba(w, h, Side::Left);
    bool varies = false;
    for (int y = 0; y < h; y += 9) {
        const float t = static_cast<float>(y) / static_cast<float>(h - 1);
        const int base_b = static_cast<int>(std::lround(0x40 + (0xA0 - 0x40) * t));   // DEEP -> ROYAL_DARK
        const int max_b = base_b + static_cast<int>(std::ceil(SIDE_FILL_RING_AMOUNT * (0xB0 - base_b))) + 1;
        for (int x = 0; x < w - SIDE_FILL_EDGE_PX; ++x) {
            const int b = at(px, w, x, y).b;
            ASSERT_TRUE(b >= base_b - 1 && b <= max_b);
            varies = varies || b > base_b + 1;
        }
    }
    ASSERT_TRUE(varies);   // there is a pattern at all
}

TEST(side_fill_of_nothing_is_empty) {
    ASSERT_TRUE(side_fill_rgba(0, 1080, Side::Left).empty());
    ASSERT_TRUE(side_fill_rgba(240, 0, Side::Right).empty());
    ASSERT_TRUE(side_fill_rgba(-5, -5, Side::Left).empty());
}

TEST(side_fill_one_pixel_bar_is_all_gold) {
    const std::vector<std::uint8_t> px = side_fill_rgba(1, 600, Side::Right);
    ASSERT_EQ(px.size(), std::size_t{600} * 4);
    for (int y = 0; y < 600; ++y) ASSERT_TRUE(at(px, 1, 0, y) == palette::GOLD);
    ASSERT_EQ(side_fill_rgba(1, 1, Side::Left).size(), std::size_t{4});
}
