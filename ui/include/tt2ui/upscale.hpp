// The optional texture upscaler (deviation UX-9), free of SDL so it is
// unit-tested: a CPU port of Hyllian's Super-xBR 2x, three passes, with the
// shader defaults (XBR_WEIGHT 0, MODE 1, edge strength 1). Ported from the
// libretro slang shaders crt/shaders/crt-super-xbr/super-xbr-pass{0,1,2}.slang,
// which carry this notice:
//
//   Super XBR Shader
//   Copyright (c) 2015 Hyllian - sergiogdb@gmail.com
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
// What the port changes:
// - Alpha. The shaders are RGB only. Here every pass filters premultiplied
//   RGBA, and the edge detector reads `luma + alpha - 1`: plain luma for an
//   opaque pixel (so opaque art scales exactly as the shader does), below
//   black for a transparent one, so a sprite's outline still counts as an
//   edge against its transparent surround.
// - Edges clamp to the nearest pixel (the shader preset clamps to a border).
// - With XBR_WEIGHT 0 the two "sharpening" taps of each filter are zero, so
//   the horizontal / vertical colours both reduce to the average of the four
//   nearest pixels, and hv_edge drops out; this code skips computing it.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace tt2ui {

namespace sxbr {

struct Px {
    float r = 0, g = 0, b = 0, a = 0;   // premultiplied, 0..1
};

inline Px add(const Px& p, const Px& q) { return {p.r + q.r, p.g + q.g, p.b + q.b, p.a + q.a}; }
inline Px mul(const Px& p, float k) { return {p.r * k, p.g * k, p.b * k, p.a * k}; }
inline Px lerp(const Px& p, const Px& q, float t) { return add(mul(p, 1.0f - t), mul(q, t)); }
inline float edge_value(const Px& p) { return 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b + p.a - 1.0f; }
inline float df(float a, float b) { return std::fabs(a - b); }

// The shaders' d_wd: how strongly the 4x4 neighbourhood runs along one diagonal.
struct Weights {
    float wp1, wp2, wp3, wp4, wp5, wp6;
};
inline float d_wd(const Weights& w, float b0, float b1, float c0, float c1, float c2, float d0, float d1, float d2,
                  float d3, float e1, float e2, float e3, float f2, float f3) {
    return w.wp1 * (df(c1, c2) + df(c1, c0) + df(e2, e1) + df(e2, e3)) + w.wp2 * (df(d2, d3) + df(d0, d1)) +
           w.wp3 * (df(d1, d3) + df(d0, d2)) + w.wp4 * df(d1, d2) + w.wp5 * (df(c0, c2) + df(e1, e3)) +
           w.wp6 * (df(b0, b1) + df(f2, f3));
}

inline constexpr Weights PASS0{2, 1, -1, 4, -1, 1};
inline constexpr Weights PASS1{1, 0, 0, 0, 0, 0};
inline constexpr Weights PASS2{0, 0, 0, 1, 0, 0};

// One filtered pixel from the shaders' 4x4 neighbourhood
//   P0 B  C  P1        n[0]  n[1]  n[2]  n[3]
//   D  E  F  F4        n[4]  n[5]  n[6]  n[7]
//   G  H  I  I4        n[8]  n[9]  n[10] n[11]
//   P2 H5 I5 P3        n[12] n[13] n[14] n[15]
// placed between E, F, H and I; `v` holds each one's edge_value.
inline Px filter(const Weights& w, const Px* const n[16], const float v[16]) {
    const float p0 = v[0], b = v[1], c = v[2], p1 = v[3], d = v[4], e = v[5], f = v[6], f4 = v[7], g = v[8],
                h = v[9], i = v[10], i4 = v[11], p2 = v[12], h5 = v[13], i5 = v[14], p3 = v[15];
    const float d_edge = d_wd(w, d, b, g, e, c, p2, h, f, p1, h5, i, f4, i5, i4) -
                         d_wd(w, c, f4, b, f, i4, p0, e, i, p3, d, h, i5, g, h5);
    // smoothstep(0, 1.000001, |d_edge|)
    float t = std::fabs(d_edge) / 1.000001f;
    t = t > 1.0f ? 1.0f : t;
    const float edge_strength = t * t * (3.0f - 2.0f * t);
    const Px& E = *n[5];
    const Px& F = *n[6];
    const Px& H = *n[9];
    const Px& I = *n[10];
    const Px diagonal = d_edge >= 0.0f ? mul(add(E, I), 0.5f) : mul(add(H, F), 0.5f);
    const Px average = mul(add(add(E, F), add(H, I)), 0.25f);
    Px out = lerp(diagonal, average, 1.0f - edge_strength);
    // Anti-ringing: stay within the four nearest pixels.
    auto clampc = [](float x, float a, float b2, float c2, float d2) {
        const float lo1 = a < b2 ? a : b2, lo2 = c2 < d2 ? c2 : d2, lo = lo1 < lo2 ? lo1 : lo2;
        const float hi1 = a > b2 ? a : b2, hi2 = c2 > d2 ? c2 : d2, hi = hi1 > hi2 ? hi1 : hi2;
        return x < lo ? lo : (x > hi ? hi : x);
    };
    out.r = clampc(out.r, E.r, F.r, H.r, I.r);
    out.g = clampc(out.g, E.g, F.g, H.g, I.g);
    out.b = clampc(out.b, E.b, F.b, H.b, I.b);
    out.a = clampc(out.a, E.a, F.a, H.a, I.a);
    return out;
}

// An image of Px, with each pixel's edge_value, read with clamped coordinates.
struct Grid {
    int w = 0, h = 0;
    std::vector<Px> px;
    std::vector<float> ev;
    std::size_t index(int x, int y) const {
        x = x < 0 ? 0 : (x >= w ? w - 1 : x);
        y = y < 0 ? 0 : (y >= h ? h - 1 : y);
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    }
    void resize(int width, int height) {
        w = width;
        h = height;
        px.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        ev.resize(px.size());
    }
    void finish() {
        for (std::size_t k = 0; k < px.size(); ++k) ev[k] = edge_value(px[k]);
    }
};

// The filter at the corner between (x, y) and (x + 1, y + 1) of `s`.
inline Px corner(const Grid& s, const Weights& w, int x, int y) {
    const Px* n[16];
    float v[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const std::size_t k = s.index(x - 1 + c, y - 1 + r);
            n[r * 4 + c] = &s.px[k];
            v[r * 4 + c] = s.ev[k];
        }
    return filter(w, n, v);
}

inline int floor_half(int x) { return x >= 0 ? x / 2 : -((1 - x) / 2); }

}  // namespace sxbr

// `rgba` is w * h straight (not premultiplied) RGBA bytes, row by row; the
// result is 2w * 2h in the same format. Empty when either size is 0 or less.
inline std::vector<std::uint8_t> super_xbr2x(const std::uint8_t* rgba, int w, int h) {
    using namespace sxbr;
    std::vector<std::uint8_t> out;
    if (!rgba || w <= 0 || h <= 0) return out;
    Grid src;
    src.resize(w, h);
    for (std::size_t k = 0; k < src.px.size(); ++k) {
        const float a = rgba[k * 4 + 3] / 255.0f;
        src.px[k] = {rgba[k * 4] / 255.0f * a, rgba[k * 4 + 1] / 255.0f * a, rgba[k * 4 + 2] / 255.0f * a, a};
    }
    src.finish();
    // Pass 0: the colour at each corner between four source pixels.
    Grid diag;
    diag.resize(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) diag.px[diag.index(x, y)] = corner(src, PASS0, x, y);
    diag.finish();
    // The 2x lattice: (2x, 2y) is source pixel (x, y), (2x+1, 2y+1) its
    // pass-0 corner; only points with i + j even exist before pass 1.
    auto lattice = [&](int i, int j, const Px*& p, float& v) {
        const Grid& g = (i & 1) ? diag : src;
        const std::size_t k = g.index(floor_half(i), floor_half(j));
        p = &g.px[k];
        v = g.ev[k];
    };
    // Pass 1: the other two lattice points, each from a neighbourhood turned
    // 45 degrees; (gx, gy) is the shaders' g1 and (hx, hy) their g2.
    static constexpr int kOffsets[16][2] = {
        {-3, 0}, {-2, -1}, {-1, -2}, {0, -3},   // P0 B C P1
        {-2, 1}, {-1, 0},  {0, -1},  {1, -2},   // D E F F4
        {-1, 2}, {0, 1},   {1, 0},   {2, -1},   // G H I I4
        {0, 3},  {1, 2},   {2, 1},   {3, 0},    // P2 H5 I5 P3
    };
    // Pass 1 fills one 2x row at a time into a ring of four, which is all
    // pass 2 reads (rows j - 2 .. j + 1 for output row j), so the 2x image
    // never exists in floating point as a whole.
    const int W2 = 2 * w, H2 = 2 * h;
    std::vector<Px> ring_px(static_cast<std::size_t>(W2) * 4);
    std::vector<float> ring_ev(ring_px.size());
    int filled = -1;   // the last 2x row computed
    auto fill_row = [&](int j) {
        Px* row = &ring_px[static_cast<std::size_t>(j & 3) * static_cast<std::size_t>(W2)];
        float* row_ev = &ring_ev[static_cast<std::size_t>(j & 3) * static_cast<std::size_t>(W2)];
        for (int i = 0; i < W2; ++i) {
            const Px* n[16];
            float v[16];
            if (((i ^ j) & 1) == 0) {
                lattice(i, j, n[0], v[0]);
                row[i] = *n[0];
            } else {
                const int gx = (i & 1) ? 1 : 0, gy = 1 - gx;   // top-right: g1 runs along x; bottom-left: along y
                const int hx = gy, hy = gx;
                for (int k = 0; k < 16; ++k) {
                    const int a = kOffsets[k][0], b = kOffsets[k][1];
                    lattice(i + a * gx + b * hx, j + a * gy + b * hy, n[k], v[k]);
                }
                row[i] = filter(PASS1, n, v);
            }
            row_ev[i] = edge_value(row[i]);
        }
    };
    // Pass 2: resample half a 2x pixel up and left, which puts the 2x
    // pixels back on their centres.
    out.resize(static_cast<std::size_t>(W2) * static_cast<std::size_t>(H2) * 4);
    auto byte = [](float x) {
        const long r = std::lround(x * 255.0f);
        return static_cast<std::uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
    };
    for (int j = 0; j < H2; ++j) {
        const int need = j + 1 < H2 ? j + 1 : H2 - 1;
        while (filled < need) fill_row(++filled);
        for (int i = 0; i < W2; ++i) {
            const Px* n[16];
            float v[16];
            for (int r = 0; r < 4; ++r) {
                int y = j - 2 + r;
                y = y < 0 ? 0 : (y >= H2 ? H2 - 1 : y);
                const std::size_t base = static_cast<std::size_t>(y & 3) * static_cast<std::size_t>(W2);
                for (int c = 0; c < 4; ++c) {
                    int x = i - 2 + c;
                    x = x < 0 ? 0 : (x >= W2 ? W2 - 1 : x);
                    n[r * 4 + c] = &ring_px[base + static_cast<std::size_t>(x)];
                    v[r * 4 + c] = ring_ev[base + static_cast<std::size_t>(x)];
                }
            }
            const Px p = filter(PASS2, n, v);
            std::uint8_t* q = &out[(static_cast<std::size_t>(j) * static_cast<std::size_t>(W2) + static_cast<std::size_t>(i)) * 4];
            q[0] = p.a > 0.0f ? byte(p.r / p.a) : 0;
            q[1] = p.a > 0.0f ? byte(p.g / p.a) : 0;
            q[2] = p.a > 0.0f ? byte(p.b / p.a) : 0;
            q[3] = byte(p.a);
        }
    }
    return out;
}

// The upscaler leaves very large sheets alone: their 2x copy would cost
// more texture memory than it is worth (the 5700x864 title animation would
// become a 79 MB texture), and Smooth filtering still scales them.
inline constexpr long UPSCALE_MAX_PIXELS = 2000000;

// Whether a w x h texture is upscaled: at most UPSCALE_MAX_PIXELS, and
// still within the GPU's largest texture once doubled (0: unknown, trusted).
inline bool can_upscale(int w, int h, int max_texture_size) {
    if (w <= 0 || h <= 0 || static_cast<long>(w) * h > UPSCALE_MAX_PIXELS) return false;
    return max_texture_size <= 0 || (2 * w <= max_texture_size && 2 * h <= max_texture_size);
}

}  // namespace tt2ui
