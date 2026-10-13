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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "tt2/profile.hpp"
#include "tt2/profile_store.hpp"
#include "tt2ui/bitmap_font.hpp"
#include "tt2ui/profile_dialogs.hpp"
#include "tt2ui/welcome_flow.hpp"

#ifndef TT2_IMPORTED_ASSETS_DIR
#define TT2_IMPORTED_ASSETS_DIR "imported-assets"
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
    {"BUTTON_OK", "images/gui/button_ok.png"},
    {"BUTTON_CANCEL", "images/gui/button_cancel.png"},
    {"BUTTON_ADD", "images/gui/button_add.png"},
    {"BUTTON_REMOVE", "images/gui/button_remove.png"},
    {"BUTTON_YES", "images/gui/button_yes.png"},
    {"BUTTON_NO", "images/gui/button_no.png"},
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
    {"FONT_DIALOG_HEADERS2", {"fonts/font_dialog_headers2.xml", 4}},
};

const std::map<SoundId, std::string> kSoundFiles = {
    {SoundId::MusicGameLoad, "music/music_game_load.ogg"},
    {SoundId::MusicMenu, "music/music_menu.ogg"},
    {SoundId::MusicLogo, "music/music_logo_animation.ogg"},
    {SoundId::LoadingSpring, "sound/loading_tee_sound.ogg"},
    {SoundId::LoadingBounce, "sound/loading_two_sound.ogg"},
    {SoundId::Button, "sound/button.ogg"},
    {SoundId::ButtonRollover, "sound/button_rollover.ogg"},
    {SoundId::ButtonWordOfTheDay, "sound/button_wordoftheday.ogg"},
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

    for (int f = 0; f < spec.frames; ++f) {
        SDL_Rect cell{spec.frame_x(f), spec.frame_y(f), spec.frame_w, spec.frame_h};
        const int shade = spec.frames > 1 ? (f * 40) / spec.frames : 0;
        Uint8 a = full_screen ? 255 : (spec.image_id == "ANI_CIRCLES" || spec.image_id == "IMAGE_SPRING" ? 60 : 255);
        if (spec.image_id == "PLAYER_BORDER") a = 0;   // only the border below
        auto clamp = [](int v) { return static_cast<Uint8>(v > 255 ? 255 : (v < 0 ? 0 : v)); };
        SDL_FillRect(s, &cell, SDL_MapRGBA(s->format, clamp(r + shade), clamp(g + shade), clamp(b + shade), a));
        if (!full_screen) {
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
        const int scale = spec != kFontFiles.end() ? spec->second.placeholder_scale : 3;
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
    SDL_RenderCopyEx(ren, img.texture, &src, &dst, 0.0, nullptr, static_cast<SDL_RendererFlip>(flip));
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

void draw_dialog(SDL_Renderer* ren, Assets& assets, const ProfileFlow& flow) {
    const Dialog& d = flow.dialog();
    // dodarkenbackground="true": DIALOG_BACKGROUND_ALPHA over the scene.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(DIALOG_BACKGROUND_ALPHA * 255));
    SDL_Rect all{0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(ren, &all);
    draw_dialog_frame(ren, assets, d);
    const float ox = d.left(), oy = d.top();
    for (const SpriteState& s : d.sprites) draw_sprite(ren, assets, s, ox, oy);
    for (const MenuButton& b : d.buttons) draw_sprite(ren, assets, b.sprite, ox, oy);
    for (const TextLabel& l : d.labels) {
        if (!l.visible) continue;
        std::string text = l.text;
        if (l.name == "INPUT_TEXT") text = flow.input_text();
        const SDL_Rect run = draw_text(ren, assets, l.font_id, text, l.x + ox, l.y + oy, l.align, l.anchor,
                                       l.char_spacing);
        if (l.name == "INPUT_TEXT" && flow.cursor_visible()) {
            // CURSOR 20x20 after the text, cursory="-8" (INFERRED placement).
            SpriteState cur = sprite("CURSOR", sheet("CURSOR", 20, 20), 0, 0);
            cur.x = static_cast<float>(run.x + run.w + 1);
            cur.y = l.y + oy - 8.0f - 2.0f;
            draw_sprite(ren, assets, cur);
        }
    }
}

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
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) assets_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) save_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc) speed = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--exit-after") && i + 1 < argc) exit_after_ms = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-frames") && i + 1 < argc) dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) dump_fps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--player") && i + 1 < argc) debug_player = argv[++i];
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: tt2 [--assets DIR] [--save-dir DIR] [--speed N] [--exit-after MS]\n");
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
        if (!save_dir.empty()) store.save(players);
    }
    std::printf("profiles: %s (%d player%s)\n", save_dir.empty() ? "(none)" : store.profiles_path().string().c_str(),
                players.count(), players.count() == 1 ? "" : "s");

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

    Assets assets(ren, assets_dir);
    const bool audio = Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0;
    if (audio) Mix_Init(MIX_INIT_OGG);
    assets.set_audio(audio);
    std::printf("assets: %s (%s)\n", assets_dir.c_str(),
                file_exists(assets_dir + "/images/loadscene/logo_anim.png") ? "imported" : "placeholders");

    ProfileFlow::Services pservices;
    pservices.play_sound = [&](SoundId id) { assets.play_sound(id); };
    pservices.on_change = [&] {
        if (!save_dir.empty() && !store.save(players))
            std::fprintf(stderr, "warning: could not write %s\n", store.profiles_path().string().c_str());
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
    services.menu_action = [&](const std::string& name) {
        if (name == "BUTTON_QUIT") running = false;
        else if (name == "BUTTON_CHANGENAME") profile.open_player_select();
        else std::printf("menu: %s (not implemented yet)\n", name.c_str());
    };
    WelcomeFlow flow(services, static_cast<std::uint32_t>(SDL_GetTicks() + 1));

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
    SceneId last_scene = flow.current();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
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
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (flow.current() == SceneId::Selection && flow.selection()) {
                auto* menu = flow.selection();
                if (e.type == SDL_MOUSEMOTION) menu->mouse_move(e.motion.x, e.motion.y, hit);
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_down(e.button.x, e.button.y, hit);
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                    menu->mouse_up(e.button.x, e.button.y, hit);
            }
        }
        const Uint32 now = SDL_GetTicks();
        int dt = static_cast<int>(now - last) * speed;
        last = now;
        if (dt > 100) dt = 100;
        flow.update(dt);
        profile.update(dt);
        simulated_ms += dt;
        if (flow.current() != last_scene) {
            last_scene = flow.current();
            std::printf("t=%ld ms: scene %d\n", simulated_ms, static_cast<int>(last_scene));
            if (last_scene == SceneId::Selection) profile.on_menu_entered();   // 0x42d9b0
        }
        if (profile.wants_text_input() != text_input_on) {
            text_input_on = profile.wants_text_input();
            if (text_input_on) SDL_StartTextInput(); else SDL_StopTextInput();
        }
        if (exit_after_ms >= 0 && simulated_ms >= exit_after_ms) running = false;

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        for (const SpriteState* s : flow.draw_list()) draw_sprite(ren, assets, *s);
        if (flow.current() == SceneId::Selection) {
            // LABEL_PLAYERNAME: FONT_FONT_UIBAR, centred at (400, 28), charspacing 2.
            draw_text(ren, assets, "FONT_FONT_UIBAR", welcome_text(players), 400, 28, TextAlign::Center,
                      Anchor::Center, 2);
        }
        if (profile.open()) draw_dialog(ren, assets, profile);
        const float black = flow.black_alpha();
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

    if (audio) Mix_CloseAudio();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
