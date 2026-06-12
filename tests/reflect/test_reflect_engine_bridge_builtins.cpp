/// @file test_reflect_engine_bridge_builtins.cpp
/// @brief `registerBuiltins` populates every aggregate built-in
/// component into the registry with the right field counts.

#include "Check.hpp"

#include <threadmaxx_reflect/engine_bridge.hpp>
#include <threadmaxx_reflect/registry.hpp>

#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE

int main() {
    using namespace threadmaxx::reflect;
    using namespace threadmaxx::reflect::engine_bridge;

    TypeRegistry reg;
    registerBuiltins(reg);

    // The built-ins manifest is 14 types.
    CHECK_EQ(reg.size(), 14u);

    // Spot-check a few.
    auto* transform = reg.find("Transform");
    CHECK(transform != nullptr);
    CHECK_EQ(transform->fields.size(), 3u);
    CHECK_EQ(transform->fields[0].name, std::string_view{"position"});
    CHECK_EQ(transform->fields[1].name, std::string_view{"orientation"});
    CHECK_EQ(transform->fields[2].name, std::string_view{"scale"});

    auto* vec3 = reg.find("Vec3");
    CHECK_EQ(vec3->fields.size(), 3u);
    CHECK_EQ(vec3->fields[0].name, std::string_view{"x"});

    auto* quat = reg.find("Quat");
    CHECK_EQ(quat->fields.size(), 4u);

    auto* health = reg.find("Health");
    CHECK_EQ(health->fields.size(), 2u);
    CHECK_EQ(health->fields[0].name, std::string_view{"current"});
    CHECK_EQ(health->fields[1].name, std::string_view{"max"});

    auto* faction = reg.find("Faction");
    CHECK_EQ(faction->fields.size(), 1u);

    EXIT_WITH_RESULT();
}

#else
int main() {
    // Bridge wasn't built — skip but pass.
    return 0;
}
#endif
