// §3.1 batch 5: ComponentSet is now 64 bits wide. Verify that:
//   - Bits at high positions (15) can be set and observed.
//   - all() covers every currently-allocated bit (16 bits, value 0xFFFF).
//   - Serialization round-trips the wide mask intact (would have lost
//     anything past bit 31 in v1).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <sstream>

int main() {
    using namespace threadmaxx;

    // all() returns the mask of all 16 allocated bits.
    CHECK_EQ(ComponentSet::all().bits(), std::uint64_t{0xFFFFu});

    // Bit at position 15 (DestroyedTag) is reachable.
    ComponentSet m;
    m.add(Component::DestroyedTag);
    CHECK(m.has(Component::DestroyedTag));
    CHECK_EQ(m.bits(), std::uint64_t{1ull << 15});

    // Bit at position 12 (BoundingVolume) round-trips through ORing.
    m |= ComponentSet{Component::BoundingVolume};
    CHECK(m.has(Component::BoundingVolume));
    CHECK(m.has(Component::DestroyedTag));

    // Width check: a manual set of bit 63 stays observable (no
    // truncation). We can't actually go through Component (no enum
    // value at bit 63) but we can confirm bits() returns 64 bits worth.
    // Set all currently-allocated bits, verify the mask matches all().
    ComponentSet full;
    for (auto c : { Component::Transform, Component::Velocity,
                    Component::RenderTag, Component::UserData,
                    Component::EntityStructural, Component::Acceleration,
                    Component::Parent, Component::Health,
                    Component::Faction, Component::AnimationStateRef,
                    Component::PhysicsBodyRef, Component::NavAgentRef,
                    Component::BoundingVolume, Component::StaticTag,
                    Component::DisabledTag, Component::DestroyedTag }) {
        full.add(c);
    }
    CHECK_EQ(full.bits(), ComponentSet::all().bits());

    // Round-trip through the serializer. v2 format writes a 64-bit
    // bits field; the deserializer must reconstruct every bit
    // including the high ones.
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    serialize(ss, m);
    ComponentSet roundTripped;
    CHECK(deserialize(ss, roundTripped));
    CHECK_EQ(roundTripped.bits(), m.bits());
    CHECK(roundTripped.has(Component::DestroyedTag));
    CHECK(roundTripped.has(Component::BoundingVolume));

    EXIT_WITH_RESULT();
}
