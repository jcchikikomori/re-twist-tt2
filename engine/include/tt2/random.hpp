#pragma once

// Text Twist 2 — extracted engine source (portable C++17)
//
// This is the C++ reconstruction of the game logic in TextTwist2.exe. It
// is intentionally self-contained: no C++Builder, no GameHouse GF2, no
// Win32. The aim is for any C++17 compiler on any OS to build a
// functionally identical engine.

#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tt2 {

// Java `java.util.Random` linear congruential generator, byte-for-byte.
//
//   seed = (seed * 0x5DEECE66D + 0xB) & ((1 << 48) - 1)
//   out  = (int32_t)(seed >> 16)                       // sign-extended top 32 bits
//
// `set_seed(s)` stores `(s ^ 0x5DEECE66D) & MASK` so that a Java program
// calling `new Random(s)` and a C++ program calling `set_seed(s)`
// produce the same first output.
class Random {
public:
    static constexpr std::uint64_t MULTIPLIER = 0x5DEECE66DULL;
    static constexpr std::uint64_t ADDEND     = 0xB;
    static constexpr std::uint64_t MASK       = (1ULL << 48) - 1;

    constexpr Random() noexcept : seed_(0) {}

    explicit Random(std::int64_t s) noexcept { set_seed(s); }

    void set_seed(std::int64_t s) noexcept {
        seed_ = (static_cast<std::uint64_t>(s) ^ MULTIPLIER) & MASK;
    }

    std::uint64_t state() const noexcept { return seed_; }

    std::int32_t next_int() noexcept {
        seed_ = (seed_ * MULTIPLIER + ADDEND) & MASK;
        return static_cast<std::int32_t>(seed_ >> 16);
    }

    // Half-open integer range, mirroring `rand(min, max)` in the binary.
    // Upper bound is exclusive; the mod-result is made non-negative.
    std::int32_t rand(std::int32_t lo, std::int32_t hi_exclusive) {
        const std::int32_t span = hi_exclusive - lo;
        if (span <= 0) {
            throw std::invalid_argument("rand: empty range");
        }
        std::int32_t r = next_int() % span;
        if (r < 0) r += span;
        return lo + r;
    }

    // `std::random_shuffle` semantics with this LCG as the RNG.
    //   for i in 1..n-1: swap(a[i], a[rand(0, i + 1)])
    template <typename T>
    void shuffle(std::vector<T>& a) {
        for (std::size_t i = 1; i < a.size(); ++i) {
            const std::size_t j = static_cast<std::size_t>(
                rand(0, static_cast<std::int32_t>(i + 1)));
            std::swap(a[i], a[j]);
        }
    }

private:
    std::uint64_t seed_;
};

// Microsoft CRT `rand()` / `srand()`, byte-for-byte (msvcrt, 32-bit):
//
//   state = state * 214013 + 2531011
//   out   = (state >> 16) & 0x7fff
//
// CONFIRMED 2026-10-13 (Ghidra 12.1.4, TextTwist2.exe 0c6e962c…): the
// Crossword word-list shuffle (0x4228f0) and the crossing-pair shuffle
// (0x439370) are MSVC `std::random_shuffle` driven by this generator,
// not by the Java LCG above; so is every rack deal and TWIST (0x43c370 ->
// 0x439370, deal_order below; game-scene.md §4.1). The game seeds it from
// the clock (`srand(timeGetTime())` at 0x46d5bd,
// docs/reverse-engineering/crossword.md §9), so the front end seeds one
// shared instance once; tests pass their own seed. The CRT default seed
// is 1.
class CrtRandom {
public:
    static constexpr std::uint32_t MULTIPLIER = 214013u;
    static constexpr std::uint32_t ADDEND     = 2531011u;
    static constexpr std::uint32_t DEFAULT_SEED = 1u;

    explicit CrtRandom(std::uint32_t seed = DEFAULT_SEED) noexcept : state_(seed) {}

    void srand(std::uint32_t seed) noexcept { state_ = seed; }
    std::uint32_t state() const noexcept { return state_; }

    std::int32_t rand() noexcept {
        state_ = state_ * MULTIPLIER + ADDEND;
        return static_cast<std::int32_t>((state_ >> 16) & 0x7fffu);
    }

    // MSVC `std::random_shuffle`'s range draw (0x4228f0 / 0x439370):
    // take 15-bit chunks of `rand()` until the mask covers `n`, then
    // reduce modulo `n`. Returns a value in [0, n).
    std::uint32_t below(std::uint32_t n) noexcept {
        std::uint32_t mask = 0x7fffu;
        std::uint32_t v = static_cast<std::uint32_t>(rand()) & 0x7fffu;
        while (mask < n && mask != 0xffffffffu) {
            v    = (v << 15) | (static_cast<std::uint32_t>(rand()) & 0x7fffu);
            mask = (mask << 15) | 0x7fffu;
        }
        return n == 0 ? 0 : v % n;
    }

    // MSVC `std::random_shuffle(first, last)` over a vector or a string:
    //   for i in 1..n-1: swap(a[i], a[below(i + 1)])
    template <typename Sequence>
    void shuffle(Sequence& a) noexcept {
        for (std::size_t i = 1; i < a.size(); ++i) {
            const std::size_t j = below(static_cast<std::uint32_t>(i + 1));
            std::swap(a[i], a[j]);
        }
    }

private:
    std::uint32_t state_;
};

// 0x43c370, the deal every rack and every TWIST goes through, in Classic,
// Word of the Day, Lightning and Crossword (game-scene.md §4.1-§4.2,
// evidence DL1-DL10). `before` is the row's letters left to right, as
// 0x43c170 reads them (@0x43c3c8); `word` is the rack word the row is
// compared with (Classic table+0x1c, Lightning word+0x1c, Crossword
// word+0x18, which holds the root).
//
//   - 0x43c280 refuses a row of fewer than two letters or of one repeated
//     letter: the row stays as it is and no rand() is drawn.
//   - Otherwise a do-while with no cap: shuffle the row (0x439370, MSVC
//     random_shuffle on the CRT rand(), n - 1 draws a pass), and shuffle
//     it again while it reads the same as `before` (compare @0x43c43d) or
//     as `word` (@0x43c49a). Each pass shuffles the last one's result.
//
// So the rack word is never dealt in order, and a twist always moves a
// letter. The refusal while a ball is still sliding (`+0x184`) is the
// scene's to check. Port guard: two different letters whose swap is
// `word` leave no order to deal, and the exe's loop would never end; the
// row comes back unchanged, as for a refusal. Play never gets there:
// every rack and Lightning word has 5+ letters, and a row of 3+ letters,
// not all the same, has at least 3 orders.
inline std::string deal_order(const std::string& before, const std::string& word, CrtRandom& crt) {
    if (before.size() < 2 || before.find_first_not_of(before[0]) == std::string::npos) return before;
    if (before.size() == 2 && word.size() == 2 && word[0] == before[1] && word[1] == before[0]) return before;
    std::string row = before;
    do {
        crt.shuffle(row);
    } while (row == before || row == word);
    return row;
}

// Monotonic millisecond clock. The binary uses `timeGetTime()` from
// WinMM; the engine references `tt2::millis()` only when the bag is
// reseeded, so a platform shim or a deterministic stub both work.
//
// The user is expected to provide one definition of
// `tt2::tt2_detail::millis_default()` (see `src/main.cpp` for the
// POSIX/Win32 shim) — the header itself does not define a default, so
// the build fails fast if a platform forgets to provide one.
namespace tt2_detail {
std::uint32_t millis_default() noexcept;
}  // namespace tt2_detail

inline std::uint32_t millis() noexcept { return tt2_detail::millis_default(); }

}  // namespace tt2
