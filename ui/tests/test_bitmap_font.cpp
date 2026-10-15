// Bitmap font parsing and layout (fonts/*.xml format).

#include "test_harness.hpp"

#include <string>

#include "tt2ui/bitmap_font.hpp"

using namespace tt2ui;

namespace {

// Three glyphs lifted from font_uibar.xml plus a Latin-1 one.
const char* kFontXml = R"(<root>
  <Font id="font" spacewidth="3" charspacing="2" lineheight="0">
    <Vector id="layers">
      <Layer image="font_uibar.png" padding="4" color="#000000" />
      <Layer image="font_uibar_stroke.png" padding="6" color="#000000" />
    </Vector>
    <Vector id="chars">
      <Char c="#42" bx="0" by="6" ix="9" iy="2" sx="173" sy="62" sw="21" sh="26" width="19" />
      <Char c="#41" bx="0" by="6" ix="10" iy="2" sx="194" sy="62" sw="26" sh="26" width="24" />
      <Char c="#2E" bx="0" by="23" ix="3" iy="5" sx="40" sy="140" sw="8" sh="9" width="6" />
      <Char c="#D2" bx="1" by="0" ix="0" iy="0" sx="0" sy="0" sw="26" sh="32" width="25" />
    </Vector>
  </Font>
</root>)";

BitmapFont load() {
    BitmapFont f;
    ASSERT_TRUE(BitmapFont::parse(kFontXml, f));
    return f;
}

}  // namespace

TEST(font_parse_reads_header_layers_and_chars) {
    BitmapFont f = load();
    ASSERT_EQ(f.space_width, 3);
    ASSERT_EQ(f.char_spacing, 2);
    ASSERT_EQ(f.layers.size(), std::size_t{2});
    ASSERT_EQ(f.layers[1].image, std::string("font_uibar_stroke.png"));
    ASSERT_EQ(f.layers[1].padding, 6);
    ASSERT_EQ(f.glyphs.size(), std::size_t{4});
    const Glyph* a = f.glyph('A');
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->sx, 194);
    ASSERT_EQ(a->width, 24);
    ASSERT_EQ(a->by, 6);
    ASSERT_TRUE(f.glyph(0xD2) != nullptr);     // Latin-1 code from c="#D2"
    ASSERT_TRUE(f.glyph('Z') == nullptr);
    ASSERT_EQ(f.glyph_height(), 32);
}

TEST(font_parse_rejects_xml_without_chars) {
    BitmapFont f;
    f.space_width = 99;
    ASSERT_TRUE(!BitmapFont::parse("<root><Font spacewidth=\"1\" /></root>", f));
    ASSERT_EQ(f.space_width, 99);   // untouched on failure
    ASSERT_TRUE(!BitmapFont::parse("", f));
}

TEST(font_measure_uses_advance_spacing_and_space_width) {
    BitmapFont f = load();
    ASSERT_EQ(f.measure(""), 0);
    ASSERT_EQ(f.measure("A"), 24);
    ASSERT_EQ(f.measure("AB"), 24 + 2 + 19);
    ASSERT_EQ(f.measure("A B"), 24 + 2 + 3 + 2 + 19);
    ASSERT_EQ(f.measure("AB", 0), 43);            // label charspacing override
    ASSERT_EQ(f.measure("AZB"), 24 + 2 + 19);     // unknown glyph skipped
    ASSERT_EQ(f.measure("\xC3\x92"), 25);          // "Ò" as UTF-8 -> #D2
}

TEST(font_layout_places_pen_positions) {
    BitmapFont f = load();
    auto run = f.layout("A B.");
    ASSERT_EQ(run.size(), std::size_t{3});
    ASSERT_EQ(run[0].cp, std::uint32_t{'A'});
    ASSERT_EQ(run[0].x, 0);
    ASSERT_EQ(run[1].cp, std::uint32_t{'B'});
    ASSERT_EQ(run[1].x, 24 + 2 + 3 + 2);
    ASSERT_EQ(run[2].cp, std::uint32_t{'.'});
    ASSERT_EQ(run[2].x, 31 + 19 + 2);
}

TEST(font_layer_rect_applies_padding_per_layer) {
    BitmapFont f = load();
    const Glyph& a = *f.glyph('A');
    // Face layer, padding 4: cell at sx + 8*ix, size sw + 8, drawn 4 px up-left.
    auto r0 = f.layer_rect(a, 0, 100, 50);
    ASSERT_EQ(r0.sx, 194 + 8 * 10);
    ASSERT_EQ(r0.sy, 62 + 8 * 2);
    ASSERT_EQ(r0.w, 26 + 8);
    ASSERT_EQ(r0.h, 26 + 8);
    ASSERT_EQ(r0.dx, 100 + 0 - 4);
    ASSERT_EQ(r0.dy, 50 + 6 - 4);
    // Stroke layer, padding 6.
    auto r1 = f.layer_rect(a, 1, 100, 50);
    ASSERT_EQ(r1.sx, 194 + 12 * 10);
    ASSERT_EQ(r1.w, 26 + 12);
    ASSERT_EQ(r1.dx, 94);
    // Out-of-range layer: padding 0.
    auto r9 = f.layer_rect(a, 9, 0, 0);
    ASSERT_EQ(r9.sx, 194);
    ASSERT_EQ(r9.dx, 0);
}

TEST(font_align_offset) {
    ASSERT_EQ(BitmapFont::align_offset(TextAlign::Left, 50), 0);
    ASSERT_EQ(BitmapFont::align_offset(TextAlign::Center, 50), -25);
    ASSERT_EQ(BitmapFont::align_offset(TextAlign::Right, 50), -50);
}

TEST(utf8_decode_handles_multibyte_and_garbage) {
    auto v = utf8_decode("a\xC3\x92\xE2\x82\xAC");   // a Ò €
    ASSERT_EQ(v.size(), std::size_t{3});
    ASSERT_EQ(v[1], std::uint32_t{0xD2});
    ASSERT_EQ(v[2], std::uint32_t{0x20AC});
    auto bad = utf8_decode("\xFF\xC3");                // stray byte, truncated sequence
    ASSERT_EQ(bad.size(), std::size_t{2});
    ASSERT_EQ(bad[0], std::uint32_t{0xFFFD});
    ASSERT_EQ(bad[1], std::uint32_t{0xFFFD});
}

TEST(placeholder_font_covers_ascii_names) {
    BitmapFont f = make_placeholder_font(3);
    ASSERT_TRUE(!f.empty());
    ASSERT_EQ(f.layers.size(), std::size_t{1});
    ASSERT_TRUE(f.glyph('A') != nullptr);
    ASSERT_TRUE(f.glyph('a') != nullptr);         // lowercase maps to uppercase
    ASSERT_EQ(f.glyph('a')->sx, f.glyph('A')->sx);
    ASSERT_TRUE(f.glyph('7') != nullptr);
    ASSERT_TRUE(f.glyph('?') != nullptr);
    ASSERT_TRUE(f.glyph('#') == nullptr);
    ASSERT_EQ(f.glyph('B')->sx, 6 * 3);           // second cell
    ASSERT_EQ(f.glyph('A')->sw, 15);
    for (const char* c = PLACEHOLDER_CHARS; *c; ++c) {
        const char* rows = placeholder_glyph_rows(static_cast<std::uint32_t>(*c));
        ASSERT_TRUE(rows != nullptr);
        ASSERT_EQ(std::string(rows).size(), std::size_t{25});
    }
    ASSERT_TRUE(placeholder_glyph_rows('#') == nullptr);
}

// ---- wrap_text: the GF2 wordwrap rule (game-scene.md §6) -------------
// Placeholder font at scale 2: a glyph advances 10 + 2, a space 6 + 2,
// so "AAA" measures 3 * 12 - 2 = 34 and "AAA BBB" 66 + 6 * 2 = 78.

TEST(wrap_text_keeps_a_word_that_fits_and_breaks_one_that_does_not) {
    const BitmapFont f = make_placeholder_font(2);
    ASSERT_EQ(f.measure("AAA BBB"), 78);
    ASSERT_EQ(wrap_text(f, "AAA BBB", 78), std::string("AAA BBB"));     // 78 <= 78
    ASSERT_EQ(wrap_text(f, "AAA BBB", 77), std::string("AAA\nBBB"));    // 78 > 77, trailing space trimmed
    ASSERT_EQ(wrap_text(f, "AAA BBB CCC", 78), std::string("AAA BBB\nCCC"));
}

TEST(wrap_text_keeps_explicit_breaks_and_blank_lines) {
    const BitmapFont f = make_placeholder_font(2);
    ASSERT_EQ(wrap_text(f, "A\nB", 390), std::string("A\nB"));
    ASSERT_EQ(wrap_text(f, "A\n\nB", 390), std::string("A\n\nB"));
    ASSERT_EQ(wrap_text(f, "A\rB", 390), std::string("A\nB"));
    ASSERT_EQ(wrap_text(f, "A\xE2\x80\xA8" "B", 390), std::string("A\nB"));   // U+2028
}

TEST(wrap_text_never_starts_a_line_with_a_space) {
    const BitmapFont f = make_placeholder_font(2);
    ASSERT_EQ(wrap_text(f, "   A", 390), std::string("A"));
    ASSERT_EQ(wrap_text(f, "A  B", 390), std::string("A  B"));                  // inner runs stay
    ASSERT_EQ(wrap_text(f, "AAA\xC2\xA0" "BBB", 40), std::string("AAA\nBBB"));   // NBSP separates words
}

TEST(wrap_text_drops_a_word_wider_than_the_label) {
    const BitmapFont f = make_placeholder_font(2);
    // "BBBBBBBB" = 8 * 12 - 2 = 94 > 40: dropped, like 0x46a7b0. The space
    // after it is still added because the line already has text.
    ASSERT_EQ(wrap_text(f, "A BBBBBBBB C", 40), std::string("A  C"));
    ASSERT_EQ(wrap_text(f, "BBBBBBBB A", 40), std::string("A"));                // no leading space
    ASSERT_EQ(wrap_text(f, "BBBBBBBB", 40), std::string(""));                   // terminates, empty line
}

TEST(wrap_text_empty_and_disabled) {
    const BitmapFont f = make_placeholder_font(2);
    ASSERT_EQ(wrap_text(f, "", 390), std::string(""));
    ASSERT_EQ(wrap_text(f, "AAA BBB", 0), std::string("AAA BBB"));     // max_width <= 0: off
    ASSERT_EQ(wrap_text(f, "AAA BBB", -5), std::string("AAA BBB"));
}

TEST(utf8_append_round_trips) {
    std::string s;
    for (std::uint32_t cp : {0x41u, 0xD2u, 0x20ACu, 0x1F600u}) utf8_append(s, cp);
    const auto v = utf8_decode(s);
    ASSERT_EQ(v.size(), std::size_t{4});
    ASSERT_EQ(v[1], std::uint32_t{0xD2});
    ASSERT_EQ(v[2], std::uint32_t{0x20AC});
    ASSERT_EQ(v[3], std::uint32_t{0x1F600});
}

TEST(placeholder_font_covers_the_import_dialogs) {
    // "Importing... 42%", file paths, and the tt2 --import "<folder>" hint.
    const BitmapFont f = make_placeholder_font(2);
    for (char c : std::string("%/\"<>")) ASSERT_TRUE(f.glyph(static_cast<std::uint32_t>(c)) != nullptr);
    ASSERT_EQ(std::string(placeholder_glyph_rows('%')), std::string("##..###.#...#...#.###..##"));
    ASSERT_TRUE(placeholder_glyph_rows('\\') == nullptr);
}
