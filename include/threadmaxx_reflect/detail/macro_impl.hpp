#pragma once

/// @file macro_impl.hpp
/// @brief Plumbing for the `THREADMAXX_REFLECT` macro:
///   - `FixedString<N>` — a literal class template usable as a C++20
///     non-type template parameter,
///   - `member_type<M>` — class / value-type extraction from a member
///     pointer,
///   - preprocessor FOR_EACH up to 16 fields.

#include <cstddef>
#include <string_view>

namespace threadmaxx::reflect::detail {

/// @brief Literal string template parameter — holds the trailing null.
template <std::size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&s)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    constexpr std::string_view view() const noexcept {
        return std::string_view(data, N - 1);
    }
};

template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

/// @brief Extract `ClassType` + `ValueType` from a member pointer.
template <typename M>
struct member_type;

template <typename C, typename T>
struct member_type<T C::*> {
    using class_type = C;
    using value_type = T;
};

} // namespace threadmaxx::reflect::detail

// -------------------------------------------------------------------------
// Preprocessor FOR_EACH(M, T, X1, X2, …, XN) emits a comma-separated list
// of `M(T, X1), M(T, X2), …`. Supports 1..16 fields (v1.0 ceiling).
// -------------------------------------------------------------------------

#define THREADMAXX_REFLECT_EXPAND(x) x

#define THREADMAXX_REFLECT_CAT_(a, b) a##b
#define THREADMAXX_REFLECT_CAT(a, b) THREADMAXX_REFLECT_CAT_(a, b)

#define THREADMAXX_REFLECT_FE_1(M, T, X1) M(T, X1)
#define THREADMAXX_REFLECT_FE_2(M, T, X1, X2) M(T, X1), M(T, X2)
#define THREADMAXX_REFLECT_FE_3(M, T, X1, X2, X3) M(T, X1), M(T, X2), M(T, X3)
#define THREADMAXX_REFLECT_FE_4(M, T, X1, X2, X3, X4) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4)
#define THREADMAXX_REFLECT_FE_5(M, T, X1, X2, X3, X4, X5) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5)
#define THREADMAXX_REFLECT_FE_6(M, T, X1, X2, X3, X4, X5, X6) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6)
#define THREADMAXX_REFLECT_FE_7(M, T, X1, X2, X3, X4, X5, X6, X7) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7)
#define THREADMAXX_REFLECT_FE_8(M, T, X1, X2, X3, X4, X5, X6, X7, X8) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), M(T, X8)
#define THREADMAXX_REFLECT_FE_9(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9)
#define THREADMAXX_REFLECT_FE_10(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10)
#define THREADMAXX_REFLECT_FE_11(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11)
#define THREADMAXX_REFLECT_FE_12(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11), M(T, X12)
#define THREADMAXX_REFLECT_FE_13(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11), M(T, X12), M(T, X13)
#define THREADMAXX_REFLECT_FE_14(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11), M(T, X12), M(T, X13), M(T, X14)
#define THREADMAXX_REFLECT_FE_15(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11), M(T, X12), M(T, X13), M(T, X14), \
    M(T, X15)
#define THREADMAXX_REFLECT_FE_16(M, T, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16) \
    M(T, X1), M(T, X2), M(T, X3), M(T, X4), M(T, X5), M(T, X6), M(T, X7), \
    M(T, X8), M(T, X9), M(T, X10), M(T, X11), M(T, X12), M(T, X13), M(T, X14), \
    M(T, X15), M(T, X16)

#define THREADMAXX_REFLECT_FE_GET_N_(\
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

#define THREADMAXX_REFLECT_FE_COUNT(...) \
    THREADMAXX_REFLECT_EXPAND(THREADMAXX_REFLECT_FE_GET_N_( \
        __VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

#define THREADMAXX_REFLECT_FE(M, T, ...) \
    THREADMAXX_REFLECT_EXPAND(THREADMAXX_REFLECT_CAT( \
        THREADMAXX_REFLECT_FE_, THREADMAXX_REFLECT_FE_COUNT(__VA_ARGS__))( \
            M, T, __VA_ARGS__))
