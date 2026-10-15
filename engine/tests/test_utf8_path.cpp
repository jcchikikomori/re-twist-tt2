// UTF-8 <-> std::filesystem::path under C++17 and C++20 (utf8_path.hpp).
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "tt2/sha256.hpp"
#include "tt2/utf8_path.hpp"

using namespace tt2;

TEST(utf8_path_round_trips_non_ascii_names) {
    const std::string name = "images/gui/Jos\xC3\xA9_\xE6\x96\x87.png";   // "José_文.png"
    ASSERT_EQ(utf8_string(utf8_path(name)), name);
    ASSERT_EQ(utf8_string(utf8_path("fonts/a.xml")), std::string("fonts/a.xml"));
    ASSERT_EQ(utf8_string(utf8_path("")), std::string());
}

TEST(utf8_path_joins_like_a_path) {
    const std::filesystem::path p = utf8_path("root") / utf8_path("sound/click.ogg");
    ASSERT_EQ(utf8_string(p), std::string("root/sound/click.ogg"));   // generic separators
    ASSERT_EQ(utf8_string(p.filename()), std::string("click.ogg"));
}

TEST(utf8_path_opens_a_non_ascii_file) {
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("tt2_utf8_test_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    const auto file = dir / utf8_path("caf\xC3\xA9.ogg");
    { std::ofstream(file, std::ios::binary) << "abc"; }
    const auto d = sha256_file(file);
    ASSERT_TRUE(d.has_value());
    ASSERT_TRUE(*d == sha256("abc"));
    ASSERT_TRUE(!sha256_file(dir / utf8_path("cafe.ogg")).has_value());   // a different name
    std::filesystem::remove_all(dir);
}
