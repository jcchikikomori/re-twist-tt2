// The system splash (Android 12+, T4.2) stays up until the game has drawn
// its first frame: MainActivity holds its own first draw until told.
#pragma once

namespace tt2android {

// Call once, right after the first SDL_RenderPresent. Calls
// MainActivity.onFirstFrame() through JNI; failures are only logged
// (MainActivity lets the splash go after a timeout anyway).
void first_frame_shown();

}  // namespace tt2android
