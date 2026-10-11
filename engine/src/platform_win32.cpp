// Windows implementation of `tt2::tt2_detail::millis_default`.
// The header `tt2/random.hpp` declares but does not define it, so every
// executable that uses the engine must compile one of these platform
// shims.

#include "tt2/random.hpp"

#if defined(_WIN32)
#  include <windows.h>

namespace tt2 {
namespace tt2_detail {
std::uint32_t millis_default() noexcept {
    return static_cast<std::uint32_t>(GetTickCount());
}
}  // namespace tt2_detail
}  // namespace tt2
#endif
