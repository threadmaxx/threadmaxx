/// @file test_migration_codec_versioned.cpp
/// @brief M7 — two codec versions for the same type-name. The bridge
/// picks the highest version ≤ the record's sourceVersion.

#include "Check.hpp"

#include <threadmaxx_migration/component.hpp>

#include <cstdint>
#include <cstring>

namespace {

// Two record-layout versions of "Stats":
//   v1.0.0 — single u32 "value"
//   v1.1.0 — { u32 value, u32 multiplier }

struct StatsV1 { std::uint32_t value{0}; };
struct StatsV2 { std::uint32_t value{0}; std::uint32_t multiplier{0}; };

bool encodeV1(const void* opaque, threadmaxx::migration::Record& dst) {
    const auto* s = static_cast<const StatsV1*>(opaque);
    threadmaxx::migration::FieldValue v{};
    v.bytes.resize(sizeof(s->value));
    std::memcpy(v.bytes.data(), &s->value, sizeof(s->value));
    dst.fields.push_back({"value", std::move(v)});
    return true;
}
bool decodeV1(const threadmaxx::migration::Record& src, void* opaque) {
    auto* s = static_cast<StatsV1*>(opaque);
    for (const auto& f : src.fields) {
        if (f.name == "value" && f.value.bytes.size() == sizeof(s->value)) {
            std::memcpy(&s->value, f.value.bytes.data(), sizeof(s->value));
        }
    }
    return true;
}

bool encodeV2(const void* opaque, threadmaxx::migration::Record& dst) {
    const auto* s = static_cast<const StatsV2*>(opaque);
    threadmaxx::migration::FieldValue v{};
    v.bytes.resize(sizeof(s->value));
    std::memcpy(v.bytes.data(), &s->value, sizeof(s->value));
    threadmaxx::migration::FieldValue m{};
    m.bytes.resize(sizeof(s->multiplier));
    std::memcpy(m.bytes.data(), &s->multiplier, sizeof(s->multiplier));
    dst.fields.push_back({"value", std::move(v)});
    dst.fields.push_back({"multiplier", std::move(m)});
    return true;
}
bool decodeV2(const threadmaxx::migration::Record& src, void* opaque) {
    auto* s = static_cast<StatsV2*>(opaque);
    for (const auto& f : src.fields) {
        if (f.name == "value" && f.value.bytes.size() == sizeof(s->value)) {
            std::memcpy(&s->value, f.value.bytes.data(), sizeof(s->value));
        } else if (f.name == "multiplier" &&
                   f.value.bytes.size() == sizeof(s->multiplier)) {
            std::memcpy(&s->multiplier, f.value.bytes.data(),
                        sizeof(s->multiplier));
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    ComponentMigrationBridge bridge;
    bridge.registerCodec("Stats", SchemaVersion{1, 0, 0}, encodeV1, decodeV1);
    bridge.registerCodec("Stats", SchemaVersion{1, 1, 0}, encodeV2, decodeV2);
    CHECK_EQ(bridge.codecCount(), 2u);

    // Asking for v1.0.0: picks V1 codec.
    {
        StatsV1 s{42u};
        Record r{};
        CHECK(bridge.encode("Stats", SchemaVersion{1, 0, 0}, &s, r));
        CHECK_EQ(r.fields.size(), 1u);
        StatsV1 reloaded{};
        CHECK(bridge.decode(r, &reloaded));
        CHECK_EQ(reloaded.value, 42u);
    }

    // Asking for v1.1.0: picks V2 codec.
    {
        StatsV2 s{99u, 7u};
        Record r{};
        CHECK(bridge.encode("Stats", SchemaVersion{1, 1, 0}, &s, r));
        CHECK_EQ(r.fields.size(), 2u);
        StatsV2 reloaded{};
        CHECK(bridge.decode(r, &reloaded));
        CHECK_EQ(reloaded.value, 99u);
        CHECK_EQ(reloaded.multiplier, 7u);
    }

    // In-between version 1.0.5: picks the highest ≤ that, i.e. V1.
    {
        StatsV1 s{17u};
        Record r{};
        CHECK(bridge.encode("Stats", SchemaVersion{1, 0, 5}, &s, r));
        CHECK_EQ(r.fields.size(), 1u);  // V1's encoder only writes "value"
        // r.sourceVersion = 1.0.5 → decode picks V1 (highest ≤ 1.0.5).
        StatsV1 reloaded{};
        CHECK(bridge.decode(r, &reloaded));
        CHECK_EQ(reloaded.value, 17u);
    }

    // Above-all version 2.0.0: picks the highest registered, V2.
    {
        const auto* enc = bridge.findEncoder("Stats", SchemaVersion{2, 0, 0});
        CHECK(enc != nullptr);
        CHECK(*enc != nullptr);
    }

    // Version BELOW the lowest: no codec returned.
    {
        const auto* enc = bridge.findEncoder("Stats", SchemaVersion{0, 9, 0});
        CHECK(enc == nullptr);
    }

    // Re-registering the same (type, version) overwrites in place.
    bool replaced = false;
    bridge.registerCodec("Stats", SchemaVersion{1, 1, 0},
                         [&replaced](const void*, Record&) {
                             replaced = true;
                             return true;
                         },
                         decodeV2);
    CHECK_EQ(bridge.codecCount(), 2u);  // size unchanged
    StatsV2 sentinel{};
    Record r{};
    CHECK(bridge.encode("Stats", SchemaVersion{1, 1, 0}, &sentinel, r));
    CHECK(replaced);

    EXIT_WITH_RESULT();
}
