// tt2-assets — original-asset manifest tool
// =========================================
//
// Subcommands:
//
//   manifest <install>          — print a manifest for the player's own
//                                 Text Twist 2 install (paths + SHA-256 only)
//   verify   <dir> <manifest>   — check a folder against a manifest
//
// The manifest never contains the files themselves. DRM / licensing files,
// code and dictionaries are skipped by name and never opened
// (tt2/asset_manifest.hpp).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tt2/asset_manifest.hpp"

namespace {

[[noreturn]] void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  tt2-assets manifest <install>\n"
        "  tt2-assets verify   <dir> <manifest>\n");
    std::exit(2);
}

int cmd_manifest(int argc, char** argv) {
    if (argc != 1) usage();
    const tt2::InstallScan scan = tt2::scan_install(std::filesystem::u8path(argv[0]));
    switch (scan.status) {
    case tt2::InstallScan::Status::NotAnInstall:
        std::fprintf(stderr, "%s does not look like a Text Twist 2 install (no %s)\n", argv[0], tt2::INSTALL_PROBE);
        return 1;
    case tt2::InstallScan::Status::Unreadable:
        std::fprintf(stderr, "cannot read %s\n", scan.detail.c_str());
        return 1;
    case tt2::InstallScan::Status::Ok:
        break;
    }
    std::fputs(scan.manifest.serialize().c_str(), stdout);
    std::fprintf(stderr, "%zu files\n", scan.manifest.entries.size());
    return 0;
}

int cmd_verify(int argc, char** argv) {
    if (argc != 2) usage();
    std::ifstream in(std::filesystem::u8path(argv[1]), std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    tt2::AssetManifest m;
    if (!tt2::AssetManifest::parse(ss.str(), m)) {
        std::fprintf(stderr, "%s is not a valid manifest\n", argv[1]);
        return 2;
    }
    const auto problems = tt2::verify_files(std::filesystem::u8path(argv[0]), m);
    for (const tt2::FileProblem& p : problems)
        std::printf("%s %s\n", p.kind == tt2::FileProblem::Kind::Missing ? "MISSING" : "MISMATCH", p.path.c_str());
    if (!problems.empty()) return 1;
    std::printf("ok (%zu files)\n", m.entries.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) usage();
    const std::string cmd = argv[1];
    if (cmd == "manifest") return cmd_manifest(argc - 2, argv + 2);
    if (cmd == "verify") return cmd_verify(argc - 2, argv + 2);
    usage();
}
