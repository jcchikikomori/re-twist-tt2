// SDL2 front end for the welcome flow (splash -> loading -> main menu)
// and the player-profile dialogs.
//
// Assets: the repository ships none of the original art. If the user
// has run `scripts/import-original.sh` the files are read from
// `imported-assets/` (or the directory given by `--assets DIR` /
// `TT2_ASSETS`). Anything missing is replaced by a procedurally drawn
// placeholder of the same size, so the flow always boots.
//
// Profiles are read from / written to the platform's per-user data
// directory (tt2::default_save_dir(): %APPDATA%, ~/Library/Application
// Support, $XDG_DATA_HOME) unless `--save-dir DIR` says otherwise.
//
// Usage: tt2 [--assets DIR] [--save-dir DIR] [--speed N] [--exit-after MS] [--dump-frames DIR] [--fps N]
//   --speed N          run the clock N times faster (debugging)
//   --exit-after MS    quit after MS simulated milliseconds (smoke tests)
//   --dump-frames DIR  write frame_NNNN.bmp at N fps of simulated time
//                      (headless with SDL_VIDEODRIVER=dummy; feeds the README GIF)
//   --player NAME      add NAME (if new) and make it current before the
//                      window opens; for smoke tests and frame dumps

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/profile_store.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/game_scene.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/profile_dialogs.hpp"
#include "tt2ui/welcome_flow.hpp"

#ifndef TT2_IMPORTED_ASSETS_DIR
#define TT2_IMPORTED_ASSETS_DIR "imported-assets"
#endif
#ifndef TT2_DATA_DIR
#define TT2_DATA_DIR "data"
#endif

namespace {

using namespace tt2ui;

// resources.xml: image id -> file, for the three welcome sections and
// the dialog GUI. BACKGROUND is section-scoped, so it is keyed
// "section/BACKGROUND".
const std::map<std::string, std::string> kImageFiles = {
    {"splash/BACKGROUND", "images/splash/background.jpg"},
    {"preloading/BACKGROUND", "images/loadscene/background.png"},
    {"LOADING_BAR", "images/loadscene/loadingbar_full.png"},
    {"LOADING_PLEASE_WAIT", "images/loadscene/loading_wait.png"},
    {"CLICK_HERE", "images/loadscene/click_here.png"},
    {"IMAGE_LOGO_ANIM", "images/loadscene/logo_anim.png"},
    {"IMAGE_LOADINGBAR_EMPTY", "images/loadscene/loadingbar_empty.png"},
    {"ANI_CIRCLES", "images/loadscene/ANIMATED_CIRCLES.png"},
    {"IMAGE_SPRING", "images/loadscene/spring.png"},
    {"IMAGE_GH_BRANDING", "images/loadscene/GH_Branding.png"},
    {"sceneselection/BACKGROUND", "images/selectionscene/background.png"},
    {"BUTTON_PLAY", "images/selectionscene/button_play.png"},
    {"BUTTON_HOWTOPLAY", "images/selectionscene/button_howtoplay.png"},
    {"BUTTON_GH", "images/selectionscene/button_gh.png"},
    {"BUTTON_HIGHSCORES", "images/selectionscene/button_highscores.png"},
    {"BUTTON_SETTINGS", "images/selectionscene/button_settings.png"},
    {"BUTTON_QUIT", "images/selectionscene/button_quit.png"},
    {"BUTTON_CHANGENAME", "images/selectionscene/change_name.png"},
    {"IMAGE_BUTTON_WOTD", "images/selectionscene/button_wotd.png"},
    {"IMAGE_BUTTON_CROSSWORD", "images/selectionscene/button_crossword.png"},
    {"IMAGE_BUTTON_LIGHTNING", "images/selectionscene/button_lightning.png"},
    {"IMAGE_BUTTON_TIMED", "images/selectionscene/button_timed.png"},
    {"IMAGE_BUTTON_UNTIMED", "images/selectionscene/button_untimed.png"},
    {"IMAGE_BUTTON_TROPHIES", "images/selectionscene/button_trophies.png"},
    {"IMAGE_LOGO_BOUNCE", "images/selectionscene/logo_bounce.png"},
    {"IMAGE_LOGO_COIL", "images/selectionscene/logo_coil.png"},
    {"IMAGE_LOGO_STATIC", "images/selectionscene/logo_static.png"},
    // section default: dialog GUI
    {"BORDER_TOP_LEFT", "images/gui/dialogdefault/top_left.png"},
    {"BORDER_TOP_CENTER", "images/gui/dialogdefault/top.png"},
    {"BORDER_TOP_RIGHT", "images/gui/dialogdefault/top_right.png"},
    {"BORDER_CENTER_LEFT", "images/gui/dialogdefault/left.png"},
    {"BORDER_CENTER_RIGHT", "images/gui/dialogdefault/right.png"},
    {"BORDER_BOTTOM_LEFT", "images/gui/dialogdefault/bottom_left.png"},
    {"BORDER_BOTTOM_CENTER", "images/gui/dialogdefault/bottom.png"},
    {"BORDER_BOTTOM_RIGHT", "images/gui/dialogdefault/bottom_right.png"},
    {"BORDER_CENTER", "images/gui/background.png"},
    // Game scene (resources.xml section "gamescene").
    {"gamescene/BACKGROUND", "images/gamescene/background.png"},
    {"gamescene/BACKGROUND_TIMED", "images/gamescene/background_timed.png"},
    {"IMAGE_UI_BAR", "images/gamescene/ui_bar.png"},
    {"BUTTON_MENU", "images/gamescene/button_menu.png"},
    {"BUTTON_Twist", "images/gamescene/button_twist.png"},
    {"BUTTON_Enter", "images/gamescene/button_enter.png"},
    {"BUTTON_LastWord", "images/gamescene/button_lastword.png"},
    {"BUTTON_Clear", "images/gamescene/button_clear.png"},
    {"BUTTON_NextRound", "images/gamescene/button_nextround.png"},
    {"BUTTON_GiveUp", "images/gamescene/button_giveup.png"},
    {"COMMENTS", "images/gamescene/tt_commentary.png"},
    {"GAME_OVER", "images/gamescene/tt_gameover.PNG"},
    {"IMAGE_POPUP_BINGO", "images/gamescene/popup_bingo.png"},
    {"IMAGE_POPUP_ALLWORDS", "images/gamescene/popup_allwords.png"},
    {"ANAGRAM_TILE", "images/gamescene/anagramTile.png"},
    {"SMALL_LETTERS", "images/gamescene/anagram_text.png"},
    {"SMALL_LETTERS_UNGUESSED", "images/gamescene/anagram_letters_revealed.png"},
    {"IMAGE_LETTERBALL_ANIM", "images/gamescene/letterball_anim.png"},
    {"IMAGE_PANEL_BG", "images/gamescene/panel_bg.png"},
    {"IMAGE_PANEL_BOTTOM", "images/gamescene/panel_bottom.png"},
    {"IMAGE_PANEL_CORNERL", "images/gamescene/panel_cornerL.png"},
    {"IMAGE_PANEL_CORNERR", "images/gamescene/panel_cornerR.png"},
    {"IMAGE_PANEL_SIDEL", "images/gamescene/panel_sideL.png"},
    {"IMAGE_PANEL_SIDER", "images/gamescene/panel_sideR.png"},
    {"BUTTON_OK", "images/gui/button_ok.png"},
    {"BUTTON_CANCEL", "images/gui/button_cancel.png"},
    {"BUTTON_ADD", "images/gui/button_add.png"},
    {"BUTTON_REMOVE", "images/gui/button_remove.png"},
    {"BUTTON_YES", "images/gui/button_yes.png"},
    {"BUTTON_NO", "images/gui/button_no.png"},
    // In-game dialogs (resources.xml, sections default / trophyscene).
    {"BUTTON_PAUSE_RESUME", "images/gui/button_pause_resume.png"},
    {"BUTTON_PAUSE_SETTINGS", "images/gui/button_pause_settings.png"},
    {"BUTTON_PAUSE_END", "images/gui/button_pause_endgame.png"},
    {"BUTTON_PAUSE_HELP", "images/gui/button_pause_help.png"},
    {"BUTTON_END_MENU", "images/gui/button_end_menu.png"},
    {"BUTTON_END_PLAYAGAIN", "images/gui/button_end_playagain.png"},
    {"IMAGE_BURST", "images/trophyscene/burst.png"},
    // DIALOG_HELP (help-and-settings.md §2).
    {"BUTTON_PREV", "images/gui/dialoghelp/button_prev.png"},
    {"BUTTON_NEXT", "images/gui/dialoghelp/button_next.png"},
    {"IMAGE_HELP_PAGE01", "images/gui/dialoghelp/image_help_page01.png"},
    {"IMAGE_HELP_PAGE02", "images/gui/dialoghelp/image_help_page02.png"},
    {"IMAGE_HELP_PAGE03", "images/gui/dialoghelp/image_help_page03.png"},
    {"IMAGE_HELP_PAGE04", "images/gui/dialoghelp/image_help_page04.png"},
    {"INPUT_BOX", "images/gui/inputlabel.png"},
    {"CURSOR", "images/gui/cursor.png"},
    {"NAME_SELECT_BUTTON", "images/gui/name_select_button.png"},
    {"PLAYER_BORDER", "images/gui/name_select_border.png"},
};

// resources.xml: font id -> XML (the PNG layers are named inside).
// The second value is the placeholder scale when the font is missing.
struct FontFile { const char* xml; int placeholder_scale; };
const std::map<std::string, FontFile> kFontFiles = {
    {"FONT_FONT_UIBAR", {"fonts/font_uibar.xml", 4}},
    {"FONT_SETTINGSTEXT", {"fonts/font_settingstext.xml", 3}},
    {"FONT_DIALOG_HEADERS", {"fonts/font_dialog_headers.xml", 4}},
    {"FONT_DIALOG_HEADERS2", {"fonts/font_dialog_headers2.xml", 4}},
    {"FONT_UI_LABELS", {"fonts/font_uibar_labels.xml", 2}},
    {"FONT_UI_NUMBERS", {"fonts/font_uibar_numbers.xml", 3}},
    {"FONT_NEXTROUND02", {"fonts/font_nextround02.xml", 4}},
};

const std::map<SoundId, std::string> kSoundFiles = {
    {SoundId::MusicGameLoad, "music/music_game_load.ogg"},
    {SoundId::MusicMenu, "music/music_menu.ogg"},
    {SoundId::MusicLogo, "music/music_logo_animation.ogg"},
    {SoundId::LoadingSpring, "sound/loading_tee_sound.ogg"},
    {SoundId::LoadingBounce, "sound/loading_two_sound.ogg"},
    {SoundId::Button, "sound/button.ogg"},
    {SoundId::ButtonWordOfTheDay, "sound/button_wordoftheday.ogg"},
    {SoundId::LetterPlace, "sound/letter_place.ogg"},
    {SoundId::LetterClear, "sound/letter_clear.ogg"},
    {SoundId::LetterLoad, "sound/letter_load.ogg"},
    {SoundId::WordCorrect, "sound/word_correct.ogg"},
    {SoundId::WordReject, "sound/word_reject.ogg"},
    {SoundId::WordBingo, "sound/word_bingo.ogg"},
    {SoundId::WordLast, "sound/word_last.ogg"},
    {SoundId::WordTwist, "sound/word_twist.ogg"},
    {SoundId::WordPassReveal, "sound/word_pass_reveal.ogg"},
    {SoundId::AllWords, "sound/game_all_words_submitted.ogg"},
    {SoundId::ClockCountdown, "sound/game_clock_countdown.ogg"},
    {SoundId::ClockTimeUp, "sound/game_clock_timeup.ogg"},
    {SoundId::LoseLife, "sound/game_lose_life.ogg"},
    {SoundId::NextRoundAchieved, "sound/game_next_round_acheived.ogg"},
    {SoundId::HighScoreAchieved, "sound/game_high_score_acheived.ogg"},
    {SoundId::MusicInGame1, "music/music_in-game_1.ogg"},
};

// DEFAULT_PAINTER (dialogs.xml): corner / edge sizes of the 9-slice.
constexpr int FRAME_SIDE_W = 37;      // top_left.png is 37x86, bottom_left.png 37x53
constexpr int FRAME_TOP_H = 86;
constexpr int FRAME_BOTTOM_H = 53;
constexpr float DIALOG_BACKGROUND_ALPHA = 0.7f;   // default.xml

bool file_exists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

bool read_text(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Procedural letter ball for IMAGE_LETTERBALL_ANIM (placeholder build):
// a pearl-white sphere lit from the top left, new geometry in the
// original's colour language. Within each 17-frame letter row the shape
// follows the sheet's motion: frames 0-3 the ball turning edge-on and
// back (horizontal squash), 4-11 the round resting pearl, 12-16 a
// morph towards a rounded square tile. The letter itself is drawn on
// top at render time.
void paint_pearl(SDL_Surface* s, SDL_Rect cell, int f) {
    static const float squash[17] = {1.0f, 0.78f, 0.18f, 0.78f, 1, 1, 1, 1, 1, 1, 1, 1, 0.92f, 1, 1, 1, 1};
    static const float power[17] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2.6f, 3.4f, 4.6f, 7.0f};
    const float sx = squash[f], n = power[f];
    const float r = (f == 12 ? 25.0f : 27.5f);
    const float cx = cell.x + cell.w * 0.5f, cy = cell.y + cell.h * 0.5f;
    Uint32* px = static_cast<Uint32*>(s->pixels);
    const int pitch = s->pitch / 4;
    for (int y = cell.y; y < cell.y + cell.h; ++y) {
        for (int x = cell.x; x < cell.x + cell.w; ++x) {
            const float u = (x + 0.5f - cx) / (r * sx), v = (y + 0.5f - cy) / r;
            const float d = std::pow(std::fabs(u), n) + std::pow(std::fabs(v), n);   // superellipse
            if (d > 1.0f) continue;
            const float edge = std::min(1.0f, (1.0f - d) * r * 0.5f);              // ~1 px antialias
            const float nz = std::sqrt(std::max(0.0f, 1.0f - std::min(1.0f, u * u + v * v)));
            // Lambert from the upper left plus a tight specular spot.
            const float lx = -0.45f, ly = -0.55f, lz = 0.70f;
            const float lam = std::max(0.0f, u * lx + v * ly + nz * lz);
            const float spec = std::pow(std::max(0.0f, lam), 24.0f);
            const float shade = 0.60f + 0.40f * lam;
            auto ch = [&](float base) { return static_cast<Uint8>(std::min(255.0f, base * shade + 255.0f * spec * 0.6f)); };
            px[y * pitch + x] = SDL_MapRGBA(s->format, ch(236), ch(238), ch(246), static_cast<Uint8>(255 * edge));
        }
    }
}

// Procedural IMAGE_BURST: a soft white glow that pulses over the 20
// frames (the sprite's colour mod turns it orange).
void paint_burst(SDL_Surface* s, SDL_Rect cell, int f) {
    const float phase = 0.75f + 0.25f * std::sin(static_cast<float>(f) / 20.0f * 6.2831853f);
    const float cx = cell.x + cell.w * 0.5f, cy = cell.y + cell.h * 0.5f, r = cell.w * 0.5f * phase;
    Uint32* px = static_cast<Uint32*>(s->pixels);
    const int pitch = s->pitch / 4;
    for (int y = cell.y; y < cell.y + cell.h; ++y)
        for (int x = cell.x; x < cell.x + cell.w; ++x) {
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy) / r;
            const float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d) * 0.8f;
            px[y * pitch + x] = SDL_MapRGBA(s->format, 255, 255, 255, static_cast<Uint8>(255 * a));
        }
}

// Placeholder art: the original colour language (royal blue / cyan /
// gold) with new geometry. Each frame of a sheet gets a slightly
// different shade so animations stay visible.
SDL_Surface* make_placeholder(const SheetSpec& spec) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, spec.sheet_w(), spec.sheet_h(), 32,
                                                    SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    const bool full_screen = spec.frame_w == SCREEN_W && spec.frame_h == SCREEN_H;
    Uint8 r = 0x15, g = 0x54, b = 0xdc;
    if (spec.image_id == "splash/BACKGROUND") { r = g = b = 0; }
    else if (spec.image_id.rfind("IMAGE_LOGO", 0) == 0) { r = 0xf4; g = 0xda; b = 0x31; }
    else if (spec.image_id.rfind("BUTTON", 0) == 0 || spec.image_id.rfind("IMAGE_BUTTON", 0) == 0 ||
             spec.image_id == "NAME_SELECT_BUTTON") {
        r = 0x03; g = 0x30; b = 0xac;
    } else if (spec.image_id == "LOADING_BAR") { r = 0xf7; g = 0xde; b = 0x2b; }
    else if (spec.image_id == "IMAGE_LOADINGBAR_EMPTY") { r = 0x01; g = 0x1c; b = 0x6a; }
    else if (spec.image_id == "ANI_CIRCLES" || spec.image_id == "IMAGE_SPRING") { r = 0x1d; g = 0xda; b = 0xf1; }
    else if (spec.image_id == "CLICK_HERE" || spec.image_id == "IMAGE_GH_BRANDING") { r = g = b = 0xfe; }
    else if (spec.image_id == "INPUT_BOX") { r = g = b = 0xf0; }
    else if (spec.image_id == "CURSOR") { r = g = b = 0x10; }
    else if (spec.image_id == "PLAYER_BORDER") { r = 0xf3; g = 0xa8; b = 0x04; }
    else if (spec.image_id == "ANAGRAM_TILE") { r = g = b = 0xf4; }
    else if (spec.image_id == "IMAGE_LETTERBALL_ANIM") { r = g = b = 0xf8; }
    else if (spec.image_id.rfind("IMAGE_PANEL", 0) == 0) { r = 0x7f; g = 0x9a; b = 0xdc; }
    else if (spec.image_id == "IMAGE_UI_BAR") { r = 0x02; g = 0x2e; b = 0xa8; }
    else if (spec.image_id == "COMMENTS" || spec.image_id == "GAME_OVER" ||
             spec.image_id.rfind("IMAGE_POPUP", 0) == 0) { r = 0x16; g = 0x9c; b = 0xd8; }

    if (spec.image_id == "IMAGE_LETTERBALL_ANIM" || spec.image_id == "IMAGE_BURST") {
        SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 0, 0, 0, 0));
        for (int f = 0; f < spec.frames; ++f) {
            SDL_Rect cell{spec.frame_x(f), spec.frame_y(f), spec.frame_w, spec.frame_h};
            if (spec.image_id == "IMAGE_BURST") paint_burst(s, cell, f);
            else paint_pearl(s, cell, f % BALL_FRAMES_PER_LETTER);
        }
        return s;
    }
    for (int f = 0; f < spec.frames; ++f) {
        SDL_Rect cell{spec.frame_x(f), spec.frame_y(f), spec.frame_w, spec.frame_h};
        const int shade = spec.frames > 1 ? (f * 40) / spec.frames : 0;
        Uint8 a = full_screen ? 255 : (spec.image_id == "ANI_CIRCLES" || spec.image_id == "IMAGE_SPRING" ? 60 : 255);
        if (spec.image_id == "PLAYER_BORDER") a = 0;   // only the border below
        if (spec.image_id == "SMALL_LETTERS" || spec.image_id == "SMALL_LETTERS_UNGUESSED") a = 0;   // text drawn instead
        if (spec.image_id == "ANAGRAM_TILE" && f == 1) { r = 0xf7; g = 0xde; b = 0x2b; }           // found = yellow
        auto clamp = [](int v) { return static_cast<Uint8>(v > 255 ? 255 : (v < 0 ? 0 : v)); };
        SDL_FillRect(s, &cell, SDL_MapRGBA(s->format, clamp(r + shade), clamp(g + shade), clamp(b + shade), a));
        const bool bordered = !full_screen && a != 0 && spec.frame_w > 4 && spec.frame_h > 4 &&
                              spec.image_id.rfind("IMAGE_PANEL", 0) != 0;
        if (bordered) {
            // 2 px gold border so the frame edges are visible.
            SDL_Rect edge[4] = {{cell.x, cell.y, cell.w, 2}, {cell.x, cell.y + cell.h - 2, cell.w, 2},
                                {cell.x, cell.y, 2, cell.h}, {cell.x + cell.w - 2, cell.y, 2, cell.h}};
            for (auto& e : edge) SDL_FillRect(s, &e, SDL_MapRGBA(s->format, 0xf3, 0xa8, 0x04, 255));
        }
    }
    return s;
}

// The placeholder font sheet: 5x5 cells painted white, one row.
SDL_Surface* make_placeholder_font_sheet(int scale) {
    const int n = static_cast<int>(std::strlen(PLACEHOLDER_CHARS));
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, n * (PLACEHOLDER_GLYPH_W + 1) * scale,
                                                    PLACEHOLDER_GLYPH_H * scale, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 0, 0, 0, 0));
    for (int i = 0; i < n; ++i) {
        const char* rows = placeholder_glyph_rows(static_cast<std::uint32_t>(PLACEHOLDER_CHARS[i]));
        if (!rows) continue;
        for (int y = 0; y < PLACEHOLDER_GLYPH_H; ++y)
            for (int x = 0; x < PLACEHOLDER_GLYPH_W; ++x)
                if (rows[y * PLACEHOLDER_GLYPH_W + x] == '#') {
                    SDL_Rect px{(i * (PLACEHOLDER_GLYPH_W + 1) + x) * scale, y * scale, scale, scale};
                    SDL_FillRect(s, &px, SDL_MapRGBA(s->format, 0xff, 0xff, 0xff, 0xff));
                }
    }
    return s;
}

struct Image {
    SDL_Surface* surface = nullptr;   // kept for alpha hit tests
    SDL_Texture* texture = nullptr;
    bool placeholder = true;
};

struct Font {
    BitmapFont font;
    std::vector<SDL_Texture*> layers;   // one per font.layers entry
    bool placeholder = true;
};

class Assets {
public:
    Assets(SDL_Renderer* r, std::string dir) : renderer_(r), dir_(std::move(dir)) {}
    ~Assets() {
        for (auto& [k, img] : images_) {
            if (img.texture) SDL_DestroyTexture(img.texture);
            if (img.surface) SDL_FreeSurface(img.surface);
        }
        for (auto& [k, f] : fonts_)
            for (SDL_Texture* t : f.layers) if (t) SDL_DestroyTexture(t);
        for (auto& [k, m] : music_) Mix_FreeMusic(m);
        for (auto& [k, c] : chunks_) Mix_FreeChunk(c);
    }

    Image& image(const SheetSpec& spec) {
        auto it = images_.find(spec.image_id);
        if (it != images_.end()) return it->second;
        Image img;
        auto f = kImageFiles.find(spec.image_id);
        if (f != kImageFiles.end()) {
            const std::string path = dir_ + "/" + f->second;
            if (file_exists(path)) {
                img.surface = IMG_Load(path.c_str());
                if (img.surface) {
                    img.placeholder = false;
                    if (img.surface->w != spec.sheet_w() || img.surface->h != spec.sheet_h())
                        std::fprintf(stderr, "warning: %s is %dx%d, sheet spec says %dx%d\n", path.c_str(),
                                     img.surface->w, img.surface->h, spec.sheet_w(), spec.sheet_h());
                } else {
                    std::fprintf(stderr, "IMG_Load(%s): %s\n", path.c_str(), IMG_GetError());
                }
            }
        }
        if (!img.surface) img.surface = make_placeholder(spec);
        return store(spec.image_id, img);
    }

    // Font by resources.xml id; falls back to the procedural 5x5 font.
    Font& font(const std::string& id) {
        auto it = fonts_.find(id);
        if (it != fonts_.end()) return it->second;
        Font f;
        auto spec = kFontFiles.find(id);
        int scale = spec != kFontFiles.end() ? spec->second.placeholder_scale : 3;
        if (id.rfind("PLACEHOLDER_", 0) == 0) scale = std::max(1, std::atoi(id.c_str() + 12));
        std::string xml;
        if (spec != kFontFiles.end() && read_text(dir_ + "/" + spec->second.xml, xml) &&
            BitmapFont::parse(xml, f.font)) {
            f.placeholder = false;
            for (const FontLayer& layer : f.font.layers) {
                const std::string path = dir_ + "/fonts/" + layer.image;
                SDL_Surface* s = file_exists(path) ? IMG_Load(path.c_str()) : nullptr;
                if (!s) std::fprintf(stderr, "warning: font layer %s missing\n", path.c_str());
                f.layers.push_back(texture_of(s));
                if (s) SDL_FreeSurface(s);
            }
        } else {
            f.font = make_placeholder_font(scale);
            SDL_Surface* s = make_placeholder_font_sheet(scale);
            f.layers.push_back(texture_of(s));
            if (s) SDL_FreeSurface(s);
        }
        return fonts_.emplace(id, f).first->second;
    }

    void play_stream(SoundId id) {
        if (!audio_) return;
        Mix_Music* m = music(id);
        if (!m) return;
        if (current_stream_ == id && Mix_PlayingMusic()) return;   // 0x42b080: keep playing
        Mix_PlayMusic(m, -1);
        Mix_VolumeMusic(MIX_MAX_VOLUME / 2);                        // volue="0.5"
        current_stream_ = id;
    }
    void play_sound(SoundId id) {
        if (!audio_) return;
        auto it = chunks_.find(id);
        if (it == chunks_.end()) {
            Mix_Chunk* c = nullptr;
            auto f = kSoundFiles.find(id);
            if (f != kSoundFiles.end()) {
                const std::string path = dir_ + "/" + f->second;
                if (file_exists(path)) c = Mix_LoadWAV(path.c_str());
            }
            it = chunks_.emplace(id, c).first;
        }
        if (it->second) Mix_PlayChannel(-1, it->second, 0);
    }
    void set_audio(bool on) { audio_ = on; }

private:
    Image& store(const std::string& key, Image img) {
        if (img.surface) {
            SDL_Surface* conv = SDL_ConvertSurfaceFormat(img.surface, SDL_PIXELFORMAT_RGBA32, 0);
            if (conv) {
                SDL_FreeSurface(img.surface);
                img.surface = conv;
            }
            img.texture = SDL_CreateTextureFromSurface(renderer_, img.surface);
            if (img.texture) SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
        }
        return images_.emplace(key, img).first->second;
    }
    SDL_Texture* texture_of(SDL_Surface* s) {
        if (!s) return nullptr;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        return t;
    }
    Mix_Music* music(SoundId id) {
        auto it = music_.find(id);
        if (it != music_.end()) return it->second;
        Mix_Music* m = nullptr;
        auto f = kSoundFiles.find(id);
        if (f != kSoundFiles.end()) {
            const std::string path = dir_ + "/" + f->second;
            if (file_exists(path)) m = Mix_LoadMUS(path.c_str());
        }
        return music_.emplace(id, m).first->second;
    }

    SDL_Renderer* renderer_;
    std::string dir_;
    bool audio_ = false;
    std::map<std::string, Image> images_;
    std::map<std::string, Font> fonts_;
    std::map<SoundId, Mix_Music*> music_;
    std::map<SoundId, Mix_Chunk*> chunks_;
    SoundId current_stream_ = SoundId::Button;
};

// Alpha of the pixel under (px, py) for the button's current frame.
Uint8 pixel_alpha(const Image& img, const SpriteState& s, float px, float py) {
    if (!img.surface) return 255;
    const int lx = static_cast<int>(px - s.left()) + s.sheet.frame_x(s.frame);
    const int ly = static_cast<int>(py - s.top()) + s.sheet.frame_y(s.frame);
    if (lx < 0 || ly < 0 || lx >= img.surface->w || ly >= img.surface->h) return 0;
    const Uint32* row = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(img.surface->pixels) + ly * img.surface->pitch);
    Uint8 r, g, b, a;
    SDL_GetRGBA(row[lx], img.surface->format, &r, &g, &b, &a);
    return a;
}

void draw_sprite(SDL_Renderer* ren, Assets& assets, const SpriteState& s, float ox = 0, float oy = 0) {
    if (!s.visible || s.alpha <= 0.0f) return;
    if (s.sheet.image_id.empty()) {
        // Image-less sprite (the splash cover): a black quad.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(s.alpha * 255));
        SDL_Rect dst{static_cast<int>(s.left() + ox), static_cast<int>(s.top() + oy), s.sheet.frame_w, s.sheet.frame_h};
        SDL_RenderFillRect(ren, &dst);
        return;
    }
    Image& img = assets.image(s.sheet);
    if (!img.texture) return;
    SDL_Rect src{s.sheet.frame_x(s.frame), s.sheet.frame_y(s.frame), s.sheet.frame_w, s.sheet.frame_h};
    SDL_Rect dst{static_cast<int>(s.left() + ox), static_cast<int>(s.top() + oy), s.sheet.frame_w, s.sheet.frame_h};
    if (s.has_clip) {
        src.x += static_cast<int>(s.clip.x);
        src.y += static_cast<int>(s.clip.y);
        src.w = static_cast<int>(s.clip.w);
        src.h = static_cast<int>(s.clip.h);
        dst.x += static_cast<int>(s.clip.x);
        dst.y += static_cast<int>(s.clip.y);
        dst.w = src.w;
        dst.h = src.h;
        if (src.w <= 0 || src.h <= 0) return;
    }
    int flip = SDL_FLIP_NONE;
    if (s.flip == Flip::Horizontal || s.flip == Flip::Both) flip |= SDL_FLIP_HORIZONTAL;
    if (s.flip == Flip::Vertical || s.flip == Flip::Both) flip |= SDL_FLIP_VERTICAL;
    SDL_SetTextureAlphaMod(img.texture, static_cast<Uint8>(s.alpha * 255));
    SDL_SetTextureColorMod(img.texture, (s.tint >> 16) & 0xFF, (s.tint >> 8) & 0xFF, s.tint & 0xFF);
    SDL_SetTextureBlendMode(img.texture, s.additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_RenderCopyEx(ren, img.texture, &src, &dst, 0.0, nullptr, static_cast<SDL_RendererFlip>(flip));
    SDL_SetTextureColorMod(img.texture, 255, 255, 255);
    SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
}

// Draw `text` with a bitmap font. (x, y) is the anchor: Top = top-centre
// of the run for centred text, Center = its middle. Returns the pixel
// rectangle of the run (for the input cursor).
SDL_Rect draw_text(SDL_Renderer* ren, Assets& assets, const std::string& font_id, const std::string& text,
                   float x, float y, TextAlign align, Anchor anchor, int char_spacing) {
    Font& f = assets.font(font_id);
    const int width = f.font.measure(text, char_spacing);
    const int height = f.font.glyph_height();
    const int left = static_cast<int>(x) + BitmapFont::align_offset(align, width);
    const int top = anchor == Anchor::Center ? static_cast<int>(y) - height / 2 : static_cast<int>(y);
    const auto run = f.font.layout(text, char_spacing);
    // Layers draw back to front: shadow, stroke, face.
    for (std::size_t li = f.layers.size(); li-- > 0;) {
        SDL_Texture* tex = f.layers[li];
        if (!tex) continue;
        for (const auto& p : run) {
            const auto r = f.font.layer_rect(*p.glyph, li, left + p.x, top);
            SDL_Rect src{r.sx, r.sy, r.w, r.h};
            SDL_Rect dst{r.dx, r.dy, r.w, r.h};
            SDL_RenderCopy(ren, tex, &src, &dst);
        }
    }
    return SDL_Rect{left, top, width, height};
}

// DEFAULT_PAINTER: nine images; the centre is background.png cropped
// around its middle (centerimages="true").
void draw_dialog_frame(SDL_Renderer* ren, Assets& assets, const Dialog& d) {
    const int x = static_cast<int>(d.left()), y = static_cast<int>(d.top());
    const int inner_w = d.w - 2 * FRAME_SIDE_W;
    const int inner_h = d.h - FRAME_TOP_H - FRAME_BOTTOM_H;
    Image& tl = assets.image(sheet("BORDER_TOP_LEFT", FRAME_SIDE_W, FRAME_TOP_H));
    if (tl.placeholder) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_Rect body{x, y, d.w, d.h};
        SDL_SetRenderDrawColor(ren, 0x08, 0x2a, 0x8e, 0xf2);
        SDL_RenderFillRect(ren, &body);
        SDL_Rect head{x, y, d.w, 56};                     // offsettop
        SDL_SetRenderDrawColor(ren, 0x03, 0x1c, 0x66, 0xff);
        SDL_RenderFillRect(ren, &head);
        SDL_SetRenderDrawColor(ren, 0xf3, 0xa8, 0x04, 0xff);
        for (int i = 0; i < 3; ++i) {
            SDL_Rect edge{x + i, y + i, d.w - 2 * i, d.h - 2 * i};
            SDL_RenderDrawRect(ren, &edge);
        }
        return;
    }
    auto blit = [&](const char* id, int w, int h, int dx, int dy, int dw, int dh, int sx = 0, int sy = 0) {
        Image& img = assets.image(sheet(id, w, h));
        if (!img.texture) return;
        SDL_Rect src{sx, sy, dw > w ? w : dw, dh > h ? h : dh};
        if (std::strcmp(id, "BORDER_CENTER") == 0) { src.w = dw; src.h = dh; }
        SDL_Rect dst{dx, dy, dw, dh};
        SDL_SetTextureAlphaMod(img.texture, 255);
        SDL_RenderCopy(ren, img.texture, &src, &dst);
    };
    // Centre first so the ornate borders overlap it. The borders fade to
    // transparent on their inner side, so the fill runs under the whole
    // frame (see dialog_fill_rects); both rects crop the same window out
    // of the middle of background.png so they line up seamlessly.
    {
        const int fill_w = d.w - FRAME_FILL_LEFT - FRAME_FILL_RIGHT;
        const int fill_h = d.h - FRAME_FILL_TOP - FRAME_FILL_BOTTOM;
        const int crop_x = (SCREEN_W - fill_w) / 2, crop_y = (SCREEN_H - fill_h) / 2;
        for (const FillRect& r : dialog_fill_rects(d.w, d.h)) {
            if (r.w <= 0 || r.h <= 0) continue;
            blit("BORDER_CENTER", SCREEN_W, SCREEN_H, x + r.x, y + r.y, r.w, r.h,
                 crop_x + (r.x - FRAME_FILL_LEFT), crop_y + (r.y - FRAME_FILL_TOP));
        }
    }
    blit("BORDER_TOP_LEFT", FRAME_SIDE_W, FRAME_TOP_H, x, y, FRAME_SIDE_W, FRAME_TOP_H);
    blit("BORDER_TOP_CENTER", 4, FRAME_TOP_H, x + FRAME_SIDE_W, y, inner_w, FRAME_TOP_H);
    blit("BORDER_TOP_RIGHT", FRAME_SIDE_W, FRAME_TOP_H, x + d.w - FRAME_SIDE_W, y, FRAME_SIDE_W, FRAME_TOP_H);
    blit("BORDER_CENTER_LEFT", FRAME_SIDE_W, 4, x, y + FRAME_TOP_H, FRAME_SIDE_W, inner_h);
    blit("BORDER_CENTER_RIGHT", FRAME_SIDE_W, 4, x + d.w - FRAME_SIDE_W, y + FRAME_TOP_H, FRAME_SIDE_W, inner_h);
    blit("BORDER_BOTTOM_LEFT", FRAME_SIDE_W, FRAME_BOTTOM_H, x, y + d.h - FRAME_BOTTOM_H, FRAME_SIDE_W, FRAME_BOTTOM_H);
    blit("BORDER_BOTTOM_CENTER", 4, FRAME_BOTTOM_H, x + FRAME_SIDE_W, y + d.h - FRAME_BOTTOM_H, inner_w, FRAME_BOTTOM_H);
    blit("BORDER_BOTTOM_RIGHT", FRAME_SIDE_W, FRAME_BOTTOM_H, x + d.w - FRAME_SIDE_W, y + d.h - FRAME_BOTTOM_H,
         FRAME_SIDE_W, FRAME_BOTTOM_H);
}

// Upper-case label for a placeholder button: "BUTTON_PAUSE_RESUME" -> "RESUME".
std::string placeholder_button_label(const std::string& name) {
    std::string label = name.rfind("BUTTON_", 0) == 0 ? name.substr(7) : name;
    for (auto& c : label) c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return label;
}

// A label that may span several lines ("\n"), `line_height` apart.
SDL_Rect draw_label_text(SDL_Renderer* ren, Assets& assets, const TextLabel& l, const std::string& text, float ox,
                         float oy) {
    SDL_Rect last{};
    const int lh = l.line_height > 0 ? l.line_height : assets.font(l.font_id).font.glyph_height();
    std::size_t start = 0;
    int line = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        const std::string part = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        last = draw_text(ren, assets, l.font_id, part, l.x + ox, l.y + oy + static_cast<float>(line * lh), l.align,
                         l.anchor, l.char_spacing);
        if (nl == std::string::npos) break;
        start = nl + 1;
        ++line;
    }
    return last;
}

// Any dialog: darkening, DEFAULT_PAINTER frame, sprites, buttons, labels.
// `input` / `cursor` drive the INPUT_TEXT label of the name dialog.
void draw_dialog(SDL_Renderer* ren, Assets& assets, const Dialog& d, const std::string& input = {},
                 bool cursor = false) {
    // dodarkenbackground="true": DIALOG_BACKGROUND_ALPHA over the scene.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(DIALOG_BACKGROUND_ALPHA * 255));
    SDL_Rect all{0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(ren, &all);
    draw_dialog_frame(ren, assets, d);
    const float ox = d.left(), oy = d.top();
    for (const SpriteState& s : d.sprites) draw_sprite(ren, assets, s, ox, oy);
    for (const MenuButton& b : d.buttons) {
        draw_sprite(ren, assets, b.sprite, ox, oy);
        if (b.sprite.visible && assets.image(b.sprite.sheet).placeholder && b.sprite.name.rfind("NAME_", 0) != 0)
            draw_text(ren, assets, "PLACEHOLDER_2", placeholder_button_label(b.sprite.name),
                      b.sprite.left() + ox + b.sprite.sheet.frame_w * 0.5f,
                      b.sprite.top() + oy + b.sprite.sheet.frame_h * 0.5f, TextAlign::Center, Anchor::Center, -1);
    }
    for (const TextLabel& l : d.labels) {
        if (!l.visible) continue;
        std::string text = l.text;
        if (l.name == "INPUT_TEXT") text = input;
        const SDL_Rect run = draw_label_text(ren, assets, l, text, ox, oy);
        if (l.name == "INPUT_TEXT" && cursor) {
            // CURSOR 20x20 after the text, cursory="-8" (INFERRED placement).
            SpriteState cur = sprite("CURSOR", sheet("CURSOR", 20, 20), 0, 0);
            cur.x = static_cast<float>(run.x + run.w + 1);
            cur.y = l.y + oy - 8.0f - 2.0f;
            draw_sprite(ren, assets, cur);
        }
    }
}

// One letter ball: IMAGE_LETTERBALL_ANIM row = letter, frame 11 = the
// resting pearl. (ox, oy) shifts a dialog-local ball onto the screen.
void draw_letter_ball(SDL_Renderer* ren, Assets& assets, const LetterBall& b, float ox, float oy) {
    const SheetSpec balls = sheet("IMAGE_LETTERBALL_ANIM", 1020, 1560, 17, 26, 442);
    SpriteState sp = sprite("BALL", balls, b.x + ox, b.y + oy);
    sp.anchor = Anchor::Center;
    sp.frame = b.sheet_frame();   // pearl at rest, flip frames while revealing
    draw_sprite(ren, assets, sp);
    // Placeholder: royal-blue letter once the flip shows it (frame 3+).
    if (assets.image(balls).placeholder && b.frame >= 3) {
        const std::string ch(1, static_cast<char>(b.letter - 'a' + 'A'));
        Font& font = assets.font("PLACEHOLDER_5");
        for (SDL_Texture* t : font.layers) if (t) SDL_SetTextureColorMod(t, 0x10, 0x3c, 0xb0);
        draw_text(ren, assets, "PLACEHOLDER_5", ch, b.x + ox, b.y + oy, TextAlign::Center, Anchor::Center, -1);
        for (SDL_Texture* t : font.layers) if (t) SDL_SetTextureColorMod(t, 255, 255, 255);
    }
}

// tt_commentary.png rows, for the placeholder build (the sheet carries the text).
const char* kCommentText[9] = {
    "SORRY, THAT'S\nNOT ONE OF THE\nLISTED WORDS", "CONGRATULATIONS!\nYOU GOT\nALL THE WORDS!",
    "YOU GOT THE\nLONGEST WORD!\nYOU QUALIFY FOR\nTHE NEXT ROUND!", "YOU'VE ALREADY\nGUESSED\nTHAT WORD!",
    "SORRY, YOU'VE ALREADY\nUSED THAT LETTER IN THE\nINTERSECTION WORD.", "YOU ALREADY HAVE\nENOUGH WORDS OF\nTHAT LENGTH!",
    "CORRECT!!\nYOU'VE EARNED EXTRA TIME!", "SORRY, ONLY THE\nHINTED LETTER WORKS\nIN THIS SPACE",
    "YOU DONT NEED\nANY WORDS OF\nTHAT LENGTH",
};

// Stretch one image over a rectangle (the panel strips are 1 px sheets).
void draw_stretched(SDL_Renderer* ren, Assets& assets, const char* id, int w, int h, SDL_Rect dst) {
    if (dst.w <= 0 || dst.h <= 0) return;
    Image& img = assets.image(sheet(id, w, h));
    if (!img.texture) return;
    SDL_SetTextureAlphaMod(img.texture, 255);
    SDL_RenderCopy(ren, img.texture, nullptr, &dst);
}

void draw_text_lines(SDL_Renderer* ren, Assets& assets, const std::string& font_id, const std::string& text,
                     float cx, float cy, int line_h) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur.push_back(c); }
    lines.push_back(cur);
    float y = cy - static_cast<float>(line_h) * static_cast<float>(lines.size() - 1) * 0.5f;
    for (const auto& l : lines) {
        draw_text(ren, assets, font_id, l, cx, y, TextAlign::Center, Anchor::Center, -1);
        y += static_cast<float>(line_h);
    }
}

// A sheet sprite that may be a placeholder: then `label` is drawn over it.
void draw_sprite_or_label(SDL_Renderer* ren, Assets& assets, const SpriteState& sp, const std::string& label,
                          const std::string& font_id, int line_h) {
    draw_sprite(ren, assets, sp);
    if (!sp.visible || sp.alpha <= 0.0f) return;
    if (assets.image(sp.sheet).placeholder && !label.empty())
        draw_text_lines(ren, assets, font_id, label, sp.left() + sp.sheet.frame_w * 0.5f,
                        sp.top() + sp.sheet.frame_h * 0.5f, line_h);
}

// DIALOG_HELP over draw_dialog(): without the imported page pictures the
// placeholder build prints its own short page text (help_strings) on the
// page that is mostly faded in.
void draw_help(SDL_Renderer* ren, Assets& assets, const Dialog& d) {
    draw_dialog(ren, assets, d);
    for (int i = 0; i < HELP_PAGE_COUNT; ++i) {
        const SpriteState* page = nullptr;
        for (const SpriteState& s : d.sprites) if (s.name == help_page_name(i)) page = &s;
        if (!page || page->alpha < 0.5f || !assets.image(page->sheet).placeholder) continue;
        const std::string text = wrap_text(assets.font("PLACEHOLDER_2").font,
                                           help_strings::PLACEHOLDER_PAGES[static_cast<std::size_t>(i)],
                                           HELP_PAGE_W - 80);
        float y = page->top() + d.top() + 40.0f;
        std::string line;
        std::istringstream lines(text);
        while (std::getline(lines, line)) {
            draw_text(ren, assets, "PLACEHOLDER_2", line, page->left() + d.left() + 40.0f, y, TextAlign::Left,
                      Anchor::TopLeft, -1);
            y += 22.0f;
        }
    }
}

void draw_game(SDL_Renderer* ren, Assets& assets, const GameScene& g) {
    draw_sprite(ren, assets, sprite("BACKGROUND", sheet(g.background_id(), SCREEN_W, SCREEN_H), 0, 0));

    // Panel around the word grid (0x426360: every piece follows the table rect).
    const int tx = static_cast<int>(g.table_x()), ty = static_cast<int>(g.table_y());
    const int tw = g.table().width, bottom = ty + g.table().height;
    draw_stretched(ren, assets, "IMAGE_PANEL_BG", 800, 195, SDL_Rect{tx, 0, tw, bottom});
    draw_stretched(ren, assets, "IMAGE_PANEL_SIDEL", 10, 1, SDL_Rect{tx - 10, 0, 10, bottom});
    draw_stretched(ren, assets, "IMAGE_PANEL_SIDER", 10, 1, SDL_Rect{tx + tw, 0, 10, bottom});
    draw_stretched(ren, assets, "IMAGE_PANEL_BOTTOM", 1, 10, SDL_Rect{tx, bottom, tw, 10});
    draw_stretched(ren, assets, "IMAGE_PANEL_CORNERL", 10, 10, SDL_Rect{tx - 10, bottom, 10, 10});
    draw_stretched(ren, assets, "IMAGE_PANEL_CORNERR", 10, 10, SDL_Rect{tx + tw, bottom, 10, 10});

    // Word grid: ANAGRAM_TILE (frame 1 = found) + SMALL_LETTERS per letter;
    // unguessed words revealed with SMALL_LETTERS_UNGUESSED.
    const SheetSpec tile = sheet("ANAGRAM_TILE", 20, 40, 1, 2, 2);
    const SheetSpec letters = sheet("SMALL_LETTERS", 180, 60, 9, 3, 27);
    const SheetSpec revealed = sheet("SMALL_LETTERS_UNGUESSED", 180, 60, 9, 3, 27);
    const bool letters_placeholder = assets.image(letters).placeholder;
    for (const auto& slot : g.slots()) {
        for (std::size_t j = 0; j < slot.word.size(); ++j) {
            const float x = static_cast<float>(slot.x + static_cast<int>(j) * ANAGRAM_TILE_W);
            const float y = static_cast<float>(slot.y);
            const int frame = slot.word[j] - 'a' + 1;
            if (slot.found || !slot.revealed) {
                SpriteState t = sprite("TILE", tile, x, y);
                t.frame = slot.found ? 1 : 0;
                draw_sprite(ren, assets, t);
            }
            if (slot.found) {
                SpriteState l = sprite("LETTER", letters, x, y);
                l.frame = frame;
                draw_sprite(ren, assets, l);
            } else if (slot.revealed) {
                SpriteState l = sprite("LETTER", revealed, x, y);
                l.frame = frame;
                draw_sprite(ren, assets, l);
            }
            if (letters_placeholder && (slot.found || slot.revealed)) {
                const std::string ch(1, static_cast<char>(slot.word[j] - 'a' + 'A'));
                draw_text(ren, assets, "PLACEHOLDER_2", ch, x + 10, y + 10, TextAlign::Center, Anchor::Center, -1);
            }
        }
    }

    for (const auto& b : g.balls()) draw_letter_ball(ren, assets, b, 0, 0);

    // UI bar and its labels (dialogs.xml SCENE_GAME, y = 574).
    draw_sprite(ren, assets, sprite("UI_BAR", sheet("IMAGE_UI_BAR", 800, 54), 0, 547));
    draw_text(ren, assets, "FONT_UI_LABELS", "SCORE:", 244, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.score_text(), 251, 574, TextAlign::Left, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_LABELS", "ROUND:", 537, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.round_text(), 541, 574, TextAlign::Left, Anchor::Center, -1);
    if (g.timed()) {   // INFERRED: the clock is hidden in Untimed
        draw_text(ren, assets, "FONT_UI_LABELS", "TIME:", 707, 574, TextAlign::Right, Anchor::Center, -1);
        draw_text(ren, assets, "FONT_UI_NUMBERS", g.timer_minutes_text(), 749, 574, TextAlign::Right, Anchor::Center, -1);
        draw_text(ren, assets, "FONT_UI_NUMBERS", g.timer_seconds_text(), 752, 574, TextAlign::Left, Anchor::Center, -1);
    }

    for (const MenuButton& b : g.buttons()) {
        draw_sprite(ren, assets, b.sprite);
        if (b.sprite.visible && assets.image(b.sprite.sheet).placeholder) {
            std::string label = b.sprite.name.substr(7);
            for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            draw_text(ren, assets, "PLACEHOLDER_2", label, b.sprite.left() + b.sprite.sheet.frame_w * 0.5f,
                      b.sprite.top() + b.sprite.sheet.frame_h * 0.5f, TextAlign::Center, Anchor::Center, -1);
        }
    }

    const SpriteState& c = g.commentary();
    draw_sprite_or_label(ren, assets, c, kCommentText[c.frame % 9], "PLACEHOLDER_2", 12);
    draw_sprite_or_label(ren, assets, g.popup_bingo(), "BINGO!\nNEXT ROUND!", "PLACEHOLDER_3", 20);
    draw_sprite_or_label(ren, assets, g.popup_all_words(), "ALL WORDS!", "PLACEHOLDER_3", 20);
    draw_sprite_or_label(ren, assets, g.game_over_sprite(), "GAME OVER", "PLACEHOLDER_4", 24);
}

// Prefer the user's own common.tree / full.tree (symlinks to the original
// install, gitignored); fall back to the open-license dictionaries.
bool load_tree(const std::string& dir, const char* original, const char* open, tt2::WordTree& out) {
    for (const char* name : {original, open}) {
        const std::string path = dir + "/" + name;
        std::string bytes;
        if (!read_text(path, bytes)) continue;
        try {
            out = tt2::WordTree::parse(bytes);
            std::printf("dictionary: %s (%zu words)\n", path.c_str(), out.size());
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
        }
    }
    return false;
}

// CTEXT_HIGHSCORE_NAMES (commontext.xml): the default-table fill names.
const std::vector<std::string> kHighScoreNames = {
    "Susan", "Karen", "Linda", "Julie", "Helen", "Sarah", "Carol", "Jane", "Janet", "Sandra", "Angela", "Ann",
    "Sharon", "Lisa", "Alison", "Sue", "Anne", "Mary", "Tracey", "Barbara", "Amanda", "Wendy", "Claire", "Emma",
    "Joanne", "Donna", "Nicola", "Diane", "Elaine", "Tracy", "Joan", "Pauline", "Debbie", "Fiona", "Denise",
    "Jackie", "Tina", "Shirley", "Louise", "Sally", "Sheila", "Judith", "Kim", "Maureen", "Brenda", "Rachel",
    "Laura", "Marie", "Dawn", "Yvonne", "Teresa",
};

}  // namespace

int main(int argc, char** argv) {
    std::string assets_dir = TT2_IMPORTED_ASSETS_DIR;
    if (const char* env = std::getenv("TT2_ASSETS")) assets_dir = env;
    std::string save_dir = tt2::default_save_dir().string();
    int speed = 1;
    long exit_after_ms = -1;
    std::string dump_dir;      // --dump-frames DIR: write BMP frames (for the README GIF)
    int dump_fps = 12;
    std::string debug_player;
    std::string data_dir = TT2_DATA_DIR;
    std::string auto_play;     // --play untimed|timed: start a game as soon as the menu is up
    std::string auto_type;     // --type "word|word": feed keystrokes into the game (smoke tests);
                               // '|' Return, ' ' twist, '~' Esc, '$' the pause menu's STOP, '>' NEXT ROUND,
                               // '?' HOW TO PLAY (pause menu, or the main menu without --play), '<' / '>' help pages
    long debug_seed = -1;      // --seed N: bag seed for --player (reproducible racks)
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) assets_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) save_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc) speed = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--exit-after") && i + 1 < argc) exit_after_ms = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-frames") && i + 1 < argc) dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) dump_fps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--player") && i + 1 < argc) debug_player = argv[++i];
        else if (!std::strcmp(argv[i], "--data-dir") && i + 1 < argc) data_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--play") && i + 1 < argc) auto_play = argv[++i];
        else if (!std::strcmp(argv[i], "--type") && i + 1 < argc) auto_type = argv[++i];
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) debug_seed = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: tt2 [--assets DIR] [--data-dir DIR] [--save-dir DIR] [--player NAME]\n"
                        "           [--play untimed|timed] [--speed N] [--exit-after MS]\n");
            return 0;
        }
    }
    if (speed < 1) speed = 1;

    // Profiles: load before the window so a corrupt file is reported early.
    tt2::PlayerManager players;
    tt2::ProfileStore store(save_dir);
    if (save_dir.empty()) std::fprintf(stderr, "warning: no save directory (HOME/APPDATA unset); profiles will not persist\n");
    else if (!store.load(players)) std::fprintf(stderr, "warning: %s is unreadable; starting with no players\n",
                                                store.profiles_path().string().c_str());
    if (!debug_player.empty()) {
        if (!players.select(debug_player)) {
            const auto r = players.add(debug_player, static_cast<std::uint32_t>(SDL_GetTicks()));
            if (r != tt2::AddPlayerResult::Added)
                std::fprintf(stderr, "warning: --player %s: %s\n", debug_player.c_str(), tt2::to_string(r));
        }
        if (debug_seed >= 0 && players.current()) players.current()->bag = tt2::BagState{static_cast<std::uint32_t>(debug_seed), 0};
        if (!save_dir.empty()) store.save(players);
    }
    std::printf("profiles: %s (%d player%s)\n", save_dir.empty() ? "(none)" : store.profiles_path().string().c_str(),
                players.count(), players.count() == 1 ? "" : "s");

    // Dictionaries: the trees must outlive every round (non-owning pointers).
    tt2::WordTree common_tree, full_tree;
    if (!load_tree(data_dir, "common.tree", "open-common.tree", common_tree) ||
        !load_tree(data_dir, "full.tree", "open-full.tree", full_tree)) {
        std::fprintf(stderr, "no dictionaries in %s (need open-common.tree + open-full.tree)\n", data_dir.c_str());
        return 1;
    }
    tt2::Random highscore_rng(static_cast<std::int64_t>(SDL_GetTicks()) + 17);
    tt2::HighScoreManager highscores(kHighScoreNames, highscore_rng);
    if (!save_dir.empty() && !store.load(highscores))
        std::fprintf(stderr, "warning: %s is unreadable; starting with default high scores\n",
                     store.highscores_path().string().c_str());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    SDL_Window* win = SDL_CreateWindow("Text Twist 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);

    // Hand pointer over clickable items, hourglass on the load
    // screen, arrow everywhere else. The original's cursor handling
    // is not decoded; this uses the platform cursors (no game assets
    // involved).
    SDL_Cursor* cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor* cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    SDL_Cursor* cursor_wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    bool cursor_hot = false;
    bool cursor_busy = false;

    Assets assets(ren, assets_dir);
    const bool audio = Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0;
    if (audio) Mix_Init(MIX_INIT_OGG);
    assets.set_audio(audio);
    std::printf("assets: %s (%s)\n", assets_dir.c_str(),
                file_exists(assets_dir + "/images/loadscene/logo_anim.png") ? "imported" : "placeholders");

    ProfileFlow::Services pservices;
    pservices.play_sound = [&](SoundId id) { assets.play_sound(id); };
    pservices.on_change = [&] {
        if (save_dir.empty()) return;
        if (!store.save(players))
            std::fprintf(stderr, "warning: could not write %s\n", store.profiles_path().string().c_str());
        if (!store.save(highscores))
            std::fprintf(stderr, "warning: could not write %s\n", store.highscores_path().string().c_str());
    };
    pservices.seed_millis = [] { return static_cast<std::uint32_t>(SDL_GetTicks()); };   // timeGetTime()
    ProfileFlow profile(players, pservices);

    bool running = true;
    WelcomeFlow::Services services;
    services.play_sound = [&](SoundId id) { assets.play_sound(id); };
    services.play_stream = [&](SoundId id) { assets.play_stream(id); };
    // The original's loader fills the bar while sections default +
    // sceneselection load. Our assets are loaded lazily and are tiny,
    // so report "done" immediately; the 3500 ms floor still applies.
    services.loader_done = [] { return true; };
    services.loader_progress = [] { return 1.0f; };
    services.preloading_done = [] { return true; };
    // Game scene and the fade-to-black around entering / leaving it
    // (TRANSITION_DEFAULT: 500 ms, half out, half in).
    std::unique_ptr<GameScene> game;
    constexpr int GAME_FADE_HALF_MS = 250;
    int game_fade_out = 0, game_fade_in = 0;
    bool pending_start = false, pending_timed = false, pending_exit = false;
    // Saved game to continue in the next start_game (0x4242a0 / 0x42a460).
    std::unique_ptr<tt2::SavedGame> pending_resume;
    auto mode_name = [](bool timed) { return tt2::save_mode_name(timed ? tt2::GameMode::Timed : tt2::GameMode::Untimed); };
    auto begin_game = [&](bool timed) {
        pending_start = true;
        pending_timed = timed;
        game_fade_out = GAME_FADE_HALF_MS;
    };
    // Yes/no MESSAGE word wrap in the label's own font (game-scene.md §6).
    const LineBreaker wrap_message = [&assets](const std::string& text, int max_width) {
        return wrap_text(assets.font("FONT_SETTINGSTEXT").font, text, max_width);
    };
    // The main menu's HOW TO PLAY (0x42ee90 -> DIALOG_HELP).
    std::optional<HelpDialog> menu_help;
    auto menu_help_press = [&](const std::string& name) {
        if (!menu_help) return;
        const MenuButton* b = menu_help->dialog().button(name);
        if (!b || !b->sprite.visible) return;
        assets.play_sound(SoundId::Button);
        if (menu_help->press(name)) menu_help.reset();
    };
    // "Continue the game?" (CTEXT_START_SAVEGAME, 0x42dc80) on the menu.
    Dialog menu_dialog;
    bool menu_dialog_open = false;
    bool menu_dialog_timed = false;
    auto request_game = [&](bool timed) {
        if (!players.current()) { profile.open_player_select(); return; }
        if (!save_dir.empty() && store.has_saved_game(players.current()->name, mode_name(timed))) {
            menu_dialog =
                build_yes_no(game_strings::SAVEGAME_HEADER, game_strings::SAVEGAME_BODY, false, wrap_message);
            menu_dialog_open = true;
            menu_dialog_timed = timed;
            return;
        }
        begin_game(timed);
    };
    auto menu_dialog_press = [&](const std::string& name) {
        if (name.empty() || !menu_dialog_open) return;
        assets.play_sound(SoundId::Button);
        menu_dialog_open = false;
        const std::string player = players.current() ? players.current()->name : std::string();
        const char* mode = mode_name(menu_dialog_timed);
        if (name == "BUTTON_YES") {
            auto g = std::make_unique<tt2::SavedGame>();
            if (store.load_saved_game(player, mode, *g)) pending_resume = std::move(g);
            else std::fprintf(stderr, "warning: saved game for %s is unreadable; starting again\n", player.c_str());
        } else if (name == "BUTTON_NO") {
            store.remove_saved_game(player, mode);   // "Select 'No' to start again."
        } else {
            return;
        }
        begin_game(menu_dialog_timed);
    };
    services.menu_action = [&](const std::string& name) {
        if (name == "BUTTON_QUIT") running = false;
        else if (name == "BUTTON_CHANGENAME") profile.open_player_select();
        else if (name == "BUTTON_PLAY_UNTIMED") request_game(false);
        else if (name == "BUTTON_PLAY") request_game(true);
        else if (name == "BUTTON_HOWTOPLAY") menu_help.emplace();
        else std::printf("menu: %s (not implemented yet)\n", name.c_str());
    };
    WelcomeFlow flow(services, static_cast<std::uint32_t>(SDL_GetTicks() + 1));
    auto start_game = [&] {
        GameScene::Services gs;
        gs.play_sound = [&](SoundId id) { assets.play_sound(id); };
        gs.on_profile_change = pservices.on_change;
        gs.wrap_text = wrap_message;
        gs.on_exit = [&] { pending_exit = true; game_fade_out = GAME_FADE_HALF_MS; };
        const std::string player = players.current()->name;
        const std::string mode = mode_name(pending_timed);
        gs.save_game = [&store, player, mode, &save_dir](const tt2::SavedGame& g) {
            if (save_dir.empty()) return;
            if (!store.save_saved_game(player, mode, g))
                std::fprintf(stderr, "warning: could not write %s\n", store.saved_game_path(player, mode).string().c_str());
            else std::printf("game: saved %s for %s\n", mode.c_str(), player.c_str());
        };
        gs.discard_save = [&store, player, mode, &save_dir] {
            if (!save_dir.empty()) store.remove_saved_game(player, mode);
        };
        game = std::make_unique<GameScene>(common_tree, full_tree, *players.current(), &highscores, pending_timed,
                                           gs, static_cast<std::int64_t>(SDL_GetTicks()), pending_resume.get());
        if (pending_resume) std::printf("game: resumed round %d\n", pending_resume->round);
        pending_resume.reset();
        assets.play_stream(SoundId::MusicInGame1);
        std::printf("game: %s, rack %s, %zu slots\n", pending_timed ? "timed" : "untimed", game->rack().c_str(),
                    game->slots().size());
    };

    const SelectionScene::HitTest hit = [&](const MenuButton& b, float px, float py) {
        if (b.hit_threshold <= 0) return true;
        Image& img = assets.image(b.sprite.sheet);
        if (img.placeholder) return true;
        return pixel_alpha(img, b.sprite, px, py) > b.hit_threshold;
    };

    Uint32 last = SDL_GetTicks();
    long simulated_ms = 0;
    int dump_accum_ms = 0;
    int dump_index = 0;
    bool text_input_on = false;
    int auto_type_ms = 0;
    SceneId last_scene = flow.current();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                if (game) game->leave();   // 0x427b20: closing the window saves a running round
            }
            if (game && e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) game->focus_lost();
            if (profile.open()) {
                // A dialog is modal: it eats the input.
                if (e.type == SDL_TEXTINPUT) profile.text_input(e.text.text);
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_BACKSPACE: profile.backspace(); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: profile.key_return(); break;
                        case SDLK_ESCAPE: profile.key_escape(); break;
                        default: break;
                    }
                }
                if (e.type == SDL_MOUSEMOTION) profile.mouse_move(e.motion.x, e.motion.y);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    profile.mouse_down(e.button.x, e.button.y);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    profile.mouse_up(e.button.x, e.button.y);
                continue;
            }
            if (game) {
                if (e.type == SDL_TEXTINPUT) {
                    for (const char* c = e.text.text; *c; ++c) game->type_letter(*c);
                }
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_BACKSPACE: game->backspace(); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: game->key_return(); break;
                        case SDLK_SPACE: game->key_space(); break;
                        case SDLK_ESCAPE: game->key_escape(); break;
                        default: break;
                    }
                }
                if (e.type == SDL_MOUSEMOTION) game->mouse_move(e.motion.x, e.motion.y);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    game->mouse_down(e.button.x, e.button.y);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    game->mouse_up(e.button.x, e.button.y);
                continue;
            }
            if (menu_dialog_open) {
                if (e.type == SDL_KEYDOWN) {
                    const auto k = e.key.keysym.sym;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) menu_dialog_press("BUTTON_YES");
                    else if (k == SDLK_ESCAPE) menu_dialog_open = false;   // INFERRED: cancel
                }
                if (e.type == SDL_MOUSEMOTION) dialog_mouse_move(menu_dialog, e.motion.x, e.motion.y);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(menu_dialog, e.button.x, e.button.y);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    menu_dialog_press(dialog_mouse_up(menu_dialog, e.button.x, e.button.y));
                continue;
            }
            if (menu_help) {
                // Deviation: Esc closes the help (the original dialogs take no keys).
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) menu_help_press("BUTTON_OK");
                if (e.type == SDL_MOUSEMOTION) dialog_mouse_move(menu_help->dialog(), e.motion.x, e.motion.y);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(menu_help->dialog(), e.button.x, e.button.y);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    menu_help_press(dialog_mouse_up(menu_help->dialog(), e.button.x, e.button.y));
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (flow.current() == SceneId::Selection && flow.selection() && game_fade_out == 0) {
                auto* menu = flow.selection();
                if (e.type == SDL_MOUSEMOTION) menu->mouse_move(e.motion.x, e.motion.y, hit);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_down(e.button.x, e.button.y, hit);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_up(e.button.x, e.button.y, hit);
            }
        }
        // Keep the pointer cursor in sync with the screen and what is
        // under it: hourglass while loading, hand over a clickable
        // item, arrow everywhere else.
        bool hot = false;
        if (profile.open()) hot = profile.pointer_over_clickable();
        else if (menu_dialog_open) hot = dialog_pointer_hot(menu_dialog);
        else if (menu_help) hot = dialog_pointer_hot(menu_help->dialog());
        else if (game) hot = game->pointer_over_clickable();
        else if (flow.current() == SceneId::Selection && flow.selection() && game_fade_out == 0)
            hot = flow.selection()->pointer_over_clickable();
        const bool busy = flow.pointer_busy();
        if ((busy != cursor_busy || hot != cursor_hot) &&
            cursor_arrow && cursor_hand && cursor_wait) {
            cursor_busy = busy;
            cursor_hot = hot;
            SDL_SetCursor(busy ? cursor_wait : (hot ? cursor_hand : cursor_arrow));
        }
        const Uint32 now = SDL_GetTicks();
        int dt = static_cast<int>(now - last) * speed;
        last = now;
        if (dt > 100) dt = 100;
        if (game_fade_out > 0) {
            game_fade_out -= dt;
            if (game_fade_out <= 0) {
                game_fade_out = 0;
                if (pending_start) { pending_start = false; start_game(); }
                else if (pending_exit) {
                    pending_exit = false;
                    game.reset();
                    assets.play_stream(SoundId::MusicMenu);
                    std::printf("t=%ld ms: back to the menu\n", simulated_ms);
                }
                game_fade_in = GAME_FADE_HALF_MS;
            }
        } else if (game_fade_in > 0) {
            game_fade_in = std::max(0, game_fade_in - dt);
        }
        // --type also answers the menu's "Continue the game?" prompt: '|' YES, '~' NO.
        if (menu_dialog_open && !auto_type.empty() && (auto_type.front() == '|' || auto_type.front() == '~')) {
            auto_type_ms += dt;
            if (auto_type_ms >= 300) {
                auto_type_ms = 0;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                menu_dialog_press(c == '|' ? "BUTTON_YES" : "BUTTON_NO");
            }
        }
        // --type on the main menu (no --play): '?' HOW TO PLAY, '<' / '>' turn, '~' OK.
        if (!game && auto_play.empty() && !menu_dialog_open && !profile.open() && !auto_type.empty() &&
            flow.current() == SceneId::Selection && game_fade_out == 0) {
            auto_type_ms += dt;
            if (auto_type_ms >= 300) {
                auto_type_ms = 0;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                if (c == '?') services.menu_action("BUTTON_HOWTOPLAY");
                else if (c == '<') menu_help_press("BUTTON_PREV");
                else if (c == '>') menu_help_press("BUTTON_NEXT");
                else if (c == '~') menu_help_press("BUTTON_OK");
            }
        }
        if (game) {
            game->update(dt);
            // --type: one key every 300 ms while a round is being played.
            auto_type_ms += dt;
            while (!auto_type.empty() && auto_type_ms >= 300) {
                auto_type_ms -= 300;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                if (c == '\n' || c == '|') game->key_return();
                else if (c == ' ') game->key_space();
                else if (c == '~') game->key_escape();                  // pause menu / back
                else if (c == '$') game->dialog_press("BUTTON_STOP");   // pause menu STOP
                else if (c == '?') game->dialog_press("BUTTON_HOWTOPLAY");   // pause menu HOW TO PLAY
                else if (c == '<') game->dialog_press("BUTTON_PREV");       // help: previous page
                else if (c == '>') {
                    if (game->dialog_kind() == GameDialog::HowToPlay) game->dialog_press("BUTTON_NEXT");
                    else game->press("BUTTON_NextRound");                   // NEXT ROUND once qualified
                }
                else game->type_letter(c);
            }
        } else {
            flow.update(dt);
        }
        profile.update(dt);
        if (menu_dialog_open) dialog_tick(menu_dialog, dt);
        if (menu_help) menu_help->tick(dt);
        simulated_ms += dt;
        if (flow.current() != last_scene) {
            last_scene = flow.current();
            std::printf("t=%ld ms: scene %d\n", simulated_ms, static_cast<int>(last_scene));
            if (last_scene == SceneId::Selection) profile.on_menu_entered();   // 0x42d9b0
            if (last_scene == SceneId::Selection && !auto_play.empty()) request_game(auto_play == "timed");
        }
        const bool want_text = profile.wants_text_input() || (game && !profile.open());
        if (want_text != text_input_on) {
            text_input_on = want_text;
            if (text_input_on) SDL_StartTextInput(); else SDL_StopTextInput();
        }
        if (exit_after_ms >= 0 && simulated_ms >= exit_after_ms) running = false;

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (game) {
            draw_game(ren, assets, *game);
        } else {
            for (const SpriteState* s : flow.draw_list()) draw_sprite(ren, assets, *s);
            if (flow.current() == SceneId::Selection) {
                // LABEL_PLAYERNAME: FONT_FONT_UIBAR, centred at (400, 28), charspacing 2.
                draw_text(ren, assets, "FONT_FONT_UIBAR", welcome_text(players), 400, 28, TextAlign::Center,
                          Anchor::Center, 2);
            }
        }
        if (game && game->dialog_kind() == GameDialog::HowToPlay) {
            draw_help(ren, assets, game->dialog());
        } else if (game && game->dialog_open()) {
            draw_dialog(ren, assets, game->dialog());
            for (const LetterBall& b : game->dialog_balls())
                draw_letter_ball(ren, assets, b, game->dialog().left(), game->dialog().top());
        }
        if (menu_dialog_open) draw_dialog(ren, assets, menu_dialog);
        if (menu_help) draw_help(ren, assets, menu_help->dialog());
        if (profile.open()) draw_dialog(ren, assets, profile.dialog(), profile.input_text(), profile.cursor_visible());
        float black = flow.black_alpha();
        if (game_fade_out > 0) black = std::max(black, 1.0f - static_cast<float>(game_fade_out) / GAME_FADE_HALF_MS);
        if (game_fade_in > 0) black = std::max(black, static_cast<float>(game_fade_in) / GAME_FADE_HALF_MS);
        if (black > 0.0f) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(black * 255));
            SDL_Rect all{0, 0, SCREEN_W, SCREEN_H};
            SDL_RenderFillRect(ren, &all);
        }
        if (!dump_dir.empty()) {
            dump_accum_ms += dt;
            const int period = 1000 / (dump_fps > 0 ? dump_fps : 12);
            if (dump_accum_ms >= period) {
                dump_accum_ms -= period;
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 24, SDL_PIXELFORMAT_RGB24);
                if (shot && SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGB24, shot->pixels, shot->pitch) == 0) {
                    char name[64];
                    std::snprintf(name, sizeof name, "/frame_%04d.bmp", dump_index++);
                    SDL_SaveBMP(shot, (dump_dir + name).c_str());
                }
                if (shot) SDL_FreeSurface(shot);
            }
        }
        SDL_RenderPresent(ren);
    }

    SDL_SetCursor(SDL_GetDefaultCursor());
    if (cursor_wait) SDL_FreeCursor(cursor_wait);
    if (cursor_hand) SDL_FreeCursor(cursor_hand);
    if (cursor_arrow) SDL_FreeCursor(cursor_arrow);
    if (audio) Mix_CloseAudio();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
