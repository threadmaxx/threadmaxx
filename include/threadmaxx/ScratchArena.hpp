#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

namespace threadmaxx {

/// Per-job bump-allocator for short-lived scratch memory.
///
/// Jobs receive a `ScratchArena&` via the three-arg `parallelFor` /
/// `single` overloads. Allocations are O(1) bump-pointer with no
/// per-element bookkeeping. There is no `free()`: the engine resets
/// the arena at the end of each wave, releasing all allocations made
/// during it. Across waves the underlying slabs are retained, so a
/// steady-state system pays one allocation amortized.
///
/// Allocated types must be trivially destructible — the arena does
/// not call destructors. Use it for POD scratch (neighbor lists,
/// sort buffers, prefix sums); use `std::vector` if you need
/// destructors or growth tracking.
///
/// @par Lifetime
///      Pointers returned by @ref allocate are valid until the next
///      `reset()` or the destruction of the arena, whichever comes
///      first. Across `reset()` they are dangling.
/// @par Thread-safety
///      Not thread-safe. Each job receives its own arena; do not share
///      across threads.
class ScratchArena {
public:
    ScratchArena() = default;

    /// Pre-allocate `initialBytes` of storage. Zero means "allocate on
    /// first use".
    explicit ScratchArena(std::size_t initialBytes);

    ScratchArena(const ScratchArena&) = delete;
    ScratchArena& operator=(const ScratchArena&) = delete;
    ScratchArena(ScratchArena&&) noexcept = default;
    ScratchArena& operator=(ScratchArena&&) noexcept = default;

    /// Allocate `count` instances of T, aligned to `alignof(T)`. The
    /// returned memory is uninitialized. Returns nullptr iff `count == 0`.
    /// @tparam T Must be trivially destructible (a `static_assert` enforces this).
    template <typename T>
    T* allocate(std::size_t count = 1) {
        static_assert(std::is_trivially_destructible_v<T>,
            "ScratchArena cannot run destructors; T must be trivially destructible");
        if (count == 0) return nullptr;
        void* p = allocateBytes(sizeof(T) * count, alignof(T));
        return static_cast<T*>(p);
    }

    /// Reset the bump pointer. All previously-issued pointers become
    /// invalid. Underlying slabs are retained for reuse.
    void reset() noexcept;

    /// Bytes used since the last reset (across all slabs touched).
    std::size_t bytesUsed() const noexcept;

    /// Total capacity across all slabs (a high-water mark proxy).
    std::size_t capacity() const noexcept;

private:
    void* allocateBytes(std::size_t size, std::size_t align);
    void  pushSlab(std::size_t minBytes);

    struct Slab {
        std::unique_ptr<std::byte[]> bytes;
        std::size_t capacity = 0;
    };
    std::vector<Slab> slabs_;
    std::size_t       slab_   = 0;  ///< index into `slabs_`
    std::size_t       offset_ = 0;  ///< bump position within slabs_[slab_]
};

} // namespace threadmaxx
