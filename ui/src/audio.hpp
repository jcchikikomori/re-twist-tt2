// The game's audio output on SDL3_mixer: one looping music track and
// SOUND_VOICES sound-effect tracks (tt2ui/audio_mix.hpp has the rules).
//
// The original plays its .ogg files through its own audio library; the
// port only needs "play this file now, at this volume", so the class
// takes file paths and hands back clips the caller keeps.
#pragma once

#include <string>
#include <vector>

struct MIX_Mixer;
struct MIX_Track;
struct MIX_Audio;

namespace tt2ui {

class AudioOut {
public:
    AudioOut() = default;
    ~AudioOut() { close(); }
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    // Opens the default playback device. False: no audio (the game runs
    // silent, as with SDL2 when Mix_OpenAudio failed).
    bool open();
    void close();
    bool ok() const { return mixer_ != nullptr; }

    // Music is streamed from disk; sounds are decoded once. Null when the
    // file is missing or unreadable. Free each clip with free_clip().
    MIX_Audio* load_music(const std::string& path);
    MIX_Audio* load_sound(const std::string& path);
    static void free_clip(MIX_Audio* clip);
    // Detaches every clip from its track, so the files can be closed (an
    // import renames the asset folder, which Windows refuses while a file
    // in it is open).
    void stop_all();

    void play_music(MIX_Audio* clip);   // loops for ever
    bool music_playing() const;
    // `own_volume`: the sound's resources.xml volume, 1 for most.
    void play_sound(MIX_Audio* clip, float own_volume);

    // GameSettings volumes, 0..1, applied to what is already playing too.
    void set_sound_volume(float v);
    void set_music_volume(float v);

private:
    MIX_Mixer* mixer_ = nullptr;
    MIX_Track* music_ = nullptr;
    std::vector<MIX_Track*> voices_;
    std::vector<float> voice_volume_;   // the own volume of each voice's sound
    float sound_volume_ = 0.5f;
    float music_volume_ = 0.5f;
};

}  // namespace tt2ui
