// The import contract between the Android importer (Kotlin) and the game:
// ui/tests/import-contract/ (README.md there). Kotlin's ImportContractTest
// reads the same files.
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
const fs::path CONTRACT = fs::path(TT2_UI_TEST_DIR) / "import-contract";

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void put(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

tt2::AssetManifest contract_manifest() {
    tt2::AssetManifest m;
    tt2::AssetManifest::parse(slurp(CONTRACT / "manifest.sha256"), m);
    return m;
}

// The folder ZipImporter leaves: every file at its manifest path, the stamp
// next to them, nothing else.
struct KotlinLayout {
    fs::path dir;
    explicit KotlinLayout(const tt2::AssetManifest& m) {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("tt2_contract_" + std::to_string(rd())) / "imported-assets";
        for (const auto& e : m.entries) put(dir / e.path, e.path);   // a file's bytes are its path
        put(dir / IMPORT_STAMP_FILE, slurp(CONTRACT / IMPORT_STAMP_FILE));
    }
    ~KotlinLayout() {
        std::error_code ec;
        fs::remove_all(dir.parent_path(), ec);
    }
};
}  // namespace

TEST(contract_manifest_is_canonical_and_self_consistent) {
    const std::string text = slurp(CONTRACT / "manifest.sha256");
    tt2::AssetManifest m;
    ASSERT_TRUE(tt2::AssetManifest::parse(text, m));
    ASSERT_EQ(m.entries.size(), std::size_t{3});
    ASSERT_EQ(m.serialize(), text);   // so its digest is the digest of the file
    for (const auto& e : m.entries) ASSERT_TRUE(e.sha256 == tt2::sha256(e.path));
}

TEST(contract_stamp_is_what_serialize_stamp_writes) {
    const tt2::AssetManifest m = contract_manifest();
    const std::string golden = slurp(CONTRACT / IMPORT_STAMP_FILE);
    ASSERT_EQ(serialize_stamp({manifest_digest(m), m.entries.size()}), golden);
    ImportStamp parsed;
    ASSERT_TRUE(parse_stamp(golden, parsed));
    ASSERT_EQ(parsed.files, std::size_t{3});
}

TEST(contract_kotlin_layout_is_imported) {
    const tt2::AssetManifest m = contract_manifest();
    KotlinLayout k(m);
    ASSERT_TRUE(import_status(k.dir, m) == ImportStatus::Imported);
}

TEST(contract_kotlin_layout_negative_cases) {
    const tt2::AssetManifest m = contract_manifest();
    {
        KotlinLayout k(m);
        fs::remove(k.dir / m.entries[1].path);
        ASSERT_TRUE(import_status(k.dir, m) == ImportStatus::Incomplete);
    }
    {
        KotlinLayout k(m);
        put(k.dir / IMPORT_STAMP_FILE, slurp(CONTRACT / IMPORT_STAMP_FILE) + "extra=1\n");   // a stamp drifts
        ASSERT_TRUE(import_status(k.dir, m) == ImportStatus::NotImported);
    }
    {
        KotlinLayout k(m);
        tt2::AssetManifest other = m;
        other.entries.pop_back();   // a newer release changed the manifest
        ASSERT_TRUE(import_status(k.dir, other) == ImportStatus::Stale);
    }
}
