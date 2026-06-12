/// @file test_network_bitstream_varuint.cpp
/// @brief NW1 — varuint encoding round-trips at every byte-width
/// boundary (1, 2, 3, 4, 5, 6, 7, 8, 9-byte ranges).

#include "Check.hpp"

#include <threadmaxx_network/bitstream.hpp>

#include <cstdint>

namespace {

void roundTrip(std::uint64_t v) {
    threadmaxx::network::BitWriter w;
    w.writeVarUInt(v);
    threadmaxx::network::BitReader r(w.finish());
    CHECK_EQ(r.readVarUInt(), v);
    CHECK(!r.exhausted());
}

std::size_t byteWidth(std::uint64_t v) {
    threadmaxx::network::BitWriter w;
    w.writeVarUInt(v);
    return w.sizeBytes();
}

} // namespace

int main() {
    // 1-byte: 0..127.
    roundTrip(0);
    roundTrip(127);
    CHECK_EQ(byteWidth(0), 1u);
    CHECK_EQ(byteWidth(127), 1u);

    // 2-byte: 128..16383.
    roundTrip(128);
    roundTrip(16383);
    CHECK_EQ(byteWidth(128), 2u);
    CHECK_EQ(byteWidth(16383), 2u);

    // 3-byte: 16384..2097151.
    roundTrip(16384);
    roundTrip(2097151);
    CHECK_EQ(byteWidth(2097151), 3u);

    // 4-byte boundary.
    roundTrip(2097152);
    CHECK_EQ(byteWidth(2097152), 4u);

    // Big values.
    roundTrip(0xFFFFFFFFull);
    roundTrip(0xFFFFFFFFFFFFFFFFull);
    CHECK_EQ(byteWidth(0xFFFFFFFFFFFFFFFFull), 10u);

    EXIT_WITH_RESULT();
}
