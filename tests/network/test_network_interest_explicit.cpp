/// @file test_network_interest_explicit.cpp
/// @brief NW9 — entity in an explicit-allow-list bypasses distance
/// filter.

#include "Check.hpp"

#include <threadmaxx_network/interest.hpp>

#include <vector>

int main() {
    using namespace threadmaxx::network;

    InterestManager im;
    ClientFocus focus{};
    focus.peer = PeerId{1};
    focus.config.radius = 10.0f;
    im.setFocus(focus);

    // Entity 42 is far away but explicitly allowed.
    im.addExplicit(PeerId{1}, NetEntityId{42});

    std::vector<EntityPosition> world = {
        {NetEntityId{1}, 5.0f, 0.0f, 0.0f},
        {NetEntityId{42}, 1000.0f, 1000.0f, 1000.0f},
        {NetEntityId{99}, 1000.0f, 1000.0f, 1000.0f},
    };
    auto v = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});

    bool seen42 = false;
    bool seen99 = false;
    for (auto id : v.visible) {
        if (id.value == 42u) seen42 = true;
        if (id.value == 99u) seen99 = true;
    }
    CHECK(seen42);
    CHECK(!seen99);

    // Removing the explicit allow drops 42 the next frame.
    im.removeExplicit(PeerId{1}, NetEntityId{42});
    auto v2 = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});
    for (auto id : v2.visible) {
        CHECK(id.value != 42u);
    }
    // And produces an "exited" notification.
    bool exit42 = false;
    for (auto id : v2.exited) {
        if (id.value == 42u) exit42 = true;
    }
    CHECK(exit42);

    EXIT_WITH_RESULT();
}
