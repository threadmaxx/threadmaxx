#pragma once

/// @file aggregate_impl.hpp
/// @brief SFINAE plumbing for compile-time aggregate field counting.
///
/// The trick: a `UniversalInit` proxy is implicitly convertible to
/// anything, so `T{UniversalInit{}, UniversalInit{}, …, UniversalInit{}}`
/// type-checks exactly when T is an aggregate with that many fields
/// (or fewer, if the trailing UniversalInits are convertible to no
/// member). We probe in descending order and return the largest N
/// that compiles.

#include <cstddef>
#include <type_traits>
#include <utility>

#include "../config.hpp"

namespace threadmaxx::reflect::detail {

struct UniversalInit {
    template <typename T>
    constexpr operator T() const noexcept; // declaration only — never invoked
};

template <typename T, std::size_t... Is>
constexpr auto is_constructible_with_n_impl(std::index_sequence<Is...>, int)
    -> decltype(T{((void)Is, UniversalInit{})...}, std::true_type{});

template <typename T, std::size_t... Is>
constexpr std::false_type is_constructible_with_n_impl(std::index_sequence<Is...>, ...);

template <typename T, std::size_t N>
inline constexpr bool is_aggregate_n =
    decltype(is_constructible_with_n_impl<T>(std::make_index_sequence<N>{}, 0))::value;

template <typename T, std::size_t N = kMaxAggregateFields>
constexpr std::size_t aggregate_field_count_impl() noexcept {
    if constexpr (N == 0) {
        return 0;
    } else if constexpr (is_aggregate_n<T, N>) {
        return N;
    } else {
        return aggregate_field_count_impl<T, N - 1>();
    }
}

// -------------------------------------------------------------------------
// Structured-bindings dispatch up to N = 16 (a safe ceiling for our
// component PODs). Adding 17+ is mechanical — but every threadmaxx
// built-in fits in 4-6 fields; nothing in flight needs more.
// -------------------------------------------------------------------------
template <std::size_t N, typename T, typename Fn>
constexpr void aggregate_for_each_impl(T& obj, Fn&& fn) {
    if constexpr (N == 1) {
        auto& [a] = obj; fn(std::size_t{0}, a);
    } else if constexpr (N == 2) {
        auto& [a, b] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b);
    } else if constexpr (N == 3) {
        auto& [a, b, c] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
    } else if constexpr (N == 4) {
        auto& [a, b, c, d] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c); fn(std::size_t{3}, d);
    } else if constexpr (N == 5) {
        auto& [a, b, c, d, e] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e);
    } else if constexpr (N == 6) {
        auto& [a, b, c, d, e, f] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
    } else if constexpr (N == 7) {
        auto& [a, b, c, d, e, f, g] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g);
    } else if constexpr (N == 8) {
        auto& [a, b, c, d, e, f, g, h] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h);
    } else if constexpr (N == 9) {
        auto& [a, b, c, d, e, f, g, h, i] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
    } else if constexpr (N == 10) {
        auto& [a, b, c, d, e, f, g, h, i, j] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
        fn(std::size_t{9}, j);
    } else if constexpr (N == 11) {
        auto& [a, b, c, d, e, f, g, h, i, j, k] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
        fn(std::size_t{9}, j); fn(std::size_t{10}, k);
    } else if constexpr (N == 12) {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
        fn(std::size_t{9}, j); fn(std::size_t{10}, k); fn(std::size_t{11}, l);
    } else if constexpr (N == 13) {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
        fn(std::size_t{9}, j); fn(std::size_t{10}, k); fn(std::size_t{11}, l);
        fn(std::size_t{12}, m);
    } else if constexpr (N == 14) {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n] = obj;
        fn(std::size_t{0}, a); fn(std::size_t{1}, b); fn(std::size_t{2}, c);
        fn(std::size_t{3}, d); fn(std::size_t{4}, e); fn(std::size_t{5}, f);
        fn(std::size_t{6}, g); fn(std::size_t{7}, h); fn(std::size_t{8}, i);
        fn(std::size_t{9}, j); fn(std::size_t{10}, k); fn(std::size_t{11}, l);
        fn(std::size_t{12}, m); fn(std::size_t{13}, n);
    } else if constexpr (N == 15) {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n, o] = obj;
        fn(std::size_t{0}, a);  fn(std::size_t{1}, b);  fn(std::size_t{2}, c);
        fn(std::size_t{3}, d);  fn(std::size_t{4}, e);  fn(std::size_t{5}, f);
        fn(std::size_t{6}, g);  fn(std::size_t{7}, h);  fn(std::size_t{8}, i);
        fn(std::size_t{9}, j);  fn(std::size_t{10}, k); fn(std::size_t{11}, l);
        fn(std::size_t{12}, m); fn(std::size_t{13}, n); fn(std::size_t{14}, o);
    } else if constexpr (N == 16) {
        auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p] = obj;
        fn(std::size_t{0}, a);  fn(std::size_t{1}, b);  fn(std::size_t{2}, c);
        fn(std::size_t{3}, d);  fn(std::size_t{4}, e);  fn(std::size_t{5}, f);
        fn(std::size_t{6}, g);  fn(std::size_t{7}, h);  fn(std::size_t{8}, i);
        fn(std::size_t{9}, j);  fn(std::size_t{10}, k); fn(std::size_t{11}, l);
        fn(std::size_t{12}, m); fn(std::size_t{13}, n); fn(std::size_t{14}, o);
        fn(std::size_t{15}, p);
    } else {
        // R1 supports up to 16; >16 is a v1.x mechanical extension.
        static_assert(N <= 16, "aggregate reflection capped at 16 fields in v1.0");
    }
}

// -------------------------------------------------------------------------
// get<I>(t) — return the I-th field by reference. Mirrors std::get for
// tuples without requiring T to be a tuple.
// -------------------------------------------------------------------------
template <std::size_t I, typename T>
constexpr decltype(auto) aggregate_get_impl(T& obj) {
    constexpr std::size_t N = aggregate_field_count_impl<std::remove_cv_t<T>>();
    static_assert(I < N, "aggregate get<I>: index out of range");
    decltype(auto) ret = [&]() -> decltype(auto) {
        if constexpr (N == 1) { auto& [a] = obj; if constexpr (I == 0) return (a); }
        else if constexpr (N == 2) { auto& [a,b] = obj;
            if constexpr (I == 0) return (a); else return (b); }
        else if constexpr (N == 3) { auto& [a,b,c] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else return (c); }
        else if constexpr (N == 4) { auto& [a,b,c,d] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else if constexpr (I == 2) return (c);
            else return (d); }
        else if constexpr (N == 5) { auto& [a,b,c,d,e] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else if constexpr (I == 2) return (c);
            else if constexpr (I == 3) return (d);
            else return (e); }
        else if constexpr (N == 6) { auto& [a,b,c,d,e,f] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else if constexpr (I == 2) return (c);
            else if constexpr (I == 3) return (d);
            else if constexpr (I == 4) return (e);
            else return (f); }
        else if constexpr (N == 7) { auto& [a,b,c,d,e,f,g] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else if constexpr (I == 2) return (c);
            else if constexpr (I == 3) return (d);
            else if constexpr (I == 4) return (e);
            else if constexpr (I == 5) return (f);
            else return (g); }
        else if constexpr (N == 8) { auto& [a,b,c,d,e,f,g,h] = obj;
            if constexpr (I == 0) return (a);
            else if constexpr (I == 1) return (b);
            else if constexpr (I == 2) return (c);
            else if constexpr (I == 3) return (d);
            else if constexpr (I == 4) return (e);
            else if constexpr (I == 5) return (f);
            else if constexpr (I == 6) return (g);
            else return (h); }
        else { static_assert(N <= 8, "aggregate get<I>: ranges >8 covered by for_each_field"); }
    }();
    return ret;
}

} // namespace threadmaxx::reflect::detail
