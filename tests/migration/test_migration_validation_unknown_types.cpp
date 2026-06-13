/// @file test_migration_validation_unknown_types.cpp
/// @brief M8 — validate() reports unknown types as warnings.

#include "Check.hpp"

#include <threadmaxx_migration/validation.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Known", SchemaVersion{1, 0, 0});

    RecordSet set{};
    auto mk = [&](std::string t, std::uint64_t id) {
        Record r{};
        r.typeName = std::move(t);
        r.stableId = id;
        r.sourceVersion = SchemaVersion{1, 0, 0};
        return r;
    };
    set.records.push_back(mk("Known", 1));
    set.records.push_back(mk("Mystery", 2));
    set.records.push_back(mk("MoreUnknown", 3));
    set.records.push_back(mk("Mystery", 4));   // duplicate dedup
    set.records.push_back(mk("ThirdUnknown", 5));

    auto report = validate(set, reg, SchemaVersion{1, 0, 0});
    CHECK(report.ok);  // warnings only, no errors
    // 3 distinct unknowns (Mystery dedup'd).
    CHECK_EQ(report.warnings.size(), 3u);

    auto names = collectUnknownTypes(set, reg);
    CHECK_EQ(names.size(), 3u);
    // Names present in some order.
    bool sawMystery = false, sawMore = false, sawThird = false;
    for (const auto& n : names) {
        if (n == "Mystery")      sawMystery = true;
        if (n == "MoreUnknown")  sawMore = true;
        if (n == "ThirdUnknown") sawThird = true;
    }
    CHECK(sawMystery);
    CHECK(sawMore);
    CHECK(sawThird);

    EXIT_WITH_RESULT();
}
