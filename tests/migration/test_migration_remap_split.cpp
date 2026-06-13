/// @file test_migration_remap_split.cpp
/// @brief M4 — split a Transform field into "position" + "rotation"
/// using the FieldRemapWithRecord variant.

#include "Check.hpp"

#include <threadmaxx_migration/transform.hpp>

#include <algorithm>

int main() {
    using namespace threadmaxx::migration;

    Record rec{};
    rec.typeName = "Entity";
    // Encode Transform as 8 bytes: 4 position + 4 rotation.
    rec.fields.push_back({"transform",
                          FieldValue{{
                              std::byte{0x10}, std::byte{0x11},
                              std::byte{0x12}, std::byte{0x13},
                              std::byte{0x20}, std::byte{0x21},
                              std::byte{0x22}, std::byte{0x23}}}});
    rec.fields.push_back({"health",
                          FieldValue{{std::byte{0x42}}}});

    FieldRemapWithRecord split{};
    split.typeName = "Entity";
    split.field    = "transform";
    split.transform = [](Record& r) {
        // Find the existing transform field.
        auto it = std::find_if(r.fields.begin(), r.fields.end(),
                               [](const RecordField& f) {
                                   return f.name == "transform";
                               });
        if (it == r.fields.end()) return;
        auto bytes = std::move(it->value.bytes);
        // Remove the original; we'll push position + rotation.
        r.fields.erase(it);
        if (bytes.size() < 8) return;
        FieldValue pos{};
        pos.bytes.assign(bytes.begin(), bytes.begin() + 4);
        FieldValue rot{};
        rot.bytes.assign(bytes.begin() + 4, bytes.begin() + 8);
        r.fields.push_back({"position", std::move(pos)});
        r.fields.push_back({"rotation", std::move(rot)});
    };

    CHECK(applyRemap(split, rec));
    CHECK_EQ(rec.fields.size(), 3u);

    // "transform" is gone.
    auto hasField = [&](std::string_view name) {
        return std::find_if(rec.fields.begin(), rec.fields.end(),
                            [&](const RecordField& f) {
                                return f.name == name;
                            }) != rec.fields.end();
    };
    CHECK(!hasField("transform"));
    CHECK(hasField("position"));
    CHECK(hasField("rotation"));
    CHECK(hasField("health"));

    // Verify the split bytes.
    auto find = [&](std::string_view name) -> const FieldValue* {
        for (const auto& f : rec.fields) {
            if (f.name == name) return &f.value;
        }
        return nullptr;
    };
    const auto* pos = find("position");
    const auto* rot = find("rotation");
    CHECK(pos != nullptr);
    CHECK(rot != nullptr);
    CHECK_EQ(pos->bytes.size(), 4u);
    CHECK_EQ(rot->bytes.size(), 4u);
    CHECK(pos->bytes[0] == std::byte{0x10});
    CHECK(rot->bytes[0] == std::byte{0x20});

    // typeName mismatch is a no-op.
    Record other{};
    other.typeName = "Other";
    other.fields.push_back({"transform",
                            FieldValue{{std::byte{0x01}}}});
    CHECK(!applyRemap(split, other));
    CHECK_EQ(other.fields.size(), 1u);

    EXIT_WITH_RESULT();
}
