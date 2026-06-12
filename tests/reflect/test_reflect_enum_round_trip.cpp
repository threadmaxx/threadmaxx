/// @file test_reflect_enum_round_trip.cpp
/// @brief `enum_cast<E>` round-trips `enum_name`. Unknown names return
/// nullopt; `enum_count` matches `enum_values().size()`.

#include "Check.hpp"

#include <threadmaxx_reflect/enum.hpp>

namespace {
enum class Faction : int {
    Player   = 0,
    Allies   = 1,
    Neutral  = 2,
    Enemies  = 3,
};
} // namespace

int main() {
    using namespace threadmaxx::reflect;

    constexpr auto cnt = enum_count<Faction>();
    CHECK_EQ(cnt, 4u);

    auto values = enum_values<Faction>();
    CHECK_EQ(values.size(), 4u);
    CHECK_EQ(values[0].first, Faction::Player);
    CHECK_EQ(values[0].second, std::string_view{"Player"});
    CHECK_EQ(values[1].second, std::string_view{"Allies"});
    CHECK_EQ(values[2].second, std::string_view{"Neutral"});
    CHECK_EQ(values[3].second, std::string_view{"Enemies"});

    // enum_cast round-trip.
    auto c1 = enum_cast<Faction>("Player");
    CHECK(c1.has_value());
    CHECK_EQ(*c1, Faction::Player);

    auto c2 = enum_cast<Faction>("Enemies");
    CHECK(c2.has_value());
    CHECK_EQ(*c2, Faction::Enemies);

    // Unknown.
    auto c3 = enum_cast<Faction>("Aliens");
    CHECK(!c3.has_value());

    // Empty input -> nullopt.
    auto c4 = enum_cast<Faction>("");
    CHECK(!c4.has_value());

    EXIT_WITH_RESULT();
}
