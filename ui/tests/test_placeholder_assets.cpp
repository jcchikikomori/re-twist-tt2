// The generated placeholder art in data/placeholder/ (tools/gen_placeholders.py).
// The game loads these files when the player hasn't imported their own
// install, so each must parse and match the size the code cuts it with.

#include "test_harness.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/improvements_dialog.hpp"
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

// Every glyph cell of every layer lies inside that layer's PNG; `scale` 2
// checks a 2x layer against the 1x XML, doubled.
bool glyphs_inside_sheets(const BitmapFont& f, const fs::path& font_dir, int scale = 1) {
    for (std::size_t li = 0; li < f.layers.size(); ++li) {
        const Size s = png_size(font_dir / f.layers[li].image);
        if (s.w == 0) return false;
        for (const auto& [cp, g] : f.glyphs) {
            const auto r = f.layer_rect(g, li, 0, 0);
            if (r.sx < 0 || r.sy < 0 || (r.sx + r.w) * scale > s.w || (r.sy + r.h) * scale > s.h) return false;
        }
    }
    return true;
}

// 2x art (deviation UX-8): exactly twice the 1x size.
bool twice(const Size& one, const Size& two) { return one.w > 0 && two.w == 2 * one.w && two.h == 2 * one.h; }

// The 1x PNGs (paths relative to `dir`) that have no 2x twin in dir/2x.
// background.png is the one 1x image without one, by design.
std::vector<std::string> missing_2x(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".png") continue;
        const fs::path rel = fs::relative(e.path(), dir);
        if (*rel.begin() == "2x" || rel == fs::path("images/gui/background.png")) continue;
        if (!fs::exists(dir / "2x" / rel)) out.push_back(rel.generic_string());
    }
    return out;
}

// The font ids main.cpp loads (kFontFiles).
const char* const kFonts[] = {
    "font_uibar", "font_settingstext", "font_dialog_headers", "font_dialog_headers2", "font_uibar_labels",
    "font_uibar_numbers", "font_nextround02", "font_tutorial01", "font_highscore_text", "arial12",
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

TEST(placeholder_has_the_ports_own_buttons_ux2) {
    // The IMPROVEMENTS badge fills BUTTON_GH's 115x100 frame, 4 states; the
    // row arrows are 36x36, 4 states. The model's sheets must match.
    const Size badge = png_size(kDir / "images" / "selectionscene" / "button_improvements.png");
    ASSERT_EQ(badge.w, 115);
    ASSERT_EQ(badge.h, 400);
    for (const char* file : {"button_arrow_left.png", "button_arrow_right.png"}) {
        const Size got = png_size(kDir / "images" / "gui" / file);
        ASSERT_EQ(got.w, IMPROVEMENTS_ARROW);
        ASSERT_EQ(got.h, IMPROVEMENTS_ARROW * 4);
    }
}

TEST(placeholder_has_the_settings_import_button_op7) {
    // SETTINGS' IMPORT is a dialog button: the same 4-state sheet as OK,
    // so the hover / pressed / disabled frames line up with the model.
    const MenuButton b = make_dialog_button("BUTTON_IMPORT", "BUTTON_IMPORT", 0, 0, Anchor::TopRight);
    const Size got = png_size(kDir / "images" / "gui" / "button_import.png");
    ASSERT_EQ(got.w, b.sprite.sheet.sheet_w());
    ASSERT_EQ(got.h, b.sprite.sheet.sheet_h());
    const Size hi = png_size(kDir / "2x" / "images" / "gui" / "button_import.png");
    ASSERT_EQ(hi.w, got.w * 2);
    ASSERT_EQ(hi.h, got.h * 2);
}

TEST(ports_own_buttons_are_not_original_files) {
    // The game takes these from data/placeholder/ only because an import
    // never has them; a name the original also used would be replaced by
    // the imported copy (or need a GameHouse file the repo must not ship).
    const std::string manifest = read(kDir.parent_path() / "original-assets.sha256");
    ASSERT_TRUE(manifest.find("images/gui/button_ok.png") != std::string::npos);   // the list is readable
    for (const char* rel : {"images/gui/button_import.png", "images/gui/button_arrow_left.png",
                            "images/gui/button_arrow_right.png", "images/selectionscene/button_improvements.png"})
        ASSERT_TRUE(manifest.find(rel) == std::string::npos);
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

// ---- 2x art (P8, deviation UX-8) -------------------------------------------

TEST(placeholder_2x_has_a_twin_for_every_1x_png) {
    const std::vector<std::string> missing = missing_2x(kDir);
    for (const std::string& m : missing) std::printf("missing 2x: %s\n", m.c_str());
    ASSERT_TRUE(missing.empty());
    ASSERT_TRUE(!fs::exists(kDir / "2x" / "images" / "gui" / "background.png"));
}

TEST(placeholder_2x_is_exactly_twice_its_1x_twin) {
    int checked = 0;
    for (const auto& e : fs::recursive_directory_iterator(kDir / "2x")) {
        if (!e.is_regular_file()) continue;
        ASSERT_TRUE(e.path().extension() == ".png");                     // no XML: fonts reuse the 1x one
        const fs::path rel = fs::relative(e.path(), kDir / "2x");
        ASSERT_TRUE(twice(png_size(kDir / rel), png_size(e.path())));
        ++checked;
    }
    ASSERT_EQ(checked, 52);   // 51 + arial12 (FONT_TOOLTIP)
}

TEST(placeholder_2x_glyphs_fit_the_doubled_1x_cells) {
    for (const char* id : kFonts) {
        BitmapFont f;
        ASSERT_TRUE(BitmapFont::parse(read(kDir / "fonts" / (std::string(id) + ".xml")), f));
        ASSERT_TRUE(glyphs_inside_sheets(f, kDir / "2x" / "fonts", 2));
    }
}

TEST(placeholder_2x_dialog_frame_pieces) {
    const fs::path d = kDir / "2x" / "images" / "gui" / "dialogdefault";
    ASSERT_TRUE(twice({37, 86}, png_size(d / "top_left.png")));          // 74x172
    ASSERT_TRUE(twice({4, 86}, png_size(d / "top.png")));                // 8x172
    ASSERT_TRUE(twice({37, 53}, png_size(d / "bottom_right.png")));      // 74x106
    ASSERT_TRUE(twice({37, 4}, png_size(d / "left.png")));               // 74x8
}

TEST(twice_rejects_anything_but_exactly_double) {
    ASSERT_TRUE(twice({119, 200}, {238, 400}));
    ASSERT_TRUE(!twice({119, 200}, {237, 400}));   // an odd width: resized, not redrawn
    ASSERT_TRUE(!twice({119, 200}, {238, 401}));
    ASSERT_TRUE(!twice({119, 200}, {119, 200}));
    ASSERT_TRUE(!twice({0, 0}, {0, 0}));           // a missing 1x file
}

TEST(missing_2x_names_each_missing_file) {
    const fs::path tmp = fs::temp_directory_path() / "tt2_missing_2x_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "images" / "gui");
    fs::create_directories(tmp / "2x" / "images" / "gui");
    std::ofstream(tmp / "images" / "gui" / "button_ok.png") << "x";
    std::ofstream(tmp / "images" / "gui" / "button_no.png") << "x";
    std::ofstream(tmp / "images" / "gui" / "background.png") << "x";      // exempt
    std::ofstream(tmp / "2x" / "images" / "gui" / "button_ok.png") << "x";
    const std::vector<std::string> m = missing_2x(tmp);
    ASSERT_EQ(m.size(), std::size_t{1});
    ASSERT_EQ(m[0], std::string("images/gui/button_no.png"));
    fs::remove_all(tmp);
}
