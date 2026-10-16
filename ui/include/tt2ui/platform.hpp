// Which platform a choice is made for. The headless model takes it as a
// value, so desktop tests can check the Android behaviour too; the host
// passes THIS_PLATFORM.
#pragma once

namespace tt2ui {

enum class Platform { Desktop, Android };

#ifdef __ANDROID__
inline constexpr Platform THIS_PLATFORM = Platform::Android;
#else
inline constexpr Platform THIS_PLATFORM = Platform::Desktop;
#endif

}  // namespace tt2ui
