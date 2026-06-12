#pragma once

/// @file types.hpp
/// @brief Core POD types shared across the editor surface.

#include <cstdint>

namespace threadmaxx::editor {

/// @brief Identifier for an EditorSession. Distinct from
/// threadmaxx::Engine identity; one engine may host more than one
/// session over its lifetime (e.g. a tear-down + re-attach).
struct SessionId {
    std::uint64_t value{0};

    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(SessionId a, SessionId b) noexcept {
        return a.value == b.value;
    }
};

/// @brief What the editor currently has selected. The five kinds map
/// 1:1 to the panels that consume them in the v1.0 surface.
enum class SelectionKind : std::uint8_t {
    None = 0,
    Entity,
    Resource,
    System,
    Event,
    TraceItem,
};

/// @brief Result of applying an `IEditCommand`.
enum class EditResult : std::uint8_t {
    Applied,
    Rejected,
    Deferred,
};

} // namespace threadmaxx::editor
