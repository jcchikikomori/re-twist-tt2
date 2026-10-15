#pragma once

// SHA-256 (FIPS 180-4), for checking files imported from the user's own
// Text Twist 2 install against data/original-assets.sha256. Written here
// because the engine takes no third-party code.
//
// Streaming: feed any number of update() calls, then finish(). A file is
// hashed in 64 KiB chunks, so a large .ogg never sits in memory whole.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tt2 {

class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() { reset(); }

    void reset() {
        state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        total_ = 0;
        used_ = 0;
    }

    void update(const void* data, std::size_t len) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        total_ += len;
        if (used_ > 0) {
            const std::size_t take = len < 64 - used_ ? len : 64 - used_;
            for (std::size_t i = 0; i < take; ++i) block_[used_ + i] = p[i];
            used_ += take;
            p += take;
            len -= take;
            if (used_ < 64) return;
            compress(block_.data());
            used_ = 0;
        }
        for (; len >= 64; p += 64, len -= 64) compress(p);
        for (std::size_t i = 0; i < len; ++i) block_[i] = p[i];
        used_ = len;
    }
    void update(std::string_view s) { update(s.data(), s.size()); }

    // Pads, returns the digest and resets, so the hasher can be reused.
    Digest finish() {
        const std::uint64_t bits = total_ * 8;
        const std::uint8_t one = 0x80;
        update(&one, 1);
        const std::uint8_t zero = 0;
        while (used_ != 56) update(&zero, 1);
        std::uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(len_be, 8);   // completes the last block
        Digest d{};
        for (std::size_t i = 0; i < 8; ++i)
            for (std::size_t b = 0; b < 4; ++b) d[i * 4 + b] = static_cast<std::uint8_t>(state_[i] >> (24 - 8 * b));
        reset();
        return d;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const std::uint8_t* blk) {
        static constexpr std::uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<std::uint32_t>(blk[i * 4]) << 24 | static_cast<std::uint32_t>(blk[i * 4 + 1]) << 16 |
                   static_cast<std::uint32_t>(blk[i * 4 + 2]) << 8 | static_cast<std::uint32_t>(blk[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t total_ = 0;   // bytes fed so far
    std::size_t used_ = 0;      // bytes waiting in block_
};

// Lowercase hex, the form sha256sum and the manifest use.
inline std::string to_hex(const Sha256::Digest& d) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (std::uint8_t b : d) {
        s += HEX[b >> 4];
        s += HEX[b & 0x0f];
    }
    return s;
}

inline Sha256::Digest sha256(std::string_view s) {
    Sha256 h;
    h.update(s);
    return h.finish();
}

// Hashes a file in 64 KiB chunks. No value when the path is not a regular
// file or a read fails part way: that is not the digest of an empty file.
// (glibc opens a directory for reading and then reports end-of-file, so
// the regular-file check comes first.)
inline std::optional<Sha256::Digest> sha256_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    Sha256 h;
    std::vector<char> buf(64 * 1024);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (in.bad()) return std::nullopt;
        h.update(buf.data(), static_cast<std::size_t>(in.gcount()));
    }
    if (!in.eof()) return std::nullopt;
    return h.finish();
}

}  // namespace tt2
