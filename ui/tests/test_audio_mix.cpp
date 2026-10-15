// Volume and voice rules of the audio front end (audio_mix.hpp).
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include "tt2ui/audio_mix.hpp"

using namespace tt2ui;

TEST(volume_gain_is_linear_inside_zero_to_one) {
    ASSERT_TRUE(volume_gain(0.0f) == 0.0f);
    ASSERT_TRUE(std::fabs(volume_gain(0.5f) - 0.5f) < 1e-6f);
    ASSERT_TRUE(volume_gain(1.0f) == 1.0f);
}

TEST(volume_gain_clamps_bad_settings) {
    ASSERT_TRUE(volume_gain(-0.25f) == 0.0f);
    ASSERT_TRUE(volume_gain(3.0f) == 1.0f);
    ASSERT_TRUE(volume_gain(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST(sound_gain_multiplies_the_sounds_own_volume) {
    // The checkbox sound (volume 0.5) at sound volume 0.8: 0.8 * 0.5 = 0.4.
    ASSERT_TRUE(std::fabs(sound_gain(0.8f, 0.5f) - 0.4f) < 1e-6f);
    ASSERT_TRUE(std::fabs(sound_gain(0.8f, 1.0f) - 0.8f) < 1e-6f);   // most sounds have none
}

TEST(sound_gain_is_silent_when_either_volume_is_off) {
    ASSERT_TRUE(sound_gain(0.0f, 1.0f) == 0.0f);
    ASSERT_TRUE(sound_gain(1.0f, 0.0f) == 0.0f);
    ASSERT_TRUE(sound_gain(1.5f, 2.0f) == 1.0f);   // both clamp first
}

TEST(pick_voice_takes_the_first_idle_voice) {
    std::vector<bool> busy = {true, true, false, false};
    ASSERT_EQ(pick_voice(4, [&](int i) { return busy[static_cast<std::size_t>(i)]; }), 2);
    busy[0] = false;
    ASSERT_EQ(pick_voice(4, [&](int i) { return busy[static_cast<std::size_t>(i)]; }), 0);
}

TEST(pick_voice_drops_the_sound_when_every_voice_is_busy) {
    ASSERT_EQ(pick_voice(SOUND_VOICES, [](int) { return true; }), -1);
    ASSERT_EQ(pick_voice(0, [](int) { return false; }), -1);   // no voices at all
}
