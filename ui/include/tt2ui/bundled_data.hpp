// Android: the game's data/ folder copied out of the APK (T1.1 of
// docs/plans/android-support.md).
//
// The APK carries data/ as assets (android/app/build.gradle.kts), and
// assets are invisible to std::filesystem, which the importer, the
// dictionary loader and the placeholder art all use. So the first launch
// (and the first launch after an update) copies them into the app's own
// files folder, and from then on the game reads plain files as it does on
// desktop.
//
// Gradle writes BUNDLE_MANIFEST next to the assets, one line per file:
//   <sha256 hex> <size> <path relative to data/>
// The copy is stamped with that manifest's exact text, so a new APK whose
// files differ (a new version, or just a rebuilt placeholder) recopies,
// and an unchanged one starts straight away. The stamp is written last:
// a copy cut short (app killed, disk full) has none and is redone.
//
// No SDL here: reading an asset is a callback, so the tests feed files
// from memory and main.cpp reads them with SDL_LoadFile.
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "tt2/sha256.hpp"
#include "tt2/utf8_path.hpp"

namespace tt2ui {

inline constexpr const char* BUNDLE_MANIFEST = "bundle-manifest.txt";
inline constexpr const char* BUNDLE_STAMP = "bundle-stamp.txt";

struct BundleEntry {
    std::string sha256;   // lower-case hex
    std::uintmax_t size = 0;
    std::string path;     // relative to data/, '/' separators
};

// A path that stays inside data/: relative, no "..", no empty parts.
inline bool safe_bundle_path(const std::string& p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos)
        return false;
    std::size_t start = 0;
    while (start <= p.size()) {
        const std::size_t end = std::min(p.find('/', start), p.size());
        const std::string part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

// Empty on any malformed line: a manifest Gradle didn't write is no
// manifest at all, and copying half of it would hide the problem.
inline std::vector<BundleEntry> parse_bundle_manifest(const std::string& text) {
    std::vector<BundleEntry> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream fields(line);
        BundleEntry e;
        std::string size;
        if (!(fields >> e.sha256 >> size) || e.sha256.size() != 64 ||
            e.sha256.find_first_not_of("0123456789abcdef") != std::string::npos ||
            size.find_first_not_of("0123456789") != std::string::npos)
            return {};
        std::getline(fields >> std::ws, e.path);
        if (!safe_bundle_path(e.path)) return {};
        e.size = std::stoull(size);
        out.push_back(std::move(e));
    }
    return out;
}

enum class BundleAction { Keep, Copy };

// `stamp` is the installed copy's BUNDLE_STAMP, empty when there is none.
inline BundleAction plan_bundle_copy(const std::string& manifest, const std::string& stamp) {
    return !stamp.empty() && stamp == manifest ? BundleAction::Keep : BundleAction::Copy;
}

struct BundleResult {
    bool ok = false;
    BundleAction action = BundleAction::Copy;
    std::size_t files = 0;   // copied this run
    std::string error;       // for the log: which file, and why
};

// Reads one asset (path relative to data/) into `bytes`; false if missing.
using ReadBundleAsset = std::function<bool(const std::string& path, std::string& bytes)>;

namespace bundle_detail {
inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline bool write_file(const std::filesystem::path& p, const std::string& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}
}  // namespace bundle_detail

// Makes `dest` (the files folder's data/) match `manifest`. `dest` holds
// only bundled files, so a copy starts from an empty folder: files a newer
// APK dropped don't linger. Every file is checked against its size and
// SHA-256 before it is written.
inline BundleResult ensure_bundled_data(const std::string& manifest, const std::filesystem::path& dest,
                                        const ReadBundleAsset& read_asset) {
    BundleResult r;
    const auto entries = parse_bundle_manifest(manifest);
    if (entries.empty()) {
        r.error = std::string(BUNDLE_MANIFEST) + ": missing or malformed";
        return r;
    }
    if (plan_bundle_copy(manifest, bundle_detail::read_file(dest / BUNDLE_STAMP)) == BundleAction::Keep) {
        r.ok = true;
        r.action = BundleAction::Keep;
        return r;
    }
    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        r.error = dest.string() + ": " + ec.message();
        return r;
    }
    std::string bytes;
    for (const auto& e : entries) {
        bytes.clear();
        if (!read_asset(e.path, bytes)) {
            r.error = e.path + ": not in the APK";
            return r;
        }
        if (bytes.size() != e.size || tt2::to_hex(tt2::sha256(bytes)) != e.sha256) {
            r.error = e.path + ": size or SHA-256 differs from " + BUNDLE_MANIFEST;
            return r;
        }
        if (!bundle_detail::write_file(dest / tt2::utf8_path(e.path), bytes)) {
            r.error = e.path + ": could not write under " + dest.string();
            return r;
        }
        ++r.files;
    }
    if (!bundle_detail::write_file(dest / BUNDLE_STAMP, manifest)) {
        r.error = std::string(BUNDLE_STAMP) + ": could not write under " + dest.string();
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace tt2ui
