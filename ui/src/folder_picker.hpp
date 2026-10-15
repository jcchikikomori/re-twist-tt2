// The native folder dialog (tinyfiledialogs). The only code that includes
// tinyfiledialogs.h is folder_picker.cpp.
#pragma once

#include "tt2ui/folder_picker.hpp"

namespace tt2ui {

// Blocks until the player picks a folder or cancels. Unavailable without a
// graphic dialog (see picker_available); never falls back to the console.
PickResult pick_install_folder();

}  // namespace tt2ui
