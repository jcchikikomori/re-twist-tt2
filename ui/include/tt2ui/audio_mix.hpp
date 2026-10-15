// Volume and voice rules for the audio front end (ui/src/audio.cpp), kept
// free of SDL so they can be unit-tested.
//
// CONFIRMED (help-and-settings.md §3.3): GameSettings stores the sound and
// music volumes as 0..1 and applies them at once (0x463db0 / 0x463e30); a
// few resources.xml sounds carry their own `volume` (0.5 for the checkbox
// and slider sounds). INFERRED: the volume maps linearly onto the mixer's
// gain, and a sound's own volume multiplies the sound volume, as SDL2_mixer
// did with Mix_VolumeChunk under Mix_Volume.
//
// Voices: the port mixes sound effects on SOUND_VOICES tracks. A new sound
// takes the first idle one; when all are busy it is dropped, which is what
// SDL2_mixer's Mix_PlayChannel(-1, ...) did with its 8 default channels.
#pragma once

#include <cmath>

namespace tt2ui {

inline constexpr int SOUND_VOICES = 8;

// A 0..1 setting as a mixer gain. Out-of-range values clamp; NaN is silence.
inline float volume_gain(float v) {
    if (!(v > 0.0f)) return 0.0f;   // also catches NaN
    return v < 1.0f ? v : 1.0f;
}

// One sound effect's gain: the sound volume times the sound's own volume.
inline float sound_gain(float sound_volume, float own_volume) {
    return volume_gain(sound_volume) * volume_gain(own_volume);
}

// The voice a new sound plays on: the first of `count` for which
// `busy(i)` is false, or -1 when every voice is busy (the sound is dropped).
template <typename Busy>
int pick_voice(int count, Busy&& busy) {
    for (int i = 0; i < count; ++i)
        if (!busy(i)) return i;
    return -1;
}

}  // namespace tt2ui
