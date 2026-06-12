#pragma once

#include <string_view>

#define THREADMAXX_ASSETS_VERSION_MAJOR 1
#define THREADMAXX_ASSETS_VERSION_MINOR 0
#define THREADMAXX_ASSETS_VERSION_PATCH 0

#define THREADMAXX_ASSETS_VERSION                                              \
    (THREADMAXX_ASSETS_VERSION_MAJOR * 10000 +                                 \
     THREADMAXX_ASSETS_VERSION_MINOR * 100 +                                   \
     THREADMAXX_ASSETS_VERSION_PATCH)

namespace threadmaxx::assets {

[[nodiscard]] constexpr std::string_view version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::assets
