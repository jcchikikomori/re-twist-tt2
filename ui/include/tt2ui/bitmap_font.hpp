// GF2 bitmap fonts (`fonts/*.xml` + PNG sheets) without SDL, so the
// text layout can be unit tested and the renderer only blits rectangles.
//
// File format (one per font, e.g. fonts/font_uibar.xml):
//
//   <Font spacewidth="3" charspacing="0" lineheight="0">
//     <Vector id="layers">
//       <Layer image="font_uibar.png"        padding="4" color="#000000" />
//       <Layer image="font_uibar_stroke.png" padding="6" color="#000000" />
//       <Layer image="font_uibar_shadow.png" padding="6" color="#000000" />
//     </Vector>
//     <Vector id="chars">
//       <Char c="#41" bx="0" by="6" ix="10" iy="2" sx="194" sy="62" sw="26" sh="26" width="24" />
//     </Vector>
//   </Font>
//
// `sx sy sw sh` is the glyph rectangle in an *unpadded* sheet; `ix iy`
// is the glyph's column/row index. Every layer PNG stores the same
// glyphs with `padding` extra pixels on each side, so in a layer with
// padding p the glyph cell is at (sx + 2p*ix, sy + 2p*iy) with size
// (sw + 2p, sh + 2p). Verified: that formula reproduces the exact pixel
// size of all nine shipped sheets (e.g. font_uibar.png 344x338 at p=4,
// font_uibar_stroke.png 392x382 at p=6). `bx by` offset the glyph from
// the pen; `width` is the advance. `c="#HH"` is a Latin-1 code.
//
// Layers are drawn last-to-first (shadow, then stroke, then face), each
// shifted by -padding so all three line up on the pen position.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace tt2ui {

struct FontLayer {
    std::string image;   // file name relative to the font XML's directory
    int padding = 0;
};

struct Glyph {
    int bx = 0, by = 0;      // pen offset
    int ix = 0, iy = 0;      // column / row index in the sheet
    int sx = 0, sy = 0;      // rectangle in the unpadded sheet
    int sw = 0, sh = 0;
    int width = 0;           // advance
};

enum class TextAlign { Left, Center, Right };

// Decode UTF-8 into code points (invalid bytes become U+FFFD).
inline std::vector<std::uint32_t> utf8_decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + n > s.size()) { out.push_back(0xFFFD); break; }
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? n : 1;
    }
    return out;
}

class BitmapFont {
public:
    int space_width = 0;
    int char_spacing = 0;
    int line_height = 0;
    std::vector<FontLayer> layers;
    std::map<std::uint32_t, Glyph> glyphs;

    bool empty() const { return glyphs.empty(); }
    const Glyph* glyph(std::uint32_t cp) const {
        auto it = glyphs.find(cp);
        return it == glyphs.end() ? nullptr : &it->second;
    }
    // Tallest glyph cell; used to centre labels vertically.
    int glyph_height() const {
        int h = 0;
        for (const auto& [cp, g] : glyphs) if (g.by + g.sh > h) h = g.by + g.sh;
        return h;
    }

    // Width of `text` in pixels with `spacing` between glyphs. A label's
    // own charspacing (dialogs.xml) replaces the font's when >= 0.
    int measure(const std::string& text, int spacing = -1) const {
        const int sp = spacing >= 0 ? spacing : char_spacing;
        int pen = 0, last_advance = 0;
        bool any = false;
        for (std::uint32_t cp : utf8_decode(text)) {
            const int adv = advance(cp);
            if (adv < 0) continue;
            pen += adv + sp;
            last_advance = sp;
            any = true;
        }
        return any ? pen - last_advance : 0;
    }

    // A glyph placed on the pen line: `x` is the pen position relative
    // to the start of the text run.
    struct Placed {
        std::uint32_t cp = 0;
        const Glyph* glyph = nullptr;
        int x = 0;
    };
    std::vector<Placed> layout(const std::string& text, int spacing = -1) const {
        const int sp = spacing >= 0 ? spacing : char_spacing;
        std::vector<Placed> out;
        int pen = 0;
        for (std::uint32_t cp : utf8_decode(text)) {
            const int adv = advance(cp);
            if (adv < 0) continue;
            if (const Glyph* g = glyph(cp)) out.push_back(Placed{cp, g, pen});
            pen += adv + sp;
        }
        return out;
    }

    // Offset of the run's left edge from the anchor x for an alignment.
    static int align_offset(TextAlign a, int width) {
        switch (a) {
            case TextAlign::Center: return -width / 2;
            case TextAlign::Right: return -width;
            default: return 0;
        }
    }

    // Where a glyph lives in layer `layer`'s sheet and where to draw it
    // for a pen at (pen_x, pen_y).
    struct LayerRect {
        int sx = 0, sy = 0, w = 0, h = 0;   // source in the layer PNG
        int dx = 0, dy = 0;                 // destination top-left
    };
    LayerRect layer_rect(const Glyph& g, std::size_t layer, int pen_x, int pen_y) const {
        const int p = layer < layers.size() ? layers[layer].padding : 0;
        LayerRect r;
        r.sx = g.sx + 2 * p * g.ix;
        r.sy = g.sy + 2 * p * g.iy;
        r.w = g.sw + 2 * p;
        r.h = g.sh + 2 * p;
        r.dx = pen_x + g.bx - p;
        r.dy = pen_y + g.by - p;
        return r;
    }

    // Parse a font XML. Returns false (and leaves `out` untouched) when
    // no <Char> entries are found.
    static bool parse(const std::string& xml, BitmapFont& out) {
        BitmapFont f;
        std::size_t pos = 0;
        while (true) {
            const std::size_t lt = xml.find('<', pos);
            if (lt == std::string::npos) break;
            const std::size_t gt = xml.find('>', lt);
            if (gt == std::string::npos) break;
            const std::string tag = xml.substr(lt + 1, gt - lt - 1);
            pos = gt + 1;
            if (starts_with(tag, "Font ")) {
                f.space_width = attr_int(tag, "spacewidth", 0);
                f.char_spacing = attr_int(tag, "charspacing", 0);
                f.line_height = attr_int(tag, "lineheight", 0);
            } else if (starts_with(tag, "Layer ")) {
                FontLayer l;
                l.image = attr(tag, "image");
                l.padding = attr_int(tag, "padding", 0);
                f.layers.push_back(l);
            } else if (starts_with(tag, "Char ")) {
                const std::string c = attr(tag, "c");
                if (c.size() < 2 || c[0] != '#') continue;
                const std::uint32_t cp = static_cast<std::uint32_t>(std::strtoul(c.c_str() + 1, nullptr, 16));
                Glyph g;
                g.bx = attr_int(tag, "bx", 0);
                g.by = attr_int(tag, "by", 0);
                g.ix = attr_int(tag, "ix", 0);
                g.iy = attr_int(tag, "iy", 0);
                g.sx = attr_int(tag, "sx", 0);
                g.sy = attr_int(tag, "sy", 0);
                g.sw = attr_int(tag, "sw", 0);
                g.sh = attr_int(tag, "sh", 0);
                g.width = attr_int(tag, "width", g.sw);
                f.glyphs[cp] = g;
            }
        }
        if (f.glyphs.empty()) return false;
        out = std::move(f);
        return true;
    }

private:
    // Advance for a code point, or -1 when the font cannot show it.
    int advance(std::uint32_t cp) const {
        if (cp == ' ') return space_width;
        const Glyph* g = glyph(cp);
        return g ? g->width : -1;
    }
    static bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
    static std::string attr(const std::string& tag, const char* name) {
        const std::string key = std::string(" ") + name + "=\"";
        const std::size_t at = tag.find(key);
        if (at == std::string::npos) return {};
        const std::size_t start = at + key.size();
        const std::size_t end = tag.find('"', start);
        return end == std::string::npos ? std::string() : tag.substr(start, end - start);
    }
    static int attr_int(const std::string& tag, const char* name, int def) {
        const std::string v = attr(tag, name);
        return v.empty() ? def : std::atoi(v.c_str());
    }
};

// Append one code point to `out` as UTF-8.
inline void utf8_append(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// GF2 label word wrap, wrapstyle="wordwrap" (game-scene.md §6, REA batch
// B2): the tokenizer 0x469a40 ends a line on \n, \r, U+2028 and U+2029
// and a word on the whitespace set below; the word flush 0x46a7b0 keeps
// a word on the line when it fits, else starts a new line when the line
// already has text and the word fits an empty one, else drops the word.
// A space is only added when the line has text, so no line starts with
// one. Returns `text` with "\n" between the lines; max_width <= 0 turns
// wrapping off. Trailing spaces are trimmed from finished lines (the
// original's handling of them is untraced; it only nudges centring).
inline bool wrap_is_break(std::uint32_t cp) { return cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029; }
inline bool wrap_is_space(std::uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200B) || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000;
}

inline std::string wrap_text(const BitmapFont& font, const std::string& text, int max_width, int spacing = -1) {
    if (max_width <= 0) return text;
    std::string out, line, word;
    auto end_line = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        out += '\n';
        line.clear();
    };
    auto flush_word = [&] {
        if (word.empty()) return;
        if (font.measure(line + word, spacing) <= max_width) {
            line += word;
        } else if (!line.empty() && font.measure(word, spacing) <= max_width) {
            end_line();
            line = word;
        }   // else: wider than the label, dropped like the original
        word.clear();
    };
    for (std::uint32_t cp : utf8_decode(text)) {
        if (wrap_is_break(cp)) {
            flush_word();
            end_line();
        } else if (wrap_is_space(cp)) {
            flush_word();
            if (!line.empty()) line += ' ';
        } else {
            utf8_append(word, cp);
        }
    }
    flush_word();
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return out + line;
}

// A procedural stand-in used when the original fonts are not imported:
// 5x5 pixel glyphs (A-Z, 0-9 and a little punctuation) scaled by
// `scale`, one layer. The renderer paints the sheet itself from
// `placeholder_glyph_rows` (25 chars, `#` = lit, row-major). Lowercase
// maps to uppercase.
inline constexpr int PLACEHOLDER_GLYPH_W = 5;
inline constexpr int PLACEHOLDER_GLYPH_H = 5;
inline constexpr const char* PLACEHOLDER_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,\'-_";

inline const char* placeholder_glyph_rows(std::uint32_t cp) {
    switch (cp) {
        case 'A': return ".###.#...#######...##...#";
        case 'B': return "####.#...#####.#...#####.";
        case 'C': return ".#####....#....#.....####";
        case 'D': return "####.#...##...##...#####.";
        case 'E': return "######....####.#....#####";
        case 'F': return "######....####.#....#....";
        case 'G': return ".#####....#..###...#.####";
        case 'H': return "#...##...#######...##...#";
        case 'I': return "#####..#....#....#..#####";
        case 'J': return "..###...#....#.#..#..##..";
        case 'K': return "#...##..#.###..#..#.#...#";
        case 'L': return "#....#....#....#....#####";
        case 'M': return "#...###.###.#.##...##...#";
        case 'N': return "#...###..##.#.##..###...#";
        case 'O': return ".###.#...##...##...#.###.";
        case 'P': return "####.#...#####.#....#....";
        case 'Q': return ".###.#...##.#.##..#..##.#";
        case 'R': return "####.#...#####.#..#.#...#";
        case 'S': return ".#####.....###.....#####.";
        case 'T': return "#####..#....#....#....#..";
        case 'U': return "#...##...##...##...#.###.";
        case 'V': return "#...##...##...#.#.#...#..";
        case 'W': return "#...##...##.#.###.###...#";
        case 'X': return "#...#.#.#...#...#.#.#...#";
        case 'Y': return "#...#.#.#...#....#....#..";
        case 'Z': return "#####...#...#...#...#####";
        case '0': return ".###.#..###.#.###..#.###.";
        case '1': return "..#...##....#....#...###.";
        case '2': return ".###.#...#..##..#...#####";
        case '3': return "####.....#..##.....#####.";
        case '4': return "#..#.#..#.#####...#....#.";
        case '5': return "######....####.....#####.";
        case '6': return ".###.#....####.#...#.###.";
        case '7': return "#####....#...#...#....#..";
        case '8': return ".###.#...#.###.#...#.###.";
        case '9': return ".###.#...#.####....#.###.";
        case '?': return ".###.#...#...#........#..";
        case '!': return "..#....#....#.........#..";
        case '.': return "......................#..";
        case ',': return ".................#...#...";
        case '\'': return "..#....#.................";
        case '-': return "...........###...........";
        case '_': return "....................#####";
        default: return nullptr;
    }
}

// Every glyph is a 5x5 cell (scaled) laid out in a single row with one
// empty column between cells.
inline BitmapFont make_placeholder_font(int scale) {
    BitmapFont f;
    f.space_width = 3 * scale;
    f.char_spacing = scale;
    f.layers.push_back(FontLayer{"placeholder", 0});
    int ix = 0;
    for (const char* c = PLACEHOLDER_CHARS; *c; ++c, ++ix) {
        Glyph g;
        g.ix = ix;
        g.sx = ix * (PLACEHOLDER_GLYPH_W + 1) * scale;
        g.sw = PLACEHOLDER_GLYPH_W * scale;
        g.sh = PLACEHOLDER_GLYPH_H * scale;
        g.width = PLACEHOLDER_GLYPH_W * scale;
        f.glyphs[static_cast<std::uint32_t>(*c)] = g;
        if (*c >= 'A' && *c <= 'Z') f.glyphs[static_cast<std::uint32_t>(*c - 'A' + 'a')] = g;
    }
    return f;
}

}  // namespace tt2ui
