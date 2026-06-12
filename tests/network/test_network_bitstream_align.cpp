/// @file test_network_bitstream_align.cpp
/// @brief NW1 — byte-aligned writes and reads work correctly when
/// preceded by sub-byte bit operations.

#include "Check.hpp"

#include <threadmaxx_network/bitstream.hpp>

#include <array>

int main() {
    using namespace threadmaxx::network;

    BitWriter w;
    w.writeBits(0b1u, 1);    // leaves the cursor at bit 1 inside byte 0
    const std::array<std::byte, 3> blob{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    w.writeBytes(blob);      // forces alignment then writes 3 raw bytes
    w.writeBits(0b1011u, 4);

    // The byte stream should be: [bit-byte][0x11][0x22][0x33][0xB] = 5 bytes.
    CHECK_EQ(w.sizeBytes(), 5u);

    BitReader r(w.finish());
    CHECK_EQ(r.readBits(1), 1ull);

    std::array<std::byte, 3> out{};
    CHECK_EQ(r.readBytes(out), 3u);
    CHECK(out == blob);
    CHECK_EQ(r.readBits(4), 0b1011ull);

    EXIT_WITH_RESULT();
}
