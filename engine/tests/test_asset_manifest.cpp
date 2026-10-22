// The original-asset manifest: format, path rules and exclusions.
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "tt2/asset_manifest.hpp"

using namespace tt2;

namespace {
const std::string HASH_A = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";   // sha256("abc")
const std::string HASH_E = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";   // sha256("")
const std::string HEAD = "# re-twist-tt2 original-asset manifest\n# version=1\n";

AssetManifest sample() {
    AssetManifest m;
    m.entries.push_back({"images/gui/button_ok.png", sha256("abc")});
    m.entries.push_back({"music/music_menu.ogg", sha256("")});
    m.entries.push_back({"images/gamescene/tt_gameover.PNG", sha256("png-1")});
    return m;
}

namespace fs = std::filesystem;

fs::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = fs::temp_directory_path() / ("tt2_manifest_test_" + std::string(tag) + "_" + std::to_string(rd()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

void put(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}

std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ui/tests/import-contract/import-roots.txt, one root per line. The Kotlin
// importer's AssetManifestTest reads the same file, so the C++ and Kotlin
// IMPORT_ROOTS can't drift apart. A blank line is kept (and fails), like
// Kotlin's readLines().
std::vector<std::string> shared_import_roots() {
    std::ifstream in(fs::path(TT2_IMPORT_CONTRACT_DIR) / "import-roots.txt", std::ios::binary);
    std::vector<std::string> roots;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        roots.push_back(line);
    }
    return roots;
}

// A fake install made of synthetic bytes, plus files that must never be
// opened: DRM / licensing files at the root and inside images/gui.
fs::path fake_install(const char* tag) {
    const fs::path root = temp_dir(tag);
    put(root / INSTALL_PROBE, "<resources/>");
    put(root / "images/gui/button_ok.png", "png-1");
    put(root / "images/gui/sub/deep.png", "png-2");
    put(root / "images/gamescene/tt_gameover.PNG", "png-3");
    put(root / "fonts/font_score.xml", "xml-1");
    put(root / "sound/click.ogg", "ogg-1");
    put(root / "texttwist2_RADRM.dll", "never");
    put(root / "readme.nfo", "never");
    put(root / "images/gui/texttwist2_RADRM.dll", "never");
    put(root / "images/gui/x.nfo", "never");
    put(root / "images/gamescene/Thumbs.db", "never");
    put(root / "Text Twist 2.exe", "never");
    return root;
}

// Parses `text` into a manifest pre-filled with a marker entry, and checks
// that a rejected text leaves it exactly as it was.
bool rejects(const std::string& text) {
    AssetManifest out;
    out.entries.push_back({"fonts/marker.xml", sha256("marker")});
    const AssetManifest before = out;
    return !AssetManifest::parse(text, out) && out == before;
}
}  // namespace

TEST(manifest_roots_match_the_shared_list) {
    const std::vector<std::string> roots(std::begin(IMPORT_ROOTS), std::end(IMPORT_ROOTS));
    const std::vector<std::string> shared = shared_import_roots();
    ASSERT_EQ(shared.size(), std::size_t{11});   // the import script's 10 folders, plus images/crosswordscene
    ASSERT_TRUE(roots == shared);                // same folders, same order
    ASSERT_EQ(std::string(INSTALL_PROBE), std::string("properties/resources.xml"));
}

TEST(manifest_accepts_an_entry_under_every_shared_root) {
    const std::vector<std::string> shared = shared_import_roots();
    ASSERT_EQ(shared.size(), std::size_t{11});
    for (const std::string& root : shared) {
        AssetManifest m;
        ASSERT_TRUE(AssetManifest::parse(HEAD + HASH_A + "  " + root + "/a.png\n", m));
        ASSERT_EQ(m.entries.size(), std::size_t{1});
        ASSERT_EQ(m.entries[0].path, root + "/a.png");
    }
}

TEST(manifest_rejects_an_entry_under_an_unknown_root) {
    // Near misses of the Crossword scene's folder: only images/crosswordscene/ itself is a root.
    ASSERT_TRUE(!rejects(HEAD + HASH_A + "  images/crosswordscene/background.png\n"));
    for (const char* p : {"images/crosswordscene", "images/crosswordscenes/background.png",
                          "images/crossword/background.png", "crosswordscene/background.png",
                          "images/creditsscene/background.png"})
        ASSERT_TRUE(rejects(HEAD + HASH_A + "  " + p + "\n"));
}

TEST(manifest_serialize_is_sha256sum_format) {
    AssetManifest m;
    m.entries.push_back({"images/gui/button_ok.png", sha256("abc")});
    ASSERT_EQ(m.serialize(), HEAD + HASH_A + "  images/gui/button_ok.png\n");
}

TEST(manifest_round_trip) {
    const AssetManifest m = sample();
    AssetManifest back;
    ASSERT_TRUE(AssetManifest::parse(m.serialize(), back));
    ASSERT_TRUE(back == m);
}

TEST(manifest_tolerates_comments_blank_lines_crlf_and_spaces) {
    const std::string text = "# re-twist-tt2 original-asset manifest\r\n"
                             "# made from my own install\r\n"
                             "\r\n"
                             "# version=1\r\n" +
                             HASH_A + "  images/gui/button ok.png\r\n" + "# trailing comment\r\n" + HASH_E +
                             "  sound/a.ogg";   // no final newline
    AssetManifest m;
    ASSERT_TRUE(AssetManifest::parse(text, m));
    ASSERT_EQ(m.entries.size(), std::size_t{2});
    ASSERT_EQ(m.entries[0].path, std::string("images/gui/button ok.png"));
    ASSERT_EQ(to_hex(m.entries[1].sha256), HASH_E);
}

TEST(manifest_with_no_entries_is_valid_and_empty) {
    AssetManifest m = sample();
    ASSERT_TRUE(AssetManifest::parse(HEAD, m));
    ASSERT_TRUE(m.entries.empty());
}

TEST(manifest_rejects_unsafe_paths) {
    for (const char* p : {"../x.png", "images/gui/../../x.png", "/abs.png", "/images/gui/a.png", "C:/x.png",
                          "images/gui/a.png:stream", "images\\gui\\a.png", "images//gui/a.png", "images/./gui/a.png",
                          "images/gui/", "images/gui/a\tb.png"})
        ASSERT_TRUE(rejects(HEAD + HASH_A + "  " + p + "\n"));
}

TEST(manifest_rejects_paths_outside_the_import_roots) {
    for (const char* p : {"properties/resources.xml", "data/words.txt", "readme.txt", "images/gui", "images/other/a.png",
                          "images/guix/a.png", "fonts"})
        ASSERT_TRUE(rejects(HEAD + HASH_A + "  " + p + "\n"));
}

TEST(manifest_rejects_bad_hashes) {
    ASSERT_TRUE(rejects(HEAD + HASH_A.substr(1) + "  fonts/a.xml\n"));                    // 63 characters
    ASSERT_TRUE(rejects(HEAD + HASH_A + "0  fonts/a.xml\n"));                             // 65 characters
    std::string upper = HASH_A;
    for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 'a' + 'A' : c);
    ASSERT_TRUE(rejects(HEAD + upper + "  fonts/a.xml\n"));                               // one canonical form
    ASSERT_TRUE(rejects(HEAD + std::string(63, '0') + "g  fonts/a.xml\n"));               // not hex
    ASSERT_TRUE(rejects(HEAD + HASH_A + " fonts/a.xml\n"));                               // one space
    ASSERT_TRUE(rejects(HEAD + HASH_A + " *fonts/a.xml\n"));                              // sha256sum binary mark
    ASSERT_TRUE(rejects(HEAD + HASH_A + "\n"));                                           // no path
}

TEST(manifest_rejects_duplicate_paths_ignoring_case) {
    ASSERT_TRUE(rejects(HEAD + HASH_A + "  fonts/a.xml\n" + HASH_E + "  fonts/a.xml\n"));
    ASSERT_TRUE(rejects(HEAD + HASH_A + "  fonts/a.xml\n" + HASH_A + "  fonts/A.XML\n"));
}

TEST(manifest_rejects_a_missing_title_or_version) {
    ASSERT_TRUE(rejects(""));
    ASSERT_TRUE(rejects("# version=1\n" + HASH_A + "  fonts/a.xml\n"));                      // no title
    ASSERT_TRUE(rejects("\n" + HEAD + HASH_A + "  fonts/a.xml\n"));                          // title not first
    ASSERT_TRUE(rejects("# re-twist-tt2 original-asset manifest\n" + HASH_A + "  fonts/a.xml\n"));   // no version
    ASSERT_TRUE(rejects("# re-twist-tt2 original-asset manifest\n" + HASH_A + "  fonts/a.xml\n# version=1\n"));
    ASSERT_TRUE(rejects("# re-twist-tt2 original-asset manifest\n# version=2\n"));
    ASSERT_TRUE(rejects("# re-twist-tt2 original-asset manifest\n"));
}

TEST(manifest_rejects_excluded_files) {
    for (const char* p : {"images/gui/texttwist2_RADRM.dll", "images/gui/readme.nfo", "sound/foo.dll", "fonts/words.tree",
                          "music/setup.exe", "images/gamescene/Thumbs.db"})
        ASSERT_TRUE(rejects(HEAD + HASH_A + "  " + p + "\n"));
}

TEST(is_excluded_decides_from_the_name_ignoring_case) {
    ASSERT_TRUE(is_excluded("texttwist2_RADRM.dll"));
    ASSERT_TRUE(is_excluded("TEXTTWIST2_RADRM.DLL"));
    ASSERT_TRUE(is_excluded("deep/folder/Readme.NFO"));
    ASSERT_TRUE(is_excluded("x.exe"));
    ASSERT_TRUE(is_excluded("data/TextTwist2Words.tree"));
    ASSERT_TRUE(is_excluded("images/gamescene/THUMBS.DB"));
    ASSERT_TRUE(is_excluded("desktop.ini"));
    ASSERT_TRUE(is_excluded(".DS_Store"));
    // Only the file name counts, and only a whole extension.
    ASSERT_TRUE(!is_excluded("images/dll/a.png"));
    ASSERT_TRUE(!is_excluded("images/gui/nfo.png"));
    ASSERT_TRUE(!is_excluded("images/gui/a.nfo.png"));
    ASSERT_TRUE(!is_excluded("sound/treehouse.ogg"));
    ASSERT_TRUE(!is_excluded("fonts/font_score.xml"));
}

TEST(is_safe_relative_accepts_ordinary_paths) {
    ASSERT_TRUE(is_safe_relative("a.png"));
    ASSERT_TRUE(is_safe_relative("images/gui/button ok.png"));
    ASSERT_TRUE(is_safe_relative("images/gui/..a.png"));   // a name that starts with dots is still a name
    ASSERT_TRUE(is_safe_relative("sound/x.y.ogg"));
    ASSERT_TRUE(!is_safe_relative(""));
    ASSERT_TRUE(!is_safe_relative(".."));
    ASSERT_TRUE(!is_safe_relative("."));
}

TEST(parse_hex_digest_round_trips_to_hex) {
    Sha256::Digest d{};
    ASSERT_TRUE(parse_hex_digest(HASH_A, d));
    ASSERT_TRUE(d == sha256("abc"));
    ASSERT_EQ(to_hex(d), HASH_A);
    const Sha256::Digest before = d;
    ASSERT_TRUE(!parse_hex_digest("", d));
    ASSERT_TRUE(!parse_hex_digest(std::string(64, 'z'), d));
    ASSERT_TRUE(d == before);   // untouched on failure
}

TEST(scan_install_lists_the_files_under_the_roots_sorted) {
    const fs::path root = fake_install("scan");
    const InstallScan scan = scan_install(root);
    ASSERT_TRUE(scan.status == InstallScan::Status::Ok);
    std::vector<std::string> paths;
    for (const auto& e : scan.manifest.entries) paths.push_back(e.path);
    const std::vector<std::string> want = {"fonts/font_score.xml", "images/gamescene/tt_gameover.PNG",
                                           "images/gui/button_ok.png", "images/gui/sub/deep.png", "sound/click.ogg"};
    ASSERT_TRUE(paths == want);
    ASSERT_TRUE(scan.manifest.entries[2].sha256 == sha256("png-1"));
    ASSERT_TRUE(scan.manifest.entries[4].sha256 == sha256("ogg-1"));
    AssetManifest back;   // what the tool prints parses again
    ASSERT_TRUE(AssetManifest::parse(scan.manifest.serialize(), back));
    ASSERT_TRUE(back == scan.manifest);
    fs::remove_all(root);
}

TEST(scan_install_never_opens_excluded_files) {
    const fs::path root = fake_install("excluded");
    std::set<std::string> opened;
    const HashFile counting = [&](const fs::path& p) {
        opened.insert(p.lexically_relative(root).generic_string());
        return sha256_file(p);
    };
    const InstallScan scan = scan_install(root, counting);
    ASSERT_TRUE(scan.status == InstallScan::Status::Ok);
    ASSERT_EQ(opened.size(), std::size_t{5});
    for (const std::string& name : opened) {
        ASSERT_TRUE(!is_excluded(name));
        ASSERT_TRUE(name.find("RADRM") == std::string::npos);
        ASSERT_TRUE(name.find(".nfo") == std::string::npos);
    }
    fs::remove_all(root);
}

TEST(scan_install_rejects_a_folder_without_the_probe) {
    const fs::path root = fake_install("probe");
    fs::remove(root / INSTALL_PROBE);
    int calls = 0;
    const InstallScan scan = scan_install(root, [&](const fs::path& p) {
        ++calls;
        return sha256_file(p);
    });
    ASSERT_TRUE(scan.status == InstallScan::Status::NotAnInstall);
    ASSERT_TRUE(scan.manifest.entries.empty());
    ASSERT_EQ(calls, 0);
    ASSERT_TRUE(scan_install(root / "does-not-exist").status == InstallScan::Status::NotAnInstall);
    fs::remove_all(root);
}

TEST(scan_install_with_no_roots_is_empty_and_skips_missing_roots) {
    const fs::path root = fake_install("roots");
    const InstallScan none = scan_install(root, sha256_file, {});
    ASSERT_TRUE(none.status == InstallScan::Status::Ok);
    ASSERT_TRUE(none.manifest.entries.empty());
    const InstallScan missing = scan_install(root, sha256_file, {"music", "images/trophyscene"});
    ASSERT_TRUE(missing.status == InstallScan::Status::Ok);   // the script skips them too
    ASSERT_TRUE(missing.manifest.entries.empty());
    fs::remove_all(root);
}

TEST(scan_install_reports_an_unreadable_file) {
    const fs::path root = fake_install("unreadable");
    const InstallScan scan = scan_install(root, [&](const fs::path& p) -> std::optional<Sha256::Digest> {
        if (p.filename() == "click.ogg") return std::nullopt;
        return sha256_file(p);
    });
    ASSERT_TRUE(scan.status == InstallScan::Status::Unreadable);
    ASSERT_EQ(scan.detail, std::string("sound/click.ogg"));
    fs::remove_all(root);
}

TEST(verify_files_reports_missing_and_mismatched_files) {
    const fs::path root = fake_install("verify");
    const AssetManifest m = scan_install(root).manifest;
    ASSERT_TRUE(verify_files(root, m).empty());
    put(root / "images/gui/button_ok.png", "png-X");   // same size, other bytes
    fs::remove(root / "sound/click.ogg");
    put(root / "images/gui/extra.png", "not listed");  // ignored
    const auto problems = verify_files(root, m);
    ASSERT_EQ(problems.size(), std::size_t{2});
    ASSERT_TRUE(problems[0].kind == FileProblem::Kind::Mismatch);
    ASSERT_EQ(problems[0].path, std::string("images/gui/button_ok.png"));
    ASSERT_TRUE(problems[1].kind == FileProblem::Kind::Missing);
    ASSERT_EQ(problems[1].path, std::string("sound/click.ogg"));
    fs::remove_all(root);
}

TEST(shipped_manifest_parses_sorted_and_covers_the_roots) {
    std::ifstream in(fs::path(TT2_SOURCE_DATA_DIR) / "original-assets.sha256", std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    AssetManifest m;
    ASSERT_TRUE(AssetManifest::parse(text, m));   // paths and hashes only; parse rejects excluded names
    ASSERT_TRUE(!m.entries.empty());
    for (std::size_t i = 1; i < m.entries.size(); ++i) ASSERT_TRUE(m.entries[i - 1].path < m.entries[i].path);
    for (const char* root : IMPORT_ROOTS) {
        const std::string prefix = std::string(root) + "/";
        bool found = false;
        for (const auto& e : m.entries) found = found || e.path.compare(0, prefix.size(), prefix) == 0;
        ASSERT_TRUE(found);
    }
    ASSERT_EQ(m.serialize(), text);   // byte-for-byte what tt2-assets prints
}

TEST(shipped_manifest_lists_the_crossword_scene_art) {
    AssetManifest m;
    ASSERT_TRUE(AssetManifest::parse(read_file(fs::path(TT2_SOURCE_DATA_DIR) / "original-assets.sha256"), m));
    ASSERT_EQ(m.entries.size(), std::size_t{329});   // 323 before the Crossword scene, plus its 6 files
    const std::string folder = "images/crosswordscene/";
    std::vector<std::string> crossword;
    for (const auto& e : m.entries)
        if (e.path.compare(0, folder.size(), folder) == 0) crossword.push_back(e.path);
    const std::vector<std::string> want = {
        "images/crosswordscene/background.png", "images/crosswordscene/btn_arrow_next.png",
        "images/crosswordscene/btn_arrow_prev.png", "images/crosswordscene/letterBar.png",
        "images/crosswordscene/letterBox.png", "images/crosswordscene/overlay.png"};
    ASSERT_TRUE(crossword == want);
}
