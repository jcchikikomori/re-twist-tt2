// The bridge to the Kotlin import dialog (Android only, Phase 3). The
// whole ZIP import runs in Kotlin (importer/ZipImporter.kt); C++ hands it
// the manifest and the stamp text, and hears back through an SDL event.
// Its import text comes from import_prompt.hpp through the JNI functions
// in android_import.cpp.
#pragma once

#include <string>

#include <SDL3/SDL.h>

namespace tt2android {

// The SDL event the dialog sends when it closes; event.user.code is 1 when
// it imported. Call once after SDL_Init; 0 when SDL has no events left.
Uint32 register_import_event();

// Opens the dialog (MainActivity.openImport); `ask_first` shows the launch
// warning, false comes from SETTINGS. False when the JNI call failed.
bool open_import(const std::string& manifest_text, const std::string& stamp_text, const std::string& assets_dir,
                 bool ask_first);

}  // namespace tt2android
