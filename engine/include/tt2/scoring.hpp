#pragma once

// Scoring rules. Three modes, all from confirmed disassembly.
//
// Classic (GameScene case 1, 0x4247fe):
//   +10 * len^2                       per placed word
//   +1000                             the FIRST bingo of the round
//   +100 * totalSlots                 when every slot is filled
//   (no time bonus)
//
// Lightning (BlitzScene, 0x4117dd..0x411825):
//   +10 * len * (len - min(len, hints))     per solved word
//   +1000                                    when the round is complete
//
// Crossword (CrisscrossScene, 0x41a637..0x41a66b):
//   +10 * len^2                       per newly filled word (once)
//   +1000                             when the puzzle is complete

#include <cstdint>
#include <cstddef>

namespace tt2 {

namespace scoring {

inline constexpr int BINGO_BONUS                = 1000;
inline constexpr int ALL_WORDS_BONUS_PER_SLOT   = 100;
inline constexpr int LIGHTNING_ROUND_BONUS      = 1000;
inline constexpr int CROSSWORD_COMPLETE_BONUS   = 1000;

// 10 * len^2. CONFIRMED for Classic and Crossword.
inline int per_word(std::size_t length) noexcept {
    const int l = static_cast<int>(length);
    return 10 * l * l;
}

// 10 * len * (len - min(len, hints)). CONFIRMED for Lightning.
inline int lightning_per_word(std::size_t length, std::size_t hints) noexcept {
    const int l = static_cast<int>(length);
    const int penalty = (hints > length) ? l : static_cast<int>(hints);
    return 10 * l * (l - penalty);
}

}  // namespace scoring

struct PlacementScore {
    int word_points     = 0;
    int bingo_bonus     = 0;
    int all_words_bonus = 0;
    int total           = 0;
    bool bingo          = false;
    bool qualified      = false;
    bool complete       = false;
};

inline PlacementScore score_classic(std::size_t length, bool is_bingo,
                                    bool already_qualified,
                                    bool round_complete,
                                    std::size_t slot_count) {
    PlacementScore p;
    p.word_points = scoring::per_word(length);
    p.bingo       = is_bingo;
    p.bingo_bonus = (is_bingo && !already_qualified) ? scoring::BINGO_BONUS : 0;
    p.all_words_bonus = round_complete
        ? scoring::ALL_WORDS_BONUS_PER_SLOT * static_cast<int>(slot_count)
        : 0;
    p.total      = p.word_points + p.bingo_bonus + p.all_words_bonus;
    p.qualified  = already_qualified || is_bingo;
    p.complete   = round_complete;
    return p;
}

inline int score_lightning_word(std::size_t length, std::size_t hints) {
    return scoring::lightning_per_word(length, hints);
}

inline int score_crossword_word(std::size_t length) {
    return scoring::per_word(length);
}

}  // namespace tt2
