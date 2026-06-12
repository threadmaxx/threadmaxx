/// @file test_network_bitstream_overflow.cpp
/// @brief NW1 — reading past the end returns exhausted() == true and
/// zero-valued reads (no UB).

#include "Check.hpp"

#include <threadmaxx_network/bitstream.hpp>

#include <array>

int main() {
    using namespace threadmaxx::network;

    BitWriter w;
    w.writeBits(0xABu, 8);

    BitReader r(w.finish());
    CHECK_EQ(r.readBits(8), 0xABull);
    CHECK(!r.exhausted());

    CHECK_EQ(r.readBits(8), 0ull);
    CHECK(r.exhausted());

    // Empty stream behaves the same.
    BitReader empty(std::span<const std::byte>{});
    CHECK_EQ(empty.readBits(4), 0ull);
    CHECK(empty.exhausted());
    CHECK_EQ(empty.readVarUInt(), 0ull);

    std::array<std::byte, 3> out{};
    BitReader short_(w.finish());
    short_.readBits(8); // consume the one byte
    CHECK_EQ(short_.readBytes(out), 0u);
    CHECK(short_.exhausted());

    EXIT_WITH_RESULT();
}
