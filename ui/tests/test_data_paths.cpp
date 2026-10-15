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

// The per-user asset folder and which folder the art loads from (OP-8).
namespace {
AssetDirInputs packaged(ImportStatus status) {
    AssetDirInputs in;
    in.user_dir = "/home/deck/.local/share/re-twist-tt2/imported-assets/";
    in.user_status = status;
    return in;
}
}  // namespace

TEST(resolve_user_asset_dir_prefers_the_flag_and_never_asks_sdl) {
    int calls = 0;
    const auto pref = [&] {
        ++calls;
        return std::string("/home/u/.local/share/re-twist-tt2/imported-assets/");
    };
    ASSERT_EQ(resolve_user_asset_dir("/tmp/ci/assets", pref), std::string("/tmp/ci/assets"));
    ASSERT_EQ(calls, 0);   // SDL_GetPrefPath would have created the real folder
    ASSERT_EQ(resolve_user_asset_dir("", pref), std::string("/home/u/.local/share/re-twist-tt2/imported-assets/"));
    ASSERT_EQ(calls, 1);
    ASSERT_EQ(resolve_user_asset_dir("", [] { return std::string(); }), std::string());   // no home folder
    ASSERT_EQ(std::string(PREF_ORG), std::string("re-twist-tt2"));
    ASSERT_EQ(std::string(PREF_APP), std::string("imported-assets"));
}

TEST(choose_assets_dir_flag_then_env_win_without_a_prompt) {
    AssetDirInputs in = packaged(ImportStatus::NotImported);
    in.flag = "D:/art";
    in.env = "/env/art";
    AssetDirChoice c = choose_assets_dir(in);
    ASSERT_EQ(c.dir, std::string("D:/art"));
    ASSERT_TRUE(c.source == AssetSource::Flag);
    ASSERT_TRUE(!c.prompt);   // nothing imported, but the player chose a folder
    in.flag.clear();
    c = choose_assets_dir(in);
    ASSERT_EQ(c.dir, std::string("/env/art"));
    ASSERT_TRUE(c.source == AssetSource::Env);
    ASSERT_TRUE(!c.prompt);
}

TEST(choose_assets_dir_uses_a_verified_import_silently) {
    AssetDirInputs in = packaged(ImportStatus::Imported);
    in.developer_dir = "/src/imported-assets";
    const AssetDirChoice c = choose_assets_dir(in);
    ASSERT_EQ(c.dir, in.user_dir);
    ASSERT_TRUE(c.source == AssetSource::User);
    ASSERT_TRUE(!c.prompt);
}

TEST(choose_assets_dir_stale_import_loads_but_asks_again) {
    const AssetDirChoice c = choose_assets_dir(packaged(ImportStatus::Stale));
    ASSERT_EQ(c.dir, std::string("/home/deck/.local/share/re-twist-tt2/imported-assets/"));
    ASSERT_TRUE(c.prompt);
}

TEST(choose_assets_dir_nothing_imported_asks_on_a_packaged_build) {
    for (ImportStatus s : {ImportStatus::NotImported, ImportStatus::Incomplete}) {
        const AssetDirChoice c = choose_assets_dir(packaged(s));
        ASSERT_TRUE(c.source == AssetSource::User);
        ASSERT_TRUE(c.prompt);   // Incomplete is not treated as imported
    }
}

TEST(choose_assets_dir_falls_back_to_the_source_tree_folder) {
    AssetDirInputs in = packaged(ImportStatus::NotImported);
    in.developer_dir = "/src/imported-assets";
    const AssetDirChoice c = choose_assets_dir(in);
    ASSERT_EQ(c.dir, std::string("/src/imported-assets"));
    ASSERT_TRUE(c.source == AssetSource::Developer);
    ASSERT_TRUE(!c.prompt);
}

TEST(choose_assets_dir_asset_dir_flag_beats_the_source_tree_folder) {
    AssetDirInputs in = packaged(ImportStatus::NotImported);
    in.user_dir = "/tmp/ci/assets";
    in.user_dir_given = true;
    in.developer_dir = "/src/imported-assets";
    const AssetDirChoice c = choose_assets_dir(in);
    ASSERT_EQ(c.dir, std::string("/tmp/ci/assets"));
    ASSERT_TRUE(c.source == AssetSource::User);
    ASSERT_TRUE(c.prompt);
}

TEST(choose_assets_dir_no_prompt_without_a_folder_or_a_manifest) {
    AssetDirInputs no_home = packaged(ImportStatus::NotImported);
    no_home.user_dir.clear();   // SDL_GetPrefPath failed
    const AssetDirChoice a = choose_assets_dir(no_home);
    ASSERT_TRUE(a.dir.empty());
    ASSERT_TRUE(!a.prompt);
    AssetDirInputs no_manifest = packaged(ImportStatus::NotImported);
    no_manifest.can_import = false;
    ASSERT_TRUE(!choose_assets_dir(no_manifest).prompt);
    no_manifest.user_status = ImportStatus::Stale;
    ASSERT_TRUE(!choose_assets_dir(no_manifest).prompt);
}

TEST(asset_source_names_for_the_startup_log) {
    ASSERT_EQ(std::string(to_string(AssetSource::Flag)), std::string("--assets"));
    ASSERT_EQ(std::string(to_string(AssetSource::Env)), std::string("TT2_ASSETS"));
    ASSERT_EQ(std::string(to_string(AssetSource::User)), std::string("user import"));
    ASSERT_EQ(std::string(to_string(AssetSource::Developer)), std::string("source tree"));
}
