#pragma once

/// @file aggregate.hpp
/// @brief Compile-time aggregate reflection — no macros, no registration.
///
/// `field_count<T>()` returns the field count of an aggregate (up to
/// `kMaxAggregateFields = 32`). `for_each_field(obj, fn)` invokes `fn`
/// for every field. `get<I>(obj)` returns the I-th field by reference.
///
/// All three are pure compile-time machinery — they cost nothing at
/// runtime, just like `std::get` on a `std::tuple`. The aggregate
/// path is what callers fall back to when the macro `THREADMAXX_REFLECT`
/// (R2) was not applied to `T`.

#include <cstddef>
#include <type_traits>

#include "detail/aggregate_impl.hpp"

namespace threadmaxx::reflect {

/// @brief Compile-time field count for aggregate T.
template <typename T>
constexpr std::size_t field_count() noexcept {
    return detail::aggregate_field_count_impl<std::remove_cv_t<std::remove_reference_t<T>>>();
}

/// @brief Get the I-th field of `obj` by reference. SFINAE-gated by
/// `I < field_count<T>()`.
template <std::size_t I, typename T>
constexpr decltype(auto) get(T& obj) {
    return detail::aggregate_get_impl<I>(obj);
}

/// @brief Walk every field of `obj`, invoking `fn(index, value)` in
/// declaration order.
///
/// When `T` was registered via `THREADMAXX_REFLECT` (R2), the named
/// overload in `field_info.hpp` takes precedence and `fn` is invoked
/// as `fn(name, value)` instead. Game code can write a single visitor
/// that accepts both shapes via overload sets.
template <typename T, typename Fn>
constexpr void for_each_field(T& obj, Fn&& fn) {
    constexpr std::size_t N = field_count<T>();
    if constexpr (N > 0) {
        detail::aggregate_for_each_impl<N>(obj, static_cast<Fn&&>(fn));
    }
}

/// @brief Compile-time check: does T look like a reflectable aggregate?
template <typename T>
inline constexpr bool is_aggregate_reflectable_v =
    std::is_aggregate_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    !std::is_polymorphic_v<std::remove_cv_t<std::remove_reference_t<T>>>;

} // namespace threadmaxx::reflect
