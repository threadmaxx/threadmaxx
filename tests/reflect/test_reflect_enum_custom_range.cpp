/// @file test_reflect_enum_custom_range.cpp
/// @brief `EnumRange<E>` specialization widens / narrows the scan.

#include "Check.hpp"

#include <threadmaxx_reflect/enum.hpp>

namespace {
enum class Skill : int {
    None      = 0,
    Combat    = 1,
    Sneaking  = 2,
    Magic     = 200,    // outside default [-128, 128] scan
    Crafting  = 201,
};
} // namespace

namespace threadmaxx::reflect {
template <>
struct EnumRange<Skill> {
    static constexpr int  min    = 0;
    static constexpr int  max    = 300;
    static constexpr bool isFlag = false;
};
} // namespace threadmaxx::reflect

int main() {
    using namespace threadmaxx::reflect;

    constexpr auto cnt = enum_count<Skill>();
    CHECK_EQ(cnt, 5u);

    CHECK_EQ(enum_name(Skill::Magic), std::string_view{"Magic"});
    CHECK_EQ(enum_name(Skill::Crafting), std::string_view{"Crafting"});

    auto c = enum_cast<Skill>("Magic");
    CHECK(c.has_value());
    CHECK_EQ(*c, Skill::Magic);

    EXIT_WITH_RESULT();
}
