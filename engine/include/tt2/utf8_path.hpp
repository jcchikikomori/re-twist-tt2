#pragma once

// UTF-8 text <-> std::filesystem::path, the same under C++17 and C++20.
// C++20 deprecates std::filesystem::u8path (GCC 16 compiles as gnu++20 by
// default, and -Werror makes that fatal) and makes path::u8string() return
// std::u8string. On Windows a plain path(std::string) would read the bytes
// in the ANSI code page, so non-ASCII folders need this conversion.

#include <filesystem>
#include <string>
#include <string_view>

namespace tt2 {

inline std::filesystem::path utf8_path(std::string_view s) {
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s.begin(), s.end());
#endif
}

// Forward slashes on every OS, as the manifest and the logs use.
inline std::string utf8_string(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace tt2
