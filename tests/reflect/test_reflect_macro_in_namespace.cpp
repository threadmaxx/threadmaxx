/// @file test_reflect_macro_in_namespace.cpp
/// @brief THREADMAXX_REFLECT works equally well when applied to a type
/// declared inside a user namespace — ADL discovers the hook via the
/// type's namespace.

#include "Check.hpp"

#include <threadmaxx_reflect/aggregate.hpp>
#include <threadmaxx_reflect/macro.hpp>

namespace game {
struct Stats {
    int strength;
    int agility;
    int wisdom;
};
} // namespace game

// Macro applied INSIDE the user's namespace so the ADL hook lands
// next to the type.
namespace game {
THREADMAXX_REFLECT(Stats, strength, agility, wisdom);
} // namespace game

int main() {
    using namespace threadmaxx::reflect;
    static_assert(detail::HasReflectHook<game::Stats>);

    game::Stats s{10, 20, 30};
    int sum = 0;
    for_each_field(s, [&](std::string_view name, auto& v) {
        (void)name;
        sum += static_cast<int>(v);
    });
    CHECK_EQ(sum, 60);
    EXIT_WITH_RESULT();
}
