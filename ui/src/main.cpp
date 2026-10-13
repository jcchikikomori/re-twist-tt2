// SDL2 front end for the welcome flow (splash -> loading -> main menu).
//
// Assets: the repository ships none of the original art. If the user
// has run `scripts/import-original.sh` the files are read from
// `imported-assets/` (or the directory given by `--assets DIR` /
// `TT2_ASSETS`). Anything missing is replaced by a procedurally drawn
// placeholder of the same size, so the flow always boots.
//
// Usage: tt2 [--assets DIR] [--speed N] [--exit-after MS] [--dump-frames DIR] [--fps N]
//   --speed N          run the clock N times faster (debugging)
//   --exit-after MS    quit after MS simulated milliseconds (smoke tests)
//   --dump-frames DIR  write frame_NNNN.bmp at N fps of simulated time
//                      (headless with SDL_VIDEODRIVER=dummy; feeds the README GIF)

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "tt2ui/welcome_flow.hpp"

#ifndef TT2_IMPORTED_ASSETS_DIR
#define TT2_IMPORTED_ASSETS_DIR "imported-assets"
#endif

namespace {

using namespace tt2ui;

// resources.xml: image id -> file, for the three welcome sections.
// BACKGROUND is section-scoped, so it is keyed "section/BACKGROUND".
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

bool file_exists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
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
    else if (spec.image_id.rfind("BUTTON", 0) == 0 || spec.image_id.rfind("IMAGE_BUTTON", 0) == 0) {
        r = 0x03; g = 0x30; b = 0xac;
    } else if (spec.image_id == "LOADING_BAR") { r = 0xf7; g = 0xde; b = 0x2b; }
    else if (spec.image_id == "IMAGE_LOADINGBAR_EMPTY") { r = 0x01; g = 0x1c; b = 0x6a; }
    else if (spec.image_id == "ANI_CIRCLES" || spec.image_id == "IMAGE_SPRING") { r = 0x1d; g = 0xda; b = 0xf1; }
    else if (spec.image_id == "CLICK_HERE" || spec.image_id == "IMAGE_GH_BRANDING") { r = g = b = 0xfe; }

    for (int f = 0; f < spec.frames; ++f) {
        SDL_Rect cell{spec.frame_x(f), spec.frame_y(f), spec.frame_w, spec.frame_h};
        const int shade = spec.frames > 1 ? (f * 40) / spec.frames : 0;
        Uint8 a = full_screen ? 255 : (spec.image_id == "ANI_CIRCLES" || spec.image_id == "IMAGE_SPRING" ? 60 : 255);
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

struct Image {
    SDL_Surface* surface = nullptr;   // kept for alpha hit tests
    SDL_Texture* texture = nullptr;
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
        if (img.surface) {
            SDL_Surface* conv = SDL_ConvertSurfaceFormat(img.surface, SDL_PIXELFORMAT_RGBA32, 0);
            if (conv) {
                SDL_FreeSurface(img.surface);
                img.surface = conv;
            }
            img.texture = SDL_CreateTextureFromSurface(renderer_, img.surface);
            if (img.texture) SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
        }
        return images_.emplace(spec.image_id, img).first->second;
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

void draw_sprite(SDL_Renderer* ren, Assets& assets, const SpriteState& s) {
    if (!s.visible || s.alpha <= 0.0f) return;
    if (s.sheet.image_id.empty()) {
        // Image-less sprite (the splash cover): a black quad.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, static_cast<Uint8>(s.alpha * 255));
        SDL_Rect dst{static_cast<int>(s.left()), static_cast<int>(s.top()), s.sheet.frame_w, s.sheet.frame_h};
        SDL_RenderFillRect(ren, &dst);
        return;
    }
    Image& img = assets.image(s.sheet);
    if (!img.texture) return;
    SDL_Rect src{s.sheet.frame_x(s.frame), s.sheet.frame_y(s.frame), s.sheet.frame_w, s.sheet.frame_h};
    SDL_Rect dst{static_cast<int>(s.left()), static_cast<int>(s.top()), s.sheet.frame_w, s.sheet.frame_h};
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

}  // namespace

int main(int argc, char** argv) {
    std::string assets_dir = TT2_IMPORTED_ASSETS_DIR;
    if (const char* env = std::getenv("TT2_ASSETS")) assets_dir = env;
    int speed = 1;
    long exit_after_ms = -1;
    std::string dump_dir;      // --dump-frames DIR: write BMP frames (for the README GIF)
    int dump_fps = 12;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) assets_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc) speed = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--exit-after") && i + 1 < argc) exit_after_ms = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-frames") && i + 1 < argc) dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) dump_fps = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: tt2 [--assets DIR] [--speed N] [--exit-after MS]\n");
            return 0;
        }
    }
    if (speed < 1) speed = 1;

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
    SceneId last_scene = flow.current();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
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
        simulated_ms += dt;
        if (flow.current() != last_scene) {
            last_scene = flow.current();
            std::printf("t=%ld ms: scene %d\n", simulated_ms, static_cast<int>(last_scene));
        }
        if (exit_after_ms >= 0 && simulated_ms >= exit_after_ms) running = false;

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        for (const SpriteState* s : flow.draw_list()) draw_sprite(ren, assets, *s);
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
