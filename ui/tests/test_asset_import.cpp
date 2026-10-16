// Importing the original assets (asset_import.hpp): temp folders,
// synthetic bytes and a manifest built in the test. Never the real
// per-user folder.
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "tt2ui/asset_import.hpp"

using namespace tt2ui;
namespace fs = std::filesystem;

namespace {
fs::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = fs::temp_directory_path() / ("tt2_import_test_" + std::string(tag) + "_" + std::to_string(rd()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

void put(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A fake install, with DRM / licensing files that must never be copied.
// The big file spans several 64 KiB chunks so hashing resumes mid-file.
struct Fixture {
    fs::path root;
    fs::path install;
    fs::path assets;   // the per-user asset folder stand-in
    tt2::AssetManifest manifest;

    explicit Fixture(const char* tag) : root(temp_dir(tag)), install(root / "Text Twist 2"), assets(root / "imported-assets") {
        put(install / tt2::INSTALL_PROBE, "<resources/>");
        put(install / "images/gui/button_ok.png", "png-1");
        put(install / "images/gamescene/tt_gameover.PNG", "png-2");
        put(install / "sound/click.ogg", std::string(200 * 1024 + 7, 'o'));
        put(install / "fonts/empty.xml", "");
        put(install / "texttwist2_RADRM.dll", "never");
        put(install / "images/gui/texttwist2_RADRM.dll", "never");
        put(install / "images/gui/x.nfo", "never");
        manifest = tt2::scan_install(install).manifest;
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    AssetImportJob job() const { return AssetImportJob(install, assets, manifest); }
};

// Runs to the end, checking progress never goes down.
bool run(AssetImportJob& j, std::size_t budget = 16 * 1024) {
    float last = j.progress();
    for (int guard = 0; guard < 100000 && !j.finished(); ++guard) {
        j.step(budget);
        if (j.progress() < last) return false;
        last = j.progress();
    }
    return j.finished();
}

bool no_leftovers(const Fixture& f) { return !fs::exists(staging_dir(f.assets)) && !fs::exists(old_dir(f.assets)); }
}  // namespace

TEST(import_full_run_copies_verifies_and_stamps) {
    Fixture f("full");
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::NotImported);
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.stage() == AssetImportJob::Stage::ReadyToCommit);
    ASSERT_TRUE(j.progress() == 1.0f);
    ASSERT_TRUE(j.commit());
    ASSERT_TRUE(j.stage() == AssetImportJob::Stage::Done);
    ASSERT_TRUE(tt2::verify_files(f.assets, f.manifest).empty());
    ASSERT_EQ(slurp(f.assets / "sound/click.ogg"), slurp(f.install / "sound/click.ogg"));
    ASSERT_TRUE(fs::exists(f.assets / IMPORT_STAMP_FILE));
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_never_copies_drm_or_unlisted_files) {
    Fixture f("drm");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j) && j.commit());
    for (const auto& entry : fs::recursive_directory_iterator(f.assets)) {
        const std::string name = entry.path().filename().string();
        ASSERT_TRUE(name.find("RADRM") == std::string::npos);
        ASSERT_TRUE(name.find(".nfo") == std::string::npos);
        ASSERT_TRUE(name != "resources.xml");   // properties/ is not an asset folder
    }
}

TEST(import_with_a_tiny_budget_resumes_mid_file) {
    Fixture f("tiny");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j, 0));   // one unit of work per step
    ASSERT_TRUE(j.commit());
    ASSERT_TRUE(tt2::verify_files(f.assets, f.manifest).empty());
}

TEST(import_does_not_touch_the_folder_until_commit) {
    Fixture f("nocommit");
    put(f.assets / "previous.txt", "old import");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.stage() == AssetImportJob::Stage::ReadyToCommit);
    ASSERT_EQ(slurp(f.assets / "previous.txt"), std::string("old import"));
    ASSERT_TRUE(!fs::exists(f.assets / IMPORT_STAMP_FILE));
    ASSERT_TRUE(fs::exists(staging_dir(f.assets)));   // waiting for the host
    AssetImportJob again = f.job();                    // a later run clears the leftover
    ASSERT_TRUE(run(again) && again.commit());
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_over_an_existing_import_replaces_it) {
    Fixture f("reimport");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    put(f.assets / "stray.png", "from an older layout");
    AssetImportJob second = f.job();
    ASSERT_TRUE(run(second) && second.commit());
    ASSERT_TRUE(!fs::exists(f.assets / "stray.png"));
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_commit_retries_a_rename_held_for_a_moment) {
    Fixture f("retry");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    AssetImportJob j = f.job();
    int calls = 0;
    j.set_commit_retries(3, 0);
    j.set_rename([&](const fs::path& a, const fs::path& b, std::error_code& ec) {
        if (++calls <= 2) { ec = std::make_error_code(std::errc::permission_denied); return; }
        fs::rename(a, b, ec);
    });
    ASSERT_TRUE(run(j) && j.commit());
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_over_a_folder_that_cannot_move_updates_it_in_place) {
    // Windows: a scanner or Explorer holds the old folder, so it can't be
    // renamed. The verified files are copied over it instead.
    Fixture f("inplace");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    put(f.assets / "images/gui/button_ok.png", "from an older release");
    put(f.assets / IMPORT_STAMP_FILE, serialize_stamp({tt2::to_hex(tt2::sha256("older")), 3}));
    put(f.assets / "stray.png", "older layout");
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Stale);
    AssetImportJob j = f.job();
    j.set_commit_retries(2, 0);
    j.set_rename([&](const fs::path& a, const fs::path& b, std::error_code& ec) {
        if (a == f.assets) { ec = std::make_error_code(std::errc::permission_denied); return; }
        fs::rename(a, b, ec);
    });
    ASSERT_TRUE(run(j) && j.commit());
    ASSERT_EQ(slurp(f.assets / "images/gui/button_ok.png"), std::string("png-1"));
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(fs::exists(f.assets / "stray.png"));   // left alone, nothing reads it
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_puts_the_old_folder_back_when_the_new_one_cannot_move) {
    Fixture f("putback");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    AssetImportJob j = f.job();
    j.set_commit_retries(2, 0);
    j.set_rename([&](const fs::path& a, const fs::path& b, std::error_code& ec) {
        if (a == staging_dir(f.assets)) { ec = std::make_error_code(std::errc::permission_denied); return; }
        fs::rename(a, b, ec);
    });
    ASSERT_TRUE(run(j) && j.commit());
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_in_place_copy_failure_keeps_the_old_stamp) {
    Fixture f("inplacefail");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    put(f.assets / IMPORT_STAMP_FILE, serialize_stamp({tt2::to_hex(tt2::sha256("older")), 3}));
    fs::remove(f.assets / "fonts/empty.xml");
    fs::create_directories(f.assets / "fonts/empty.xml");   // a folder where a file goes: the copy fails
    AssetImportJob j = f.job();
    j.set_commit_retries(2, 0);
    j.set_rename([&](const fs::path& a, const fs::path& b, std::error_code& ec) {
        if (a == f.assets) { ec = std::make_error_code(std::errc::permission_denied); return; }
        fs::rename(a, b, ec);
    });
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(!j.commit());
    ASSERT_TRUE(j.error().kind == ImportError::Kind::CommitFailed);
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Stale);   // asked again next launch
    ASSERT_TRUE(!fs::exists(staging_dir(f.assets)));
}

TEST(import_rejects_a_folder_that_is_not_an_install) {
    Fixture f("notinstall");
    fs::remove(f.install / tt2::INSTALL_PROBE);
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.stage() == AssetImportJob::Stage::Failed);
    ASSERT_TRUE(j.error().kind == ImportError::Kind::NotAnInstall);
    ASSERT_TRUE(!fs::exists(f.assets));
    ASSERT_TRUE(!j.commit());
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_rejects_an_empty_manifest) {
    Fixture f("empty");
    AssetImportJob j(f.install, f.assets, tt2::AssetManifest{});
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.error().kind == ImportError::Kind::EmptyManifest);
    ASSERT_TRUE(!fs::exists(f.assets));
}

TEST(import_one_byte_off_fails_and_copies_nothing) {
    Fixture f("mismatch");
    put(f.install / "images/gui/button_ok.png", "png-X");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.stage() == AssetImportJob::Stage::Failed);
    ASSERT_TRUE(j.error().kind == ImportError::Kind::Mismatch);
    ASSERT_EQ(j.error().count, std::size_t{1});
    ASSERT_EQ(j.error().paths.at(0), std::string("images/gui/button_ok.png"));
    ASSERT_TRUE(!fs::exists(f.assets));
    ASSERT_TRUE(no_leftovers(f));
}

TEST(import_missing_file_fails_with_its_path) {
    Fixture f("missing");
    fs::remove(f.install / "sound/click.ogg");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j));
    ASSERT_TRUE(j.error().kind == ImportError::Kind::Missing);
    ASSERT_EQ(j.error().paths.at(0), std::string("sound/click.ogg"));
    ASSERT_TRUE(!fs::exists(f.assets));
}

TEST(import_error_keeps_five_paths_and_the_full_count) {
    Fixture f("many");
    tt2::AssetManifest m = f.manifest;
    for (int i = 0; i < 8; ++i) m.entries.push_back({"images/gui/gone" + std::to_string(i) + ".png", tt2::sha256("x")});
    AssetImportJob j(f.install, f.assets, m);
    ASSERT_TRUE(run(j));
    ASSERT_EQ(j.error().paths.size(), IMPORT_ERROR_PATHS);
    ASSERT_EQ(j.error().count, std::size_t{8});
}

TEST(import_failed_reimport_keeps_the_previous_import) {
    Fixture f("keep");
    AssetImportJob first = f.job();
    ASSERT_TRUE(run(first) && first.commit());
    put(f.install / "sound/click.ogg", "damaged");
    AssetImportJob second = f.job();
    ASSERT_TRUE(run(second));
    ASSERT_TRUE(second.stage() == AssetImportJob::Stage::Failed);
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(tt2::verify_files(f.assets, f.manifest).empty());
}

TEST(import_status_spots_stale_and_incomplete_imports) {
    Fixture f("status");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j) && j.commit());
    tt2::AssetManifest newer = f.manifest;   // a release that changed the manifest
    newer.entries.push_back({"images/gui/new.png", tt2::sha256("new")});
    ASSERT_TRUE(import_status(f.assets, newer) == ImportStatus::Stale);
    fs::remove(f.assets / "fonts/empty.xml");
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Incomplete);
    put(f.assets / IMPORT_STAMP_FILE, "format=re-twist-tt2 asset import\nversion=2\n");
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::NotImported);
}

TEST(import_recovers_from_a_crash_between_the_renames) {
    Fixture f("crash");
    AssetImportJob j = f.job();
    ASSERT_TRUE(run(j) && j.commit());
    fs::rename(f.assets, old_dir(f.assets));   // the crash: <dir>.old, no <dir>
    put(staging_dir(f.assets) / "half.png", "half");
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
    ASSERT_TRUE(no_leftovers(f));
    put(old_dir(f.assets) / "stale.png", "after the swap");   // the crash after the swap: drop .old
    recover_interrupted_import(f.assets);
    ASSERT_TRUE(no_leftovers(f));
    ASSERT_TRUE(import_status(f.assets, f.manifest) == ImportStatus::Imported);
}

TEST(import_folder_with_a_trailing_separator) {
    // SDL_GetPrefPath ends in a separator: the staging folder is still a sibling.
    Fixture f("slash");
    const fs::path slashed = f.assets / "";
    ASSERT_EQ(staging_dir(slashed), staging_dir(f.assets));
    ASSERT_EQ(staging_dir(f.assets).parent_path(), f.root);
    AssetImportJob j(f.install, slashed, f.manifest);
    ASSERT_TRUE(run(j) && j.commit());
    ASSERT_TRUE(import_status(slashed, f.manifest) == ImportStatus::Imported);
}

TEST(import_stamp_round_trip_and_strict_parse) {
    const ImportStamp s{tt2::to_hex(tt2::sha256("m")), 304};
    ImportStamp back;
    ASSERT_TRUE(parse_stamp(serialize_stamp(s), back));
    ASSERT_TRUE(back == s);
    const ImportStamp before = back;
    ASSERT_TRUE(!parse_stamp("", back));
    ASSERT_TRUE(!parse_stamp("version=1\nformat=re-twist-tt2 asset import\n", back));   // format first
    ASSERT_TRUE(!parse_stamp("format=re-twist-tt2 asset import\nversion=1\nfiles=3\n", back));   // no manifest
    ASSERT_TRUE(!parse_stamp(serialize_stamp(s) + "extra=1\n", back));
    ASSERT_TRUE(!parse_stamp("format=re-twist-tt2 asset import\nversion=1\nmanifest=abc\nfiles=3\n", back));
    ASSERT_TRUE(!parse_stamp("format=re-twist-tt2 asset import\nversion=1\nmanifest=" + s.manifest + "\nfiles=-3\n", back));
    ASSERT_TRUE(back == before);
}

TEST(import_names_for_the_log) {
    ASSERT_EQ(std::string(to_string(ImportStatus::Imported)), std::string("imported"));
    ASSERT_EQ(std::string(to_string(ImportStatus::NotImported)), std::string("not imported"));
    ASSERT_EQ(std::string(to_string(ImportError::Kind::Mismatch)), std::string("files differ from the expected version"));
    ASSERT_EQ(std::string(to_string(ImportError::Kind::None)), std::string("ok"));
}
