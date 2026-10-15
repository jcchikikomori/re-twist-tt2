// Importing the original art, fonts and sounds from the player's own Text
// Twist 2 install (docs/design/import-assets.md). Headless: std::filesystem
// only, so every stage is unit-tested.
//
// AssetImportJob, one byte budget per frame:
//   CheckSource   the folder holds properties/resources.xml
//   VerifySource  hash every manifest entry inside the install; any missing
//                 or different file fails the import and copies nothing
//   Copy          copy the entries into <assets_dir>.staging
//   VerifyCopy    hash the copies again (disk or permission trouble)
//   ReadyToCommit wait: the host frees its textures, sounds and music
//                 first (Windows can't rename a folder with open files)
//   commit()      <dir> -> <dir>.old, <dir>.staging -> <dir>, drop .old
//
// Only manifest entries are copied, and the manifest can't list DRM,
// code or dictionary files (tt2/asset_manifest.hpp), so nothing else in
// the install is ever opened.
//
// <assets_dir>/import-stamp.txt records which manifest the folder was
// verified against; import_status() reads it on every launch without
// hashing anything.
#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "tt2/asset_manifest.hpp"
#include "tt2/sha256.hpp"

namespace tt2ui {

inline constexpr const char* IMPORT_STAMP_FILE = "import-stamp.txt";
inline constexpr std::size_t IMPORT_ERROR_PATHS = 5;   // paths kept for the error dialog

namespace import_detail {
// SDL_GetPrefPath ends in a separator; "<dir>.staging" needs the bare name.
inline std::filesystem::path bare(const std::filesystem::path& dir) {
    return dir.has_filename() ? dir : dir.parent_path();
}
inline std::filesystem::path sibling(const std::filesystem::path& dir, const char* suffix) {
    std::filesystem::path p = bare(dir);
    p += suffix;
    return p;
}
}  // namespace import_detail

inline std::filesystem::path staging_dir(const std::filesystem::path& assets_dir) {
    return import_detail::sibling(assets_dir, ".staging");
}
inline std::filesystem::path old_dir(const std::filesystem::path& assets_dir) {
    return import_detail::sibling(assets_dir, ".old");
}

// The manifest a folder was verified against, by the digest of its text.
inline std::string manifest_digest(const tt2::AssetManifest& m) { return tt2::to_hex(tt2::sha256(m.serialize())); }

struct ImportStamp {
    std::string manifest;   // manifest_digest()
    std::size_t files = 0;

    friend bool operator==(const ImportStamp& a, const ImportStamp& b) {
        return a.manifest == b.manifest && a.files == b.files;
    }
};

inline std::string serialize_stamp(const ImportStamp& s) {
    return "format=re-twist-tt2 asset import\nversion=1\nmanifest=" + s.manifest + "\nfiles=" +
           std::to_string(s.files) + "\n";
}

// Strict, like settings.txt: a line it doesn't know means "not imported".
inline bool parse_stamp(std::string_view text, ImportStamp& out) {
    ImportStamp s;
    bool format = false, version = false, manifest = false, files = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return false;
        const std::string_view key = line.substr(0, eq), val = line.substr(eq + 1);
        if (!format) {
            if (key != "format" || val != "re-twist-tt2 asset import") return false;
            format = true;
        } else if (key == "version") {
            if (val != "1") return false;
            version = true;
        } else if (key == "manifest") {
            tt2::Sha256::Digest d{};
            if (!tt2::parse_hex_digest(val, d)) return false;
            s.manifest = std::string(val);
            manifest = true;
        } else if (key == "files") {
            if (val.empty() || val.size() > 9 || val.find_first_not_of("0123456789") != std::string_view::npos)
                return false;
            s.files = static_cast<std::size_t>(std::stoul(std::string(val)));
            files = true;
        } else {
            return false;
        }
    }
    if (!format || !version || !manifest || !files) return false;
    out = s;
    return true;
}

// A crash between commit()'s two renames leaves <dir>.old and no <dir>:
// put the previous import back. A leftover .old next to a complete <dir>,
// or a leftover staging folder, is dropped.
inline void recover_interrupted_import(const std::filesystem::path& assets_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = import_detail::bare(assets_dir), old = old_dir(assets_dir);
    if (fs::exists(old, ec)) {
        if (!fs::exists(dir, ec)) fs::rename(old, dir, ec);
        else fs::remove_all(old, ec);
    }
    fs::remove_all(staging_dir(assets_dir), ec);
}

enum class ImportStatus {
    NotImported,   // no valid stamp
    Imported,      // stamped with this manifest, every file present
    Stale,         // stamped with another manifest (a newer release changed it)
    Incomplete,    // stamped, but a file has gone missing
};

// Cheap enough for every launch: reads the stamp and checks that files
// exist; hashes nothing. Recovers an interrupted commit first.
inline ImportStatus import_status(const std::filesystem::path& assets_dir, const tt2::AssetManifest& m) {
    namespace fs = std::filesystem;
    recover_interrupted_import(assets_dir);
    std::ifstream in(assets_dir / IMPORT_STAMP_FILE, std::ios::binary);
    if (!in) return ImportStatus::NotImported;
    std::ostringstream ss;
    ss << in.rdbuf();
    ImportStamp stamp;
    if (!parse_stamp(ss.str(), stamp)) return ImportStatus::NotImported;
    if (stamp.manifest != manifest_digest(m) || stamp.files != m.entries.size()) return ImportStatus::Stale;
    std::error_code ec;
    for (const auto& e : m.entries)
        if (!fs::is_regular_file(assets_dir / fs::u8path(e.path), ec)) return ImportStatus::Incomplete;
    return ImportStatus::Imported;
}

struct ImportError {
    enum class Kind {
        None,
        NotAnInstall,    // no properties/resources.xml
        EmptyManifest,   // nothing to import
        Missing,         // a manifest file isn't in the install
        Mismatch,        // a file differs (another version of the game)
        WriteFailed,     // copying, or the copy didn't verify
        CommitFailed,    // the folder swap failed; the old import is kept
    };
    Kind kind = Kind::None;
    std::vector<std::string> paths;   // the first IMPORT_ERROR_PATHS problem files
    std::size_t count = 0;            // every problem file
};

class AssetImportJob {
public:
    enum class Stage { CheckSource, VerifySource, Copy, VerifyCopy, ReadyToCommit, Done, Failed };

    AssetImportJob(std::filesystem::path source, std::filesystem::path assets_dir, tt2::AssetManifest m)
        : source_(std::move(source)), dir_(import_detail::bare(assets_dir)), manifest_(std::move(m)) {}

    Stage stage() const { return stage_; }
    const ImportError& error() const { return error_; }
    bool finished() const { return stage_ == Stage::ReadyToCommit || stage_ == Stage::Done || stage_ == Stage::Failed; }

    // 0..1 by bytes across the three passes (verify, copy, verify again).
    float progress() const {
        if (stage_ == Stage::ReadyToCommit || stage_ == Stage::Done) return 1.0f;
        if (total_bytes_ == 0) return 0.0f;
        const float p = static_cast<float>(done_bytes_) / static_cast<float>(total_bytes_ * 3);
        return p < 1.0f ? p : 1.0f;
    }

    // Does work until about `byte_budget` bytes were read, a stage ends, or
    // the job finishes. Always makes some progress, even with a budget of 0.
    void step(std::size_t byte_budget) {
        std::size_t used = 0;
        do {
            switch (stage_) {
            case Stage::CheckSource: check_source(); break;
            case Stage::VerifySource: used += hash_step(source_, false); break;
            case Stage::Copy: used += copy_step(); break;
            case Stage::VerifyCopy: used += hash_step(staging_dir(dir_), true); break;
            default: return;
            }
        } while (used < byte_budget && !finished());
    }

    // Swaps the verified copy into place and stamps it. Call it only after
    // the host has released everything it loaded from the asset folder.
    bool commit() {
        namespace fs = std::filesystem;
        if (stage_ != Stage::ReadyToCommit) return false;
        const fs::path staging = staging_dir(dir_), old = old_dir(dir_);
        {
            std::ofstream out(staging / IMPORT_STAMP_FILE, std::ios::binary | std::ios::trunc);
            out << serialize_stamp({manifest_digest(manifest_), manifest_.entries.size()});
            if (!out.flush()) return fail(ImportError::Kind::CommitFailed, IMPORT_STAMP_FILE);
        }
        std::error_code ec;
        fs::remove_all(old, ec);
        const bool had_dir = fs::exists(dir_, ec);
        if (had_dir) {
            fs::rename(dir_, old, ec);
            if (ec) return fail(ImportError::Kind::CommitFailed, dir_.u8string());
        } else {
            fs::create_directories(dir_.parent_path(), ec);
        }
        fs::rename(staging, dir_, ec);
        if (ec) {
            if (had_dir) fs::rename(old, dir_, ec);   // put the previous import back
            return fail(ImportError::Kind::CommitFailed, dir_.u8string());
        }
        fs::remove_all(old, ec);
        stage_ = Stage::Done;
        return true;
    }

private:
    static constexpr std::size_t CHUNK = 64 * 1024;

    bool fail(ImportError::Kind kind, const std::string& path = {}) {
        if (error_.kind == ImportError::Kind::None) error_.kind = kind;
        if (!path.empty()) note(path);
        std::error_code ec;
        std::filesystem::remove_all(staging_dir(dir_), ec);
        in_.close();
        out_.close();
        stage_ = Stage::Failed;
        return false;
    }
    void note(const std::string& path) {
        if (error_.paths.size() < IMPORT_ERROR_PATHS) error_.paths.push_back(path);
        ++error_.count;
    }

    void check_source() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_regular_file(source_ / tt2::INSTALL_PROBE, ec)) {
            fail(ImportError::Kind::NotAnInstall);
            return;
        }
        if (manifest_.entries.empty()) {
            fail(ImportError::Kind::EmptyManifest);
            return;
        }
        for (const auto& e : manifest_.entries) {
            const auto size = fs::file_size(source_ / fs::u8path(e.path), ec);
            if (!ec) total_bytes_ += static_cast<std::size_t>(size);
        }
        index_ = 0;
        stage_ = Stage::VerifySource;
    }

    // One unit of hashing: open the next file, or read one chunk of it, or
    // finish it. Returns the bytes read. `copy` picks the pass.
    std::size_t hash_step(const std::filesystem::path& root, bool copy) {
        namespace fs = std::filesystem;
        if (index_ == manifest_.entries.size()) {
            index_ = 0;
            if (error_.count > 0) {
                fail(copy ? ImportError::Kind::WriteFailed : (mismatch_ ? ImportError::Kind::Mismatch
                                                                        : ImportError::Kind::Missing));
            } else {
                stage_ = copy ? Stage::ReadyToCommit : Stage::Copy;
            }
            return 0;
        }
        const auto& e = manifest_.entries[index_];
        if (!in_.is_open()) {
            std::error_code ec;
            const fs::path p = root / fs::u8path(e.path);
            if (fs::is_regular_file(p, ec)) in_.open(p, std::ios::binary);
            if (!in_.is_open()) {
                note(e.path);   // missing or unreadable
                ++index_;
            }
            hasher_.reset();
            return 0;
        }
        buf_.resize(CHUNK);
        in_.read(buf_.data(), static_cast<std::streamsize>(buf_.size()));
        const auto got = static_cast<std::size_t>(in_.gcount());
        hasher_.update(buf_.data(), got);
        done_bytes_ += got;
        if (in_.bad()) {
            in_.close();
            note(e.path);
            ++index_;
        } else if (in_.eof()) {
            in_.close();
            if (hasher_.finish() != e.sha256) {
                if (!copy) mismatch_ = true;
                note(e.path);
            }
            ++index_;
        }
        return got;
    }

    // One unit of copying, into the staging folder.
    std::size_t copy_step() {
        namespace fs = std::filesystem;
        const fs::path staging = staging_dir(dir_);
        std::error_code ec;
        if (index_ == 0 && !in_.is_open() && !started_copy_) {
            fs::remove_all(staging, ec);   // a leftover from an earlier run
            started_copy_ = true;
        }
        if (index_ == manifest_.entries.size()) {
            index_ = 0;
            stage_ = Stage::VerifyCopy;
            return 0;
        }
        const auto& e = manifest_.entries[index_];
        if (!in_.is_open()) {
            const fs::path to = staging / fs::u8path(e.path);
            fs::create_directories(to.parent_path(), ec);
            in_.open(source_ / fs::u8path(e.path), std::ios::binary);
            out_.open(to, std::ios::binary | std::ios::trunc);
            if (!in_ || !out_) fail(ImportError::Kind::WriteFailed, e.path);
            return 0;
        }
        buf_.resize(CHUNK);
        in_.read(buf_.data(), static_cast<std::streamsize>(buf_.size()));
        const auto got = static_cast<std::size_t>(in_.gcount());
        out_.write(buf_.data(), static_cast<std::streamsize>(got));
        done_bytes_ += got;
        if (in_.bad() || !out_) {
            fail(ImportError::Kind::WriteFailed, e.path);
        } else if (in_.eof()) {
            in_.close();
            out_.close();
            if (out_.fail()) fail(ImportError::Kind::WriteFailed, e.path);
            else ++index_;
        }
        return got;
    }

    std::filesystem::path source_;
    std::filesystem::path dir_;
    tt2::AssetManifest manifest_;
    Stage stage_ = Stage::CheckSource;
    ImportError error_;
    std::size_t index_ = 0;
    std::size_t total_bytes_ = 0;   // one pass
    std::size_t done_bytes_ = 0;    // across all three passes
    bool mismatch_ = false;
    bool started_copy_ = false;
    std::ifstream in_;
    std::ofstream out_;
    tt2::Sha256 hasher_;
    std::vector<char> buf_;
};

}  // namespace tt2ui
