// SDL3 front end for the welcome flow (splash -> loading -> main menu)
// and the player-profile dialogs.
//
// Assets: the repository ships none of the original art. The player
// imports it from their own install (the launch prompt, SETTINGS, or
// `tt2 --import DIR`) into a per-user folder; `--assets DIR` /
// `TT2_ASSETS` override it (docs/design/import-assets.md). Anything
// missing is replaced by a procedurally drawn placeholder of the same
// size, so the flow always boots.
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
//   --renderer NAME    auto, vulkan, opengl or software: the first renderer to
//                      try, for this run only (IMPROVEMENTS saves it)
//   --hover X,Y        keep the cursor at canvas point (X, Y) every frame, so
//                      frame dumps show hover frames and tooltips (smoke tests)

#include <SDL3/SDL.h>
#ifdef __ANDROID__
// Renames main() to SDL_main(), the symbol SDLActivity looks up in
// libmain.so. Desktop builds keep a plain main() without it.
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <unistd.h>
#endif
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/profile_store.hpp"
#include "tt2/utf8_path.hpp"
#include "tt2/word_tree.hpp"
#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/blitz_scene.hpp"
#include "tt2ui/asset_import.hpp"
#include "tt2ui/data_paths.hpp"
#include "tt2ui/dialog_fade.hpp"
#include "tt2ui/frame_clock.hpp"
#include "tt2ui/game_scene.hpp"
#include "tt2ui/help_dialog.hpp"
#include "tt2ui/highscore_dialog.hpp"
#include "tt2ui/import_prompt.hpp"
#include "tt2ui/improvements_dialog.hpp"
#include "tt2ui/render_options.hpp"
#include "tt2ui/settings_dialog.hpp"
#include "tt2ui/side_fill.hpp"
#include "tt2ui/tooltip.hpp"
#include "tt2ui/trophy_room.hpp"
#include "tt2ui/upscale.hpp"
#include "tt2ui/profile_dialogs.hpp"
#include "audio.hpp"
#include "folder_picker.hpp"
#ifdef __ANDROID__
#include "tt2ui/bundled_data.hpp"
#endif
#include "display_sdl.hpp"
#include "render_backend_sdl.hpp"
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
    // The port's own art (deviation UX-2): generated, never in the original
    // install, so it always comes from data/placeholder.
    {"BUTTON_IMPROVEMENTS", "images/selectionscene/button_improvements.png"},
    {"BUTTON_ARROW_LEFT", "images/gui/button_arrow_left.png"},
    {"BUTTON_ARROW_RIGHT", "images/gui/button_arrow_right.png"},
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
    {"IMAGE_GRA_CREDITS", "images/gui/gra_credits.png"},
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
    {"BUTTON_Pass", "images/gamescene/button_pass.png"},
    {"BUTTON_PASS2", "images/gamescene/button_pass2.png"},
    {"IMAGE_IMAGE_POPUP_0", "images/gamescene/image_popup_0.png"},
    {"IMAGE_IMAGE_POPUP_3", "images/gamescene/image_popup_3.png"},
    {"IMAGE_IMAGE_POPUP_5", "images/gamescene/image_popup_5.png"},
    {"IMAGE_IMAGE_POPUP_8", "images/gamescene/image_popup_8.png"},
    {"IMAGE_IMAGE_POPUP_10", "images/gamescene/image_popup_10.png"},
    {"IMAGE_POPUP_BINGO", "images/gamescene/popup_bingo.png"},
    {"IMAGE_POPUP_ALLWORDS", "images/gamescene/popup_allwords.png"},
    {"ANAGRAM_TILE", "images/gamescene/anagramTile.png"},
    {"SMALL_LETTERS", "images/gamescene/anagram_text.png"},
    {"SMALL_LETTERS_UNGUESSED", "images/gamescene/anagram_letters_revealed.png"},
    {"IMAGE_LETTERBALL_ANIM", "images/gamescene/letterball_anim.png"},
    {"IMAGE_LETTERBALL_40", "images/gamescene/letterball_anim_40.png"},
    {"IMAGE_BLITZDOT", "images/gamescene/blitzdot.png"},
    {"LetterBox", "images/gamescene/letterBox.png"},
    {"IMAGE_PANEL_BG", "images/gamescene/panel_bg.png"},
    {"IMAGE_PANEL_BOTTOM", "images/gamescene/panel_bottom.png"},
    {"IMAGE_PANEL_CORNERL", "images/gamescene/panel_cornerL.png"},
    {"IMAGE_PANEL_CORNERR", "images/gamescene/panel_cornerR.png"},
    {"IMAGE_PANEL_SIDEL", "images/gamescene/panel_sideL.png"},
    {"IMAGE_PANEL_SIDER", "images/gamescene/panel_sideR.png"},
    {"IMAGE_IMAGE_POPUP_6", "images/gamescene/image_popup_6.png"},
    {"IMAGE_HINT_LETTERS", "images/gamescene/hint_letters.png"},
    // SCENE_BLITZ (resources.xml section "blitzscene"); its panel pieces
    // share ids with gamescene's, so they carry the section.
    {"blitzscene/BACKGROUND_BLITZ", "images/blitzscene/background_blitz.png"},
    {"IMAGE_TIMERBAR", "images/blitzscene/timerBar.PNG"},
    {"IMAGE_BLITZ_MARK", "images/blitzscene/x.png"},
    {"IMAGE_PAUSEVEIL", "images/blitzscene/pauseshield.png"},
    {"IMAGE_BTN_ARROW_UP", "images/blitzscene/btn_arrow_up.png"},
    {"IMAGE_BTN_ARROW_DOWN", "images/blitzscene/btn_arrow_down.png"},
    {"blitzscene/IMAGE_PANEL_BG", "images/blitzscene/panel_bg.png"},
    {"blitzscene/IMAGE_PANEL_TOP", "images/blitzscene/panel_top.png"},
    {"blitzscene/IMAGE_PANEL_BOTTOM", "images/blitzscene/panel_bottom.png"},
    {"blitzscene/IMAGE_PANEL_SIDER", "images/blitzscene/panel_sideR.png"},
    {"blitzscene/IMAGE_PANEL_CORNERR", "images/blitzscene/panel_cornerR.png"},
    {"blitzscene/IMAGE_PANEL_CORNERTOPR", "images/blitzscene/panel_cornertopR.png"},
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
    {"IMAGE_BUTTON_READMORE", "images/gui/button_readmore.png"},
    {"IMAGE_BUTTON_PLAY", "images/gui/button_play.png"},
    {"IMAGE_BURST", "images/trophyscene/burst.png"},
    // SCENE_TROPHY and the game scene's trophy popup (resources.xml section
    // trophyscene). Sic: IMAGE_TROPHY18 / 19 / 20 are trophy20 / 18 / 19.png.
    {"trophyscene/BACKGROUND", "images/trophyscene/background.png"},
    {"IMAGE_TROPHY_RIBBON", "images/trophyscene/trophy_ribbon.png"},
    {"IMAGE_TROPHY1", "images/trophyscene/trophy.png"},
    {"IMAGE_TROPHY2", "images/trophyscene/trophy2.png"},
    {"IMAGE_TROPHY3", "images/trophyscene/trophy3.png"},
    {"IMAGE_TROPHY4", "images/trophyscene/trophy04.png"},
    {"IMAGE_TROPHY5", "images/trophyscene/trophy05.png"},
    {"IMAGE_TROPHY6", "images/trophyscene/trophy06.png"},
    {"IMAGE_TROPHY7", "images/trophyscene/trophy07.png"},
    {"IMAGE_TROPHY8", "images/trophyscene/trophy08.png"},
    {"IMAGE_TROPHY9", "images/trophyscene/trophy09.png"},
    {"IMAGE_TROPHY10", "images/trophyscene/trophy10.png"},
    {"IMAGE_TROPHY11", "images/trophyscene/trophy11.png"},
    {"IMAGE_TROPHY12", "images/trophyscene/trophy12.png"},
    {"IMAGE_TROPHY13", "images/trophyscene/trophy13.png"},
    {"IMAGE_TROPHY14", "images/trophyscene/trophy14.png"},
    {"IMAGE_TROPHY15", "images/trophyscene/trophy15.png"},
    {"IMAGE_TROPHY16", "images/trophyscene/trophy16.png"},
    {"IMAGE_TROPHY17", "images/trophyscene/trophy17.png"},
    {"IMAGE_TROPHY18", "images/trophyscene/trophy20.png"},
    {"IMAGE_TROPHY19", "images/trophyscene/trophy18.png"},
    {"IMAGE_TROPHY20", "images/trophyscene/trophy19.png"},
    {"IMAGE_TROPHY21", "images/trophyscene/trophy21.png"},
    {"IMAGE_TROPHY22", "images/trophyscene/trophy22.png"},
    {"IMAGE_TROPHY23", "images/trophyscene/trophy23.png"},
    {"IMAGE_TROPHY24", "images/trophyscene/trophy24.png"},
    {"IMAGE_PARTICLEBALL", "images/gamescene/particleball.png"},
    // DIALOG_HELP (help-and-settings.md §2).
    // DIALOG_SETTINGS (help-and-settings.md §3).
    {"SLIDER_BACK", "images/gui/slider_back.png"},
    {"SLIDER_BUTTON", "images/gui/slider_button.png"},
    {"CHECKBOX02", "images/gui/button_check.png"},
    {"IMAGE_DIFFICULTY_TAB", "images/gui/difficulty_tab.png"},
    // DIALOG_HIGHSCORES (profiles.md §4.4).
    {"TABS", "images/gui/tabs.png"},
    {"IMAGE_TAB_PAGE01", "images/gui/tab_page01.png"},
    {"IMAGE_TAB_PAGE02", "images/gui/tab_page02.png"},
    {"BUTTON_INFO", "images/gui/button_info.png"},
    // The port's own IMPORT (deviation OP-7): only data/placeholder/ has it.
    {"BUTTON_IMPORT", "images/gui/button_import.png"},
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
    {"FONT_TUTORIAL", {"fonts/font_tutorial01.xml", 3}},
    {"FONT_FONT_HIGHSCORE_TEXT", {"fonts/font_highscore_text.xml", 2}},
    {"FONT_HIGHSCOREBODY", {"fonts/font_highscorebody.xml", 2}},
    {"FONT_HIGHSCORELABEL", {"fonts/font_highscores_label.xml", 2}},
    {"FONT_TOOLTIP", {"fonts/arial12.xml", 2}},
    {"FONT_TROPHYNAMES", {"fonts/font_trophynames.xml", 2}},
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
    {SoundId::CheckboxOn, "sound/checkbox_on.ogg"},
    {SoundId::CheckboxOff, "sound/checkbox_off.ogg"},
    {SoundId::Slider, "sound/slider_4.ogg"},
    {SoundId::TrophyWon, "sound/game_trophy_won.ogg"},
    {SoundId::MusicTrophyRoom, "music/music_trophy_room.ogg"},
    {SoundId::LightningStartRound, "sound/lightning_start_round.ogg"},
    {SoundId::LightningTimeAdded, "sound/lightning_time_added.ogg"},
    {SoundId::LightningTimeLow, "sound/lightning_time_low.ogg"},
    {SoundId::LightningHint, "sound/lightning_hint.ogg"},
    {SoundId::LightningSkip, "sound/lightning_skip.ogg"},
};
// The few resources.xml sounds whose attribute is spelled `volume` (the
// `volue` ones are never read; help-and-settings.md §3.3).
const std::map<SoundId, float> kSoundVolumes = {
    {SoundId::CheckboxOn, 0.5f}, {SoundId::CheckboxOff, 0.5f}, {SoundId::Slider, 0.5f},
};

// DEFAULT_PAINTER (dialogs.xml): corner / edge sizes of the 9-slice.
constexpr int FRAME_SIDE_W = 37;      // top_left.png is 37x86, bottom_left.png 37x53
constexpr int FRAME_TOP_H = 86;
constexpr int FRAME_BOTTOM_H = 53;

// UTF-8 paths on every OS: std::fopen takes the ANSI code page on
// Windows, so an asset folder under C:\Users\José looked empty.
bool file_exists(const std::string& path) {
    if (SDL_IOStream* f = SDL_IOFromFile(path.c_str(), "rb")) {
        SDL_CloseIO(f);
        return true;
    }
    return false;
}

bool read_text(const std::string& path, std::string& out) {
    std::ifstream in(tt2::utf8_path(path), std::ios::binary);
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
    // 60x60 balls keep their 27.5 px radius; the 18x18 burst particle
    // (IMAGE_PARTICLEBALL) shrinks to fit its cell.
    const float r = std::min(f == 12 ? 25.0f : 27.5f, cell.w * 0.5f - 1.0f);
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
            px[y * pitch + x] = SDL_MapSurfaceRGBA(s, ch(236), ch(238), ch(246), static_cast<Uint8>(255 * edge));
        }
    }
}

// Procedural LetterBox (placeholder build): frame 0 a royal-blue square
// with a darker round socket, frame 1 a small gold dot. New geometry in
// the original's colours.
void paint_letter_box(SDL_Surface* s, SDL_Rect cell, int f) {
    const float cx = cell.x + cell.w * 0.5f, cy = cell.y + cell.h * 0.5f;
    Uint32* px = static_cast<Uint32*>(s->pixels);
    const int pitch = s->pitch / 4;
    for (int y = cell.y; y < cell.y + cell.h; ++y)
        for (int x = cell.x; x < cell.x + cell.w; ++x) {
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            Uint32 c = SDL_MapSurfaceRGBA(s, 0, 0, 0, 0);
            if (f == 1) {
                if (d <= 14.0f) c = SDL_MapSurfaceRGBA(s, 0xf7, 0xb8, 0x10, 255);
            } else {
                const bool rim = x - cell.x < 2 || y - cell.y < 2 || cell.x + cell.w - x <= 2 || cell.y + cell.h - y <= 2;
                if (rim) c = SDL_MapSurfaceRGBA(s, 0x7f, 0x9a, 0xdc, 255);
                else if (d <= 29.0f) c = SDL_MapSurfaceRGBA(s, 0x0c, 0x2c, 0x8c, 255);
                else c = SDL_MapSurfaceRGBA(s, 0x15, 0x54, 0xdc, 255);
            }
            px[y * pitch + x] = c;
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
            px[y * pitch + x] = SDL_MapSurfaceRGBA(s, 255, 255, 255, static_cast<Uint8>(255 * a));
        }
}

// Placeholder art: the original colour language (royal blue / cyan /
// gold) with new geometry. Each frame of a sheet gets a slightly
// different shade so animations stay visible.
SDL_Surface* make_placeholder(const SheetSpec& spec) {
    SDL_Surface* s = SDL_CreateSurface(spec.sheet_w(), spec.sheet_h(), SDL_PIXELFORMAT_RGBA32);
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
    else if (spec.image_id.rfind("IMAGE_POPUP", 0) == 0 || spec.image_id.rfind("IMAGE_IMAGE_POPUP", 0) == 0) {
        r = 0x16; g = 0x9c; b = 0xd8;
    }

    if (spec.image_id == "IMAGE_LETTERBALL_ANIM" || spec.image_id == "IMAGE_BURST" || spec.image_id == "LetterBox" ||
        spec.image_id == "IMAGE_PARTICLEBALL") {
        SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, 0, 0, 0, 0));
        for (int f = 0; f < spec.frames; ++f) {
            SDL_Rect cell{spec.frame_x(f), spec.frame_y(f), spec.frame_w, spec.frame_h};
            if (spec.image_id == "IMAGE_BURST") paint_burst(s, cell, f);
            else if (spec.image_id == "LetterBox") paint_letter_box(s, cell, f);
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
        if (spec.image_id == "ANAGRAM_TILE" && f == 1) { r = 0xf7; g = 0xde; b = 0x2b; }           // revealed = yellow
        auto clamp = [](int v) { return static_cast<Uint8>(v > 255 ? 255 : (v < 0 ? 0 : v)); };
        SDL_FillSurfaceRect(s, &cell, SDL_MapSurfaceRGBA(s, clamp(r + shade), clamp(g + shade), clamp(b + shade), a));
        const bool bordered = !full_screen && a != 0 && spec.frame_w > 4 && spec.frame_h > 4 &&
                              spec.image_id.rfind("IMAGE_PANEL", 0) != 0;
        if (bordered) {
            // 2 px gold border so the frame edges are visible.
            SDL_Rect edge[4] = {{cell.x, cell.y, cell.w, 2}, {cell.x, cell.y + cell.h - 2, cell.w, 2},
                                {cell.x, cell.y, 2, cell.h}, {cell.x + cell.w - 2, cell.y, 2, cell.h}};
            for (auto& e : edge) SDL_FillSurfaceRect(s, &e, SDL_MapSurfaceRGBA(s, 0xf3, 0xa8, 0x04, 255));
        }
    }
    return s;
}

// The placeholder font sheet: 5x5 cells painted white, one row.
SDL_Surface* make_placeholder_font_sheet(int scale) {
    const int n = static_cast<int>(std::strlen(PLACEHOLDER_CHARS));
    SDL_Surface* s = SDL_CreateSurface(n * (PLACEHOLDER_GLYPH_W + 1) * scale, PLACEHOLDER_GLYPH_H * scale,
                                       SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, 0, 0, 0, 0));
    for (int i = 0; i < n; ++i) {
        const char* rows = placeholder_glyph_rows(static_cast<std::uint32_t>(PLACEHOLDER_CHARS[i]));
        if (!rows) continue;
        for (int y = 0; y < PLACEHOLDER_GLYPH_H; ++y)
            for (int x = 0; x < PLACEHOLDER_GLYPH_W; ++x)
                if (rows[y * PLACEHOLDER_GLYPH_W + x] == '#') {
                    SDL_Rect px{(i * (PLACEHOLDER_GLYPH_W + 1) + x) * scale, y * scale, scale, scale};
                    SDL_FillSurfaceRect(s, &px, SDL_MapSurfaceRGBA(s, 0xff, 0xff, 0xff, 0xff));
                }
    }
    return s;
}

// SDL3 samples textures linearly by default; SDL2 (and the original's
// unscaled 800x600) drew them pixel for pixel, so ask for nearest.
SDL_Texture* nearest(SDL_Texture* t) {
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    return t;
}

// IMPROVEMENTS' TEXTURE FILTER (deviation UX-8) as an SDL scale mode.
SDL_ScaleMode scale_mode_of(tt2::TextureFilter f) {
    switch (f) {
        case tt2::TextureFilter::Sharp: return SDL_SCALEMODE_NEAREST;
        case tt2::TextureFilter::Smooth: return SDL_SCALEMODE_LINEAR;
        case tt2::TextureFilter::PixelArt: return SDL_SCALEMODE_PIXELART;
    }
    return SDL_SCALEMODE_NEAREST;
}

// A texture holding the surface's pixels. On the software renderer it is a
// streaming texture: SDL 3.4.18 run-length encodes static textures there,
// and its RLE blitter writes opaque alpha into a render target, which drew
// every dialog shadow solid black inside the fade layers (draw_dialog_layer).
SDL_Texture* upload(SDL_Renderer* ren, SDL_Surface* s) {
    if (!s) return nullptr;
    const char* name = SDL_GetRendererName(ren);
    if (!name || std::strcmp(name, SDL_SOFTWARE_RENDERER) != 0) return nearest(SDL_CreateTextureFromSurface(ren, s));
    SDL_Surface* rgba = s->format == SDL_PIXELFORMAT_RGBA32 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    if (!rgba) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(ren, rgba->format, SDL_TEXTUREACCESS_STREAMING, rgba->w, rgba->h);
    if (t && !SDL_UpdateTexture(t, nullptr, rgba->pixels, rgba->pitch)) {
        SDL_DestroyTexture(t);
        t = nullptr;
    }
    if (rgba != s) SDL_DestroySurface(rgba);
    return nearest(t);
}

// SDL3 draws with float rects only; the frame code keeps its whole pixels.
SDL_FRect frect(const SDL_Rect& r) {
    return SDL_FRect{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)};
}
void render_copy(SDL_Renderer* ren, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst) {
    const SDL_FRect s = src ? frect(*src) : SDL_FRect{}, d = dst ? frect(*dst) : SDL_FRect{};
    SDL_RenderTexture(ren, t, src ? &s : nullptr, dst ? &d : nullptr);
}
void render_fill(SDL_Renderer* ren, const SDL_Rect* r) {
    const SDL_FRect f = frect(*r);
    SDL_RenderFillRect(ren, &f);
}
void render_outline(SDL_Renderer* ren, const SDL_Rect* r) {
    const SDL_FRect f = frect(*r);
    SDL_RenderRect(ren, &f);
}

struct Image {
    SDL_Surface* surface = nullptr;   // kept for alpha hit tests; always the 1x file
    SDL_Texture* texture = nullptr;
    bool placeholder = true;
    // Texture pixels per canvas pixel: 2 for data/placeholder/2x/ (UX-8),
    // whose textures are always sampled linearly (Assets::apply_filter).
    float density = 1.0f;
};

struct Font {
    BitmapFont font;                    // the 1x XML, also for 2x layers
    std::vector<SDL_Texture*> layers;   // one per font.layers entry
    bool placeholder = true;
    float density = 1.0f;               // 2: layers from data/placeholder/2x/fonts/
};

class Assets {
public:
    Assets(SDL_Renderer* r, std::string dir) : renderer_(r), dir_(std::move(dir)) {}
    ~Assets() { release(); }

    const std::string& dir() const { return dir_; }
    // data/placeholder/ (tools/gen_placeholders.py): generated fonts, dialog
    // frame and buttons, used for each file the asset folder lacks.
    void set_placeholder_dir(std::string dir) { placeholder_dir_ = std::move(dir); }

    // The filter for every art texture (images and font layers), applied to
    // those already loaded too. Returns what applies: a renderer without
    // SDL_SCALEMODE_PIXELART gets Smooth (render_options.hpp).
    tt2::TextureFilter set_filter(tt2::TextureFilter f) {
        filter_ = f;
        for (auto& [k, img] : images_)
            if (img.density == 1.0f) apply_filter(img.texture);
        for (auto& [k, font] : fonts_)
            if (font.density == 1.0f)
                for (SDL_Texture* t : font.layers) apply_filter(t);
        return filter_;
    }

    // Deviation UX-8 (P8): load placeholder art from data/placeholder/2x/
    // where it exists, for files the asset folder lacks. Call it before the
    // first image or font is loaded; the software renderer leaves it off.
    void set_hires_art(bool on) { hires_art_ = on; }

    // Switches folders at run time (after an import). Frees everything from
    // the old folder first; textures come back lazily on the same renderer.
    // Call it between frames, never from inside a draw callback.
    void set_dir(std::string dir) {
        release();
        dir_ = std::move(dir);
        current_stream_ = SoundId::Button;
    }

    // Frees every texture, sound and song. main() calls it before
    // AudioOut::close and SDL_DestroyRenderer: the renderer frees its own
    // textures, so destroying them afterwards (from ~Assets at the end of
    // main) read freed memory and crashed the Windows Release build.
    void release() {
        release_art();
        if (audio_) audio_->stop_all();   // the tracks let go of the files
        for (auto& [k, m] : music_) AudioOut::free_clip(m);
        for (auto& [k, c] : chunks_) AudioOut::free_clip(c);
        music_.clear();
        chunks_.clear();
    }

    // Deviation UX-9 (upscale.hpp): upscale the art 2x with Super-xBR as it
    // loads. The work runs on worker threads; the 1x texture shows until
    // poll_upscales() swaps the 2x one in, so a big image never stalls a
    // frame. Turning it on or off reloads the art between frames.
    void set_upscale(bool on, int max_texture_size) {
        max_texture_ = max_texture_size;
        if (on == upscale_) return;
        upscale_ = on;
        release_art();
    }

    // Once a frame, between frames: start queued upscales while workers
    // are free, and swap finished ones in (textures are made on this thread).
    void poll_upscales() {
        const std::size_t workers = std::max(1u, std::min(4u, std::thread::hardware_concurrency() - 1));
        std::size_t running = 0;
        for (UpscaleJob& j : upscale_jobs_) {
            if (j.started) {
                ++running;
                continue;
            }
            if (running >= workers) break;
            j.result = std::async(std::launch::async, [px = std::move(j.pixels), w = j.w, h = j.h] {
                return super_xbr2x(px.data(), w, h);
            });
            j.started = true;
            ++running;
        }
        for (auto it = upscale_jobs_.begin(); it != upscale_jobs_.end();) {
            if (!it->started || it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            std::vector<std::uint8_t> big = it->result.get();
            SDL_Surface* s = SDL_CreateSurfaceFrom(2 * it->w, 2 * it->h, SDL_PIXELFORMAT_RGBA32, big.data(), 8 * it->w);
            SDL_Texture* t = texture_of(s, 2.0f);
            if (s) SDL_DestroySurface(s);
            if (it->layer < 0) swap_in_image(it->key, t);
            else swap_in_font_layer(it->key, static_cast<std::size_t>(it->layer), t);
            ++upscaled_;
            it = upscale_jobs_.erase(it);
        }
        if (upscale_jobs_.empty() && upscaled_ + upscale_skipped_ != upscale_logged_) {
            upscale_logged_ = upscaled_ + upscale_skipped_;
            std::printf("upscaler: %d textures at 2x, %d left at 1x (too large)\n", upscaled_, upscale_skipped_);
        }
    }

    Image& image(const SheetSpec& spec) {
        auto it = images_.find(spec.image_id);
        if (it != images_.end()) return it->second;
        Image img;
        auto f = kImageFiles.find(spec.image_id);
        SDL_Surface* hires = nullptr;
        if (f != kImageFiles.end()) {
            const std::string path = find_art(f->second);
            const std::string path_2x = placeholder_2x(f->second);
            if (!path_2x.empty()) hires = IMG_Load(path_2x.c_str());
            if (file_exists(path)) {
                img.surface = IMG_Load(path.c_str());
                if (img.surface) {
                    img.placeholder = false;
                    if (img.surface->w != spec.sheet_w() || img.surface->h != spec.sheet_h())
                        std::fprintf(stderr, "warning: %s is %dx%d, sheet spec says %dx%d\n", path.c_str(),
                                     img.surface->w, img.surface->h, spec.sheet_w(), spec.sheet_h());
                } else {
                    std::fprintf(stderr, "IMG_Load(%s): %s\n", path.c_str(), SDL_GetError());
                }
            }
        }
        if (!img.surface) {
            img.surface = make_placeholder(spec);
            img.placeholder = true;
        }
        if (hires && img.surface && (hires->w != 2 * img.surface->w || hires->h != 2 * img.surface->h)) {
            std::fprintf(stderr, "warning: 2x %s is not twice the 1x size; using 1x\n", f->second.c_str());
            SDL_DestroySurface(hires);
            hires = nullptr;
        }
        return store(spec.image_id, img, hires);
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
        const std::string xml_path = spec != kFontFiles.end() ? find_art(spec->second.xml) : std::string();
        if (!xml_path.empty() && read_text(xml_path, xml) && BitmapFont::parse(xml, f.font)) {
            f.placeholder = false;
            // The layers sit next to the XML they came with; a placeholder
            // font takes all of its layers from 2x/fonts/ when every one is there.
            std::string font_dir = xml_path.substr(0, xml_path.find_last_of('/'));
            bool all_2x = !f.font.layers.empty();
            for (const FontLayer& layer : f.font.layers)
                all_2x = all_2x && !placeholder_2x("fonts/" + layer.image).empty() &&
                         font_dir == placeholder_dir_ + "/fonts";
            if (all_2x) {
                font_dir = placeholder_dir_ + "/2x/fonts";
                f.density = 2.0f;
            }
            std::vector<SDL_Surface*> sources;
            for (const FontLayer& layer : f.font.layers) {
                const std::string path = font_dir + "/" + layer.image;
                SDL_Surface* s = file_exists(path) ? IMG_Load(path.c_str()) : nullptr;
                if (!s) std::fprintf(stderr, "warning: font layer %s missing\n", path.c_str());
                if (s && layer.mask) s = tint_mask(s, layer.color);
                f.layers.push_back(texture_of(s, f.density));
                sources.push_back(s);
            }
            // A font swaps to 2x only with every layer at 2x, so each glyph's
            // layers keep lining up.
            bool all_fit = f.density == 1.0f && upscale_;
            for (SDL_Surface* s : sources) all_fit = all_fit && s && can_upscale(s->w, s->h, max_texture_);
            for (std::size_t li = 0; li < sources.size(); ++li) {
                if (all_fit) queue_upscale(id, static_cast<int>(li), sources[li]);
                if (sources[li]) SDL_DestroySurface(sources[li]);
            }
            if (all_fit) font_pending_[id].assign(sources.size(), nullptr);
            else if (f.density == 1.0f && upscale_) ++upscale_skipped_;
        } else {
            f.font = make_placeholder_font(scale);
            SDL_Surface* s = make_placeholder_font_sheet(scale);
            f.layers.push_back(texture_of(s));
            if (s) SDL_DestroySurface(s);
        }
        return fonts_.emplace(id, f).first->second;
    }

    void play_stream(SoundId id) {
        if (!audio_) return;
        MIX_Audio* m = music(id);
        if (!m) return;
        if (current_stream_ == id && audio_->music_playing()) return;   // 0x42b080: keep playing
        audio_->play_music(m);
        current_stream_ = id;
    }
    void play_sound(SoundId id) {
        if (!audio_) return;
        auto it = chunks_.find(id);
        if (it == chunks_.end()) {
            MIX_Audio* c = nullptr;
            auto f = kSoundFiles.find(id);
            if (f != kSoundFiles.end()) {
                const std::string path = in_dir(f->second);
                if (file_exists(path)) c = audio_->load_sound(path);
            }
            it = chunks_.emplace(id, c).first;
        }
        auto v = kSoundVolumes.find(id);
        if (it->second) audio_->play_sound(it->second, v != kSoundVolumes.end() ? v->second : 1.0f);
    }
    // Null: no audio device, every sound is skipped.
    void set_audio(AudioOut* out) { audio_ = out; }
    // GameSettings volumes, 0..1, applied at once (0x463db0 / 0x463e30).
    // INFERRED: a linear map onto the mixer's gain (tt2ui/audio_mix.hpp).
    void set_sound_volume(float v) {
        if (audio_) audio_->set_sound_volume(v);
    }
    void set_music_volume(float v) {
        if (audio_) audio_->set_music_volume(v);
    }

private:
    // `hires` (taken over) is the 2x texture source; img.surface stays the
    // 1x file for the alpha hit tests (pixel_alpha).
    Image& store(const std::string& key, Image img, SDL_Surface* hires = nullptr) {
        if (img.surface) {
            SDL_Surface* conv = SDL_ConvertSurface(img.surface, SDL_PIXELFORMAT_RGBA32);
            if (conv) {
                SDL_DestroySurface(img.surface);
                img.surface = conv;
            }
            if (hires) img.density = 2.0f;
            img.texture = texture_of(hires ? hires : img.surface, img.density);
            // Art from a file at 1x (procedural placeholders excepted).
            if (upscale_ && !img.placeholder && img.density == 1.0f && img.texture) {
                if (can_upscale(img.surface->w, img.surface->h, max_texture_)) queue_upscale(key, -1, img.surface);
                else ++upscale_skipped_;
            }
        }
        if (hires) SDL_DestroySurface(hires);
        return images_.emplace(key, img).first->second;
    }
    // A `mask=` font layer (arial12): grey coverage -> the layer colour with
    // that coverage as alpha (bitmap_font.hpp apply_mask_color). Takes `s`.
    static SDL_Surface* tint_mask(SDL_Surface* s, std::uint32_t color) {
        SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(s);
        if (!rgba || !SDL_LockSurface(rgba)) return rgba;
        for (int y = 0; y < rgba->h; ++y)
            apply_mask_color(static_cast<std::uint8_t*>(rgba->pixels) + static_cast<std::size_t>(y) * rgba->pitch,
                             static_cast<std::size_t>(rgba->w), color);
        SDL_UnlockSurface(rgba);
        return rgba;
    }

    SDL_Texture* texture_of(SDL_Surface* s, float density = 1.0f) {
        if (!s) return nullptr;
        SDL_Texture* t = upload(renderer_, s);
        if (!t) return nullptr;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        // 2x art is drawn at 0.9 of its size at 1080p and 0.5 at 800x600:
        // nearest sampling would drop whole rows of it, so it is always
        // linear. Everything else follows TEXTURE FILTER.
        if (density != 1.0f) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        else apply_filter(t);
        return t;
    }
    // The textures and hit masks, but not the sounds. Upscales in flight are
    // waited for (std::future) and dropped.
    void release_art() {
        upscale_jobs_.clear();
        for (auto& [k, layers] : font_pending_)
            for (SDL_Texture* t : layers) if (t) SDL_DestroyTexture(t);
        font_pending_.clear();
        for (auto& [k, img] : images_) {
            if (img.texture) SDL_DestroyTexture(img.texture);
            if (img.surface) SDL_DestroySurface(img.surface);
        }
        for (auto& [k, f] : fonts_)
            for (SDL_Texture* t : f.layers) if (t) SDL_DestroyTexture(t);
        images_.clear();
        fonts_.clear();
        upscaled_ = upscale_skipped_ = upscale_logged_ = 0;
    }
    // Copies `s` (RGBA32) for a worker; the job starts in poll_upscales().
    void queue_upscale(const std::string& key, int layer, SDL_Surface* s) {
        SDL_Surface* rgba = s->format == SDL_PIXELFORMAT_RGBA32 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        if (!rgba) return;
        UpscaleJob j;
        j.key = key;
        j.layer = layer;
        j.w = rgba->w;
        j.h = rgba->h;
        j.pixels.resize(static_cast<std::size_t>(j.w) * static_cast<std::size_t>(j.h) * 4);
        for (int y = 0; y < j.h; ++y)
            std::memcpy(&j.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(j.w) * 4],
                        static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::size_t>(y) * rgba->pitch,
                        static_cast<std::size_t>(j.w) * 4);
        if (rgba != s) SDL_DestroySurface(rgba);
        upscale_jobs_.push_back(std::move(j));
    }
    void swap_in_image(const std::string& key, SDL_Texture* t) {
        auto it = images_.find(key);
        if (it == images_.end() || !t) {
            if (t) SDL_DestroyTexture(t);
            return;
        }
        if (it->second.texture) SDL_DestroyTexture(it->second.texture);
        it->second.texture = t;
        it->second.density = 2.0f;
    }
    void swap_in_font_layer(const std::string& key, std::size_t layer, SDL_Texture* t) {
        auto pending = font_pending_.find(key);
        auto font = fonts_.find(key);
        if (pending == font_pending_.end() || font == fonts_.end() || layer >= pending->second.size()) {
            if (t) SDL_DestroyTexture(t);
            return;
        }
        pending->second[layer] = t;
        for (SDL_Texture* p : pending->second)
            if (!p) return;   // more layers to come
        for (std::size_t li = 0; li < font->second.layers.size(); ++li) {
            if (font->second.layers[li]) SDL_DestroyTexture(font->second.layers[li]);
            font->second.layers[li] = pending->second[li];
        }
        font->second.density = 2.0f;
        font_pending_.erase(pending);
    }

    // data/placeholder/2x/<rel> when 2x art is on and the asset folder
    // lacks <rel>; empty otherwise.
    std::string placeholder_2x(const std::string& rel) const {
        if (!hires_art_ || placeholder_dir_.empty()) return {};
        const std::string own = in_dir(rel);
        if (!own.empty() && file_exists(own)) return {};
        const std::string p = placeholder_dir_ + "/2x/" + rel;
        return file_exists(p) ? p : std::string();
    }
    void apply_filter(SDL_Texture* t) {
        if (!t) return;
        if (SDL_SetTextureScaleMode(t, scale_mode_of(filter_))) return;
        if (filter_ != tt2::TextureFilter::PixelArt) return;
        filter_ = filter_with_support(filter_, false);
        std::printf("filter: pixel art unsupported (%s); using %s\n", SDL_GetError(), tt2::to_string(filter_));
        set_filter(filter_);   // the ones already set to pixel art too
    }
    MIX_Audio* music(SoundId id) {
        auto it = music_.find(id);
        if (it != music_.end()) return it->second;
        MIX_Audio* m = nullptr;
        auto f = kSoundFiles.find(id);
        if (f != kSoundFiles.end()) {
            const std::string path = in_dir(f->second);
            if (file_exists(path)) m = audio_->load_music(path);
        }
        return music_.emplace(id, m).first->second;
    }

    SDL_Renderer* renderer_;
    // A file inside the asset folder; empty (never found) without a folder.
    std::string in_dir(const std::string& rel) const { return dir_.empty() ? std::string() : dir_ + "/" + rel; }
    // Art: the asset folder's file, else the generated placeholder, else
    // the asset folder's path (missing, so the caller draws its own).
    std::string find_art(const std::string& rel) const {
        const std::string own = in_dir(rel);
        if (!own.empty() && file_exists(own)) return own;
        if (!placeholder_dir_.empty() && file_exists(placeholder_dir_ + "/" + rel)) return placeholder_dir_ + "/" + rel;
        return own;
    }

    std::string dir_;
    std::string placeholder_dir_;
    AudioOut* audio_ = nullptr;
    std::map<std::string, Image> images_;
    std::map<std::string, Font> fonts_;
    tt2::TextureFilter filter_ = tt2::TextureFilter::Sharp;
    bool hires_art_ = false;
    struct UpscaleJob {
        std::string key;                    // images_ or fonts_ key
        int layer = -1;                     // -1: an image; else a font layer
        int w = 0, h = 0;                   // the 1x size
        std::vector<std::uint8_t> pixels;   // RGBA32, handed to the worker
        std::future<std::vector<std::uint8_t>> result;
        bool started = false;
    };
    bool upscale_ = false;
    int max_texture_ = 0;
    std::deque<UpscaleJob> upscale_jobs_;
    std::map<std::string, std::vector<SDL_Texture*>> font_pending_;   // finished layers of a font
    int upscaled_ = 0, upscale_skipped_ = 0, upscale_logged_ = 0;
    std::map<SoundId, MIX_Audio*> music_;
    std::map<SoundId, MIX_Audio*> chunks_;
    SoundId current_stream_ = SoundId::Button;
};

// Alpha of the pixel under (px, py) for the button's current frame.
Uint8 pixel_alpha(const Image& img, const SpriteState& s, float px, float py) {
    if (!img.surface) return 255;
    const int lx = static_cast<int>(px - s.left()) + s.sheet.frame_x(s.frame);
    const int ly = static_cast<int>(py - s.top()) + s.sheet.frame_y(s.frame);
    if (lx < 0 || ly < 0 || lx >= img.surface->w || ly >= img.surface->h) return 0;
    Uint8 r = 0, g = 0, b = 0, a = 0;
    if (!SDL_ReadSurfacePixel(img.surface, lx, ly, &r, &g, &b, &a)) return 255;
    return a;
}

void draw_sprite(SDL_Renderer* ren, Assets& assets, const SpriteState& s, float ox = 0, float oy = 0) {
    if (!s.visible || s.alpha <= 0.0f) return;
    // Float destination rects: the scene moves sprites in fractions of a
    // pixel (eased ball slides, scales), and truncating each frame to whole
    // logical pixels made slow motion step once the 800x600 canvas is
    // scaled up to the window. Textures keep nearest sampling, so art stays
    // crisp; only the placement gets finer.
    if (s.sheet.image_id.empty()) {
        // Image-less sprite (the splash cover): a black quad.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(s.alpha * 255));
        SDL_FRect dst{s.left() + ox, s.top() + oy, static_cast<float>(s.sheet.frame_w),
                      static_cast<float>(s.sheet.frame_h)};
        SDL_RenderFillRect(ren, &dst);
        return;
    }
    Image& img = assets.image(s.sheet);
    if (!img.texture) return;
    SDL_Rect src{s.sheet.frame_x(s.frame), s.sheet.frame_y(s.frame), s.sheet.frame_w, s.sheet.frame_h};
    SDL_FRect dst{s.left() + ox, s.top() + oy, static_cast<float>(s.sheet.frame_w),
                  static_cast<float>(s.sheet.frame_h)};
    if (s.scale != 1.0f || s.scale_x != 1.0f || s.scale_y != 1.0f) {
        // Scale about the anchor point (x, y); clipped sprites never scale.
        const float sx = s.scale * s.scale_x, sy = s.scale * s.scale_y;
        if (sx <= 0.0f || sy <= 0.0f) return;
        dst.x = s.x + ox + (s.left() - s.x) * sx;
        dst.y = s.y + oy + (s.top() - s.y) * sy;
        dst.w = s.sheet.frame_w * sx;
        dst.h = s.sheet.frame_h * sy;
    }
    if (s.has_clip) {
        src.x += static_cast<int>(s.clip.x);
        src.y += static_cast<int>(s.clip.y);
        src.w = static_cast<int>(s.clip.w);
        src.h = static_cast<int>(s.clip.h);
        dst.x += static_cast<int>(s.clip.x);
        dst.y += static_cast<int>(s.clip.y);
        dst.w = static_cast<float>(src.w);
        dst.h = static_cast<float>(src.h);
        if (src.w <= 0 || src.h <= 0) return;
    }
    int flip = SDL_FLIP_NONE;
    if (s.flip == Flip::Horizontal || s.flip == Flip::Both) flip |= SDL_FLIP_HORIZONTAL;
    if (s.flip == Flip::Vertical || s.flip == Flip::Both) flip |= SDL_FLIP_VERTICAL;
    SDL_SetTextureAlphaMod(img.texture, static_cast<Uint8>(s.alpha * 255));
    SDL_SetTextureColorMod(img.texture, (s.tint >> 16) & 0xFF, (s.tint >> 8) & 0xFF, s.tint & 0xFF);
    SDL_SetTextureBlendMode(img.texture, s.additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    const float d = img.density;   // source in texture pixels, destination in canvas pixels
    const SDL_FRect srcf{src.x * d, src.y * d, src.w * d, src.h * d};
    SDL_RenderTextureRotated(ren, img.texture, &srcf, &dst, 0.0, nullptr, static_cast<SDL_FlipMode>(flip));
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
            const float d = f.density;
            const SDL_FRect src{r.sx * d, r.sy * d, r.w * d, r.h * d};
            const SDL_FRect dst{static_cast<float>(r.dx), static_cast<float>(r.dy), static_cast<float>(r.w),
                                static_cast<float>(r.h)};
            SDL_RenderTexture(ren, tex, &src, &dst);
        }
    }
    return SDL_Rect{left, top, width, height};
}

// Lines of `text` centred on (cx, cy), scaled about that point and drawn
// at `alpha` (the trophy popup's title, a Label under scale modifiers).
void draw_text_scaled(SDL_Renderer* ren, Assets& assets, const std::string& font_id, const std::string& text,
                      float cx, float cy, float scale, float alpha) {
    if (scale <= 0.0f || alpha <= 0.0f) return;
    Font& f = assets.font(font_id);
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur.push_back(c); }
    lines.push_back(cur);
    const int line_h = f.font.line_height > 0 ? f.font.line_height : f.font.glyph_height();
    const float block_h = static_cast<float>(line_h) * static_cast<float>(lines.size());
    const Uint8 a = static_cast<Uint8>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    for (SDL_Texture* t : f.layers) if (t) SDL_SetTextureAlphaMod(t, a);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int width = f.font.measure(lines[i], -1);
        const float left = -0.5f * static_cast<float>(width);
        const float top = -0.5f * block_h + static_cast<float>(line_h) * static_cast<float>(i);
        const auto run = f.font.layout(lines[i], -1);
        for (std::size_t li = f.layers.size(); li-- > 0;) {
            SDL_Texture* tex = f.layers[li];
            if (!tex) continue;
            for (const auto& g : run) {
                const auto r = f.font.layer_rect(*g.glyph, li, 0 + g.x, 0);
                const float d = f.density;
                const SDL_FRect src{r.sx * d, r.sy * d, r.w * d, r.h * d};
                const SDL_FRect dst{cx + (left + static_cast<float>(r.dx)) * scale,
                                    cy + (top + static_cast<float>(r.dy)) * scale, static_cast<float>(r.w) * scale,
                                    static_cast<float>(r.h) * scale};
                SDL_RenderTexture(ren, tex, &src, &dst);
            }
        }
    }
    for (SDL_Texture* t : f.layers) if (t) SDL_SetTextureAlphaMod(t, 255);
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
        render_fill(ren, &body);
        SDL_Rect head{x, y, d.w, 56};                     // offsettop
        SDL_SetRenderDrawColor(ren, 0x03, 0x1c, 0x66, 0xff);
        render_fill(ren, &head);
        SDL_SetRenderDrawColor(ren, 0xf3, 0xa8, 0x04, 0xff);
        for (int i = 0; i < 3; ++i) {
            SDL_Rect edge{x + i, y + i, d.w - 2 * i, d.h - 2 * i};
            render_outline(ren, &edge);
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
        const float d = img.density;
        const SDL_FRect srcf{src.x * d, src.y * d, src.w * d, src.h * d};
        const SDL_FRect dstf = frect(dst);
        SDL_RenderTexture(ren, img.texture, &srcf, &dstf);
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
// DIALOG_TUTORIAL's buttons are named BUTTON_HELP / BUTTON_OK, so their
// images name them instead.
std::string placeholder_button_label(const std::string& name, const std::string& image = {}) {
    if (image == "IMAGE_BUTTON_READMORE") return "READ MORE";
    if (image == "IMAGE_BUTTON_PLAY") return "PLAY";
    std::string label = name.rfind("BUTTON_", 0) == 0 ? name.substr(7) : name;
    for (auto& c : label) c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return label;
}

// A label that may span several lines ("\n"), `line_height` apart.
SDL_Rect draw_label_text(SDL_Renderer* ren, Assets& assets, const TextLabel& l, const std::string& text, float ox,
                         float oy) {
    SDL_Rect last{};
    if (l.alpha <= 0.0f) return last;
    // Page fades (HelpPager): every font layer at the label's alpha.
    std::vector<SDL_Texture*>& layers = assets.font(l.font_id).layers;
    const Uint8 a = static_cast<Uint8>(std::clamp(l.alpha, 0.0f, 1.0f) * 255.0f);
    if (a < 255)
        for (SDL_Texture* t : layers) if (t) SDL_SetTextureAlphaMod(t, a);
    const int lh = l.line_height > 0 ? l.line_height : assets.font(l.font_id).font.glyph_height();
    const float top = first_line_y(l, text_line_count(text), lh);
    std::size_t start = 0;
    int line = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        const std::string part = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        last = draw_text(ren, assets, l.font_id, part, l.x + ox, top + oy + static_cast<float>(line * lh), l.align,
                         l.anchor, l.char_spacing);
        if (nl == std::string::npos) break;
        start = nl + 1;
        ++line;
    }
    if (a < 255)
        for (SDL_Texture* t : layers) if (t) SDL_SetTextureAlphaMod(t, 255);
    return last;
}

// Any dialog: DEFAULT_PAINTER frame, sprites, buttons, labels. The
// backdrop and the show / hide fade come from draw_dialog_layer().
// `input` / `cursor` drive the INPUT_TEXT label of the name dialog.
void draw_dialog(SDL_Renderer* ren, Assets& assets, const Dialog& d, const std::string& input = {},
                 bool cursor = false) {
    draw_dialog_frame(ren, assets, d);
    const float ox = d.left(), oy = d.top();
    for (const SpriteState& s : d.sprites) draw_sprite(ren, assets, s, ox, oy);
    for (const MenuButton& b : d.buttons) {
        draw_sprite(ren, assets, b.sprite, ox, oy);
        const bool placeholder = b.sprite.visible && assets.image(b.sprite.sheet).placeholder;
        if (placeholder && b.sprite.name.rfind("SWITCH_", 0) == 0) {
            // Placeholder switch: no caption (the dialog labels it); a filled
            // centre marks "on" (rows 4..7).
            if (b.sprite.frame >= SWITCH_ON_ROW_OFFSET) {
                SDL_SetRenderDrawColor(ren, 0xff, 0xd0, 0x40, 0xff);
                SDL_Rect dot{static_cast<int>(b.sprite.left() + ox) + 6, static_cast<int>(b.sprite.top() + oy) + 6,
                             b.sprite.sheet.frame_w - 12, b.sprite.sheet.frame_h - 12};
                render_fill(ren, &dot);
            }
            continue;
        }
        if (placeholder && b.sprite.name.rfind("NAME_", 0) != 0)
            draw_text(ren, assets, "PLACEHOLDER_2", placeholder_button_label(b.sprite.name, b.sprite.sheet.image_id),
                      b.sprite.left() + ox + b.sprite.sheet.frame_w * 0.5f,
                      b.sprite.top() + oy + b.sprite.sheet.frame_h * 0.5f, TextAlign::Center, Anchor::Center, -1);
    }
    for (const TextLabel& l : d.labels) {
        if (!l.visible) continue;
        if (!l.fallback_for.empty()) {
            // A text stand-in: only while its picture is missing.
            const SpriteState* pic = nullptr;
            for (const SpriteState& s : d.sprites) if (s.name == l.fallback_for) pic = &s;
            if (!pic || !assets.image(pic->sheet).placeholder) continue;
        }
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
void draw_letter_ball(SDL_Renderer* ren, Assets& assets, const LetterBall& b, float ox, float oy,
                      float scale = 1.0f, float alpha = 1.0f) {
    const SheetSpec balls = sheet("IMAGE_LETTERBALL_ANIM", 1020, 1560, 17, 26, 442);
    SpriteState sp = sprite("BALL", balls, b.x + ox, b.y + oy);
    sp.anchor = Anchor::Center;
    sp.frame = b.sheet_frame();   // pearl at rest, flip frames while revealing
    sp.scale = scale;
    sp.alpha = alpha;
    draw_sprite(ren, assets, sp);
    // Placeholder: royal-blue letter once the flip shows it (frame 3+).
    if (assets.image(balls).placeholder && b.frame >= 3 && scale >= 0.9f && alpha >= 0.5f) {
        const std::string ch(1, static_cast<char>(b.letter - 'a' + 'A'));
        Font& font = assets.font("PLACEHOLDER_5");
        for (SDL_Texture* t : font.layers) if (t) SDL_SetTextureColorMod(t, 0x10, 0x3c, 0xb0);
        draw_text(ren, assets, "PLACEHOLDER_5", ch, b.x + ox, b.y + oy, TextAlign::Center, Anchor::Center, -1);
        for (SDL_Texture* t : font.layers) if (t) SDL_SetTextureColorMod(t, 255, 255, 255);
    }
}

// Placeholder-build text for each popup (our own wording; the imported
// PNGs carry the original text).
const char* popup_placeholder_text(PopupKind k) {
    switch (k) {
        case PopupKind::NotListed: return "NOT IN THE\nDICTIONARY";
        case PopupKind::AllWords: return "ALL WORDS!";
        case PopupKind::Bingo: return "BINGO!\nNEXT ROUND!";
        case PopupKind::AlreadyGuessed: return "ALREADY\nFOUND";
        case PopupKind::EnoughOfLength: return "THAT LENGTH\nIS FULL";
        case PopupKind::ExtraTime: return "+8 SECONDS";
        case PopupKind::NoneOfLength: return "NO WORDS OF\nTHAT LENGTH";
        case PopupKind::Definitions: return "CLICK A FOUND\nWORD TO LOOK\nIT UP";
    }
    return "";
}

// Stretch one image over a rectangle (the panel strips are 1 px sheets).
void draw_stretched(SDL_Renderer* ren, Assets& assets, const char* id, int w, int h, SDL_Rect dst) {
    if (dst.w <= 0 || dst.h <= 0) return;
    Image& img = assets.image(sheet(id, w, h));
    if (!img.texture) return;
    SDL_SetTextureAlphaMod(img.texture, 255);
    render_copy(ren, img.texture, nullptr, &dst);
}

// DEFAULT_TOOLTIP (tooltip.hpp): FONT_TOOLTIP text wrapped at maxwidth,
// padding 3 inside a #FFFFE1 box with a black border, all at alpha 220,
// placed by tooltip_place() from where the cursor was when it appeared.
void draw_tooltip(SDL_Renderer* ren, Assets& assets, const TooltipTracker& tip) {
    if (!tip.visible()) return;
    Font& f = assets.font("FONT_TOOLTIP");
    const std::string text = wrap_text(f.font, tip.text(), TOOLTIP_MAX_W - 2 * TOOLTIP_PADDING);
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    const int line_h = f.font.line_height > 0 ? f.font.line_height : f.font.glyph_height();
    int text_w = 0;
    for (const auto& l : lines) text_w = std::max(text_w, f.font.measure(l));
    const int w = text_w + 2 * TOOLTIP_PADDING;
    const int h = static_cast<int>(lines.size()) * line_h + 2 * TOOLTIP_PADDING;
    const TooltipPlace at = tooltip_place(tip.anchor_x(), tip.anchor_y(), static_cast<float>(w),
                                          static_cast<float>(h), SCREEN_W, SCREEN_H);
    const SDL_Rect box{static_cast<int>(at.x), static_cast<int>(at.y), w, h};
    const Uint8 a = static_cast<Uint8>(TOOLTIP_ALPHA);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, (TOOLTIP_BACKGROUND >> 16) & 0xFF, (TOOLTIP_BACKGROUND >> 8) & 0xFF,
                           TOOLTIP_BACKGROUND & 0xFF, a);
    render_fill(ren, &box);
    SDL_SetRenderDrawColor(ren, (TOOLTIP_BORDER >> 16) & 0xFF, (TOOLTIP_BORDER >> 8) & 0xFF, TOOLTIP_BORDER & 0xFF, a);
    render_outline(ren, &box);
    for (SDL_Texture* t : f.layers) if (t) SDL_SetTextureAlphaMod(t, a);
    float y = static_cast<float>(box.y + TOOLTIP_PADDING);
    for (const auto& l : lines) {
        draw_text(ren, assets, "FONT_TOOLTIP", l, static_cast<float>(box.x + TOOLTIP_PADDING), y, TextAlign::Left,
                  Anchor::Top, -1);
        y += static_cast<float>(line_h);
    }
    for (SDL_Texture* t : f.layers) if (t) SDL_SetTextureAlphaMod(t, 255);
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

// DIALOG_SETTINGS: the shared dialog drawing plus the two slider knobs
// (SLIDER_BUTTON 33x140, 4 rows, anchor centre; 0x468440).
void draw_settings(SDL_Renderer* ren, Assets& assets, const SettingsDialog& d) {
    draw_dialog(ren, assets, d.dialog());
    for (const SettingsSlider& s : d.sliders()) {
        SpriteState knob = sprite("SLIDER_KNOB", sheet("SLIDER_BUTTON", 33, 140, 1, 4, 4), s.knob_x(), s.knob_y());
        knob.anchor = Anchor::Center;
        knob.frame = s.knob_frame();
        draw_sprite(ren, assets, knob, d.dialog().left(), d.dialog().top());
    }
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

    // Word grid (0x430800): per letter an ANAGRAM_TILE (frame 0 white,
    // frame 1 yellow for a word revealed at round end) and a SMALL_LETTERS
    // sprite, both centre-anchored. Found and revealed words appear with
    // the 0x4310f0 flip (SlotView::tile).
    const SheetSpec tile = sheet("ANAGRAM_TILE", 20, 40, 1, 2, 2);
    const SheetSpec letters = sheet("SMALL_LETTERS", 180, 60, 9, 3, 27);
    const bool letters_placeholder = assets.image(letters).placeholder;
    for (const auto& slot : g.slots()) {
        for (std::size_t j = 0; j < slot.word.size(); ++j) {
            const float cx = static_cast<float>(slot.x + static_cast<int>(j) * ANAGRAM_TILE_W) + ANAGRAM_TILE_W * 0.5f;
            const float cy = static_cast<float>(slot.y) + ANAGRAM_TILE_H * 0.5f;
            const AnagramTile st = slot.tile(static_cast<int>(j));
            SpriteState t = sprite("TILE", tile, cx, cy);
            t.anchor = Anchor::Center;
            t.frame = st.tile_frame;
            t.scale_y = st.tile_scale_y;
            draw_sprite(ren, assets, t);
            if (!st.letter_visible) continue;
            SpriteState l = sprite("LETTER", letters, cx, cy);
            l.anchor = Anchor::Center;
            l.frame = slot.word[j] - 'a' + 1;
            l.scale_y = st.letter_scale_y;
            draw_sprite(ren, assets, l);
            if (letters_placeholder && st.letter_scale_y >= 0.5f) {
                const std::string ch(1, static_cast<char>(slot.word[j] - 'a' + 'A'));
                draw_text(ren, assets, "PLACEHOLDER_2", ch, cx, cy, TextAlign::Center, Anchor::Center, -1);
            }
        }
    }

    for (const auto& box : g.letter_boxes()) draw_sprite(ren, assets, box);   // under the balls (0x43ab90)
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

    // 0x428ec0: one popup at a time; the label waits until it has grown.
    if (g.popup_kind()) {
        const SpriteState& p = g.popup();
        draw_sprite_or_label(ren, assets, p, p.scale >= 0.9f ? popup_placeholder_text(*g.popup_kind()) : "",
                             "PLACEHOLDER_3", 20);
    }
    // 0x429510: the trophy and its FONT_TROPHYNAMES title, bottom right.
    if (g.trophy_popup_id() >= 0) {
        const SpriteState& t = g.trophy_popup();
        if (t.visible) draw_sprite(ren, assets, t);
        const SpriteState& l = g.trophy_label();
        if (l.visible) draw_text_scaled(ren, assets, "FONT_TROPHYNAMES", l.name, l.x, l.y, l.scale, l.alpha);
    }
}

// SCENE_BLITZ (dialogs.xml order): background, the list panel, the time
// bar, the cells, hint letters and balls, the marks, the UI bar, the
// buttons, PAUSE_VEIL over the list, then the two popups.
void draw_blitz(SDL_Renderer* ren, Assets& assets, const BlitzScene& g) {
    draw_sprite(ren, assets, sprite("BACKGROUND", sheet(g.background_id(), SCREEN_W, SCREEN_H), 0, 0));
    for (const auto& p : g.panel()) draw_sprite(ren, assets, p);
    draw_sprite(ren, assets, g.timer_bar());
    for (const auto& box : g.letter_boxes()) draw_sprite(ren, assets, box);
    // The list look's dots and 24 px balls need the real art; the
    // placeholder build keeps the 60 px pearls alone.
    const bool small_art = !assets.image(blitz_small_ball_sheet()).placeholder;
    if (!assets.image(blitz_dot_sheet()).placeholder)
        for (const auto& d : g.blitz_dots()) draw_sprite(ren, assets, d);
    const SheetSpec hints = hint_letter_sheet();
    for (const auto& h : g.hint_letters()) {
        draw_sprite(ren, assets, h);
        if (assets.image(hints).placeholder) {
            const std::string ch(1, static_cast<char>('A' + h.frame - 1));
            draw_text(ren, assets, "PLACEHOLDER_5", ch, h.x, h.y, TextAlign::Center, Anchor::Center, -1);
        }
    }
    for (const auto& b : g.balls()) {
        if (b.alpha > 0.0f) draw_letter_ball(ren, assets, b.ball, 0, 0, b.scale, b.alpha);
        if (!b.small || !small_art) continue;
        SpriteState sp = sprite("BALL_40", blitz_small_ball_sheet(), b.ball.x, b.ball.y);
        sp.anchor = Anchor::Center;
        sp.frame = b.ball.sheet_frame();
        sp.scale = BLITZ_SMALL_SCALE * b.scale;
        draw_sprite(ren, assets, sp);
    }
    for (const auto& m : g.marks()) draw_sprite(ren, assets, m);

    draw_sprite(ren, assets, sprite("UI_BAR", sheet("IMAGE_UI_BAR", 800, 54), 0, 547));
    draw_text(ren, assets, "FONT_UI_LABELS", "SCORE:", 244, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.score_text(), 251, 574, TextAlign::Left, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_LABELS", "ROUND:", 537, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.round_text(), 542, 574, TextAlign::Left, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_LABELS", "TIME:", 707, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.timer_minutes_text(), 749, 574, TextAlign::Right, Anchor::Center, -1);
    draw_text(ren, assets, "FONT_UI_NUMBERS", g.timer_seconds_text(), 752, 574, TextAlign::Left, Anchor::Center, -1);

    for (const MenuButton& b : g.buttons()) {
        draw_sprite(ren, assets, b.sprite);
        if (b.sprite.visible && assets.image(b.sprite.sheet).placeholder) {
            std::string label = b.sprite.name.substr(7);
            for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            draw_text(ren, assets, "PLACEHOLDER_2", label, b.sprite.left() + b.sprite.sheet.frame_w * 0.5f,
                      b.sprite.top() + b.sprite.sheet.frame_h * 0.5f, TextAlign::Center, Anchor::Center, -1);
        }
    }
    if (g.pause_veil()) draw_sprite(ren, assets, sprite("PAUSE_VEIL", sheet("IMAGE_PAUSEVEIL", 220, 545), 0, 0));

    if (g.popup_kind()) {
        const SpriteState& p = g.popup();
        draw_sprite_or_label(ren, assets, p, p.scale >= 0.9f ? popup_placeholder_text(*g.popup_kind()) : "",
                             "PLACEHOLDER_3", 20);
    }
    if (g.trophy_popup_id() >= 0) {
        const SpriteState& t = g.trophy_popup();
        if (t.visible) draw_sprite(ren, assets, t);
        const SpriteState& l = g.trophy_label();
        if (l.visible) draw_text_scaled(ren, assets, "FONT_TROPHYNAMES", l.name, l.x, l.y, l.scale, l.alpha);
    }
}

// SCENE_TROPHY (dialogs.xml): background, ribbons, the header, the
// description label, the sparkle under the trophies, then the button.
void draw_trophy_room(SDL_Renderer* ren, Assets& assets, const TrophyRoom& room) {
    draw_sprite(ren, assets, sprite("BACKGROUND", sheet(room.background_id(), 800, 600), 0, 0));
    for (const auto& r : room.ribbons()) draw_sprite(ren, assets, r);
    draw_text(ren, assets, "FONT_DIALOG_HEADERS", trophy_strings::HEADER, TrophyRoom::HEADER_X, TrophyRoom::HEADER_Y,
              TextAlign::Center, Anchor::Top, 4);
    float y = TrophyRoom::LABEL_Y;
    std::string cur;
    const std::string label = room.label() + "\n";
    for (char c : label) {
        if (c != '\n') { cur.push_back(c); continue; }
        draw_text(ren, assets, "FONT_SETTINGSTEXT", cur, TrophyRoom::LABEL_X, y, TextAlign::Center, Anchor::Top, -1);
        y += static_cast<float>(TrophyRoom::LABEL_LINE_H);
        cur.clear();
    }
    if (room.sparkle_sprite().visible) draw_sprite(ren, assets, room.sparkle_sprite());
    for (const auto& t : room.trophies()) if (t.visible) draw_sprite(ren, assets, t);
    for (const auto& b : room.buttons()) draw_sprite(ren, assets, b.sprite);
}

// --probe-media FILE: decode FILE with the game's own loaders (images through
// SDL_image, .ogg / .wav through SDL_mixer, without an audio device) and
// return false when that fails. CI runs it on ui/tests/media/ against each
// package: the first SDL3 Windows zip showed a black splash because its
// SDL_image loaded libjpeg at run time and the zip didn't carry it.
bool probe_media(const std::string& path) {
    const std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : std::string();
    if (ext == ".ogg" || ext == ".wav") {
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
        MIX_Mixer* mixer = MIX_Init() ? MIX_CreateMixer(&spec) : nullptr;
        MIX_Audio* clip = mixer ? MIX_LoadAudio(mixer, path.c_str(), true) : nullptr;
        const bool ok = clip != nullptr;
        if (!ok) std::fprintf(stderr, "probe: %s: %s\n", path.c_str(), SDL_GetError());
        MIX_DestroyAudio(clip);
        if (mixer) MIX_DestroyMixer(mixer);
        MIX_Quit();
        return ok;
    }
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) {
        std::fprintf(stderr, "probe: %s: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    std::printf("probe: %s: %dx%d\n", path.c_str(), s->w, s->h);
    SDL_DestroySurface(s);
    return true;
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

// One dialog slot on screen (dialog_fade.hpp). The open dialog is drawn
// into `live` every frame and shown at its fade alpha. When it closes, that
// last frame becomes `frozen` and fades out over the scene, so a closing
// dialog needs no model state of its own.
// The side bars beside the 4:3 game (deviation UX-6, side_fill.hpp): one
// texture per side at the bar's pixel size, rebuilt when the output size
// changes, drawn in output pixels with the logical presentation switched
// off for the two blits. --dump-frames crops to the 4:3 rectangle, so the
// bars never reach a dump.
struct SideFillTextures {
    SDL_Texture* left = nullptr;
    SDL_Texture* right = nullptr;
    SDL_FRect left_dst{}, right_dst{};
    int out_w = 0, out_h = 0;

    void release() {
        if (left) SDL_DestroyTexture(left);
        if (right) SDL_DestroyTexture(right);
        left = right = nullptr;
    }
    static SDL_Texture* make(SDL_Renderer* ren, int w, int h, Side side) {
        std::vector<std::uint8_t> px = side_fill_rgba(w, h, side);
        if (px.empty()) return nullptr;
        SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        SDL_Texture* t = upload(ren, s);
        if (s) SDL_DestroySurface(s);
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
        return t;
    }
    void rebuild(SDL_Renderer* ren, int w, int h) {
        release();
        out_w = w;
        out_h = h;
        const PillarBars bars = pillar_bars(w, h);
        if (bars.left.empty()) return;
        // A half-pixel edge (an odd leftover) rounds outwards; the game is
        // drawn after the bars and covers the overlap.
        const int lw = static_cast<int>(std::ceil(bars.left.w));
        const int rx = static_cast<int>(std::floor(bars.right.x));
        left = make(ren, lw, h, Side::Left);
        right = make(ren, w - rx, h, Side::Right);
        left_dst = {0, 0, static_cast<float>(lw), static_cast<float>(h)};
        right_dst = {static_cast<float>(rx), 0, static_cast<float>(w - rx), static_cast<float>(h)};
    }
    void draw(SDL_Renderer* ren) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(ren, &w, &h);
        if (w != out_w || h != out_h) rebuild(ren, w, h);
        if (!left || !right) return;
        int lw = 0, lh = 0;
        SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
        SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
        SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_RenderTexture(ren, left, nullptr, &left_dst);
        SDL_RenderTexture(ren, right, nullptr, &right_dst);
        SDL_SetRenderLogicalPresentation(ren, lw, lh, mode);
    }
};

struct DialogLayer {
    DialogFade fade;
    SDL_Texture* live = nullptr;
    SDL_Texture* frozen = nullptr;

    void release() {
        if (live) SDL_DestroyTexture(live);
        if (frozen) SDL_DestroyTexture(frozen);
        live = frozen = nullptr;
    }
};

// A layer's render target: 800x600, or the 4:3 game rectangle's pixel size
// with hi-res layers (deviation UX-8, render_options.hpp layer_size). It is
// composited 1:1, so nearest sampling keeps it exact.
SDL_Texture* layer_texture(SDL_Renderer* ren, LayerSize size) {
    SDL_Texture* t = nearest(
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, size.w, size.h));
    if (!t) return nullptr;
    // The layer holds premultiplied colour (drawn with BLEND onto a clear
    // target), so it is composited with ONE / ONE_MINUS_SRC_ALPHA. A
    // renderer that refuses it falls back to BLEND, which only darkens soft
    // edges a little while fading.
    if (!SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED)) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

void composite_layer(SDL_Renderer* ren, SDL_Texture* t, float alpha) {
    if (!t || alpha <= 0.0f) return;
    const Uint8 m = color_byte(alpha);
    SDL_SetTextureColorMod(t, m, m, m);   // premultiplied: colour fades with alpha
    SDL_SetTextureAlphaMod(t, m);
    render_copy(ren, t, nullptr, nullptr);
}

// A dialog pushed over another, or popped off it: the one moving between
// `under` and `top` keeps its alpha (dialog_fade.hpp hand_off).
void hand_off_layers(DialogLayer& under, int under_id, DialogLayer& top, int top_id) {
    const HandOff h = hand_off(under.fade, under_id, top.fade, top_id);
    if (h.under_closing) std::swap(under.live, under.frozen);
    if (h.top_closing) std::swap(top.live, top.frozen);
}

// `id` names the dialog open in this slot now (0 = none); `draw` paints it.
// Order on screen: the backdrop (dodarkenbackground, 0x40d190), the dialog
// that is closing, then the open one. A dialog popped off the open one
// (closing_above) goes last instead, over its own fading backdrop.
// `px` is the target size for this frame; a layer of another size (the
// output changed) is made again.
template <typename Draw>
void draw_dialog_layer(SDL_Renderer* ren, LayerSize px, DialogLayer& layer, int id, int dt_ms, Draw&& draw) {
    if (layer.fade.update(id, dt_ms)) std::swap(layer.live, layer.frozen);
    if (!layer.fade.visible()) return;
    const auto darken = [&](float fade) {
        const Rgba8 dark = darken_color(fade);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, dark.r, dark.g, dark.b, dark.a);
        SDL_Rect all{0, 0, SCREEN_W, SCREEN_H};
        render_fill(ren, &all);
    };
    const bool above = layer.fade.closing_above();
    const auto draw_closing = [&] {
        if (!layer.fade.closing()) return;
        if (above) darken(layer.fade.closing_alpha());
        composite_layer(ren, layer.frozen, layer.fade.closing_alpha());
    };
    darken(layer.fade.backdrop());
    if (!above) draw_closing();
    if (layer.fade.open_id() != 0) {
        float lw = 0, lh = 0;
        if (layer.live && SDL_GetTextureSize(layer.live, &lw, &lh) &&
            (static_cast<int>(lw) != px.w || static_cast<int>(lh) != px.h)) {
            SDL_DestroyTexture(layer.live);
            layer.live = nullptr;
        }
        if (!layer.live) layer.live = layer_texture(ren, px);
        if (!layer.live || !SDL_SetRenderTarget(ren, layer.live)) {
            draw();   // no render targets: draw straight to the screen, unfaded
        } else {
            // The dialog code draws in 800x600 canvas units; the target's own view
            // scales them to its pixels (1 at 800x600).
            SDL_SetRenderScale(ren, static_cast<float>(px.w) / SCREEN_W, static_cast<float>(px.h) / SCREEN_H);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
            SDL_RenderClear(ren);
            draw();
            SDL_SetRenderScale(ren, 1.0f, 1.0f);
            SDL_SetRenderTarget(ren, nullptr);
            composite_layer(ren, layer.live, layer.fade.open_alpha());
        }
    }
    if (above) draw_closing();
}

#ifdef __ANDROID__
// Android discards stdout and stderr, so the game's printf diagnostics
// (dictionary, renderer, import status) would vanish. Route both through a
// pipe into logcat, one line per entry: `adb logcat -s tt2`.
void redirect_stdio_to_logcat() {
    static int fds[2];
    if (pipe(fds) != 0) return;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    std::thread([] {
        std::string line;
        char buf[512];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                if (buf[i] != '\n') {
                    line += buf[i];
                    continue;
                }
                __android_log_write(ANDROID_LOG_INFO, "tt2", line.c_str());
                line.clear();
            }
        }
    }).detach();
}
#endif

int main(int argc, char** argv) {
#ifdef __ANDROID__
    redirect_stdio_to_logcat();
#endif
    std::string assets_flag;      // --assets DIR: read the art from DIR, never ask
    std::string assets_env;       // TT2_ASSETS, same as --assets
    if (const char* env = std::getenv("TT2_ASSETS")) assets_env = env;
    std::string asset_dir_flag;   // --asset-dir DIR: the per-user import folder (default SDL_GetPrefPath)
    std::string import_from;      // --import DIR: import the player's own install, then exit
    std::string manifest_path;    // --asset-manifest FILE: replaces data/original-assets.sha256
    std::string import_pick;      // --import-pick DIR: answers the in-game folder picker (smoke tests)
    std::string save_dir = tt2::default_save_dir().string();
    [[maybe_unused]] bool save_dir_given = false;   // read on Android only
    int speed = 1;
    long exit_after_ms = -1;
    std::string dump_dir;      // --dump-frames DIR: write BMP frames (for the README GIF)
    int dump_fps = 12;
    int fixed_dt = 0;          // --fixed-dt MS: every frame advances MS ms (repeatable frame dumps)
    std::string debug_player;
    std::string data_dir = TT2_DATA_DIR;
    bool data_dir_given = false;
    std::string auto_play;     // --play untimed|timed|lightning: start a game as soon as the menu is up
    bool hover_set = false;    // --hover X,Y: the cursor parked on one point (smoke tests)
    float hover_x = 0, hover_y = 0;
    std::string auto_type;     // --type "word|word": feed keystrokes into the game (smoke tests);
                               // '|' Return, ' ' twist, '~' Esc, '$' the pause menu's STOP, '>' NEXT ROUND,
                               // '?' HOW TO PLAY (pause menu, or the main menu without --play), '<' / '>' help pages,
                               // '#' SETTINGS, '@' its INFO (credits), '6'..'8' max length,
                               // '=' the main menu's IMPROVEMENTS (then '<' / '>' its display mode,
                               //     '(' / ')' its resolution, '[' / ']' its texture filter, '*' its upscaler,
                               //     '{' its renderer),
                               // '!' fullscreen switch, '^' Alt+Enter,
                               // '%' the shown PASS / GIVE UP button, '}' NEXT ROUND;
                               // the missing-assets prompt comes first: '|' YES / OK, '~' NO / OK;
                               // '&' the main menu SETTINGS' IMPORT (re-import, deviation OP-7),
                               // '"' the main menu's CHANGE NAME, then '-' its REMOVE,
                               // '+' the main menu's TROPHIES, then '~' its MENU,
                               // '$' the main menu's HIGH SCORES, then '1'..'5' its tabs, '~' OK
    long debug_seed = -1;      // --seed N: bag seed for --player (reproducible racks)
    std::vector<std::string> probe_files;   // --probe-media FILE (repeatable): decode, report, exit
    std::string renderer_flag;              // --renderer auto|vulkan|opengl|software: this run only
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) assets_flag = argv[++i];
        else if (!std::strcmp(argv[i], "--asset-dir") && i + 1 < argc) asset_dir_flag = argv[++i];
        else if (!std::strcmp(argv[i], "--import") && i + 1 < argc) import_from = argv[++i];
        else if (!std::strcmp(argv[i], "--asset-manifest") && i + 1 < argc) manifest_path = argv[++i];
        else if (!std::strcmp(argv[i], "--import-pick") && i + 1 < argc) import_pick = argv[++i];
        else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) { save_dir = argv[++i]; save_dir_given = true; }
        else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc) speed = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--exit-after") && i + 1 < argc) exit_after_ms = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-frames") && i + 1 < argc) dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) dump_fps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--fixed-dt") && i + 1 < argc) fixed_dt = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--player") && i + 1 < argc) debug_player = argv[++i];
        else if (!std::strcmp(argv[i], "--data-dir") && i + 1 < argc) { data_dir = argv[++i]; data_dir_given = true; }
        else if (!std::strcmp(argv[i], "--play") && i + 1 < argc) auto_play = argv[++i];
        else if (!std::strcmp(argv[i], "--type") && i + 1 < argc) auto_type = argv[++i];
        else if (!std::strcmp(argv[i], "--hover") && i + 1 < argc)
            hover_set = std::sscanf(argv[++i], "%f,%f", &hover_x, &hover_y) == 2;
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) debug_seed = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--probe-media") && i + 1 < argc) probe_files.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--renderer") && i + 1 < argc) renderer_flag = argv[++i];
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: tt2 [--assets DIR] [--data-dir DIR] [--save-dir DIR] [--player NAME]\n"
                        "           [--play untimed|timed|lightning] [--speed N] [--exit-after MS]\n"
                        "           [--import DIR] [--asset-dir DIR] [--asset-manifest FILE]\n"
                        "           [--renderer auto|vulkan|opengl|software]\n"
                        "\n"
                        "  --import DIR          copy the art, fonts and sounds from your own Text Twist 2\n"
                        "                        install at DIR into the asset folder, then exit\n"
                        "  --asset-dir DIR       the asset folder imports go to and load from\n"
                        "                        (default: the per-user folder, e.g.\n"
                        "                        ~/.local/share/re-twist-tt2/imported-assets)\n"
                        "  --assets DIR          load the art from DIR as is (no import, no checks)\n"
                        "  --asset-manifest FILE check imports against FILE instead of\n"
                        "                        data/original-assets.sha256\n"
                        "  --renderer NAME       try NAME first for this run (IMPROVEMENTS saves it for good)\n");
            return 0;
        }
    }
    if (!probe_files.empty()) {
        bool all_ok = true;
        for (const std::string& f : probe_files) all_ok = probe_media(f) && all_ok;
        std::printf("probe: %s\n", all_ok ? "ok" : "failed");
        return all_ok ? 0 : 1;
    }
    tt2::RendererPref renderer_override = tt2::RendererPref::Auto;
    if (!renderer_flag.empty() && !tt2::parse_renderer_pref(renderer_flag, renderer_override)) {
        std::fprintf(stderr, "unknown --renderer %s (auto, vulkan, opengl or software)\n", renderer_flag.c_str());
        return 2;
    }
    if (speed < 1) speed = 1;
    if (fixed_dt < 0) fixed_dt = 0;
    // The game time so far. The RNG seeds below read the clock, as the original
    // seeds from timeGetTime(); under --fixed-dt they read this simulated clock
    // instead, so a whole run (rack order, idle timers) repeats exactly.
    long simulated_ms = 0;
    auto seed_ticks = [&]() -> std::uint32_t {
        return fixed_dt > 0 ? static_cast<std::uint32_t>(1000 + simulated_ms) : static_cast<std::uint32_t>(SDL_GetTicks());
    };

    // The data folder (dictionaries, asset manifest). Packaged builds
    // (AppImage, Windows zip) carry it next to the executable; a source
    // build falls back to TT2_DATA_DIR.
#ifdef __ANDROID__
    // The APK's assets/data/ is invisible to std::filesystem, so it is
    // copied into the files folder first (bundled_data.hpp), on the first
    // launch and after each update. SDL_LoadFile reads APK assets.
    // Saves and the import live beside it (tt2ui::android_dirs).
    const char* android_files = SDL_GetAndroidInternalStoragePath();
    if (!android_files) {
        std::fprintf(stderr, "android: no internal storage path: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Text Twist 2", "The game could not start.", nullptr);
        return 1;
    }
    const tt2ui::AndroidDirs android = tt2ui::android_dirs(android_files);
    if (!save_dir_given) save_dir = android.saves;
    if (!data_dir_given) {
        const auto read_asset = [](const std::string& rel, std::string& bytes) {
            size_t size = 0;
            void* p = SDL_LoadFile(("data/" + rel).c_str(), &size);
            if (!p) return false;
            bytes.assign(static_cast<const char*>(p), size);
            SDL_free(p);
            return true;
        };
        std::string manifest;
        read_asset(tt2ui::BUNDLE_MANIFEST, manifest);
        data_dir = android.data;
        const auto r = tt2ui::ensure_bundled_data(manifest, tt2::utf8_path(data_dir), read_asset);
        if (!r.ok) {
            std::fprintf(stderr, "bundled data: %s\n", r.error.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Text Twist 2",
                                     "The game's files could not be set up. Reinstalling it may help.", nullptr);
            return 1;
        }
        std::printf("bundled data: %s (%s, %zu files copied)\n", data_dir.c_str(),
                    r.action == tt2ui::BundleAction::Keep ? "up to date" : "copied", r.files);
    }
#else
    if (!data_dir_given) {
        std::string exe_dir;
        if (const char* base = SDL_GetBasePath()) exe_dir = base;
        data_dir = tt2ui::pick_data_dir(tt2ui::data_dir_candidates(exe_dir, data_dir), [](const std::string& d) {
            std::error_code ec;
            return std::filesystem::exists(std::filesystem::path(d) / "open-common.tree", ec) ||
                   std::filesystem::exists(std::filesystem::path(d) / "common.tree", ec);
        });
    }
#endif

    // The original art: the player's own install, imported into a per-user
    // folder and checked against data/original-assets.sha256
    // (docs/design/import-assets.md).
    if (manifest_path.empty()) manifest_path = data_dir + "/original-assets.sha256";
    tt2::AssetManifest manifest;
    std::string manifest_text;
    const bool manifest_ok = read_text(manifest_path, manifest_text) && tt2::AssetManifest::parse(manifest_text, manifest);
    if (!manifest_ok) std::fprintf(stderr, "warning: no valid asset manifest at %s; importing is off\n", manifest_path.c_str());
    // SDL_GetPrefPath creates its folder, so it only runs when the per-user
    // folder is actually needed and --asset-dir didn't name one.
    const auto pref_path = [&] {
        std::string out;
#ifdef __ANDROID__
        // SDL_GetPrefPath would return the bare files folder (android_dirs).
        std::error_code ec;
        std::filesystem::create_directories(tt2::utf8_path(android.imported_assets), ec);
        if (!ec) out = android.imported_assets;
#else
        if (char* p = SDL_GetPrefPath(tt2ui::PREF_ORG, tt2ui::PREF_APP)) {
            out = p;
            SDL_free(p);
        }
#endif
        return out;
    };
    const bool needs_user_dir = !import_from.empty() || (assets_flag.empty() && assets_env.empty());
    const std::string user_dir = needs_user_dir ? tt2ui::resolve_user_asset_dir(asset_dir_flag, pref_path) : std::string();

    if (!import_from.empty()) {
        if (!manifest_ok) return 1;
        if (user_dir.empty()) {
            std::fprintf(stderr, "import: no per-user folder (HOME/APPDATA unset); pass --asset-dir DIR\n");
            return 1;
        }
        std::printf("import: %s -> %s\n", import_from.c_str(), user_dir.c_str());
        tt2ui::AssetImportJob job(tt2::utf8_path(import_from), tt2::utf8_path(user_dir), manifest);
        while (!job.finished()) job.step(4 << 20);
        if (job.stage() == tt2ui::AssetImportJob::Stage::ReadyToCommit) job.commit();   // nothing is loaded yet
        if (job.stage() != tt2ui::AssetImportJob::Stage::Done) {
            const tt2ui::ImportError& err = job.error();
            std::fprintf(stderr, "import failed: %s\n", tt2ui::to_string(err.kind));
            for (const std::string& path : err.paths) std::fprintf(stderr, "  %s\n", path.c_str());
            if (err.count > err.paths.size()) std::fprintf(stderr, "  ... and %zu more\n", err.count - err.paths.size());
            return 1;
        }
        std::printf("import: ok, %zu files verified\n", manifest.entries.size());
        return 0;
    }

    tt2ui::AssetDirInputs asset_inputs;
    asset_inputs.flag = assets_flag;
    asset_inputs.env = assets_env;
    asset_inputs.user_dir = user_dir;
    asset_inputs.user_dir_given = !asset_dir_flag.empty();
    asset_inputs.can_import = manifest_ok;
    if (!user_dir.empty() && manifest_ok)
        asset_inputs.user_status = tt2ui::import_status(tt2::utf8_path(user_dir), manifest);
    {
        std::error_code ec;
        if (std::filesystem::is_directory(tt2::utf8_path(TT2_IMPORTED_ASSETS_DIR), ec))
            asset_inputs.developer_dir = TT2_IMPORTED_ASSETS_DIR;
    }
    const tt2ui::AssetDirChoice asset_choice = tt2ui::choose_assets_dir(asset_inputs);

    // Profiles: load before the window so a corrupt file is reported early.
    tt2::PlayerManager players;
    tt2::ProfileStore store(save_dir);
    if (save_dir.empty()) std::fprintf(stderr, "warning: no save directory (HOME/APPDATA unset); profiles will not persist\n");
    else if (!store.load(players)) std::fprintf(stderr, "warning: %s is unreadable; starting with no players\n",
                                                store.profiles_path().string().c_str());
    if (!debug_player.empty()) {
        if (!players.select(debug_player)) {
            const auto r = players.add(debug_player, seed_ticks());
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
    tt2::Random highscore_rng(static_cast<std::int64_t>(seed_ticks()) + 17);
    tt2::HighScoreManager highscores(kHighScoreNames, highscore_rng);
    if (!save_dir.empty() && !store.load(highscores))
        std::fprintf(stderr, "warning: %s is unreadable; starting with default high scores\n",
                     store.highscores_path().string().c_str());

    // settings.txt: volumes and window mode (replaces the registry group).
    tt2::GameSettings settings;
    if (!save_dir.empty() && !store.load(settings))
        std::fprintf(stderr, "warning: %s is unreadable; using the default settings\n",
                     store.settings_path().string().c_str());
    // improvements.txt: the port's display options (deviation UX-4). A
    // build without one yet starts from settings.txt's fullscreen flag.
    tt2::Improvements improvements = tt2::Improvements::from_legacy(settings);
    if (!save_dir.empty() && store.has_improvements() && !store.load(improvements))
        std::fprintf(stderr, "warning: %s is unreadable; using the display options from settings.txt\n",
                     store.improvements_path().string().c_str());
    settings.fullscreen = improvements.legacy_fullscreen();
    // The renderer this run starts with: the file's, or --renderer for this run only.
    const tt2::RendererPref launch_renderer = renderer_flag.empty() ? improvements.renderer : renderer_override;
    std::printf("improvements: window %s%s\n", tt2::to_string(improvements.window),
                !save_dir.empty() && !store.has_improvements() ? " (from settings.txt)" : "");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    // Hidden until apply_display() has given it its mode and size (UX-5).
    // High pixel density: on a scaled display the canvas is drawn at the
    // real pixel size, not stretched from the point size.
    SDL_Window* win = SDL_CreateWindow("Text Twist 2", SCREEN_W, SCREEN_H, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    // Deviation (UX-1, display-and-timing.md §2): the original renders with
    // Direct3D 9 only; the port tries Vulkan, then OpenGL, then software
    // (render_backend.hpp).
    const RendererPick pick = create_renderer(win, launch_renderer);
    SDL_Renderer* ren = pick.renderer;
    if (!ren) {
        std::fprintf(stderr, "renderer: %s\n", pick.log.c_str());
        return 1;
    }
    const RenderCaps render_caps = pick.caps;
    // The original presents with vsync on; IMPROVEMENTS can turn it off
    // (deviation UX-7). A renderer that refuses keeps the row disabled.
    bool vsync_ok = true;
    auto apply_vsync = [&](bool on) {
        if (SDL_SetRenderVSync(ren, on ? 1 : 0) || !on) return;
        std::fprintf(stderr, "warning: no vsync: %s\n", SDL_GetError());
        vsync_ok = false;
    };
    apply_vsync(improvements.vsync);
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    std::printf("renderer: %s; enhancements %s, max texture %d\n", pick.log.c_str(),
                render_caps.enhancements ? "on" : "off", render_caps.max_texture_size);
    // Deviation UX-5 (display-and-timing.md §1, §3): any offered size, and
    // three modes; Native falls back to Borderless when the switch fails.
    bool native_failed = false;
    auto apply_window = [&](const tt2::Improvements& imp) {
        const DisplayApplied applied = apply_display(win, imp, query_display(win));
        native_failed = applied.native_failed;
        std::printf("display: %s\n", applied.log.c_str());
    };
    apply_window(improvements);
    SDL_ShowWindow(win);
    SDL_SyncWindow(win);
    std::printf("display: %s\n", check_presentation(ren).c_str());
    // One fading dialog slot per dialog source, drawn in this order.
    SideFillTextures side_fill;
    DialogLayer game_under_layer, game_layer, game_credits_layer, menu_layer, menu_help_layer, menu_settings_layer,
        menu_highscores_layer, menu_credits_layer, menu_improvements_layer, menu_notice_layer, profile_layer, import_layer;
    // The player dialogs under the top one (deepest: select, name input, message).
    std::array<DialogLayer, 2> profile_under_layers;

    // Hand pointer over clickable items, hourglass on the load
    // screen, arrow everywhere else. The original's cursor handling
    // is not decoded; this uses the platform cursors (no game assets
    // involved).
    SDL_Cursor* cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    SDL_Cursor* cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    SDL_Cursor* cursor_wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    bool cursor_hot = false;
    bool cursor_busy = false;

    Assets assets(ren, asset_choice.dir);
    // Deviation UX-8: the filter and hi-res dialog layers; the software
    // renderer keeps Sharp and 800x600 layers (render_options.hpp).
    EffectiveOptions render_opts = effective(improvements, render_caps);
    assets.set_hires_art(render_opts.hires_art);   // before any art loads
    auto apply_render_options = [&](const tt2::Improvements& imp) {
        render_opts = effective(imp, render_caps);
        render_opts.filter = assets.set_filter(render_opts.filter);
        assets.set_upscale(render_opts.upscaler, render_caps.max_texture_size);
        std::printf("render: filter %s%s, dialog layers %s, upscaler %s\n", tt2::to_string(render_opts.filter),
                    render_opts.filter != imp.filter ? " (forced)" : "", render_opts.hires_layers ? "hi-res" : "800x600",
                    render_opts.upscaler ? "super-xbr" : (imp.upscaler ? "off (forced)" : "off"));
    };
    apply_render_options(improvements);
    assets.set_placeholder_dir(data_dir + "/placeholder");
    std::printf("placeholders: %s/placeholder%s, %s\n", data_dir.c_str(),
                file_exists(data_dir + "/placeholder/fonts/font_settingstext.xml") ? "" : " (missing)",
                render_opts.hires_art ? "2x" : "1x");
    AudioOut audio_out;
    const bool audio = audio_out.open();
    assets.set_audio(audio ? &audio_out : nullptr);
    assets.set_sound_volume(settings.sound_volume);
    assets.set_music_volume(settings.music_volume);
    // DIALOG_SETTINGS, from the pause menu and the main menu.
    SettingsHost settings_host;
    settings_host.current = [&] { return settings; };
    settings_host.apply_sound_volume = [&](float v) { assets.set_sound_volume(v); };
    settings_host.apply_music_volume = [&](float v) { assets.set_music_volume(v); };
    settings_host.commit = [&](const tt2::GameSettings& g) {
        // UX-3: SETTINGS no longer changes the window; settings.txt only
        // mirrors IMPROVEMENTS' mode for older builds.
        settings = g;
        settings.fullscreen = improvements.legacy_fullscreen();
        std::printf("settings: sound %.2f, music %.2f, %s\n", settings.sound_volume, settings.music_volume,
                    settings.fullscreen ? "fullscreen" : "windowed");
        if (!save_dir.empty() && !store.save(settings))
            std::fprintf(stderr, "warning: could not write %s\n", store.settings_path().string().c_str());
    };
    std::printf("assets: %s (%s, %s%s)\n", asset_choice.dir.empty() ? "(none)" : asset_choice.dir.c_str(),
                tt2ui::to_string(asset_choice.source),
                asset_choice.source == tt2ui::AssetSource::User ? tt2ui::to_string(asset_inputs.user_status)
                : file_exists(asset_choice.dir + "/images/loadscene/logo_anim.png") ? "unverified"
                                                                                    : "placeholders",
                asset_choice.prompt ? ", import needed" : "");

    ProfileFlow::Services pservices;
    pservices.play_sound = [&](SoundId id) { assets.play_sound(id); };
    pservices.on_change = [&] {
        if (save_dir.empty()) return;
        if (!store.save(players))
            std::fprintf(stderr, "warning: could not write %s\n", store.profiles_path().string().c_str());
        if (!store.save(highscores))
            std::fprintf(stderr, "warning: could not write %s\n", store.highscores_path().string().c_str());
    };
    pservices.seed_millis = [&] { return seed_ticks(); };   // timeGetTime()
    pservices.forget_player = [&](const std::string& name) {
        if (!save_dir.empty() && !store.remove_saved_games(name))
            std::fprintf(stderr, "warning: could not remove the saved games of %s\n", name.c_str());
    };
    pservices.wrap = [&assets](const std::string& text, int max_width) {
        return wrap_text(assets.font("FONT_SETTINGSTEXT").font, text, max_width);
    };
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
    // (TRANSITION_DEFAULT: 500 ms to black, then 500 ms back; each half
    // uses FadeBlackTransition's full `time`, 0x46c362 / 0x46c78f).
    std::unique_ptr<GameScene> game;
    std::unique_ptr<BlitzScene> blitz;   // SCENE_BLITZ (Lightning) instead of `game`
    // The scene being played, Classic or Lightning: most of the front end
    // talks to both through the same member names.
    auto with_play = [&](auto&& fn) {
        if (game) fn(*game);
        else if (blitz) fn(*blitz);
    };
    // SCENE_TROPHY: opened from the menu (0x42e390) and left by its MENU
    // button (0x42fcb0), both through TRANSITION_DEFAULT like the game.
    std::unique_ptr<TrophyRoom> trophy_room;
    bool pending_trophies = false, pending_trophy_exit = false;
    constexpr int GAME_FADE_HALF_MS = 500;
    int game_fade_out = 0, game_fade_in = 0;
    bool pending_start = false, pending_exit = false;
    tt2::GameMode pending_mode = tt2::GameMode::Timed;
    // Saved game to continue in the next start_game (0x4242a0 / 0x42a460).
    std::unique_ptr<tt2::SavedGame> pending_resume;
    auto mode_name = [](tt2::GameMode m) { return tt2::save_mode_name(m); };
    // 0x5fdea8: the mode last chosen on the menu, Timed at launch; the high
    // scores open on its tab (0x4063f0).
    tt2::GameMode last_mode = tt2::GameMode::Timed;
    auto begin_game = [&](tt2::GameMode m) {
        last_mode = m;
        pending_start = true;
        pending_mode = m;
        game_fade_out = GAME_FADE_HALF_MS;
    };
    // Yes/no MESSAGE word wrap in the label's own font (game-scene.md §6).
    const LineBreaker wrap_message = [&assets](const std::string& text, int max_width) {
        return wrap_text(assets.font("FONT_SETTINGSTEXT").font, text, max_width);
    };
    // The main menu's HOW TO PLAY (0x42ee90 -> DIALOG_HELP).
    std::optional<HelpDialog> menu_help;
    // The main menu's HIGH SCORES (0x42f0d0 -> DIALOG_HIGHSCORES, OK only).
    std::optional<HighScoreDialog> menu_highscores;
    auto menu_highscores_result = [&](HighScoreDialog::Result r) {
        if (r != HighScoreDialog::Result::None) menu_highscores.reset();
    };
    auto menu_help_press = [&](const std::string& name) {
        if (!menu_help) return;
        const MenuButton* b = menu_help->dialog().button(name);
        if (!b || !b->sprite.visible) return;
        assets.play_sound(SoundId::Button);
        if (menu_help->press(name)) menu_help.reset();
    };
    // The main menu's SETTINGS (0x42ef5f -> DIALOG_SETTINGS). OK stores the
    // current player's max word length and saves the players (0x40c620).
    std::optional<SettingsDialog> menu_settings;
    // The missing-assets prompt (import_prompt.hpp), declared here because
    // SETTINGS can start it too (IMPORT, deviation OP-7).
    std::optional<tt2ui::ImportPromptFlow> import_prompt;
    bool import_from_settings = false;   // false: the boot prompt, which holds the welcome flow
    const std::string import_dir = asset_inputs.user_dir;
    auto open_menu_settings = [&] {
        // Deviation (OP-7): the main menu's SETTINGS gains the asset row.
        const SettingsDialog::AssetRow row{asset_inputs.user_status, !import_dir.empty() && manifest_ok};
        menu_settings.emplace(settings, players.current() ? players.current()->max_word_length : 6,
                              settings_host.fullscreen_supported,
                              SettingsDialog::Services{[&](SoundId id) { assets.play_sound(id); },
                                                       settings_host.apply_sound_volume,
                                                       settings_host.apply_music_volume},
                              row);
    };
    std::function<void()> start_import_from_settings;   // set once the import services exist
    auto menu_settings_result = [&](SettingsDialog::Result r) {
        if (!menu_settings) return;
        if (r == SettingsDialog::Result::Ok || r == SettingsDialog::Result::ImportAssets) {
            if (players.current()) {
                players.current()->max_word_length = menu_settings->max_word_length();
                pservices.on_change();
            }
            settings_host.commit(menu_settings->values());
        }
        if (r == SettingsDialog::Result::Ok || r == SettingsDialog::Result::Cancel) menu_settings.reset();
        if (r == SettingsDialog::Result::ImportAssets) {   // kept like OK, then the picker
            menu_settings.reset();
            if (start_import_from_settings) start_import_from_settings();
        }
    };
    // IMPROVEMENTS (deviation UX-2) from the main menu. OK applies the
    // window mode, saves improvements.txt and mirrors settings.txt.
    std::optional<ImprovementsDialog> menu_improvements;
    auto commit_improvements = [&](const tt2::Improvements& imp) {
        if (imp.window != improvements.window || imp.resolution != improvements.resolution) apply_window(imp);
        if (imp.vsync != improvements.vsync) apply_vsync(imp.vsync);
        if (imp.filter != improvements.filter || imp.upscaler != improvements.upscaler) apply_render_options(imp);
        improvements = imp;
        std::printf("improvements: window %s, resolution %s, vsync %s, frame limit %d\n",
                    tt2::to_string(improvements.window), tt2::to_string(improvements.resolution).c_str(),
                    improvements.vsync ? "on" : "off", improvements.frame_limit);
        if (!save_dir.empty() && !store.save(improvements))
            std::fprintf(stderr, "warning: could not write %s\n", store.improvements_path().string().c_str());
        settings_host.commit(settings);   // settings.txt's fullscreen follows (UX-4)
    };
    auto menu_improvements_result = [&](ImprovementsDialog::Result r) {
        if (!menu_improvements || r == ImprovementsDialog::Result::None) return;
        if (r == ImprovementsDialog::Result::Ok) commit_improvements(menu_improvements->values());
        menu_improvements.reset();
    };
    // Alt+Enter (deviation, see is_fullscreen_shortcut): flip between a
    // window and borderless, save both files, and keep open dialogs in step.
    auto toggle_fullscreen = [&] {
        tt2::Improvements imp = improvements;
        imp.set_window(imp.toggled_window());   // back to the last fullscreen mode picked (UX-5)
        commit_improvements(imp);
        const bool fullscreen = improvements.legacy_fullscreen();
        if (menu_settings) menu_settings->sync_fullscreen(fullscreen);
        if (menu_improvements) menu_improvements->sync_window(improvements.window);
        with_play([&](auto& g) { g.window_mode_changed(fullscreen); });
    };
    // "Continue the game?" (CTEXT_START_SAVEGAME, 0x42dc80) on the menu. Port
    // improvement: the original never shows it in play (a save just resumes,
    // and starting again means passing until GIVE UP); see game-scene.md 7.
    // IMPROVEMENTS -> ASK TO RESUME Off brings that back.
    Dialog menu_dialog;
    bool menu_dialog_open = false;
    tt2::GameMode menu_dialog_mode = tt2::GameMode::Timed;
    // Loads the current player's save for the next begin_game().
    auto load_resume = [&](tt2::GameMode m) {
        const std::string player = players.current() ? players.current()->name : std::string();
        auto g = std::make_unique<tt2::SavedGame>();
        if (store.load_saved_game(player, mode_name(m), *g)) pending_resume = std::move(g);
        else std::fprintf(stderr, "warning: saved game for %s is unreadable; starting again\n", player.c_str());
    };
    auto request_game = [&](tt2::GameMode m) {
        if (!players.current()) { profile.open_player_select(); return; }
        if (!save_dir.empty() && store.has_saved_game(players.current()->name, mode_name(m))) {
            if (!improvements.resume_prompt) {   // the original: resume without asking
                load_resume(m);
                begin_game(m);
                return;
            }
            menu_dialog =
                build_yes_no(game_strings::SAVEGAME_HEADER, game_strings::SAVEGAME_BODY, false, wrap_message);
            menu_dialog_open = true;
            menu_dialog_mode = m;
            return;
        }
        begin_game(m);
    };
    // Entering the menu: the profile check (0x42d9b0), then --play.
    auto menu_entered = [&] {
        profile.on_menu_entered();
        if (auto_play == "timed") request_game(tt2::GameMode::Timed);
        else if (auto_play == "lightning") request_game(tt2::GameMode::Blitz);
        else if (!auto_play.empty()) request_game(tt2::GameMode::Untimed);
    };

    // The missing-assets prompt (import_prompt.hpp): asked once per launch,
    // on boot, when no verified import exists (OP-9 revised). It is modal and
    // holds the welcome flow, so the splash plays with the imported art.
    std::optional<tt2ui::AssetImportJob> import_job;
    tt2ui::ImportPromptServices import_services;
    import_services.pick_folder = [&] {
        if (!import_pick.empty()) return tt2ui::PickResult{tt2ui::PickResult::Kind::Picked, import_pick};
        return tt2ui::pick_install_folder();
    };
    import_services.start_job = [&](const std::string& folder) {
        std::printf("assets: importing from %s\n", folder.c_str());
        import_job.emplace(tt2::utf8_path(folder), tt2::utf8_path(import_dir), manifest);
    };
    // One frame's share: 64 KiB pieces until the byte budget or ~12 ms is
    // spent. A local disk imports the whole install in ~130 ms; a slow mount
    // (WSL's /mnt/c) can still stall on a single file open.
    import_services.step_job = [&](std::size_t budget) {
        if (!import_job) return;
        const Uint64 start = SDL_GetTicks();
        for (std::size_t done = 0; !import_job->finished() && done < budget && SDL_GetTicks() - start < 12;
             done += 64 * 1024)
            import_job->step(64 * 1024);
    };
    import_services.commit_job = [&] {
        if (!import_job) return false;
        assets.release();   // Windows can't rename a folder with the music file open
        const bool ok = import_job->commit();
        if (!ok && import_from_settings) assets.play_stream(SoundId::MusicMenu);   // still the old folder
        return ok;
    };
    import_services.job_view = [&] {
        tt2ui::ImportJobView v;
        if (!import_job) return v;
        v.stage = import_job->stage();
        v.progress = import_job->progress();
        v.error = import_job->error();
        return v;
    };
    import_services.on_imported = [&] {
        asset_inputs.user_status = tt2ui::ImportStatus::Imported;   // SETTINGS shows it from now on
        assets.set_dir(import_dir);   // textures reload lazily on the live renderer
        // On boot the splash starts its own music once the prompt closes.
        if (import_from_settings) assets.play_stream(SoundId::MusicMenu);
        std::printf("assets: reloaded from %s\n", import_dir.c_str());
    };
    import_services.play_sound = [&](SoundId id) { assets.play_sound(id); };
    import_services.wrap = wrap_message;
    start_import_from_settings = [&] {
        import_from_settings = true;
        import_prompt.emplace(import_services, false);   // straight to the picker
        std::printf("assets: re-import from settings\n");
    };

    auto menu_dialog_press = [&](const std::string& name) {
        if (name.empty() || !menu_dialog_open) return;
        assets.play_sound(SoundId::Button);
        menu_dialog_open = false;
        const std::string player = players.current() ? players.current()->name : std::string();
        const char* mode = mode_name(menu_dialog_mode);
        if (name == "BUTTON_YES") {
            load_resume(menu_dialog_mode);
        } else if (name == "BUTTON_NO") {
            store.remove_saved_game(player, mode);   // "Select 'No' to start again."
        } else {
            return;
        }
        begin_game(menu_dialog_mode);
    };
    services.menu_action = [&](const std::string& name) {
        if (name == "BUTTON_QUIT") running = false;
        else if (name == "BUTTON_CHANGENAME") profile.open_player_select();
        else if (name == "BUTTON_PLAY_UNTIMED") request_game(tt2::GameMode::Untimed);
        else if (name == "BUTTON_PLAY") request_game(tt2::GameMode::Timed);
        else if (name == "BUTTON_BLITZ") request_game(tt2::GameMode::Blitz);
        else if (name == "BUTTON_HOWTOPLAY") menu_help.emplace();
        else if (name == "BUTTON_HIGHSCORES")
            menu_highscores.emplace(highscores, HighScoreDialog::Opener::Menu, last_mode,
                                    [&](SoundId id) { assets.play_sound(id); });
        else if (name == "BUTTON_TROPHY" && players.current()) {
            pending_trophies = true;
            game_fade_out = GAME_FADE_HALF_MS;
        }
        else if (name == "BUTTON_SETTINGS") open_menu_settings();
        else if (name == "BUTTON_IMPROVEMENTS") {
            menu_improvements.emplace(improvements,
                                      ImprovementsDialog::Services{[&](SoundId id) { assets.play_sound(id); }},
                                      ImprovementsDialog::Context{render_caps.backend, launch_renderer,
                                                                  query_display(win), native_failed});
            if (!vsync_ok) menu_improvements->set_row_enabled("VSYNC", false);
        }
        else std::printf("menu: %s (not implemented yet)\n", name.c_str());
    };
    WelcomeFlow flow(services, seed_ticks() + 1);
    if (asset_choice.prompt) {
        import_prompt.emplace(import_services);
        std::printf("t=0 ms: assets: prompt shown\n");
    }
    // The boot prompt holds the welcome flow: no splash timer behind it.
    auto boot_prompt_open = [&] { return import_prompt && !import_from_settings; };
    auto start_game = [&] {
        GameScene::Services gs;
        gs.play_sound = [&](SoundId id) { assets.play_sound(id); };
        gs.on_profile_change = pservices.on_change;
        gs.open_url = [](const std::string& url) {
            // Headless runs (SDL_VIDEODRIVER=dummy) only log: no browser
            // should pop up from a smoke test.
            const char* driver = SDL_GetCurrentVideoDriver();
            const bool headless = driver && !std::strcmp(driver, "dummy");
            if (!headless && SDL_OpenURL(url.c_str())) return;
            std::printf("game: look up %s\n", url.c_str());
        };
        gs.wrap_text = wrap_message;
        gs.settings = settings_host;
        gs.on_exit = [&] { pending_exit = true; game_fade_out = GAME_FADE_HALF_MS; };
        const std::string player = players.current()->name;
        const std::string mode = mode_name(pending_mode);
        gs.save_game = [&store, player, mode, &save_dir](const tt2::SavedGame& g) {
            if (save_dir.empty()) return;
            if (!store.save_saved_game(player, mode, g))
                std::fprintf(stderr, "warning: could not write %s\n", store.saved_game_path(player, mode).string().c_str());
            else std::printf("game: saved %s for %s\n", mode.c_str(), player.c_str());
        };
        gs.discard_save = [&store, player, mode, &save_dir] {
            if (!save_dir.empty()) store.remove_saved_game(player, mode);
        };
        if (pending_mode == tt2::GameMode::Blitz) {
            // 0x415a70: MUSIC_in-game_1 (at 0.7; the port has no per-stream gain).
            blitz = std::make_unique<BlitzScene>(common_tree, full_tree, *players.current(), &highscores, gs,
                                                 static_cast<std::int64_t>(seed_ticks()), pending_resume.get());
            std::string racks;
            for (const auto& w : blitz->words()) racks += (racks.empty() ? "" : " ") + w.answer;
            std::printf("game: lightning round %d, words %s\n", blitz->round_number(), racks.c_str());
        } else {
            const bool timed = pending_mode == tt2::GameMode::Timed;
            game = std::make_unique<GameScene>(common_tree, full_tree, *players.current(), &highscores, timed,
                                               gs, static_cast<std::int64_t>(seed_ticks()), pending_resume.get());
            std::printf("game: %s, rack %s, %zu slots\n", timed ? "timed" : "untimed", game->rack().c_str(),
                        game->slots().size());
        }
        if (pending_resume) std::printf("game: resumed round %d\n", pending_resume->round);
        pending_resume.reset();
        assets.play_stream(SoundId::MusicInGame1);
    };

    const SelectionScene::HitTest hit = [&](const MenuButton& b, float px, float py) {
        if (b.hit_threshold <= 0) return true;
        Image& img = assets.image(b.sprite.sheet);
        if (img.placeholder) return true;
        return pixel_alpha(img, b.sprite, px, py) > b.hit_threshold;
    };

    // Frame timing (frame_clock.hpp, deviation UX-7): whole-ms steps that add
    // up to the real time at any refresh rate, and the optional frame limit.
    FrameClock frame_clock;
    frame_clock.advance(SDL_GetTicksNS());
    FramePacer frame_pacer;
    int dump_accum_ms = 0;
    int dump_index = 0;
    bool text_input_on = false;
    int auto_type_ms = 0;
    SceneId last_scene = flow.current();
    // Tooltips: the cursor in canvas units, and the one tooltip on screen.
    float mouse_x = 0, mouse_y = 0;
    TooltipTracker tooltip;
    // 0x474ae0: the widget with a tooltip under the cursor, on the surface
    // that takes the input now. A dialog hides what is under it; the top
    // hovered button decides, with or without a tooltip.
    auto tooltip_target = [&](std::string& key, std::string& text) {
        key.clear();
        text.clear();
        auto pick = [&](const std::vector<MenuButton>& buttons, TooltipSurface surface, const char* tag) {
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                if (!it->hover || !it->sprite.visible) continue;
                text = tooltip_text(surface, it->sprite.name);
                if (!text.empty()) key = std::string(tag) + "/" + it->sprite.name;
                return;
            }
        };
        auto pick_settings = [&](const SettingsDialog& sd, const char* tag) {
            if (sd.credits()) return;
            for (const auto& sl : sd.sliders())
                if (sl.hover) {
                    text = tooltip_text(TooltipSurface::Settings, sl.name);
                    key = std::string(tag) + "/" + sl.name;
                    return;
                }
            pick(sd.dialog().buttons, TooltipSurface::Settings, tag);
        };
        if (import_prompt) return;
        if (trophy_room) {   // BUTTON_Menu: tooltip="CTOOLTIP_INGAME_MENU"
            pick(trophy_room->buttons(), TooltipSurface::Game, "trophy");
            return;
        }
        if (profile.open()) {
            if (profile.dialog().kind == DialogKind::PlayerSelect)
                pick(profile.dialog().buttons, TooltipSurface::PlayerSelect, "players");
            return;
        }
        if (game || blitz) {
            with_play([&](auto& g) {
                if (const SettingsDialog* sd = g.settings_dialog()) { pick_settings(*sd, "game-settings"); return; }
                switch (g.dialog_kind()) {
                    case GameDialog::None: pick(g.buttons(), TooltipSurface::Game, "game"); break;
                    case GameDialog::PauseMenu: pick(g.dialog().buttons, TooltipSurface::PauseMenu, "pause"); break;
                    case GameDialog::Tutorial:
                    case GameDialog::NextRound:
                    case GameDialog::PassRound:
                    case GameDialog::EndGame:
                    case GameDialog::Congrats: pick(g.dialog().buttons, TooltipSurface::RoundDialog, "round"); break;
                    default: break;
                }
            });
            return;
        }
        if (menu_dialog_open || menu_improvements || menu_help || menu_highscores) return;
        if (menu_settings) { pick_settings(*menu_settings, "settings"); return; }
        if (flow.current() == SceneId::Selection && flow.selection() && game_fade_out == 0)
            pick(flow.selection()->buttons(), TooltipSurface::Selection, "menu");
    };
    while (running) {
        // --hover: a motion event at that canvas point every frame, handed
        // straight to the loop below (the dummy driver drops pushed motion).
        bool hover_pending = hover_set;
        SDL_Event e;
        while (hover_pending || SDL_PollEvent(&e)) {
            if (hover_pending) {
                e = SDL_Event{};
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.windowID = SDL_GetWindowID(win);
                e.motion.x = hover_x;
                e.motion.y = hover_y;
                hover_pending = false;
            } else {
                // Mouse positions in the 800x600 canvas, whatever the window size
                // (SDL2's logical size did this on its own).
                SDL_ConvertEventToRenderCoordinates(ren, &e);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
            }
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                with_play([](auto& g) { g.leave(); });   // 0x427b20 / 0x4169d0: closing the window saves a running round
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) with_play([](auto& g) { g.focus_lost(); });
            if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                std::printf("display: %s\n", check_presentation(ren).c_str());
            // Before any routing, so Alt+Enter (and its auto-repeat) never
            // submits a word or presses OK.
            if (e.type == SDL_EVENT_KEY_DOWN && (e.key.mod & SDL_KMOD_ALT) &&
                (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                if (is_fullscreen_shortcut(true, true, e.key.repeat)) toggle_fullscreen();
                continue;
            }
            if (import_prompt) {
                // The missing-assets prompt is modal, above everything else.
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    const auto k = e.key.key;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) import_prompt->key_return();
                    else if (k == SDLK_ESCAPE) import_prompt->key_escape();
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION) dialog_mouse_move(import_prompt->dialog(), e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(import_prompt->dialog(), e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    import_prompt->press(dialog_mouse_up(import_prompt->dialog(), e.button.x, e.button.y));
                continue;
            }
            if (profile.open()) {
                // A dialog is modal: it eats the input.
                if (e.type == SDL_EVENT_TEXT_INPUT) profile.text_input(e.text.text);
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    switch (e.key.key) {
                        case SDLK_BACKSPACE: profile.backspace(); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: profile.key_return(); break;
                        case SDLK_ESCAPE: profile.key_escape(); break;
                        default: break;
                    }
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION) profile.mouse_move(e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    profile.mouse_down(e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    profile.mouse_up(e.button.x, e.button.y);
                continue;
            }
            if (game || blitz) {
                with_play([&](auto& g) {
                    if (e.type == SDL_EVENT_TEXT_INPUT) {
                        for (const char* c = e.text.text; *c; ++c) g.type_letter(*c);
                    }
                    if (e.type == SDL_EVENT_KEY_DOWN) {
                        switch (e.key.key) {
                            case SDLK_BACKSPACE: g.backspace(); break;
                            case SDLK_RETURN:
                            case SDLK_KP_ENTER: g.key_return(); break;
                            case SDLK_DELETE: g.key_delete(); break;
                            case SDLK_SPACE: g.key_space(); break;
                            case SDLK_ESCAPE: g.key_escape(); break;
                            default: break;
                        }
                    }
                    if (e.type == SDL_EVENT_MOUSE_MOTION) g.mouse_move(e.motion.x, e.motion.y);
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                        g.mouse_down(e.button.x, e.button.y);
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                        g.mouse_up(e.button.x, e.button.y);
                });
                // 0x415c50: Tab, Down and Right go to the next word, Up and
                // Left to the previous one (INFERRED key codes 0x14..0x17).
                if (blitz && e.type == SDL_EVENT_KEY_DOWN) {
                    switch (e.key.key) {
                        case SDLK_TAB:
                        case SDLK_DOWN:
                        case SDLK_RIGHT: blitz->key_word(true); break;
                        case SDLK_UP:
                        case SDLK_LEFT: blitz->key_word(false); break;
                        default: break;
                    }
                }
                continue;
            }
            if (trophy_room) {
                if (game_fade_out > 0) continue;
                if (e.type == SDL_EVENT_MOUSE_MOTION) trophy_room->mouse_move(e.motion.x, e.motion.y, hit);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    trophy_room->mouse_down(e.button.x, e.button.y, hit);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    trophy_room->mouse_up(e.button.x, e.button.y, hit);
                continue;
            }
            if (menu_dialog_open) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    const auto k = e.key.key;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) menu_dialog_press("BUTTON_YES");
                    else if (k == SDLK_ESCAPE) menu_dialog_open = false;   // INFERRED: cancel
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION) dialog_mouse_move(menu_dialog, e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(menu_dialog, e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu_dialog_press(dialog_mouse_up(menu_dialog, e.button.x, e.button.y));
                continue;
            }
            if (menu_settings) {
                // Deviation: Esc cancels, Return is OK (the original dialogs take no keys).
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    const auto k = e.key.key;
                    if (k == SDLK_ESCAPE) menu_settings_result(menu_settings->press("BUTTON_CANCEL"));
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                        menu_settings_result(menu_settings->press("BUTTON_OK"));
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION) menu_settings->mouse_move(e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    menu_settings->mouse_down(e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu_settings_result(menu_settings->mouse_up(e.button.x, e.button.y));
                continue;
            }
            if (menu_improvements) {
                // Esc cancels, Return is OK, as in the main menu's SETTINGS.
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    const auto k = e.key.key;
                    if (k == SDLK_ESCAPE) menu_improvements_result(menu_improvements->press("BUTTON_CANCEL"));
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                        menu_improvements_result(menu_improvements->press("BUTTON_OK"));
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION) menu_improvements->mouse_move(e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    menu_improvements->mouse_down(e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu_improvements_result(menu_improvements->mouse_up(e.button.x, e.button.y));
                continue;
            }
            if (menu_highscores) {
                // Deviation: Esc is OK, like the help (the original dialog takes no keys).
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)
                    menu_highscores_result(menu_highscores->press("BUTTON_OK"));
                if (e.type == SDL_EVENT_MOUSE_MOTION) dialog_mouse_move(menu_highscores->dialog(), e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(menu_highscores->dialog(), e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu_highscores_result(menu_highscores->mouse_up(e.button.x, e.button.y));
                continue;
            }
            if (menu_help) {
                // Deviation: Esc closes the help (the original dialogs take no keys).
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) menu_help_press("BUTTON_OK");
                if (e.type == SDL_EVENT_MOUSE_MOTION) dialog_mouse_move(menu_help->dialog(), e.motion.x, e.motion.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    dialog_mouse_down(menu_help->dialog(), e.button.x, e.button.y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu_help_press(dialog_mouse_up(menu_help->dialog(), e.button.x, e.button.y));
                continue;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
            if (flow.current() == SceneId::Selection && flow.selection() && game_fade_out == 0) {
                auto* menu = flow.selection();
                if (e.type == SDL_EVENT_MOUSE_MOTION) menu->mouse_move(e.motion.x, e.motion.y, hit);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_down(e.button.x, e.button.y, hit);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_up(e.button.x, e.button.y, hit);
            }
        }
        // Keep the pointer cursor in sync with the screen and what is
        // under it: hourglass while loading, hand over a clickable
        // item, arrow everywhere else.
        bool hot = false;
        if (import_prompt) hot = dialog_pointer_hot(import_prompt->dialog());
        else if (profile.open()) hot = profile.pointer_over_clickable();
        else if (menu_dialog_open) hot = dialog_pointer_hot(menu_dialog);
        else if (menu_help) hot = dialog_pointer_hot(menu_help->dialog());
        else if (menu_highscores) hot = dialog_pointer_hot(menu_highscores->dialog());
        else if (menu_settings) hot = menu_settings->pointer_hot();
        else if (menu_improvements) hot = menu_improvements->pointer_hot();
        else if (game || blitz) with_play([&](auto& g) { hot = g.pointer_over_clickable(); });
        else if (trophy_room) hot = trophy_room->pointer_over_clickable();
        else if (flow.current() == SceneId::Selection && flow.selection() && game_fade_out == 0)
            hot = flow.selection()->pointer_over_clickable();
        const bool busy = flow.pointer_busy();
        if ((busy != cursor_busy || hot != cursor_hot) &&
            cursor_arrow && cursor_hand && cursor_wait) {
            cursor_busy = busy;
            cursor_hot = hot;
            SDL_SetCursor(busy ? cursor_wait : (hot ? cursor_hand : cursor_arrow));
        }
        const int real_dt = frame_clock.advance(SDL_GetTicksNS(), speed);
        const int dt = fixed_dt > 0 ? std::min(fixed_dt * speed, FRAME_DT_MAX_MS) : real_dt;
        if (game_fade_out > 0) {
            game_fade_out -= dt;
            if (game_fade_out <= 0) {
                game_fade_out = 0;
                if (pending_start) { pending_start = false; start_game(); }
                else if (pending_trophies) {
                    pending_trophies = false;
                    TrophyRoom::Services ts;
                    ts.play_sound = [&](SoundId id) { assets.play_sound(id); };
                    ts.on_profile_change = pservices.on_change;
                    ts.on_exit = [&] { pending_trophy_exit = true; game_fade_out = GAME_FADE_HALF_MS; };
                    trophy_room = std::make_unique<TrophyRoom>(*players.current(), ts);
                    // DEVIATION: 0x42fb20 plays it at 0.7; the port has no per-stream gain.
                    assets.play_stream(SoundId::MusicTrophyRoom);
                    std::printf("t=%ld ms: trophy room\n", simulated_ms);
                }
                else if (pending_trophy_exit) {
                    pending_trophy_exit = false;
                    trophy_room.reset();
                    assets.play_stream(SoundId::MusicMenu);
                    std::printf("t=%ld ms: back to the menu\n", simulated_ms);
                }
                else if (pending_exit) {
                    pending_exit = false;
                    game.reset();
                    blitz.reset();
                    assets.play_stream(SoundId::MusicMenu);
                    std::printf("t=%ld ms: back to the menu\n", simulated_ms);
                }
                game_fade_in = GAME_FADE_HALF_MS;
            }
        } else if (game_fade_in > 0) {
            game_fade_in = std::max(0, game_fade_in - dt);
        }
        // --type answers the missing-assets prompt first: '|' YES / OK, '~' NO / OK.
        // Only while it waits for an answer: a key during the import would be lost.
        const bool prompt_waiting = import_prompt && (import_prompt->state() == tt2ui::ImportPromptFlow::State::Prompt ||
                                                      import_prompt->state() == tt2ui::ImportPromptFlow::State::Result);
        if (prompt_waiting && !auto_type.empty() && (auto_type.front() == '|' || auto_type.front() == '~')) {
            auto_type_ms += dt;
            if (auto_type_ms >= 300) {
                auto_type_ms = 0;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                if (c == '|') import_prompt->key_return();
                else import_prompt->key_escape();
            }
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
        // --type with a player dialog up: '-' REMOVE, '|' Return, '~' Esc.
        if (profile.open() && !auto_type.empty() && flow.current() == SceneId::Selection) {
            auto_type_ms += dt;
            if (auto_type_ms >= 300) {
                auto_type_ms = 0;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                if (c == '-') profile.press("BUTTON_REMOVE");
                else if (c == '|') profile.key_return();
                else if (c == '~') profile.key_escape();
            }
        }
        // --type on the main menu (no --play): '?' HOW TO PLAY, '<' / '>' turn, '~' OK / CANCEL;
        // '"' CHANGE NAME (player select);
        // '#' SETTINGS, '6'..'8' max length, '!' fullscreen, '@' credits, '|' OK, '^' Alt+Enter;
        // '=' IMPROVEMENTS, then '<' / '>' its display mode, '(' / ')' its resolution, '[' / ']' its filter,
        // '*' its upscaler, '{' its renderer, '|' OK (then OK on "Restart needed"), '~' CANCEL.
        if (!game && !blitz && auto_play.empty() && !import_prompt && !menu_dialog_open && !profile.open() && !auto_type.empty() &&
            flow.current() == SceneId::Selection && game_fade_out == 0) {
            auto_type_ms += dt;
            if (auto_type_ms >= 300) {
                auto_type_ms = 0;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                if (trophy_room) {
                    if (c == '~') trophy_room->press("BUTTON_Menu");
                }
                else if (c == '"' && !menu_improvements && !menu_settings && !menu_help) profile.open_player_select();
                else if (menu_improvements && (c == '<' || c == '>'))
                    menu_improvements->press(c == '<' ? "BUTTON_WINDOW_PREV" : "BUTTON_WINDOW_NEXT");
                else if (menu_improvements && (c == '(' || c == ')'))
                    menu_improvements->press(c == '(' ? "BUTTON_RESOLUTION_PREV" : "BUTTON_RESOLUTION_NEXT");
                else if (menu_improvements && (c == '[' || c == ']'))
                    menu_improvements->press(c == '[' ? "BUTTON_FILTER_PREV" : "BUTTON_FILTER_NEXT");
                else if (menu_improvements && c == '*') menu_improvements->press("BUTTON_UPSCALER_NEXT");
                else if (menu_improvements && c == '{') menu_improvements->press("BUTTON_RENDERER_NEXT");
                else if (menu_improvements && c == '|') menu_improvements_result(menu_improvements->press("BUTTON_OK"));
                else if (menu_improvements && c == '~')
                    menu_improvements_result(menu_improvements->press("BUTTON_CANCEL"));
                else if (c == '?') services.menu_action("BUTTON_HOWTOPLAY");
                else if (menu_highscores && c == '~') menu_highscores_result(menu_highscores->press("BUTTON_OK"));
                else if (menu_highscores && c >= '1' && c <= '5')
                    menu_highscores->press(HighScoreDialog::TABS[static_cast<std::size_t>(c - '1')].switch_name);
                else if (c == '+') services.menu_action("BUTTON_TROPHY");
                else if (c == '$') services.menu_action("BUTTON_HIGHSCORES");
                else if (c == '#') services.menu_action("BUTTON_SETTINGS");
                else if (c == '=') services.menu_action("BUTTON_IMPROVEMENTS");
                else if (c == '^') toggle_fullscreen();
                else if (menu_settings && c == '~') menu_settings_result(menu_settings->press("BUTTON_CANCEL"));
                else if (menu_settings && c == '|') menu_settings_result(menu_settings->press("BUTTON_OK"));
                else if (menu_settings && c == '!') menu_settings->press("SWITCH_FULLSCREEN");
                else if (menu_settings && c == '@') menu_settings->press("BUTTON_INFO");
                else if (menu_settings && c == '&') menu_settings_result(menu_settings->press("BUTTON_IMPORT"));
                else if (menu_settings && c >= '6' && c <= '8')
                    menu_settings->press(SettingsDialog::length_switch(c - '0'));
                else if (menu_settings && c == '<') menu_settings->press("BUTTON_PREV");   // credits pages
                else if (menu_settings && c == '>') menu_settings->press("BUTTON_NEXT");
                else if (c == '<') menu_help_press("BUTTON_PREV");
                else if (c == '>') menu_help_press("BUTTON_NEXT");
                else if (c == '~') menu_help_press("BUTTON_OK");
            }
        }
        if (trophy_room) trophy_room->update(dt);
        if (game || blitz) {
            with_play([&](auto& g) { g.update(dt); });
            // --type: one key every 300 ms while a round is being played.
            auto_type_ms += dt;
            while (!auto_type.empty() && auto_type_ms >= 300) {
                auto_type_ms -= 300;
                const char c = auto_type.front();
                auto_type.erase(0, 1);
                // Lightning: '[' / ']' the previous / next word (Up / Tab).
                if (blitz && (c == '[' || c == ']')) { blitz->key_word(c == ']'); continue; }
                with_play([&](auto& g) {
                    if (c == '\n' || c == '|') g.key_return();
                    else if (c == ' ') g.key_space();
                    else if (c == '_') g.key_delete();                 // Delete = CLEAR
                    else if (c == '~') g.key_escape();                  // pause menu / back
                    else if (c == '$') g.dialog_press("BUTTON_STOP");   // pause menu STOP
                    else if (c == '?') g.dialog_press("BUTTON_HOWTOPLAY");   // pause menu HOW TO PLAY
                    else if (c == '#') g.dialog_press("BUTTON_SETTINGS");    // pause menu SETTINGS
                    else if (c == '^') toggle_fullscreen();                   // Alt+Enter
                    else if (c == '%')                                         // the shown PASS / GIVE UP
                        for (const char* n : {"BUTTON_Pass_2", "BUTTON_Pass", "BUTTON_GiveUp"}) g.press(n);
                    else if (c == '}') g.press("BUTTON_NextRound");         // qualified: next round
                    else if (c == '!') g.dialog_press("SWITCH_FULLSCREEN");  // settings: fullscreen
                    else if (c == '@') g.dialog_press("BUTTON_INFO");        // settings: credits
                    else if (c >= '6' && c <= '8') g.dialog_press(SettingsDialog::length_switch(c - '0'));
                    else if (c == '<') g.dialog_press("BUTTON_PREV");       // help: previous page
                    else if (c == '>') {
                        if (g.dialog_kind() == GameDialog::HowToPlay || g.dialog_kind() == GameDialog::Settings)
                            g.dialog_press("BUTTON_NEXT");                  // help or credits: next page
                        else g.press("BUTTON_NextRound");                   // NEXT ROUND once qualified
                    }
                    else g.type_letter(c);
                });
            }
        } else if (!boot_prompt_open()) {
            flow.update(dt);
        }
        profile.update(dt);
        if (import_prompt) {
            import_prompt->tick(dt);   // runs the picker and the import job
            if (!import_prompt->open()) {
                if (import_job && import_job->stage() == tt2ui::AssetImportJob::Stage::Failed) {
                    // The dialog stays generic; the details, absolute paths included, go to the log.
                    const tt2ui::ImportError& err = import_job->error();
                    std::fprintf(stderr, "import failed: %s\n", tt2ui::to_string(err.kind));
                    for (const std::string& path : err.paths) std::fprintf(stderr, "  %s\n", path.c_str());
                }
                import_prompt.reset();
                import_job.reset();
                std::printf("t=%ld ms: assets: prompt closed\n", simulated_ms);
                import_from_settings = false;   // on boot, the flow now runs on to the menu
            }
        }
        if (menu_dialog_open) dialog_tick(menu_dialog, dt);
        if (menu_help) menu_help->tick(dt);
        if (menu_highscores) menu_highscores->tick(dt);
        if (menu_settings) menu_settings->tick(dt);
        if (menu_improvements) menu_improvements->tick(dt);
        simulated_ms += dt;
        if (flow.current() != last_scene) {
            last_scene = flow.current();
            std::printf("t=%ld ms: scene %d\n", simulated_ms, static_cast<int>(last_scene));
            if (last_scene == SceneId::Selection) menu_entered();
        }
        const bool want_text = profile.wants_text_input() || ((game || blitz) && !profile.open());
        if (want_text != text_input_on) {
            text_input_on = want_text;
            if (text_input_on) SDL_StartTextInput(win); else SDL_StopTextInput(win);
        }
        if (exit_after_ms >= 0 && simulated_ms >= exit_after_ms) running = false;

        assets.poll_upscales();   // UX-9: swap finished 2x textures in
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (improvements.widescreen_fill) side_fill.draw(ren);   // UX-6; top / bottom bars stay black
        int out_w = 0, out_h = 0;
        SDL_GetRenderOutputSize(ren, &out_w, &out_h);
        const LayerSize layer_px = layer_size(out_w, out_h, render_opts.hires_layers, render_caps.max_texture_size);
        if (game) {
            draw_game(ren, assets, *game);
        } else if (blitz) {
            draw_blitz(ren, assets, *blitz);
        } else if (trophy_room) {
            draw_trophy_room(ren, assets, *trophy_room);
        } else if (!boot_prompt_open()) {   // black behind the boot prompt
            for (const SpriteState* s : flow.draw_list()) draw_sprite(ren, assets, *s);
            if (flow.current() == SceneId::Selection) {
                // LABEL_PLAYERNAME: FONT_FONT_UIBAR, centred at (400, 28), charspacing 2.
                draw_text(ren, assets, "FONT_FONT_UIBAR", welcome_text(players), 400, 28, TextAlign::Center,
                          Anchor::Center, 2);
            }
        }
        // The dialog still open under the top one (the pause menu under
        // "Exit Game?", SETTINGS or HOW TO PLAY), with its own backdrop:
        // each GF2 dialog darkens what is behind it (dodarkenbackground).
        const Dialog* game_under = nullptr;
        const Dialog* game_credits = nullptr;   // DIALOG_CREDITS over the in-game settings
        int game_under_id = 0, game_top_id = 0;
        with_play([&](auto& g) {
            game_under = g.under_dialog();
            game_under_id = game_under ? static_cast<int>(g.under_kind()) : 0;
            game_top_id = static_cast<int>(g.dialog_kind());
            game_credits = g.settings_dialog() ? g.settings_dialog()->credits() : nullptr;
        });
        hand_off_layers(game_under_layer, game_under_id, game_layer, game_top_id);
        draw_dialog_layer(ren, layer_px, game_under_layer, game_under_id, dt,
                          [&] { draw_dialog(ren, assets, *game_under); });
        draw_dialog_layer(ren, layer_px, game_layer, game_top_id, dt, [&] {
            with_play([&](const auto& g) {
                if (g.settings_dialog()) {
                    draw_settings(ren, assets, *g.settings_dialog());
                    return;
                }
                if (g.dialog_kind() == GameDialog::HowToPlay) {
                    draw_help(ren, assets, g.dialog());
                    return;
                }
                draw_dialog(ren, assets, g.dialog());
                // 0x409100: the burst is linked in under its ball.
                const SheetSpec particle = sheet("IMAGE_PARTICLEBALL", 18, 18);
                for (const Particle& p : g.dialog_particles()) {
                    SpriteState sp = sprite("PARTICLE", particle, p.x() + g.dialog().left(), p.y() + g.dialog().top());
                    sp.anchor = Anchor::Center;
                    sp.alpha = p.alpha();
                    draw_sprite(ren, assets, sp);
                }
                for (const LetterBall& b : g.dialog_balls())
                    draw_letter_ball(ren, assets, b, g.dialog().left(), g.dialog().top());
            });
        });
        // DIALOG_CREDITS opens over the settings (0x40c760) with its own
        // backdrop and fade.
        draw_dialog_layer(ren, layer_px, game_credits_layer, game_credits ? 1 : 0, dt,
                          [&] { draw_dialog(ren, assets, *game_credits); });
        draw_dialog_layer(ren, layer_px, menu_layer, menu_dialog_open ? 1 + static_cast<int>(menu_dialog.kind) : 0, dt,
                          [&] { draw_dialog(ren, assets, menu_dialog); });
        draw_dialog_layer(ren, layer_px, menu_help_layer, menu_help ? 1 : 0, dt,
                          [&] { draw_help(ren, assets, menu_help->dialog()); });
        draw_dialog_layer(ren, layer_px, menu_highscores_layer, menu_highscores ? 1 : 0, dt,
                          [&] { draw_dialog(ren, assets, menu_highscores->dialog()); });
        draw_dialog_layer(ren, layer_px, menu_settings_layer, menu_settings ? 1 : 0, dt,
                          [&] { draw_settings(ren, assets, *menu_settings); });
        const Dialog* menu_credits = menu_settings ? menu_settings->credits() : nullptr;
        draw_dialog_layer(ren, layer_px, menu_credits_layer, menu_credits ? 1 : 0, dt,
                          [&] { draw_dialog(ren, assets, *menu_credits); });
        draw_dialog_layer(ren, layer_px, menu_improvements_layer, menu_improvements ? 1 : 0, dt,
                          [&] { draw_dialog(ren, assets, menu_improvements->dialog()); });
        // "Restart needed" opens over IMPROVEMENTS with its own backdrop and fade.
        const Dialog* menu_notice = menu_improvements ? menu_improvements->notice() : nullptr;
        draw_dialog_layer(ren, layer_px, menu_notice_layer, menu_notice ? 1 : 0, dt,
                          [&] { draw_dialog(ren, assets, *menu_notice); });
        // Each player dialog still open under the top one, deepest first,
        // with its own backdrop (dodarkenbackground).
        const std::vector<Dialog>& profile_under = profile.under();
        const int profile_top_id = static_cast<int>(profile.dialog().kind);
        for (std::size_t i = 0; i < profile_under_layers.size(); ++i)
            hand_off_layers(profile_under_layers[i],
                            i < profile_under.size() ? static_cast<int>(profile_under[i].kind) : 0, profile_layer,
                            profile_top_id);
        for (std::size_t i = 0; i < profile_under_layers.size(); ++i) {
            const Dialog* d = i < profile_under.size() ? &profile_under[i] : nullptr;
            draw_dialog_layer(ren, layer_px, profile_under_layers[i], d ? static_cast<int>(d->kind) : 0, dt,
                              [&] {
                                  // The name input keeps its typed text under a message (no cursor).
                                  draw_dialog(ren, assets, *d,
                                              d->kind == DialogKind::NewPlayer ? profile.input_text() : std::string());
                              });
        }
        draw_dialog_layer(ren, layer_px, profile_layer, profile_top_id, dt, [&] {
            draw_dialog(ren, assets, profile.dialog(), profile.input_text(), profile.cursor_visible());
        });
        const int import_id = import_prompt ? 1 + static_cast<int>(import_prompt->state()) : 0;
        draw_dialog_layer(ren, layer_px, import_layer, import_id, dt, [&] { draw_dialog(ren, assets, import_prompt->dialog()); });
        {
            std::string tip_key, tip_text;
            tooltip_target(tip_key, tip_text);
            tooltip.update(tip_key, tip_text, mouse_x, mouse_y, dt);
            draw_tooltip(ren, assets, tooltip);
        }
        float black = flow.black_alpha();
        if (game_fade_out > 0) black = std::max(black, 1.0f - static_cast<float>(game_fade_out) / GAME_FADE_HALF_MS);
        if (game_fade_in > 0) black = std::max(black, static_cast<float>(game_fade_in) / GAME_FADE_HALF_MS);
        if (black > 0.0f) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(black * 255));
            SDL_Rect all{0, 0, SCREEN_W, SCREEN_H};
            render_fill(ren, &all);
        }
        if (!dump_dir.empty()) {
            dump_accum_ms += dt;
            const int period = 1000 / (dump_fps > 0 ? dump_fps : 12);
            if (dump_accum_ms >= period) {
                dump_accum_ms -= period;
                // ReadPixels returns the screen in output pixels: 800x600 windowed,
                // larger once the window is fullscreen and scaled. Crop the
                // letterboxed game out of it and scale that back to 800x600.
                SDL_FRect game_rect{};
                SDL_GetRenderLogicalPresentationRect(ren, &game_rect);
                const SDL_Rect read{static_cast<int>(std::lround(game_rect.x)), static_cast<int>(std::lround(game_rect.y)),
                                    static_cast<int>(std::lround(game_rect.w)), static_cast<int>(std::lround(game_rect.h))};
                SDL_Surface* raw = SDL_RenderReadPixels(ren, nullptr);
                SDL_Surface* shot = SDL_CreateSurface(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_RGB24);
                if (raw && shot && SDL_BlitSurfaceScaled(raw, &read, shot, nullptr, SDL_SCALEMODE_NEAREST)) {
                    char name[64];
                    std::snprintf(name, sizeof name, "/frame_%04d.bmp", dump_index++);
                    SDL_SaveBMP(shot, (dump_dir + name).c_str());
                }
                if (raw) SDL_DestroySurface(raw);
                if (shot) SDL_DestroySurface(shot);
            }
        }
        SDL_RenderPresent(ren);
        if (const std::uint64_t wait = frame_pacer.wait_ns(SDL_GetTicksNS(), frame_budget_ns(improvements.frame_limit)))
            SDL_DelayPrecise(wait);
    }

    SDL_SetCursor(SDL_GetDefaultCursor());
    if (cursor_wait) SDL_DestroyCursor(cursor_wait);
    if (cursor_hand) SDL_DestroyCursor(cursor_hand);
    if (cursor_arrow) SDL_DestroyCursor(cursor_arrow);
    assets.release();   // before the mixer and the renderer it came from
    audio_out.close();
    for (DialogLayer* l : {&game_under_layer, &game_layer, &game_credits_layer, &menu_layer, &menu_help_layer, &menu_settings_layer,
                           &menu_highscores_layer, &menu_credits_layer, &menu_improvements_layer, &menu_notice_layer, &profile_layer,
                           &import_layer})
        l->release();
    for (DialogLayer& l : profile_under_layers) l.release();
    side_fill.release();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
