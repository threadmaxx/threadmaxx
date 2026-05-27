#pragma once

// Header-only parser for the TOU .SHP file header. Lives in
// `examples/tou2d/` so both the importer CLI and the matching test
// (tests/tou2d_shp_import_test.cpp) can include the same source of
// truth without dragging in the rest of the engine.
//
// The parser is intentionally narrow — it decodes only the bytes the
// reverse-engineering pass has confirmed:
//   * Leading 0x00 padding / version byte.
//   * NUL-terminated ASCII display name at offset 1.
//   * 3-byte stat triplet immediately after the name terminator.
// Everything past the triplet is opaque body and lives in body.bin.
//
// See TOU_PLAN.md § 3.0 for the inventory of stock-ship files and the
// open questions about the stat encoding.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace tou2d::shp {

/// Defensive cap on display-name length — the manual's longest stock
/// ship name ("Imperium Tie Fighter") is 20 chars. 64 leaves plenty of
/// headroom for fan-made ships while still bounding work for malformed
/// input.
inline constexpr std::size_t kNameMax = 64;

/// Decoded header fields. Anything not listed here lives in body.bin
/// (presumed sprite frames; format unresolved).
struct ParsedHeader {
    std::string                 displayName;
    std::array<std::uint8_t, 3> statTriplet{};
    std::size_t                 payloadStart = 0;
};

/// Pure parser — no I/O. Returns true on success; on failure the
/// out-param is left in an unspecified state. See file-header comment
/// for the validation rules.
inline bool parseHeader(std::span<const std::uint8_t> data,
                        ParsedHeader& out) {
    if (data.size() < 5)         return false;
    if (data[0] != 0x00)         return false;

    std::size_t nameEnd = 1;
    while (nameEnd < data.size() &&
           nameEnd < 1 + kNameMax &&
           data[nameEnd] != 0x00) {
        const std::uint8_t b = data[nameEnd];
        if (b < 0x20 || b > 0x7E) return false;
        ++nameEnd;
    }
    if (nameEnd >= 1 + kNameMax)        return false;
    if (nameEnd >= data.size())         return false;
    if (data[nameEnd] != 0x00)          return false;
    if (nameEnd == 1)                   return false;  // empty name
    if (nameEnd + 3 >= data.size())     return false;

    out.displayName.assign(reinterpret_cast<const char*>(data.data() + 1),
                           nameEnd - 1);
    out.statTriplet[0] = data[nameEnd + 1];
    out.statTriplet[1] = data[nameEnd + 2];
    out.statTriplet[2] = data[nameEnd + 3];
    out.payloadStart   = nameEnd + 4;
    return true;
}

} // namespace tou2d::shp
