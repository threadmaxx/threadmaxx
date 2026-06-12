#pragma once

/// @file enum.hpp
/// @brief Compile-time enum reflection.
///
/// `enum_name<E::X>()` returns the literal `"X"`. `enum_values<E>()`
/// is a span of `{value, name}` pairs covering every enumerator in
/// the scan range (default `[-128, 128]`). `enum_cast<E>("X")`
/// parses a name back to a value.
///
/// Customize the scan range per-enum:
///
/// ```cpp
/// template <> struct threadmaxx::reflect::EnumRange<MyEnum> {
///     static constexpr int min = 0;
///     static constexpr int max = 1024;
/// };
/// ```
///
/// Flag enums opt in via `isFlag = true` for bit-by-bit name
/// rendering and OR'd `enum_cast`.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config.hpp"
#include "detail/enum_impl.hpp"

namespace threadmaxx::reflect {

/// @brief Customization point for enum scan range.
template <typename E>
struct EnumRange {
    static constexpr int  min    = kEnumScanRangeMin;
    static constexpr int  max    = kEnumScanRangeMax;
    static constexpr bool isFlag = false;
};

/// @brief Return the literal name of a single enum value.
/// Empty string_view when the value isn't an enumerator.
template <auto V>
constexpr std::string_view enum_name() noexcept {
    constexpr auto raw = detail::enumNameRaw<V>();
    if constexpr (!detail::isValidEnumName(raw)) {
        return std::string_view{};
    } else {
        return detail::stripScope(raw);
    }
}

namespace detail {

template <typename E, int I, int Max>
constexpr std::size_t countValidEnumNames() noexcept {
    if constexpr (I > Max) {
        return 0;
    } else {
        constexpr auto name =
            enum_name<static_cast<E>(I)>();
        return (name.empty() ? 0u : 1u) +
               countValidEnumNames<E, I + 1, Max>();
    }
}

template <typename E, int I, int Max, typename Out>
constexpr void fillValidEnumPairs(Out& out, std::size_t& cursor) {
    if constexpr (I <= Max) {
        constexpr auto name =
            enum_name<static_cast<E>(I)>();
        if constexpr (!name.empty()) {
            out[cursor].first  = static_cast<E>(I);
            out[cursor].second = name;
            ++cursor;
        }
        fillValidEnumPairs<E, I + 1, Max>(out, cursor);
    }
}

template <typename E>
constexpr auto buildEnumValueList() {
    constexpr int Min = EnumRange<E>::min;
    constexpr int Max = EnumRange<E>::max;
    constexpr std::size_t N = countValidEnumNames<E, Min, Max>();
    std::array<std::pair<E, std::string_view>, N> arr{};
    std::size_t cursor = 0;
    fillValidEnumPairs<E, Min, Max>(arr, cursor);
    return arr;
}

template <typename E>
inline constexpr auto enumValueList = buildEnumValueList<E>();

} // namespace detail

/// @brief Compile-time count of enumerators in the scan range.
template <typename E>
constexpr std::size_t enum_count() noexcept {
    return detail::enumValueList<E>.size();
}

/// @brief Span of (value, name) pairs for `E`, in ascending value order.
template <typename E>
constexpr std::span<const std::pair<E, std::string_view>> enum_values() noexcept {
    return std::span<const std::pair<E, std::string_view>>(
        detail::enumValueList<E>.data(),
        detail::enumValueList<E>.size());
}

/// @brief Stringify a runtime enum value; empty if unknown.
template <typename E>
constexpr std::string_view enum_name(E value) noexcept {
    for (const auto& kv : detail::enumValueList<E>) {
        if (kv.first == value) return kv.second;
    }
    return {};
}

/// @brief Parse a name back to the enum value. nullopt if unknown.
/// For flag enums (`EnumRange<E>::isFlag = true`), accepts a `|`-
/// separated list and OR's the bits.
template <typename E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
    if constexpr (EnumRange<E>::isFlag) {
        using U = std::underlying_type_t<E>;
        U acc = 0;
        std::size_t pos = 0;
        while (pos < name.size()) {
            std::size_t bar = name.find('|', pos);
            std::string_view tok = (bar == std::string_view::npos)
                                       ? name.substr(pos)
                                       : name.substr(pos, bar - pos);
            // Trim whitespace.
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) {
                tok.remove_prefix(1);
            }
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) {
                tok.remove_suffix(1);
            }
            if (tok.empty()) {
                if (bar == std::string_view::npos) break;
                pos = bar + 1;
                continue;
            }
            bool matched = false;
            for (const auto& kv : detail::enumValueList<E>) {
                if (kv.second == tok) {
                    acc |= static_cast<U>(kv.first);
                    matched = true;
                    break;
                }
            }
            if (!matched) return std::nullopt;
            if (bar == std::string_view::npos) break;
            pos = bar + 1;
        }
        return static_cast<E>(acc);
    } else {
        for (const auto& kv : detail::enumValueList<E>) {
            if (kv.second == name) return kv.first;
        }
        return std::nullopt;
    }
}

} // namespace threadmaxx::reflect
