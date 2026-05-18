// threadmaxx_simd — layout traits.
//
// The `simd_batchable` concept gates which PODs the library will
// accept into its kernels. The intent is to be strict enough to
// catch unsafe types at compile time but loose enough that the
// engine's existing POD types pass without modification.
//
// A type is `simd_batchable` iff:
//
//   1. It's trivially copyable — kernels are free to memcpy / overwrite
//      blocks of elements without invoking user-defined ctors / dtors.
//   2. It has standard layout — the kernels must be able to access
//      member fields through pointer arithmetic that matches the C
//      ABI's natural offsets.
//   3. Its alignment is no stricter than `std::max_align_t` — kernels
//      do unaligned loads/stores (no `_mm256_load_ps`-style strict
//      alignment) so they don't fault, but over-aligned input types
//      would silently waste lanes and confuse stride math.

#pragma once

#include <cstddef>
#include <type_traits>

namespace threadmaxx::simd {

template <class T>
concept simd_batchable =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    (alignof(T) <= alignof(std::max_align_t));

} // namespace threadmaxx::simd
