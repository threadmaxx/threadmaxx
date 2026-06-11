#pragma once

#include <string_view>

#define THREADMAXX_INPUT_VERSION_MAJOR 0
#define THREADMAXX_INPUT_VERSION_MINOR 1
#define THREADMAXX_INPUT_VERSION_PATCH 0

#define THREADMAXX_INPUT_VERSION                                              \
    (THREADMAXX_INPUT_VERSION_MAJOR * 10000 +                                 \
     THREADMAXX_INPUT_VERSION_MINOR * 100 +                                   \
     THREADMAXX_INPUT_VERSION_PATCH)

namespace threadmaxx::input {

constexpr std::string_view version_string() noexcept { return "0.1.0"; }

}  // namespace threadmaxx::input
