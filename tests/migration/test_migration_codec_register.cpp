/// @file test_migration_codec_register.cpp
/// @brief M7 — register a codec for a custom game-side type; encode +
/// decode round-trip a value via Record.

#include "Check.hpp"

#include <threadmaxx_migration/component.hpp>

#include <cstdint>
#include <cstring>

namespace {

struct Faction {
    std::uint32_t teamId{0};
    std::uint32_t score{0};
};

bool encodeFaction(const void* opaque, threadmaxx::migration::Record& dst) {
    const auto* f = static_cast<const Faction*>(opaque);
    threadmaxx::migration::FieldValue teamBytes{};
    teamBytes.bytes.resize(sizeof(f->teamId));
    std::memcpy(teamBytes.bytes.data(), &f->teamId, sizeof(f->teamId));
    threadmaxx::migration::FieldValue scoreBytes{};
    scoreBytes.bytes.resize(sizeof(f->score));
    std::memcpy(scoreBytes.bytes.data(), &f->score, sizeof(f->score));
    dst.fields.push_back({"teamId", std::move(teamBytes)});
    dst.fields.push_back({"score",  std::move(scoreBytes)});
    return true;
}

bool decodeFaction(const threadmaxx::migration::Record& src, void* opaque) {
    auto* f = static_cast<Faction*>(opaque);
    for (const auto& field : src.fields) {
        if (field.name == "teamId" &&
            field.value.bytes.size() == sizeof(f->teamId)) {
            std::memcpy(&f->teamId, field.value.bytes.data(),
                        sizeof(f->teamId));
        } else if (field.name == "score" &&
                   field.value.bytes.size() == sizeof(f->score)) {
            std::memcpy(&f->score, field.value.bytes.data(),
                        sizeof(f->score));
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    ComponentMigrationBridge bridge;
    CHECK(!bridge.hasType("Faction"));
    CHECK_EQ(bridge.codecCount(), 0u);

    bridge.registerCodec("Faction", SchemaVersion{1, 0, 0},
                         encodeFaction, decodeFaction);
    CHECK(bridge.hasType("Faction"));
    CHECK_EQ(bridge.codecCount(), 1u);

    // Encode.
    Faction source{42u, 1000u};
    Record dst{};
    CHECK(bridge.encode("Faction", SchemaVersion{1, 0, 0}, &source, dst));
    CHECK_EQ(dst.typeName, std::string{"Faction"});
    CHECK_EQ(dst.fields.size(), 2u);

    // Decode round-trip.
    Faction roundtrip{};
    CHECK(bridge.decode(dst, &roundtrip));
    CHECK_EQ(roundtrip.teamId, 42u);
    CHECK_EQ(roundtrip.score, 1000u);

    // Unknown type fails both encode + decode.
    Record other{};
    other.typeName = "Unknown";
    other.sourceVersion = SchemaVersion{1, 0, 0};
    CHECK(!bridge.encode("Unknown", SchemaVersion{1, 0, 0}, &source, other));
    Faction discard{};
    CHECK(!bridge.decode(other, &discard));

    EXIT_WITH_RESULT();
}
