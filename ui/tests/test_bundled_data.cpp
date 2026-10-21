// Copying data/ out of the APK on Android (bundled_data.hpp).
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>

#include "tt2/sha256.hpp"
#include "tt2ui/bundled_data.hpp"

using namespace tt2ui;
namespace fs = std::filesystem;

namespace {
fs::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = fs::temp_directory_path() / ("tt2_bundle_test_" + std::string(tag) + "_" + std::to_string(rd()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A fake APK: the asset files, and the manifest Gradle would write for them.
struct Apk {
    std::map<std::string, std::string> files;
    int reads = 0;

    std::string manifest() const {
        std::string out;
        for (const auto& [path, bytes] : files)
            out += tt2::to_hex(tt2::sha256(bytes)) + " " + std::to_string(bytes.size()) + " " + path + "\n";
        return out;
    }

    ReadBundleAsset reader() {
        return [this](const std::string& path, std::string& bytes) {
            ++reads;
            const auto it = files.find(path);
            if (it == files.end()) return false;
            bytes = it->second;
            return true;
        };
    }
};

Apk sample_apk() {
    Apk apk;
    apk.files["open-common.tree"] = "abc0def1";
    apk.files["open-full.tree"] = "xyz1";
    apk.files["placeholder/2x/fonts/font_menu.png"] = std::string(3000, '\x7f');
    return apk;
}
}  // namespace

TEST(bundle_manifest_parses_hash_size_and_path) {
    const auto apk = sample_apk();
    const auto entries = parse_bundle_manifest(apk.manifest());
    ASSERT_EQ(entries.size(), std::size_t{3});
    ASSERT_EQ(entries[0].path, std::string("open-common.tree"));
    ASSERT_EQ(entries[0].size, std::uintmax_t{8});
    ASSERT_EQ(entries[0].sha256, tt2::to_hex(tt2::sha256("abc0def1")));
    ASSERT_EQ(entries[2].path, std::string("placeholder/2x/fonts/font_menu.png"));
}

TEST(bundle_manifest_keeps_spaces_in_paths_and_skips_blank_lines) {
    const std::string h(64, 'a');
    const auto entries = parse_bundle_manifest(h + " 3 placeholder/My Font.png\r\n\n");
    ASSERT_EQ(entries.size(), std::size_t{1});
    ASSERT_EQ(entries[0].path, std::string("placeholder/My Font.png"));
}

TEST(bundle_manifest_rejects_any_malformed_line) {
    const std::string h(64, 'a');
    ASSERT_TRUE(parse_bundle_manifest("").empty());
    ASSERT_TRUE(parse_bundle_manifest(h + " 3\n").empty());                       // no path
    ASSERT_TRUE(parse_bundle_manifest(h + " x3 a.tree\n").empty());               // size not a number
    ASSERT_TRUE(parse_bundle_manifest(std::string(63, 'a') + " 3 a.tree\n").empty());   // short hash
    ASSERT_TRUE(parse_bundle_manifest(std::string(64, 'A') + " 3 a.tree\n").empty());   // upper-case hex
    // One bad line spoils the whole manifest, not just its own entry.
    ASSERT_TRUE(parse_bundle_manifest(h + " 3 a.tree\nnot a line\n").empty());
}

TEST(bundle_paths_must_stay_inside_data) {
    ASSERT_TRUE(safe_bundle_path("open-full.tree"));
    ASSERT_TRUE(safe_bundle_path("placeholder/2x/images/x.png"));
    ASSERT_TRUE(!safe_bundle_path(""));
    ASSERT_TRUE(!safe_bundle_path("/etc/passwd"));
    ASSERT_TRUE(!safe_bundle_path("../shared_prefs/x.xml"));
    ASSERT_TRUE(!safe_bundle_path("placeholder/../../x"));
    ASSERT_TRUE(!safe_bundle_path("placeholder//x.png"));
    ASSERT_TRUE(!safe_bundle_path("placeholder/./x.png"));
    ASSERT_TRUE(!safe_bundle_path("placeholder\\x.png"));
    ASSERT_TRUE(!safe_bundle_path("C:x.png"));
    const std::string h(64, 'a');
    ASSERT_TRUE(parse_bundle_manifest(h + " 3 ../x\n").empty());
}

TEST(bundle_plan_copies_without_a_matching_stamp) {
    ASSERT_TRUE(plan_bundle_copy("m1", "") == BundleAction::Copy);     // fresh install or cut-short copy
    ASSERT_TRUE(plan_bundle_copy("m2", "m1") == BundleAction::Copy);   // a newer APK
    ASSERT_TRUE(plan_bundle_copy("m1", "m1") == BundleAction::Keep);
}

TEST(bundle_fresh_install_copies_every_file_and_stamps_last) {
    const auto dest = temp_dir("fresh") / "data";
    auto apk = sample_apk();
    const auto r = ensure_bundled_data(apk.manifest(), dest, apk.reader());
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.action == BundleAction::Copy);
    ASSERT_EQ(r.files, std::size_t{3});
    ASSERT_EQ(slurp(dest / "open-common.tree"), std::string("abc0def1"));
    ASSERT_EQ(slurp(dest / "placeholder/2x/fonts/font_menu.png"), std::string(3000, '\x7f'));
    ASSERT_EQ(slurp(dest / BUNDLE_STAMP), apk.manifest());
    fs::remove_all(dest.parent_path());
}

TEST(bundle_same_apk_keeps_the_copy_without_reading_assets) {
    const auto dest = temp_dir("keep") / "data";
    auto apk = sample_apk();
    ASSERT_TRUE(ensure_bundled_data(apk.manifest(), dest, apk.reader()).ok);
    apk.reads = 0;
    const auto r = ensure_bundled_data(apk.manifest(), dest, apk.reader());
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.action == BundleAction::Keep);
    ASSERT_EQ(r.files, std::size_t{0});
    ASSERT_EQ(apk.reads, 0);
    fs::remove_all(dest.parent_path());
}

TEST(bundle_newer_apk_recopies_and_drops_files_it_no_longer_ships) {
    const auto dest = temp_dir("update") / "data";
    auto apk = sample_apk();
    ASSERT_TRUE(ensure_bundled_data(apk.manifest(), dest, apk.reader()).ok);
    apk.files["open-full.tree"] = "xyz1xyz0";
    apk.files.erase("placeholder/2x/fonts/font_menu.png");
    const auto r = ensure_bundled_data(apk.manifest(), dest, apk.reader());
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.action == BundleAction::Copy);
    ASSERT_EQ(slurp(dest / "open-full.tree"), std::string("xyz1xyz0"));
    ASSERT_TRUE(!fs::exists(dest / "placeholder/2x/fonts/font_menu.png"));
    ASSERT_EQ(slurp(dest / BUNDLE_STAMP), apk.manifest());
    fs::remove_all(dest.parent_path());
}

TEST(bundle_copy_cut_short_has_no_stamp_and_is_redone) {
    const auto dest = temp_dir("cut") / "data";
    auto apk = sample_apk();
    ASSERT_TRUE(ensure_bundled_data(apk.manifest(), dest, apk.reader()).ok);
    // The app was killed mid-copy: some files written, stamp gone.
    fs::remove(dest / BUNDLE_STAMP);
    fs::remove(dest / "open-full.tree");
    const auto r = ensure_bundled_data(apk.manifest(), dest, apk.reader());
    ASSERT_TRUE(r.ok);
    ASSERT_TRUE(r.action == BundleAction::Copy);
    ASSERT_EQ(slurp(dest / "open-full.tree"), std::string("xyz1"));
    fs::remove_all(dest.parent_path());
}

TEST(bundle_missing_asset_fails_loudly_and_leaves_no_stamp) {
    const auto dest = temp_dir("missing") / "data";
    auto apk = sample_apk();
    const auto manifest = apk.manifest();
    apk.files.erase("open-full.tree");
    const auto r = ensure_bundled_data(manifest, dest, apk.reader());
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(r.error.find("open-full.tree") != std::string::npos);
    ASSERT_TRUE(!fs::exists(dest / BUNDLE_STAMP));
    fs::remove_all(dest.parent_path());
}

TEST(bundle_asset_that_differs_from_the_manifest_is_refused) {
    const auto dest = temp_dir("corrupt") / "data";
    auto apk = sample_apk();
    const auto manifest = apk.manifest();
    apk.files["open-common.tree"] = "abc0def2";   // same size, other bytes
    const auto r = ensure_bundled_data(manifest, dest, apk.reader());
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(r.error.find("open-common.tree") != std::string::npos);
    ASSERT_TRUE(!fs::exists(dest / BUNDLE_STAMP));
    fs::remove_all(dest.parent_path());
}

TEST(bundle_without_a_manifest_fails_and_keeps_the_old_copy) {
    const auto dest = temp_dir("nomanifest") / "data";
    auto apk = sample_apk();
    ASSERT_TRUE(ensure_bundled_data(apk.manifest(), dest, apk.reader()).ok);
    const auto r = ensure_bundled_data("", dest, apk.reader());
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(r.error.find(BUNDLE_MANIFEST) != std::string::npos);
    ASSERT_EQ(slurp(dest / "open-common.tree"), std::string("abc0def1"));   // nothing was deleted
    fs::remove_all(dest.parent_path());
}
