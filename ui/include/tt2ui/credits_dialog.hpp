// DIALOG_CREDITS (798x595): the settings INFO button opens it over
// DIALOG_SETTINGS (0x40c760); OK (0x408c00) closes it again.
//
// CONFIRMED (REA, help-and-settings.md §5): the ctor 0x403e90 wires OK,
// reads clipy / clipheight / scrollduration and fills VERSION. Nothing
// reads the scroll fields afterwards, so the original never scrolls: it
// shows GRA_CREDITS (a picture of the 2008 GameHouse team), the half-size
// logo, VERSION and one MESSAGE line ("Copyright ©2008 GameHouse, Inc.").
//
// Deviation: the port adds two pages, reached with NEXT and crossfaded
// like the HOW TO PLAY pages (HelpPager, 200 ms).
//   Page 1 (P0_): the original credits. GRA_CREDITS loads from the user's
//                 imported-assets/ only; without it the placeholder build
//                 draws the same names as text (the P0_ORIG_ labels).
//   Page 2 (P1_): who made this recreation possible.
//   Page 3 (P2_): the software licences of what the game and its packages
//                 carry; the full texts ship in the licenses/ folder
//                 (docs/design/asset-credits.md, "What the Packages Carry").
#pragma once

#include <string>

#include "tt2ui/game_dialogs.hpp"
#include "tt2ui/help_dialog.hpp"

#ifndef TT2_VERSION
#define TT2_VERSION "dev"
#endif

namespace tt2ui {

namespace credits_strings {
struct Section {
    const char* heading;
    const char* lines;
};
// Page 1, text stand-in for GRA_CREDITS: the roles and names the picture
// shows, in its three columns and its QA block.
struct OriginalSection {
    float x, y;   // the heading's top centre inside the 708x394 picture
    const char* heading;
    const char* lines;
};
inline constexpr OriginalSection ORIGINAL[] = {
    {118, 26, "Producer", "Teagen Densmore"},
    {118, 96, "Art Lead", "Renee Wall"},
    {118, 176, "Dictionary", "Brandon Godfrey\nNick Huntington\nIan Adams"},
    {354, 26, "Audio Design", "Jesse Holt"},
    {354, 96, "Design", "Teagen Densmore\nMike Dietrich\nDave Quinn\nKingsley Sur"},
    {590, 26, "Programming", "Dave Quinn"},
    {590, 96, "Art", "Rodney Bambao"},
    {590, 176, "Engine and Tools", "Chris Blackwell\nLuc Bloom\nDavid Dunham"},
    {354, 276, "Quality Assurance",
     "Ian Adams   Alex Jones   Chuck Little\nKraig Shaver   Nick Huntington   Tyler Johnston\n"
     "Brahm Lichty   Howard Smith   Stephanie Talbott   Marcus King"},
};
inline constexpr const char* ORIGINAL_MESSAGE = "Copyright ©2008 GameHouse, Inc.";   // TEXT_CREDITS0

// Page 2.
inline constexpr Section RECREATION[] = {
    {"Made possible by", "Claude (Anthropic)"},
    {"Compiled by", "John Cyrill Corsanes"},
    {"Word lists", "ENABLE2k (public domain)\nFrequencyWords by Hermit Dave (CC BY-SA 4.0)"},
    {"Built with", "SDL3, SDL3_image and SDL3_mixer"},
};
inline constexpr const char* RECREATION_MESSAGE = "A fan recreation of Text Twist 2,\nnot affiliated with GameHouse, Inc.";

// Page 3. No middle dot or dash: the fonts are Latin-1 and lack both.
inline constexpr Section LICENSES[] = {
    {"Software licenses",
     "Super-xBR upscaler by Hyllian (MIT)\n"
     "SDL3, SDL3_image and SDL3_mixer (zlib)\n"
     "stb_image and stb_vorbis (public domain)\n"
     "tinyfiledialogs by Guillaume Vareille (zlib)\n"
     "Luckiest Guy (Apache 2.0), Arimo (SIL OFL 1.1)\n"
     "Windows: MinGW-w64 runtime (MIT, BSD)\n"
     "Windows: GCC runtime (GPL 3, runtime exception)\n"
     "Linux: AppImage runtime (MIT)"},
};
inline constexpr const char* LICENSES_MESSAGE =
    "re-twist-tt2 is MIT licensed. Full texts are in\nthe licenses folder next to the game.";
inline constexpr const char* VERSION_PREFIX = "VERSION ";
}  // namespace credits_strings

inline constexpr int CREDITS_W = 798;
inline constexpr int CREDITS_H = 595;
inline constexpr int CREDITS_PAGE_COUNT = 3;
// GRA_CREDITS: (46, 124), 708x394.
inline constexpr float CREDITS_AREA_LEFT = 46.0f;
inline constexpr float CREDITS_AREA_TOP = 124.0f;
inline constexpr float CREDITS_AREA_W = 708.0f;
inline constexpr int CREDITS_HEADING_H = 38;   // FONT_DIALOG_HEADERS2 line
inline constexpr int CREDITS_LINE_H = 34;      // FONT_NEXTROUND02 line
inline constexpr int CREDITS_SECTION_GAP = 8;
inline constexpr int CREDITS_ORIGINAL_LINE_H = 24;   // the picture's name rows
inline constexpr int CREDITS_MESSAGE_LINE_H = 22;
inline constexpr float CREDITS_TURN_X = 580.0f;      // PREV / NEXT, left of OK

inline std::string credits_page_name(int i) { return "P" + std::to_string(i) + "_"; }

inline Dialog build_credits() {
    namespace cs = credits_strings;
    Dialog d;
    d.w = CREDITS_W;
    d.h = CREDITS_H;
    // LOGO_STATIC (scale .5) with its two children (sceneselection images).
    // INFERRED: a child's offset scales with its parent; frame 0 is shown.
    auto part = [&](const char* name, SheetSpec s, float x, float y) {
        SpriteState sp = sprite(name, s, x, y);
        sp.scale = 0.5f;
        d.sprites.push_back(sp);
    };
    part("LOGO_STATIC", sheet("IMAGE_LOGO_STATIC", 254, 204), 338, 30);
    part("T", sheet("IMAGE_LOGO_COIL", 3298, 253, 17, 1, 17), 338 - 160 * 0.5f, 30 - 18 * 0.5f);
    part("2", sheet("IMAGE_LOGO_BOUNCE", 1728, 252, 12, 1, 12), 338 + 228 * 0.5f, 30 - 34 * 0.5f);
    auto message = [&](const std::string& name, const char* text, float alpha) {
        // anchorpoint "left" at (32, 534) centres the text block on y = 534.
        const int lines = text_line_count(text);
        TextLabel m{name, "FONT_FONT_HIGHSCORE_TEXT", text, 32,
                    534.0f - lines * CREDITS_MESSAGE_LINE_H / 2.0f, TextAlign::Left, Anchor::Top, 0};
        m.line_height = CREDITS_MESSAGE_LINE_H;
        m.alpha = alpha;
        d.labels.push_back(m);
    };

    // Page 1: the original credits.
    d.sprites.push_back(sprite("P0_GRA_CREDITS", sheet("IMAGE_GRA_CREDITS", 708, 394), CREDITS_AREA_LEFT,
                               CREDITS_AREA_TOP));
    int i = 0;
    for (const auto& s : cs::ORIGINAL) {
        const std::string id = std::to_string(i++);
        TextLabel h{"P0_ORIG_HEADING_" + id, "PLACEHOLDER_2", s.heading, CREDITS_AREA_LEFT + s.x,
                    CREDITS_AREA_TOP + s.y, TextAlign::Center, Anchor::Top, -1};
        h.fallback_for = "P0_GRA_CREDITS";
        d.labels.push_back(h);
        TextLabel n{"P0_ORIG_NAMES_" + id, "PLACEHOLDER_2", s.lines, CREDITS_AREA_LEFT + s.x,
                    CREDITS_AREA_TOP + s.y + CREDITS_ORIGINAL_LINE_H + 6, TextAlign::Center, Anchor::Top, -1};
        n.line_height = CREDITS_ORIGINAL_LINE_H;
        n.fallback_for = "P0_GRA_CREDITS";
        d.labels.push_back(n);
    }
    message("P0_MESSAGE", cs::ORIGINAL_MESSAGE, 1.0f);

    // Pages 2 and 3: headed sections, hidden until NEXT.
    auto sections = [&](int page, const auto& list, const char* text) {
        const std::string prefix = credits_page_name(page);
        const float cx = CREDITS_AREA_LEFT + CREDITS_AREA_W / 2.0f;
        float y = CREDITS_AREA_TOP + 22.0f;   // clear of the logo (bottom ~130)
        int n = 0;
        for (const auto& s : list) {
            const std::string id = std::to_string(n++);
            TextLabel h{prefix + "HEADING_" + id, "FONT_DIALOG_HEADERS2", s.heading, cx, y, TextAlign::Center,
                        Anchor::Top, 0};
            h.alpha = 0.0f;
            d.labels.push_back(h);
            y += CREDITS_HEADING_H;
            TextLabel l{prefix + "LINES_" + id, "FONT_NEXTROUND02", s.lines, cx, y, TextAlign::Center, Anchor::Top, 1};
            l.line_height = CREDITS_LINE_H;
            l.alpha = 0.0f;
            d.labels.push_back(l);
            y += CREDITS_LINE_H * text_line_count(s.lines) + CREDITS_SECTION_GAP;
        }
        message(prefix + "MESSAGE", text, 0.0f);
    };
    sections(1, cs::RECREATION, cs::RECREATION_MESSAGE);
    sections(2, cs::LICENSES, cs::LICENSES_MESSAGE);

    // VERSION: anchorpoint "right", right-aligned at (771, 87).
    d.labels.push_back(TextLabel{"VERSION", "FONT_FONT_HIGHSCORE_TEXT", std::string(cs::VERSION_PREFIX) + TT2_VERSION,
                                 771, 87, TextAlign::Right, Anchor::Center, 0});
    d.buttons.push_back(make_dialog_button("BUTTON_PREV", "BUTTON_PREV", CREDITS_TURN_X, 535, Anchor::Center));
    d.buttons.push_back(make_dialog_button("BUTTON_NEXT", "BUTTON_NEXT", CREDITS_TURN_X, 535, Anchor::Center));
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 714, 535, Anchor::Center));
    d.button("BUTTON_PREV")->sprite.visible = false;   // page 1
    return d;
}

// An open DIALOG_CREDITS: the dialog and its page turner.
class CreditsDialog {
public:
    const Dialog& dialog() const { return dialog_; }
    Dialog& dialog() { return dialog_; }
    int page() const { return pager_.page; }

    // Runs a button by name; true when OK closed the dialog (0x408c00).
    bool press(const std::string& name) {
        const MenuButton* b = dialog_.button(name);
        if (!b || !b->enabled || !b->sprite.visible) return false;
        if (name == "BUTTON_OK") return true;
        pager_.turn(dialog_, name == "BUTTON_PREV" ? -1 : 1);
        return false;
    }
    void tick(int dt_ms) {
        pager_.tick(dialog_, dt_ms);
        dialog_tick(dialog_, dt_ms);
    }

private:
    Dialog dialog_ = build_credits();
    HelpPager pager_{0, -1, 0, CREDITS_PAGE_COUNT, credits_page_name};
};

}  // namespace tt2ui
