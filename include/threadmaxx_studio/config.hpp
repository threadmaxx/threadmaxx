#pragma once

/// @file config.hpp
/// @brief Capacity caps and build-time switches for threadmaxx_studio.
///
/// All caps are conservative defaults; tune via the host's CMake
/// before pulling the library in.

#include <cstddef>

namespace threadmaxx::studio {

/// @brief Hard cap on registered panels in a single `PanelHost`. v1.0
/// targets 50; the cap is a safety net, not a budget.
constexpr std::size_t kMaxPanels = 256;

/// @brief Cap on panel id / title length in bytes. Panels longer than
/// this are truncated by the framework's debug output paths.
constexpr std::size_t kMaxPanelLabelBytes = 128;

} // namespace threadmaxx::studio
