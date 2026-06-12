/// @file test_network_interest_distance.cpp
/// @brief NW9 — 1000 entities scattered; client at world origin only
/// receives entities within `radius`.

#include "Check.hpp"

#include <threadmaxx_network/interest.hpp>

#include <random>
#include <vector>

int main() {
    using namespace threadmaxx::network;

    InterestManager im;
    ClientFocus focus{};
    focus.peer = PeerId{1};
    focus.x = focus.y = focus.z = 0.0f;
    focus.config.radius = 50.0f;
    im.setFocus(focus);

    std::vector<EntityPosition> world;
    world.reserve(1000);
    std::mt19937_64 rng{12345};
    std::uniform_real_distribution<float> dist(-200.0f, 200.0f);
    for (std::uint64_t i = 1; i <= 1000; ++i) {
        EntityPosition p{};
        p.id = NetEntityId{i};
        p.x = dist(rng);
        p.y = dist(rng);
        p.z = dist(rng);
        world.push_back(p);
    }

    auto vis = im.buildVisibleSet(PeerId{1},
        std::span<const EntityPosition>{world.data(), world.size()});

    // Every visible entity must be within 50 units of origin.
    for (auto id : vis.visible) {
        bool found = false;
        for (const auto& w : world) {
            if (w.id == id) {
                const float d2 = w.x * w.x + w.y * w.y + w.z * w.z;
                CHECK(d2 <= 50.0f * 50.0f + 0.001f);
                found = true; break;
            }
        }
        CHECK(found);
    }
    // And there should be at least a handful (50/200 → ~1.5%, ~15 entities).
    CHECK(!vis.visible.empty());

    EXIT_WITH_RESULT();
}
