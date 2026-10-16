// Driver for the extracted engine. Reads two `.tree` files, builds a
// Classic round, prints the rack, the on-screen slots, and the scoring
// for a small batch of guesses. Also exercises Lightning.
//
// Usage:
//   ./tt2_engine <common.tree> <full.tree>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/random.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/round_lightning.hpp"
#include "tt2/word_tree.hpp"

namespace {

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

const char* status_name(tt2::GuessStatus s) {
    switch (s) {
        case tt2::GuessStatus::NotAWord:  return "NotAWord";
        case tt2::GuessStatus::Duplicate: return "Duplicate";
        case tt2::GuessStatus::NoRoom:    return "NoRoom";
        case tt2::GuessStatus::Uncommon:  return "Uncommon";
        case tt2::GuessStatus::Accepted:  return "Accepted";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <common.tree> <full.tree>\n", argv[0]);
        return 1;
    }

    // 1. Load both dictionaries.
    const std::string common_bytes = slurp(argv[1]);
    const std::string full_bytes   = slurp(argv[2]);
    const auto common = tt2::WordTree::parse(common_bytes);
    const auto full   = tt2::WordTree::parse(full_bytes);
    std::printf("common: %zu words, %zu bytes\n", common.size(), common_bytes.size());
    std::printf("full:   %zu words, %zu bytes\n", full.size(),   full_bytes.size());

    // Round-trip sanity check.
    const std::string rt = common.serialize();
    if (rt != common_bytes) {
        std::fprintf(stderr, "round-trip mismatch for common!\n");
        return 3;
    }
    const std::string rt2 = full.serialize();
    if (rt2 != full_bytes) {
        std::fprintf(stderr, "round-trip mismatch for full!\n");
        return 3;
    }
    std::puts("round-trip: identical (canonical encoding preserved)");

    // 2. Build a Classic round.
    tt2::Random rng(static_cast<std::int64_t>(tt2::millis()));
    tt2::Bag bag(common);
    tt2::ClassicRound::Config cfg;
    cfg.timed           = false;
    cfg.wotd            = false;
    cfg.max_word_length = 8;
    cfg.round_ms        = 0;
    tt2::ClassicRound round(common, full, cfg);
    const auto r = round.start(rng, bag, 0);

    std::printf("\n--- Classic round ---\n");
    std::printf("rack:        %s\n", r.rack.c_str());
    std::printf("slots:       %zu\n", r.slot_count);
    for (const auto& w : r.board_words) std::printf("  - %s\n", w.c_str());

    // 3. Score a few guesses.
    const std::vector<std::string> guesses = {
        r.rack,                  // bingo
        r.rack.substr(0, 3),     // first 3 letters (often a word)
        r.rack.substr(0, 4),     // first 4 letters
        "zzqq",                  // nonsense
        r.rack,                  // duplicate
    };
    for (const auto& g : guesses) {
        const auto rep = round.guess(g);
        std::printf("guess %-10s -> status=%s, points=%d, bingo=%d, filled_last=%d\n",
                    g.c_str(), status_name(rep.status), rep.points_awarded,
                    rep.bingo ? 1 : 0, rep.filled_last ? 1 : 0);
    }
    std::printf("final score: %d  (qualified=%d)\n",
                round.score(), round.qualified() ? 1 : 0);

    // 4. Lightning smoke test: deal round 1, solve the first word.
    tt2::LightningRound lightning(common, full);
    lightning.start(1, rng, bag, 8);
    std::printf("\n--- Lightning round 1 (%zu words) ---\n", lightning.words().size());
    for (const auto& w : lightning.words()) std::printf("word: %s\n", w.word.c_str());
    const auto s1 = lightning.submit(lightning.current_word().word);
    std::printf("solved the first word: +%d, +%d ms (score=%d, time=%d ms)\n", s1.points, s1.time_added_ms,
                lightning.score(), lightning.time_ms());

    return 0;
}
