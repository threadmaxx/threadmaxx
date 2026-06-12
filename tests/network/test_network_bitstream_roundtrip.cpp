/// @file test_network_bitstream_roundtrip.cpp
/// @brief NW1 — write {bits=3, varuint, byte-blob, bits=11}, read
/// back, recover exact values.

#include "Check.hpp"

#include <threadmaxx_network/bitstream.hpp>

#include <array>
#include <cstdint>

int main() {
    using namespace threadmaxx::network;

    BitWriter w;
    w.writeBits(0b101u, 3);
    w.writeVarUInt(123456789ull);
    const std::array<std::byte, 4> blob{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    w.writeBytes(blob);
    w.writeBits(0b10101010101u, 11);

    BitReader r(w.finish());
    CHECK_EQ(r.readBits(3), 0b101ull);
    CHECK_EQ(r.readVarUInt(), 123456789ull);
    std::array<std::byte, 4> blobOut{};
    CHECK_EQ(r.readBytes(blobOut), 4u);
    CHECK(blobOut == blob);
    CHECK_EQ(r.readBits(11), 0b10101010101ull);
    CHECK(!r.exhausted());

    EXIT_WITH_RESULT();
}
