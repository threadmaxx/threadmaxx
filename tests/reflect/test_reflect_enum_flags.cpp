/// @file test_reflect_enum_flags.cpp
/// @brief Flag-enum opt-in. `enum_cast` parses `|`-separated names
/// and OR's the bits.

#include "Check.hpp"

#include <type_traits>

#include <threadmaxx_reflect/enum.hpp>

namespace {
enum class CollisionMask : int {
    None    = 0,
    Player  = 1,
    Enemy   = 2,
    Terrain = 4,
    Trigger = 8,
};
} // namespace

namespace threadmaxx::reflect {
template <>
struct EnumRange<CollisionMask> {
    static constexpr int  min    = 0;
    static constexpr int  max    = 16;
    static constexpr bool isFlag = true;
};
} // namespace threadmaxx::reflect

int main() {
    using namespace threadmaxx::reflect;
    using U = std::underlying_type_t<CollisionMask>;

    // OR-of-names parsing.
    auto a = enum_cast<CollisionMask>("Player|Enemy");
    CHECK(a.has_value());
    CHECK_EQ(static_cast<U>(*a),
             static_cast<U>(CollisionMask::Player) |
             static_cast<U>(CollisionMask::Enemy));

    // Whitespace tolerance.
    auto b = enum_cast<CollisionMask>("Player | Trigger");
    CHECK(b.has_value());
    CHECK_EQ(static_cast<U>(*b),
             static_cast<U>(CollisionMask::Player) |
             static_cast<U>(CollisionMask::Trigger));

    // Single value.
    auto c = enum_cast<CollisionMask>("Terrain");
    CHECK(c.has_value());
    CHECK_EQ(static_cast<U>(*c), 4);

    // Unknown token in OR-list aborts the parse.
    auto d = enum_cast<CollisionMask>("Player|FooBar");
    CHECK(!d.has_value());

    EXIT_WITH_RESULT();
}
