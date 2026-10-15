// The generated placeholder art in data/placeholder/ (tools/gen_placeholders.py).
// The game loads these files when the player hasn't imported their own
// install, so each must parse and match the size the code cuts it with.

#include "test_harness.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/profile_dialogs.hpp"

using namespace tt2ui;
namespace fs = std::filesystem;

namespace {

const fs::path kDir = fs::path(TT2_SOURCE_DATA_DIR) / "placeholder";

struct Size {
    int w = 0, h = 0;
};

// Width and height from a PNG's IHDR chunk; {0, 0} for anything else.
Size png_size(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    unsigned char b[24] = {};
    if (!in.read(reinterpret_cast<char*>(b), sizeof b)) return {};
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (int i = 0; i < 8; ++i) if (b[i] != sig[i]) return {};
    if (b[12] != 'I' || b[13] != 'H' || b[14] != 'D' || b[15] != 'R') return {};
    auto be32 = [&](int o) { return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; };
    return {be32(16), be32(20)};
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every glyph cell of every layer lies inside that layer's PNG.
bool glyphs_inside_sheets(const BitmapFont& f, const fs::path& font_dir) {
    for (std::size_t li = 0; li < f.layers.size(); ++li) {
        const Size s = png_size(font_dir / f.layers[li].image);
        if (s.w == 0) return false;
        for (const auto& [cp, g] : f.glyphs) {
            const auto r = f.layer_rect(g, li, 0, 0);
            if (r.sx < 0 || r.sy < 0 || r.sx + r.w > s.w || r.sy + r.h > s.h) return false;
        }
    }
    return true;
}

// The font ids main.cpp loads (kFontFiles).
const char* const kFonts[] = {
    "font_uibar", "font_settingstext", "font_dialog_headers", "font_dialog_headers2", "font_uibar_labels",
    "font_uibar_numbers", "font_nextround02", "font_tutorial01", "font_highscore_text",
};

}  // namespace

TEST(placeholder_fonts_parse_and_fit_their_sheets) {
    for (const char* name : kFonts) {
        BitmapFont f;
        ASSERT_TRUE(BitmapFont::parse(read(kDir / "fonts" / (std::string(name) + ".xml")), f));
        ASSERT_TRUE(!f.layers.empty());
        ASSERT_TRUE(glyphs_inside_sheets(f, kDir / "fonts"));
    }
}

TEST(placeholder_fonts_cover_the_game_text) {
    // Every character in the UI strings, plus Latin-1 accents for names.
    const std::string needed =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        ".,:;!?'\"%/<>()-+&#@*=_~|$\xC0\xC9\xD6\xDC\xE0\xE9\xF1\xFC";
    for (const char* name : kFonts) {
        BitmapFont f;
        ASSERT_TRUE(BitmapFont::parse(read(kDir / "fonts" / (std::string(name) + ".xml")), f));
        for (unsigned char c : needed) ASSERT_TRUE(f.glyph(c) != nullptr);
        ASSERT_TRUE(f.space_width > 0);
    }
}

TEST(placeholder_text_wraps_like_the_original) {
    // The face is sized to the original's advance widths: the launch
    // prompt's longest line fits the 600 px import dialog's text box.
    BitmapFont f;
    ASSERT_TRUE(BitmapFont::parse(read(kDir / "fonts" / "font_settingstext.xml"), f));
    const int w = f.measure("This game needs the art, fonts and sounds from your own copy");
    ASSERT_TRUE(w > 400);
    ASSERT_TRUE(w < 590);
}

TEST(placeholder_dialog_frame_matches_the_frame_sizes) {
    const fs::path d = kDir / "images" / "gui" / "dialogdefault";
    struct Piece { const char* file; int w, h; };
    const Piece pieces[] = {
        {"top_left.png", 37, 86}, {"top.png", 4, 86}, {"top_right.png", 37, 86},
        {"left.png", 37, 4}, {"right.png", 37, 4},
        {"bottom_left.png", 37, 53}, {"bottom.png", 4, 53}, {"bottom_right.png", 37, 53},
    };
    for (const Piece& p : pieces) {
        const Size s = png_size(d / p.file);
        ASSERT_EQ(s.w, p.w);
        ASSERT_EQ(s.h, p.h);
    }
    const Size bg = png_size(kDir / "images" / "gui" / "background.png");
    ASSERT_EQ(bg.w, 800);
    ASSERT_EQ(bg.h, 600);
}

TEST(placeholder_buttons_are_four_state_sheets) {
    const MenuButton b = make_dialog_button("BUTTON_YES", "BUTTON_YES", 0, 0, Anchor::Top);
    struct Sheet { const char* file; int w, h; };
    const Sheet sheets[] = {
        {"button_yes.png", b.sprite.sheet.sheet_w(), b.sprite.sheet.sheet_h()},
        {"button_no.png", 119, 200}, {"button_ok.png", 119, 200}, {"button_cancel.png", 119, 200},
        {"button_add.png", 119, 200}, {"button_remove.png", 119, 200},
        {"button_end_menu.png", 157, 208}, {"button_end_playagain.png", 157, 208},
        {"button_readmore.png", 157, 208}, {"button_play.png", 157, 208},
        {"button_pause_resume.png", 181, 208}, {"button_pause_settings.png", 181, 208},
        {"button_pause_endgame.png", 181, 208}, {"button_pause_help.png", 181, 208},
    };
    for (const Sheet& s : sheets) {
        const Size got = png_size(kDir / "images" / "gui" / s.file);
        ASSERT_EQ(got.w, s.w);
        ASSERT_EQ(got.h, s.h);
    }
}

TEST(placeholder_licences_ship_with_the_fonts) {
    ASSERT_TRUE(fs::exists(kDir / "README.md"));
    ASSERT_TRUE(read(kDir / "OFL-Arimo.txt").find("SIL Open Font License") != std::string::npos);
    ASSERT_TRUE(read(kDir / "LICENSE-LuckiestGuy.txt").find("Apache License") != std::string::npos);
}

TEST(png_size_rejects_non_png_and_missing_files) {
    ASSERT_EQ(png_size(kDir / "README.md").w, 0);
    ASSERT_EQ(png_size(kDir / "no-such-file.png").w, 0);
}

TEST(glyphs_inside_sheets_catches_a_glyph_past_the_edge) {
    BitmapFont f;
    ASSERT_TRUE(BitmapFont::parse(read(kDir / "fonts" / "font_highscore_text.xml"), f));
    ASSERT_TRUE(glyphs_inside_sheets(f, kDir / "fonts"));
    Glyph far = *f.glyph('A');
    far.sx = 100000;   // off the sheet
    f.glyphs['A'] = far;
    ASSERT_TRUE(!glyphs_inside_sheets(f, kDir / "fonts"));
}
