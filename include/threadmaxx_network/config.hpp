#pragma once

/// @file config.hpp
/// @brief Runtime settings for ServerSession / ClientSession.

#include <cstdint>

namespace threadmaxx::network {

struct NetworkConfig {
    /// @brief Server-assigned session salt for `Welcome.serverSalt`.
    /// `0` makes the server pick a deterministic salt from a small
    /// counter (useful for tests).
    std::uint64_t serverSeed{0};

    /// @brief Soft cap on simultaneous connected peers (server side).
    /// Hello packets past this cap are dropped silently.
    std::uint32_t maxPeers{64};

    /// @brief Window of recently-acked sequences carried in
    /// `PacketHeader::ackBits`. v1.0 is 32.
    std::uint32_t ackWindow{32};
};

} // namespace threadmaxx::network
