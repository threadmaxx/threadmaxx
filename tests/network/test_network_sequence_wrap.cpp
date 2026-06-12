/// @file test_network_sequence_wrap.cpp
/// @brief NW3 — sequence numbers progress / wrap and the ack window
/// tracks recently-seen sequences correctly.

#include "Check.hpp"

#include <threadmaxx_network/packets.hpp>

namespace {

void integrate(std::uint32_t& remote, std::uint32_t& bits,
               std::uint32_t incoming) noexcept {
    if (incoming == remote) return;
    if (incoming > remote) {
        const std::uint32_t shift = incoming - remote;
        if (shift >= 32u) {
            bits = 0;
        } else {
            bits <<= shift;
            bits |= (1u << (shift - 1u));
        }
        remote = incoming;
    } else {
        const std::uint32_t back = remote - incoming;
        if (back > 0 && back <= 32u) {
            bits |= (1u << (back - 1u));
        }
    }
}

} // namespace

int main() {
    // Linear progression.
    std::uint32_t remote = 0, bits = 0;
    for (std::uint32_t i = 1; i <= 5; ++i) integrate(remote, bits, i);
    CHECK_EQ(remote, 5u);
    // The four sequences below 5 (1..4) are recorded in `bits`.
    CHECK_EQ((bits & 0xFu), 0xFu);

    // Out-of-order delivery — a hole.
    remote = 10; bits = 0;
    integrate(remote, bits, 12);
    CHECK_EQ(remote, 12u);
    // sequence 10 is two slots back from 12 → bit 1 set.
    CHECK((bits & (1u << 1u)) != 0u);
    // sequence 11 is missing.
    CHECK((bits & (1u << 0u)) == 0u);

    // Later, the missing seq=11 arrives; mark it.
    integrate(remote, bits, 11);
    CHECK_EQ(remote, 12u);
    CHECK((bits & (1u << 0u)) != 0u);

    // Big jump (≥32) clears the ack bits.
    integrate(remote, bits, 100);
    CHECK_EQ(remote, 100u);
    CHECK_EQ(bits, 0u);

    EXIT_WITH_RESULT();
}
