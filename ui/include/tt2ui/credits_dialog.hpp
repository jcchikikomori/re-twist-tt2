// DIALOG_CREDITS (798x595): the settings INFO button opens it over
// DIALOG_SETTINGS (0x40c760); OK (0x408c00) closes it again.
//
// CONFIRMED (REA, help-and-settings.md §5): the ctor 0x403e90 wires OK,
// reads clipy / clipheight / scrollduration and fills VERSION. Nothing
// reads the scroll fields afterwards, so the original never scrolls: it
// shows GRA_CREDITS (a picture of GameHouse's 2008 staff list), the half
// size logo, VERSION and one MESSAGE line.
//
// Deviation: the port credits this project instead. GRA_CREDITS names the
// people who made the 2008 game, not this recreation, so its area holds
// the project's own credit text, and MESSAGE names the original game.
#pragma once

#include "tt2ui/game_dialogs.hpp"

#ifndef TT2_VERSION
#define TT2_VERSION "dev"
#endif

namespace tt2ui {

namespace credits_strings {
struct Section {
    const char* heading;
    const char* lines;
};
inline constexpr Section SECTIONS[] = {
    {"Created by", "John Cyrill Corsanes"},
    {"Word lists", "ENABLE2k (public domain)\nFrequencyWords by Hermit Dave (CC BY-SA 4.0)"},
    {"Built with", "SDL2, SDL2_image and SDL2_mixer"},
};
inline constexpr const char* MESSAGE = "A fan recreation of Text Twist 2\n(2008, GameHouse, Inc.)";
inline constexpr const char* VERSION_PREFIX = "VERSION ";
}  // namespace credits_strings

inline constexpr int CREDITS_W = 798;
inline constexpr int CREDITS_H = 595;
// GRA_CREDITS sits at (46, 124) and is 708x394; the credit text is centred
// in that box.
inline constexpr float CREDITS_AREA_X = 46.0f + 708.0f / 2.0f;
inline constexpr float CREDITS_AREA_TOP = 124.0f;
inline constexpr int CREDITS_HEADING_H = 40;   // FONT_DIALOG_HEADERS2 line
inline constexpr int CREDITS_LINE_H = 34;      // FONT_NEXTROUND02 line
inline constexpr int CREDITS_SECTION_GAP = 26;
inline constexpr int CREDITS_MESSAGE_LINE_H = 22;

inline Dialog build_credits() {
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
    float y = CREDITS_AREA_TOP + 10.0f;
    int i = 0;
    for (const auto& s : credits_strings::SECTIONS) {
        const std::string id = std::to_string(i++);
        d.labels.push_back(TextLabel{"HEADING_" + id, "FONT_DIALOG_HEADERS2", s.heading, CREDITS_AREA_X, y,
                                     TextAlign::Center, Anchor::Top, 0});
        y += CREDITS_HEADING_H;
        TextLabel lines{"LINES_" + id, "FONT_NEXTROUND02", s.lines, CREDITS_AREA_X, y, TextAlign::Center, Anchor::Top,
                        1};
        lines.line_height = CREDITS_LINE_H;
        d.labels.push_back(lines);
        y += CREDITS_LINE_H * text_line_count(s.lines) + CREDITS_SECTION_GAP;
    }
    // MESSAGE: anchorpoint "left" at (32, 534) centres the text block on
    // y = 534; two 22 px lines start 22 px above it.
    TextLabel message{"MESSAGE", "FONT_FONT_HIGHSCORE_TEXT", credits_strings::MESSAGE, 32,
                      534.0f - CREDITS_MESSAGE_LINE_H, TextAlign::Left, Anchor::Top, 0};
    message.line_height = CREDITS_MESSAGE_LINE_H;
    d.labels.push_back(message);
    // VERSION: anchorpoint "right", right-aligned at (771, 87).
    d.labels.push_back(TextLabel{"VERSION", "FONT_FONT_HIGHSCORE_TEXT",
                                 std::string(credits_strings::VERSION_PREFIX) + TT2_VERSION, 771, 87,
                                 TextAlign::Right, Anchor::Center, 0});
    d.buttons.push_back(make_dialog_button("BUTTON_OK", "BUTTON_OK", 714, 535, Anchor::Center));
    return d;
}

}  // namespace tt2ui
