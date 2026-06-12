/// @file test_editor_property_custom_type.cpp
/// @brief E7 — register a custom reflection for a user-side component
/// type; the field appears in the property panel.

#include "Check.hpp"

#include <threadmaxx_editor/properties.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

namespace {

struct LootCount {
    std::int32_t gold;
    std::int32_t gems;
};

THREADMAXX_REFLECT(LootCount, gold, gems);

threadmaxx::UserComponentId gLootId{};

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        gLootId = engine.registerUserComponent<LootCount>();
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
        LootCount lc{100, 5};
        threadmaxx::addUserComponent(seed, gLootId, entity, lc);
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;

    threadmaxx::reflect::TypeRegistry reg;
    const auto* lootType = reg.registerType<LootCount>("LootCount");
    CHECK(lootType != nullptr);
    CHECK_EQ(lootType->fields.size(), 2u);

    threadmaxx::editor::PropertyEditor ed{engine, reg};
    threadmaxx::editor::PropertyEditor::ComponentBinding b{};
    b.typeName = "LootCount";
    b.type = lootType;
    b.get = [](const threadmaxx::World& w,
               threadmaxx::EntityHandle e) -> const void* {
        return threadmaxx::user::tryGet<LootCount>(w, gLootId, e);
    };
    b.set = [](threadmaxx::CommandBuffer&,
               threadmaxx::EntityHandle,
               const void*) {
        // v1.0 doesn't ship a user-component setter trampoline; the
        // setField path is exercised by the built-in test. This test
        // only verifies that custom registrations surface in
        // componentsOn() and readField().
    };
    ed.addBinding(b);

    CHECK(engine.initialize(game));
    engine.step();

    const auto comps = ed.componentsOn(game.entity);
    bool sawLoot = false;
    for (const auto& c : comps) {
        if (c == "LootCount") sawLoot = true;
    }
    CHECK(sawLoot);

    auto goldVal = ed.readField(game.entity, "LootCount", "gold");
    CHECK(goldVal.has_value());
    std::int32_t gold{};
    CHECK(goldVal->get(gold));
    CHECK_EQ(gold, 100);

    auto gemsVal = ed.readField(game.entity, "LootCount", "gems");
    std::int32_t gems{};
    CHECK(gemsVal->get(gems));
    CHECK_EQ(gems, 5);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
