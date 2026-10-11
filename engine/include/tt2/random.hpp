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
