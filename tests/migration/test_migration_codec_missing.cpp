/// @file test_migration_codec_missing.cpp
/// @brief M7 — a record with no registered codec is rejected when
/// keepUnknownFields=false / pipeline failOnUnknownType=true.
/// keepUnknownFields=true keeps the bytes around, M5's pipeline path
/// handles the resulting opaque tombstone.

#include "Check.hpp"

#include <threadmaxx_migration/component.hpp>
#include <threadmaxx_migration/pipeline.hpp>

int main() {
    using namespace threadmaxx::migration;

    ComponentMigrationBridge bridge;
    // No codecs registered.

    Record r{};
    r.typeName = "Mystery";
    r.sourceVersion = SchemaVersion{1, 0, 0};

    CHECK(!bridge.hasType("Mystery"));
    CHECK(bridge.findEncoder("Mystery", SchemaVersion{1, 0, 0}) == nullptr);
    CHECK(bridge.findDecoder("Mystery", SchemaVersion{1, 0, 0}) == nullptr);

    // encode / decode both fail without a codec.
    int dummy = 0;
    CHECK(!bridge.encode("Mystery", SchemaVersion{1, 0, 0}, &dummy, r));
    CHECK(!bridge.decode(r, &dummy));

    // Pipeline-side: when the registry doesn't know the type,
    // failOnUnknownType=true causes an error (matches M5's behavior
    // even without the bridge).
    MigrationRegistry reg;
    MigrationPipeline pipeline{reg};
    RecordSet input{};
    input.records.push_back(r);
    {
        MigrationOptions opts{};  // default fail
        auto result = pipeline.migrate(input, SchemaVersion{1, 0, 0}, opts);
        CHECK(!result.ok);
        CHECK(!result.errors.empty());
    }

    // keepUnknownFields=true: the pipeline keeps the record as an
    // opaque tombstone with a warning. (Currently the pipeline reads
    // keepUnknownFields but the path also goes through failOnUnknownType
    // for the type-unknown case; this exercises the latter.)
    {
        MigrationOptions opts{};
        opts.failOnUnknownType = false;
        opts.keepUnknownFields = true;
        auto result = pipeline.migrate(input, SchemaVersion{1, 0, 0}, opts);
        CHECK(result.ok);
        CHECK(!result.warnings.empty());
        CHECK(result.warnings[0].find("tombstone") != std::string::npos);
        CHECK_EQ(result.output.records.size(), 1u);
        CHECK_EQ(result.output.records[0].typeName, std::string{"Mystery"});
    }

    // Bridge with a registered encoder but NO decoder: findDecoder
    // returns nullptr; decode fails.
    bridge.registerCodec("EncodeOnly", SchemaVersion{1, 0, 0},
                         [](const void*, Record&) { return true; },
                         /*decoder=*/nullptr);
    CHECK(bridge.hasType("EncodeOnly"));
    CHECK(bridge.findEncoder("EncodeOnly", SchemaVersion{1, 0, 0}) != nullptr);
    CHECK(bridge.findDecoder("EncodeOnly", SchemaVersion{1, 0, 0}) == nullptr);
    Record rEncode{};
    rEncode.typeName = "EncodeOnly";
    rEncode.sourceVersion = SchemaVersion{1, 0, 0};
    CHECK(!bridge.decode(rEncode, &dummy));

    EXIT_WITH_RESULT();
}
