/// @file test_reflect_engine_bridge_user_component.cpp
/// @brief `registerUserComponentType<T>()` brings a game-side
/// component into the registry, side-by-side with engine built-ins.

#include "Check.hpp"

#include <threadmaxx_reflect/engine_bridge.hpp>
#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE

namespace game {
struct CubeRender { int meshId; int colorIdx; };
THREADMAXX_REFLECT(CubeRender, meshId, colorIdx);

struct InventorySlot { int itemId; int count; };
THREADMAXX_REFLECT(InventorySlot, itemId, count);
} // namespace game

int main() {
    using namespace threadmaxx::reflect;
    using namespace threadmaxx::reflect::engine_bridge;

    TypeRegistry reg;
    registerBuiltins(reg);
    const std::size_t builtinCount = reg.size();
    CHECK_EQ(builtinCount, 14u);

    auto* cube = registerUserComponentType<game::CubeRender>(reg, "CubeRender");
    auto* slot = registerUserComponentType<game::InventorySlot>(reg, "InventorySlot");
    CHECK(cube != nullptr);
    CHECK(slot != nullptr);

    CHECK_EQ(reg.size(), 16u);

    CHECK_EQ(cube->fields.size(), 2u);
    CHECK_EQ(slot->fields.size(), 2u);
    CHECK_EQ(slot->fields[0].name, std::string_view{"itemId"});

    EXIT_WITH_RESULT();
}

#else
int main() { return 0; }
#endif
