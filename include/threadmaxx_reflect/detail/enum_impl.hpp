#pragma once

/// @file enum_impl.hpp
/// @brief `__PRETTY_FUNCTION__` / `__FUNCSIG__` enum-name extraction.
///
/// The idea (Daniil Goncharov's magic_enum trick): instantiate a
/// templated probe with a specific enum constant; read the function
/// signature; parse the literal name out of it. Per-compiler delimiters
/// differ — clang / gcc use `__PRETTY_FUNCTION__`, MSVC uses
/// `__FUNCSIG__`. An empty return means "no enumerator at that value".

#include <string_view>

namespace threadmaxx::reflect::detail {

template <auto V>
constexpr std::string_view enumNameRaw() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    constexpr std::string_view raw = __PRETTY_FUNCTION__;
    // gcc:   "constexpr std::string_view threadmaxx::reflect::detail::enumNameRaw() [with auto V = (E)1; std::string_view = std::basic_string_view<char>]"
    // clang: "std::string_view threadmaxx::reflect::detail::enumNameRaw() [V = E::Foo]"
    // Find "V = " then parse until the trailing ']' or ';'.
    constexpr std::string_view sentinel = "V = ";
    const auto start = raw.find(sentinel);
    if (start == std::string_view::npos) return {};
    auto i = start + sentinel.size();
    auto end = i;
    int depth = 0;
    while (end < raw.size()) {
        const char c = raw[end];
        if (c == '<') ++depth;
        else if (c == '>') --depth;
        else if (depth == 0 && (c == ';' || c == ']')) break;
        ++end;
    }
    std::string_view body = raw.substr(i, end - i);
    // Strip trailing whitespace.
    while (!body.empty() && (body.back() == ' ' || body.back() == '\t')) {
        body.remove_suffix(1);
    }
    return body;
#elif defined(_MSC_VER)
    constexpr std::string_view raw = __FUNCSIG__;
    // MSVC: "auto __cdecl threadmaxx::reflect::detail::enumNameRaw<E::Foo>(void) noexcept"
    constexpr std::string_view sentinel = "enumNameRaw<";
    const auto start = raw.find(sentinel);
    if (start == std::string_view::npos) return {};
    auto i = start + sentinel.size();
    auto end = raw.find('>', i);
    if (end == std::string_view::npos) return {};
    return raw.substr(i, end - i);
#else
    return {};
#endif
}

/// @brief `true` if `raw` is a valid enum-name token (i.e. not an
/// integer cast like `(E)42` indicating "no enumerator here").
constexpr bool isValidEnumName(std::string_view raw) noexcept {
    if (raw.empty()) return false;
    // The "no enumerator here" cases:
    //   gcc:   "(MyEnum)5"
    //   clang: "(anonymous namespace)::E(5)" or "5"
    if (raw.front() == '(') return false;
    if (raw.front() == '-' || (raw.front() >= '0' && raw.front() <= '9')) return false;
    return true;
}

/// @brief Strip enclosing scope qualifiers ("Foo::Bar::Baz" -> "Baz").
constexpr std::string_view stripScope(std::string_view s) noexcept {
    const auto pos = s.rfind("::");
    if (pos == std::string_view::npos) return s;
    return s.substr(pos + 2);
}

} // namespace threadmaxx::reflect::detail
