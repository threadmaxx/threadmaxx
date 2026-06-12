/// @file test_reflect_enum_basic.cpp
/// @brief `enum_name<V>()` returns the literal token. Cases:
/// well-known enumerator, missing enumerator, anonymous-namespace enum.

#include "Check.hpp"

#include <threadmaxx_reflect/enum.hpp>

namespace {
enum class Color : int {
    Red   = 0,
    Green = 1,
    Blue  = 2,
};
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    constexpr auto r = enum_name<Color::Red>();
    constexpr auto g = enum_name<Color::Green>();
    constexpr auto b = enum_name<Color::Blue>();
    CHECK_EQ(r, std::string_view{"Red"});
    CHECK_EQ(g, std::string_view{"Green"});
    CHECK_EQ(b, std::string_view{"Blue"});

    // Out-of-range value -> empty.
    constexpr auto missing = enum_name<static_cast<Color>(99)>();
    CHECK(missing.empty());

    // Runtime stringify.
    CHECK_EQ(enum_name(Color::Red), std::string_view{"Red"});
    CHECK_EQ(enum_name(Color::Green), std::string_view{"Green"});
    CHECK_EQ(enum_name(static_cast<Color>(42)), std::string_view{});

    EXIT_WITH_RESULT();
}
