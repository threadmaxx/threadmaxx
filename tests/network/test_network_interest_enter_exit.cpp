/// @file test_network_interest_enter_exit.cpp
/// @brief NW9 — entity crossing into AOI produces an "entered"
/// entry; crossing out produces an "exited" entry.

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

    // Frame 0: only entity 1 within range.
    std::vector<EntityPosition> world = {
        {NetEntityId{1}, 5.0f, 0.0f, 0.0f},
        {NetEntityId{2}, 100.0f, 0.0f, 0.0f},
    };
    auto v0 = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});
    CHECK_EQ(v0.visible.size(), 1u);
    CHECK_EQ(v0.entered.size(), 1u);
    CHECK_EQ(v0.exited.size(), 0u);

    // Frame 1: entity 2 walks into range, entity 1 stays.
    world[1].x = 5.0f;
    auto v1 = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});
    CHECK_EQ(v1.visible.size(), 2u);
    CHECK_EQ(v1.entered.size(), 1u);
    CHECK(v1.entered[0].value == 2u);
    CHECK_EQ(v1.exited.size(), 0u);

    // Frame 2: entity 1 walks out of range.
    world[0].x = 100.0f;
    auto v2 = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});
    CHECK_EQ(v2.visible.size(), 1u);
    CHECK_EQ(v2.exited.size(), 1u);
    CHECK(v2.exited[0].value == 1u);

    EXIT_WITH_RESULT();
}
