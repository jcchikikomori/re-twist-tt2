// When the native folder dialog may open (folder_picker.hpp).
#include "test_harness.hpp"

#include <string>

#include "tt2ui/folder_picker.hpp"

using namespace tt2ui;

TEST(picker_available_on_a_desktop_with_a_graphic_backend) {
    ASSERT_TRUE(picker_available("windows", "windows_wchar"));
    ASSERT_TRUE(picker_available("x11", "zenity"));
    ASSERT_TRUE(picker_available("wayland", "kdialog"));
    ASSERT_TRUE(picker_available("cocoa", "applescript"));
}

TEST(picker_unavailable_headless) {
    for (const char* driver : {"dummy", "offscreen", "", "KMSDRM", "evdev"})
        ASSERT_TRUE(!picker_available(driver, "zenity"));   // CI smoke runs never block
}

TEST(picker_unavailable_with_only_a_console_backend) {
    for (const char* backend : {"dialog", "whiptail", "basicinput", "no_solution", ""})
        ASSERT_TRUE(!picker_available("x11", backend));   // it would read from the terminal
}

TEST(pick_result_defaults_to_unavailable) {
    const PickResult r;
    ASSERT_TRUE(r.kind == PickResult::Kind::Unavailable);
    ASSERT_TRUE(r.path.empty());
}
