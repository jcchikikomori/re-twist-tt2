// Trophies: when the 24 ACHIEVEMENT slots of a player are won.
//
// CONFIRMED (REA, docs/reverse-engineering/trophies.md):
//   - A slot is 0 (not won), 1 (won, not seen in the trophy room yet) or
//     2 (seen). Every award site checks for 0 and writes 1, so a trophy is
//     never won twice; the trophy room turns 1 into 2 after its reveal.
//   - Every threshold is an exact match (== 5, == 1000, ...), so a counter
//     that is already past it never awards the trophy.
//   - Each function returns the trophy id it awarded, or -1, the way the
//     original's helpers feed the game scene's popup queue.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tt2/profile.hpp"

namespace tt2 {

// Slot order = TEXT_TROPHY_DESCRIPTIONS = ACHIEVEMENT/0..23.
enum Trophy : int {
    TROPHY_QUICK_DRAW = 0,
    TROPHY_STRAIGHT_SHOOTER = 1,
    TROPHY_BULLS_EYE = 2,
    TROPHY_BRAINSTORM = 3,
    TROPHY_ROLLING_THUNDER = 4,
    TROPHY_LIGHTNING_REFLEXES = 5,
    TROPHY_NICK_OF_TIME = 6,
    TROPHY_SPEED_SPELLER = 7,
    TROPHY_PUZZLE_PRO = 8,
    TROPHY_PERFECT_PUZZLER = 9,
    TROPHY_LOOK_MA = 10,
    TROPHY_BINGO_BLASTER = 11,
    TROPHY_BINGO_BOSS = 12,
    TROPHY_BONKERS_FOR_BINGOS = 13,
    TROPHY_WITH_A_TWIST = 14,
    TROPHY_DIZZY = 15,
    TROPHY_TWISTED_TWISTER = 16,
    TROPHY_WORD_NERD = 17,
    TROPHY_WORDSMITH = 18,
    TROPHY_SUPER_SPELLER = 19,
    TROPHY_WELL_ROUNDED = 20,
    TROPHY_MANIAC = 21,
    TROPHY_FANATIC = 22,
    TROPHY_HUMAN_DICTIONARY = 23,
};

inline constexpr std::int32_t TROPHY_NOT_WON = 0;
inline constexpr std::int32_t TROPHY_NEW = 1;    // won, the trophy room has not shown it yet
inline constexpr std::int32_t TROPHY_SEEN = 2;

// 0x428840: Speed Speller when the round's 120 s clock still reads
// 115000..120000 ms, i.e. the round was finished within 5 s. The clock
// runs in every mode (hidden in Untimed), so this takes the elapsed time.
inline constexpr int SPEED_SPELLER_MS = 5000;
// 0x40f2b0 caps the smallest per-mode round count at 99.
inline constexpr std::int32_t ROUND_COUNT_CAP = 99;

// Win `id` if it is not won yet: the slot becomes TROPHY_NEW.
inline int award_trophy(PlayerProfile& p, int id) {
    if (id < 0 || static_cast<std::size_t>(id) >= TROPHY_COUNT) return -1;
    std::int32_t& slot = p.trophies[static_cast<std::size_t>(id)];
    if (slot != TROPHY_NOT_WON) return -1;
    slot = TROPHY_NEW;
    return id;
}

namespace trophy_detail {
// The three-tier helpers: `count` == a / b / c wins first / first+1 / first+2.
inline int tiers(PlayerProfile& p, std::int32_t count, int first, std::int32_t a, std::int32_t b, std::int32_t c) {
    if (count == a) return award_trophy(p, first);
    if (count == b) return award_trophy(p, first + 1);
    if (count == c) return award_trophy(p, first + 2);
    return -1;
}

// One check of a round-end award: win `id` when `ok`, and list it in `won`.
inline void award_if(PlayerProfile& p, std::vector<int>& won, bool ok, int id) {
    const int r = ok ? award_trophy(p, id) : -1;
    if (r >= 0) won.push_back(r);
}
}  // namespace trophy_detail

// 0x40f120: accepted words in a row without a twist (scene +0x36c).
inline int award_no_twist_streak(PlayerProfile& p, std::int32_t streak) {
    return trophy_detail::tiers(p, streak, TROPHY_QUICK_DRAW, 5, 10, 20);
}

// 0x40f260: bingo words in a row (scene +0x374); any other accepted word
// resets the streak, a new round does not.
inline int award_bingo_streak(PlayerProfile& p, std::int32_t streak) {
    return trophy_detail::tiers(p, streak, TROPHY_BINGO_BLASTER, 5, 10, 20);
}

// 0x40f170: one more twist (TRACKER/TWISTS).
inline int count_twist(PlayerProfile& p) {
    p.twists += 1;
    return trophy_detail::tiers(p, p.twists, TROPHY_WITH_A_TWIST, 1000, 5000, 10000);
}

// 0x40f1e0: one more word spelled (TRACKER/SOLVED).
inline int count_word(PlayerProfile& p) {
    p.solved += 1;
    return trophy_detail::tiers(p, p.solved, TROPHY_WORD_NERD, 1000, 5000, 10000);
}

// 0x40f3a0: one more uncommon word (TRACKER/HARD_SOLVED).
inline int count_uncommon(PlayerProfile& p) {
    p.hard_solved += 1;
    return p.hard_solved == 500 ? award_trophy(p, TROPHY_HUMAN_DICTIONARY) : -1;
}

// 0x40f2b0: one more round completed in `mode` (TRACKER/0..4, indexed like
// GameMode). The smallest count over all five modes, capped at 99, wins
// one tier per call: 30+ Fanatic, 10..29 Maniac, 1..9 Well Rounded. With
// 30+ and Fanatic already won it returns -1 without trying the lower ones.
inline int count_round(PlayerProfile& p, GameMode mode) {
    p.rounds_completed[static_cast<std::size_t>(mode)] += 1;
    std::int32_t m = ROUND_COUNT_CAP;
    for (std::int32_t r : p.rounds_completed) m = std::min(m, r);
    if (m >= 30) return award_trophy(p, TROPHY_FANATIC);
    if (m >= 10) return award_trophy(p, TROPHY_MANIAC);
    if (m >= 1) return award_trophy(p, TROPHY_WELL_ROUNDED);
    return -1;
}

// 0x428840: a finished round wins Speed Speller when it took 5 s or less.
inline int award_speed_speller(PlayerProfile& p, int elapsed_ms) {
    return elapsed_ms >= 0 && elapsed_ms <= SPEED_SPELLER_MS ? award_trophy(p, TROPHY_SPEED_SPELLER) : -1;
}

// 0x424730: every slot filled in a round without a twist.
inline int award_look_ma(PlayerProfile& p, bool twisted_this_round) {
    return twisted_this_round ? -1 : award_trophy(p, TROPHY_LOOK_MA);
}

// 0x4116e0, the round's last word in Lightning: one check per trophy, in
// id order, with the round's flags and the clock before anything is added.
inline constexpr int NICK_OF_TIME_MS = 3000;   // `t < 3001`
struct LightningRoundFlags {
    bool twisted = false;   // +0x3bc
    bool passed = false;    // +0x3bd
    bool hinted = false;    // +0x3be
    int time_ms = 0;        // +0x2fc
};
inline std::vector<int> award_lightning_round(PlayerProfile& p, const LightningRoundFlags& f) {
    std::vector<int> won;
    trophy_detail::award_if(p, won, !f.twisted, TROPHY_BRAINSTORM);
    trophy_detail::award_if(p, won, !f.passed, TROPHY_ROLLING_THUNDER);
    trophy_detail::award_if(p, won, !f.hinted, TROPHY_LIGHTNING_REFLEXES);
    trophy_detail::award_if(p, won, f.time_ms <= NICK_OF_TIME_MS, TROPHY_NICK_OF_TIME);
    return won;
}

// 0x41e930, the Crossword puzzle solved (every slot filled): one check per
// trophy, in id order, with the round's flags (crossword.md §5.1, §6;
// trophies.md ids 8 and 9). Rejected guesses (popups 0 and 3) set
// neither flag. Well Rounded's TRACKER/3 is counted later, when
// DIALOG_NEXTROUND closes (0x41b000): count_round(p, GameMode::Crossword).
struct CrosswordRoundFlags {
    bool twisted = false;   // +0x370: TWIST pressed this round (0x41b060)
    bool mistake = false;   // +0x371: a solved word taken back this round (0x41eaf0)
};
inline std::vector<int> award_crossword_round(PlayerProfile& p, const CrosswordRoundFlags& f) {
    std::vector<int> won;
    trophy_detail::award_if(p, won, !f.twisted, TROPHY_PUZZLE_PRO);
    trophy_detail::award_if(p, won, !f.mistake, TROPHY_PERFECT_PUZZLER);
    return won;
}

}  // namespace tt2
