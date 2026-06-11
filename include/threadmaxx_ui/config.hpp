#pragma once

/// @file config.hpp
/// @brief Library-wide compile-time defaults — depth caps on the per-frame
/// stacks (ID, layout, clip) and the initial reservation hints. UI1 only
/// uses `kIdStackDepth` and the draw-list initial capacities; later batches
/// consume the layout / clip caps.

#include <cstddef>
#include <cstdint>

namespace threadmaxx::ui {

/// Maximum depth of the widget ID stack. Past this depth, `pushId()` asserts
/// in debug and silently truncates in release.
inline constexpr std::size_t kIdStackDepth = 32;

/// Maximum depth of the layout stack (rows / columns / child regions).
inline constexpr std::size_t kLayoutStackDepth = 16;

/// Maximum depth of the clip-rect stack.
inline constexpr std::size_t kClipStackDepth = 16;

/// Initial reservation for the draw-list command buffer. Sized so that an
/// average editor frame doesn't grow it past warmup.
inline constexpr std::size_t kInitialDrawListCommands = 1024;

/// Initial reservation for the draw-list text-byte arena (label strings,
/// inspected scalars, debug-HUD rows).
inline constexpr std::size_t kInitialDrawListTextBytes = 4096;

} // namespace threadmaxx::ui
