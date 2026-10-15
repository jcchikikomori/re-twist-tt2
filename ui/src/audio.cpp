#include "audio.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cstdio>

#include "tt2ui/audio_mix.hpp"

namespace tt2ui {

bool AudioOut::open() {
    if (mixer_) return true;
    // SDL2 opened the device with 1024-frame buffers (Mix_OpenAudio); keep
    // that latency so sounds stay in step with the animations they belong to.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
    if (!MIX_Init()) {
        std::fprintf(stderr, "MIX_Init: %s\n", SDL_GetError());
        return false;
    }
    mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!mixer_) {
        std::fprintf(stderr, "MIX_CreateMixerDevice: %s\n", SDL_GetError());
        MIX_Quit();
        return false;
    }
    music_ = MIX_CreateTrack(mixer_);
    for (int i = 0; i < SOUND_VOICES; ++i) {
        if (MIX_Track* t = MIX_CreateTrack(mixer_)) {
            voices_.push_back(t);
            voice_volume_.push_back(1.0f);
        }
    }
    set_music_volume(music_volume_);
    return true;
}

void AudioOut::close() {
    if (!mixer_) return;
    // Destroying the mixer also destroys its tracks.
    MIX_DestroyMixer(mixer_);
    mixer_ = nullptr;
    music_ = nullptr;
    voices_.clear();
    voice_volume_.clear();
    MIX_Quit();
}

namespace {

MIX_Audio* load(MIX_Mixer* mixer, const std::string& path, bool predecode) {
    if (!mixer || path.empty()) return nullptr;
    MIX_Audio* clip = MIX_LoadAudio(mixer, path.c_str(), predecode);
    if (!clip) std::fprintf(stderr, "warning: %s: %s\n", path.c_str(), SDL_GetError());
    return clip;
}

}  // namespace

MIX_Audio* AudioOut::load_music(const std::string& path) { return load(mixer_, path, false); }

MIX_Audio* AudioOut::load_sound(const std::string& path) { return load(mixer_, path, true); }

void AudioOut::free_clip(MIX_Audio* clip) { MIX_DestroyAudio(clip); }

void AudioOut::stop_all() {
    if (music_) MIX_SetTrackAudio(music_, nullptr);
    for (MIX_Track* t : voices_) MIX_SetTrackAudio(t, nullptr);
}

void AudioOut::play_music(MIX_Audio* clip) {
    if (!music_ || !clip) return;
    MIX_SetTrackAudio(music_, clip);
    const SDL_PropertiesID opts = SDL_CreateProperties();
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    MIX_PlayTrack(music_, opts);
    SDL_DestroyProperties(opts);
    MIX_SetTrackGain(music_, volume_gain(music_volume_));
}

bool AudioOut::music_playing() const { return music_ && MIX_TrackPlaying(music_); }

void AudioOut::play_sound(MIX_Audio* clip, float own_volume) {
    if (!clip) return;
    const int v = pick_voice(static_cast<int>(voices_.size()),
                             [&](int i) { return MIX_TrackPlaying(voices_[static_cast<std::size_t>(i)]); });
    if (v < 0) return;
    const auto i = static_cast<std::size_t>(v);
    voice_volume_[i] = own_volume;
    MIX_SetTrackAudio(voices_[i], clip);
    MIX_SetTrackGain(voices_[i], sound_gain(sound_volume_, own_volume));
    MIX_PlayTrack(voices_[i], 0);
}

void AudioOut::set_sound_volume(float v) {
    sound_volume_ = v;
    for (std::size_t i = 0; i < voices_.size(); ++i)
        MIX_SetTrackGain(voices_[i], sound_gain(sound_volume_, voice_volume_[i]));
}

void AudioOut::set_music_volume(float v) {
    music_volume_ = v;
    if (music_) MIX_SetTrackGain(music_, volume_gain(music_volume_));
}

}  // namespace tt2ui
