// tt2-tree — dictionary tool
// =========================
//
// Replaces the old TypeScript `tools/tree-cli.ts`. Subcommands:
//
//   decode <in.tree> [out.txt]            — dump words, one per line
//   decode --stats <in.tree>              — word count + length histogram
//   encode <in.txt> <out.tree>            — build a .tree from a word list
//   merge  <a.tree> <b.tree> <out.tree>   — union of two .tree files
//   diff   <a.tree> <b.tree>              — set difference (a − b)
//   verify <file.tree>                    — round-trip the file
//
// All subcommands are byte-exact and produce canonical encodings.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tt2/word_tree.hpp"

namespace {

[[noreturn]] void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  tt2-tree decode <in.tree> [out.txt]\n"
        "  tt2-tree decode --stats <in.tree>\n"
        "  tt2-tree encode <in.txt> <out.tree>\n"
        "  tt2-tree merge  <a.tree> <b.tree> <out.tree>\n"
        "  tt2-tree diff   <a.tree> <b.tree>\n"
        "  tt2-tree verify <file.tree>\n");
    std::exit(2);
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string slurp_text(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_binary(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "cannot open %s for writing\n", path.c_str());
        std::exit(2);
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

int cmd_decode(int argc, char** argv) {
    bool stats = false;
    std::string in_path;
    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--stats") stats = true;
        else if (in_path.empty()) in_path = a;
    }
    if (in_path.empty()) usage();

    const std::string bytes = slurp(in_path);
    const auto tree = tt2::WordTree::parse(bytes);
    if (stats) {
        std::map<std::size_t, std::size_t> hist;
        for (const auto& w : tree.words()) ++hist[w.size()];
        std::printf("words: %zu\n", tree.size());
        for (const auto& [len, c] : hist) std::printf("  %zu: %zu\n", len, c);
        return 0;
    }
    for (const auto& w : tree.words()) std::printf("%s\n", w.c_str());
    return 0;
}

int cmd_encode(int argc, char** argv) {
    if (argc != 2) usage();
    const std::string in_path  = argv[0];
    const std::string out_path = argv[1];
    const std::string text = slurp_text(in_path);
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // Normalize: lowercase, strip whitespace, skip empty / non a-z.
        std::string w;
        for (char c : line) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            if (c >= 'a' && c <= 'z') w.push_back(c);
            else if (!w.empty()) break;
        }
        if (w.empty()) continue;
        words.push_back(w);
    }
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    const auto tree = tt2::WordTree::from_words(words);
    write_binary(out_path, tree.serialize());
    std::fprintf(stderr, "wrote %zu words → %s\n", tree.size(), out_path.c_str());
    return 0;
}

int cmd_merge(int argc, char** argv) {
    if (argc != 3) usage();
    const auto a = tt2::WordTree::parse(slurp(argv[0]));
    const auto b = tt2::WordTree::parse(slurp(argv[1]));
    std::vector<std::string> words = a.words();
    auto bwords = b.words();
    words.insert(words.end(), bwords.begin(), bwords.end());
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    const auto out = tt2::WordTree::from_words(words);
    write_binary(argv[2], out.serialize());
    std::fprintf(stderr, "merged: %zu words → %s\n", out.size(), argv[2]);
    return 0;
}

int cmd_diff(int argc, char** argv) {
    if (argc != 2) usage();
    const auto a = tt2::WordTree::parse(slurp(argv[0]));
    const auto b = tt2::WordTree::parse(slurp(argv[1]));
    std::set<std::string> bs(b.words().begin(), b.words().end());
    for (const auto& w : a.words()) if (!bs.count(w)) std::printf("%s\n", w.c_str());
    return 0;
}

int cmd_verify(int argc, char** argv) {
    if (argc != 1) usage();
    const std::string bytes = slurp(argv[0]);
    const auto t = tt2::WordTree::parse(bytes);
    const std::string rt = t.serialize();
    if (rt == bytes) {
        std::printf("OK  %s  (%zu words, %zu bytes, round-trip identical)\n",
                    argv[0], t.size(), bytes.size());
        return 0;
    }
    std::fprintf(stderr, "FAIL %s  round-trip mismatch (%zu → %zu bytes)\n",
                 argv[0], bytes.size(), rt.size());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) usage();
    const std::string cmd = argv[1];
    int rc = 0;
    if (cmd == "decode") rc = cmd_decode(argc - 2, argv + 2);
    else if (cmd == "encode") rc = cmd_encode(argc - 2, argv + 2);
    else if (cmd == "merge")  rc = cmd_merge(argc - 2, argv + 2);
    else if (cmd == "diff")   rc = cmd_diff(argc - 2, argv + 2);
    else if (cmd == "verify") rc = cmd_verify(argc - 2, argv + 2);
    else usage();
    return rc;
}
