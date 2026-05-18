// threadmaxx_simd — view adapter for contiguous arrays.
//
// `span_view<T>` is the thin wrapper kernels accept. It doesn't own
// memory — it just bundles a `std::span<T>` and exposes the
// minimum surface kernels need: `data()`, `size()`, `empty()`. The
// design notes (§4.3) call this out as "the key abstraction"; future
// batches may grow it (chunk-stride iterators, etc.) but the size /
// data / empty triple is the stable contract.

#pragma once

#include "traits.hpp"

#include <cstddef>
#include <span>
#include <type_traits>

namespace threadmaxx::simd {

template <class T>
struct span_view {
    static_assert(simd_batchable<std::remove_const_t<T>>,
        "span_view<T>: T (with const stripped) must satisfy `simd_batchable`.");

    std::span<T> values;

    constexpr T* data() const noexcept { return values.data(); }
    constexpr std::size_t size() const noexcept { return values.size(); }
    constexpr bool empty() const noexcept { return values.empty(); }
};

/// Factory for a writable view. The deduced `T` keeps `simd_batchable`
/// honest at instantiation — bad inputs surface as a `static_assert`
/// failure inside `span_view`, not as a deep template trace.
template <class T>
constexpr span_view<T> view(std::span<T> s) noexcept {
    return span_view<T>{s};
}

/// Factory for a read-only view.
template <class T>
constexpr span_view<const T> view(std::span<const T> s) noexcept {
    return span_view<const T>{s};
}

} // namespace threadmaxx::simd
