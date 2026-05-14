#pragma once

#include <cstdint>
#include <string_view>

namespace threadmaxx {

namespace detail {

/// FNV-1a 64-bit hash, constexpr-friendly. Used by @ref TaskTag to
/// derive an `unordered_map` bucket key from a name string at
/// construction time. The bucket key is an accelerator, NOT an
/// equality key — @ref TaskTag equality compares the string itself,
/// so hash collisions are a performance issue (extra strcmp in the
/// bucket's collision chain), never a correctness one.
constexpr std::uint64_t fnv1aTaskTag(std::string_view s) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (char c : s) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

} // namespace detail

/// §3.4 batch 11 — labelled producer/consumer edge used by the DAG
/// scheduler.
///
/// A `TaskTag` carries both the human-readable name (a non-owning
/// `std::string_view`, typically aimed at a string literal) and a
/// pre-computed FNV-1a hash of that name. The hash is the
/// `unordered_map` bucket selector; `operator==` compares the names
/// directly so hash collisions can never silently re-order systems.
///
/// Construction is `constexpr`, so `TaskTag{"physics-results"}` in a
/// system's `dependencies()` body computes the hash at compile time
/// and emits zero runtime work.
///
/// @par Usage
/// @code
/// class PhysicsSystem : public ISystem {
///     static constexpr TaskTag kResult{"physics-results"};
///     std::span<const TaskTag> provides() const noexcept override {
///         return {&kResult, 1};
///     }
///     // ...
/// };
///
/// class AISystem : public ISystem {
///     static constexpr TaskTag kPhysics{"physics-results"};
///     std::span<const TaskTag> dependencies() const noexcept override {
///         return {&kPhysics, 1};
///     }
///     // ...
/// };
/// @endcode
///
/// The lifetime constraint on `name` is the same as for `ISystem::name()`
/// — the string must outlive the engine. String literals trivially
/// satisfy this.
struct TaskTag {
    std::string_view name;
    std::uint64_t    hash = 0;

    constexpr TaskTag() noexcept = default;

    constexpr explicit TaskTag(std::string_view n) noexcept
        : name(n), hash(detail::fnv1aTaskTag(n)) {}

    constexpr bool operator==(const TaskTag& o) const noexcept {
        return name == o.name;
    }
    constexpr bool operator!=(const TaskTag& o) const noexcept {
        return !(*this == o);
    }

    /// True iff this tag carries a non-empty name.
    constexpr bool valid() const noexcept { return !name.empty(); }
};

} // namespace threadmaxx
