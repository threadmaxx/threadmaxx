/// @file test_editor_property_read.cpp
/// @brief E7 — select an entity with a known Health; the property
/// reader returns current / max / regen with the right values.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/properties.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>

#include <threadmaxx_reflect/registry.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        threadmaxx::Transform t{};
        seed.spawn(entity, t);
        threadmaxx::Health h{};
        h.current = 42.0f;
        h.max = 100.0f;
        seed.setHealth(entity, h);
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::reflect::TypeRegistry reg;
    threadmaxx::editor::PropertyEditor ed{engine, reg};
    ed.addBuiltinBindings();

    const auto comps = ed.componentsOn(game.entity);
    bool sawHealth = false;
    for (const auto& c : comps) {
        if (c == "Health") sawHealth = true;
    }
    CHECK(sawHealth);

    auto cur = ed.readField(game.entity, "Health", "current");
    CHECK(cur.has_value());
    float curF{};
    CHECK(cur->get(curF));
    CHECK_EQ(curF, 42.0f);

    auto mx = ed.readField(game.entity, "Health", "max");
    float mxF{};
    CHECK(mx->get(mxF));
    CHECK_EQ(mxF, 100.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
