// The original-asset manifest: format, path rules and exclusions.
#include "test_harness.hpp"

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

// Parses `text` into a manifest pre-filled with a marker entry, and checks
// that a rejected text leaves it exactly as it was.
bool rejects(const std::string& text) {
    AssetManifest out;
    out.entries.push_back({"fonts/marker.xml", sha256("marker")});
    const AssetManifest before = out;
    return !AssetManifest::parse(text, out) && out == before;
}
}  // namespace

TEST(manifest_roots_match_the_import_script) {
    const std::vector<std::string> roots(std::begin(IMPORT_ROOTS), std::end(IMPORT_ROOTS));
    const std::vector<std::string> script = {"images/splash", "images/loadscene", "images/selectionscene",
                                             "images/gui",    "images/gamescene", "images/trophyscene",
                                             "fonts",         "music",            "sound"};
    ASSERT_TRUE(roots == script);
    ASSERT_EQ(std::string(INSTALL_PROBE), std::string("properties/resources.xml"));
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
