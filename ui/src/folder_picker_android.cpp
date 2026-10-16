// Android has no folder dialog, and tinyfiledialogs (zenity, kdialog,
// osascript, Win32) has nothing to run there. Reporting Unavailable is what
// a desktop without a dialog program does too, so the import prompt shows
// the same "no dialog" path. Phase 3 of docs/plans/android-support.md
// replaces this with a ZIP picker written in Kotlin.
#include "folder_picker.hpp"

namespace tt2ui {

PickResult pick_install_folder() {
    return PickResult{};   // Kind::Unavailable
}

}  // namespace tt2ui
