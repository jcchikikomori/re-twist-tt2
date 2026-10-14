// Run-time dictionary lookup for packaged builds (data_paths.hpp).
#include "test_harness.hpp"

#include <set>
#include <string>

#include "tt2ui/data_paths.hpp"

using namespace tt2ui;

TEST(data_dir_candidates_look_next_to_the_executable_first) {
    const auto c = data_dir_candidates("/opt/tt2/usr/bin/", "/src/data");
    ASSERT_EQ(c.size(), std::size_t{3});
    ASSERT_EQ(c[0], std::string("/opt/tt2/usr/bin/data"));                 // Windows zip layout
    ASSERT_EQ(c[1], std::string("/opt/tt2/usr/bin/../share/tt2/data"));    // AppImage / install
    ASSERT_EQ(c[2], std::string("/src/data"));                             // development build
}

TEST(data_dir_candidates_without_an_exe_dir_use_the_source_tree) {
    const auto c = data_dir_candidates("", "/src/data");
    ASSERT_EQ(c.size(), std::size_t{1});
    ASSERT_EQ(c[0], std::string("/src/data"));
}

TEST(pick_data_dir_takes_the_first_folder_with_a_dictionary) {
    const auto c = data_dir_candidates("C:\\Games\\tt2\\", "/src/data");
    const std::set<std::string> present{"C:\\Games\\tt2\\data", "/src/data"};
    const auto has = [&](const std::string& d) { return present.count(d) > 0; };
    ASSERT_EQ(pick_data_dir(c, has), std::string("C:\\Games\\tt2\\data"));
}

TEST(pick_data_dir_falls_back_to_the_last_candidate) {
    const auto c = data_dir_candidates("/usr/bin/", "/src/data");
    const auto none = [](const std::string&) { return false; };
    ASSERT_EQ(pick_data_dir(c, none), std::string("/src/data"));   // so the error names it
    ASSERT_EQ(pick_data_dir({}, none), std::string());
}
