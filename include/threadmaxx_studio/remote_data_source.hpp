#pragma once

/// @file remote_data_source.hpp
/// @brief ST30 — `RemoteDataSource`: the studio-side endpoint of the
/// M7 out-of-process attach pair. Reaches a `StudioAgent` (ST29) over
/// any `network::ITransport`.
///
/// **Cache layer**: panels poll `engineSnapshot()` once per frame and
/// MUST NOT pay a round-trip per accessor. The data source caches
/// every response on `pump()` and returns the last-cached value
/// thereafter. Panels that need a fresh sample call
/// `requestEngineSnapshot()` (typically once per studio frame from
/// the panel host); the response lands on a later `pump()`.
///
/// **Graceful degradation**: every accessor returns `std::nullopt`
/// while the cache is cold (no response received yet, or after a
/// transport reset). Panels MUST render a placeholder rather than
/// crash — same contract as the rest of `IStudioDataSource`.
///
/// **Lifetime**: caller owns the transport. The data source does NOT
/// take ownership.

#include "data_source.hpp"

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/transport.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace threadmaxx::studio {

class RemoteDataSource final : public IStudioDataSource {
public:
    /// @brief Bind to a transport and the agent's `PeerId`. Both must
    /// outlive the data source.
    RemoteDataSource(network::ITransport& transport,
                     network::PeerId agentPeer) noexcept;

    AttachMode mode() const noexcept override { return AttachMode::Remote; }

    /// @brief Latest cached engine summary, or `nullopt` if no
    /// response has been received yet.
    std::optional<EngineFrameSummary> engineSnapshot() const override;

    /// @brief Queue a GetEngineSnapshot request to the agent.
    /// Returns the requestId. The response lands on a later `pump()`.
    std::uint32_t requestEngineSnapshot();

    /// @brief Ship a SubmitCommand request to the agent. The agent's
    /// registered factory for @p label produces the concrete
    /// `editor::IEditCommand` on the game side and pushes it through
    /// the agent's `editor::CommandStack`. Returns `true` if the
    /// request bytes were successfully posted to the transport;
    /// acceptance (factory found + command non-null) is reported back
    /// asynchronously via `lastCommandAccepted()` after `pump()`.
    bool submitCommand(std::string_view label) override;

    /// @brief Last requestId issued by `submitCommand`. Zero if none.
    [[nodiscard]] std::uint32_t lastCommandRequestId() const noexcept {
        return lastCommandRequestId_;
    }

    /// @brief Most-recent CommandResult.ok bit received from the agent.
    /// Defaults to `false` until the first response lands. Useful for
    /// the bandwidth panel (ST33) + this round-trip test.
    [[nodiscard]] bool lastCommandAccepted() const noexcept {
        return lastCommandAccepted_;
    }

    /// @brief Cumulative CommandResult responses received.
    [[nodiscard]] std::size_t commandResponsesReceived() const noexcept {
        return commandResponsesReceived_;
    }

    /// @brief Drain the transport, decode every response, and update
    /// the cache. Returns the number of responses processed this
    /// call. Unknown / truncated frames are silently dropped — the
    /// transport may legitimately deliver garbage on a UDP wire,
    /// and ST32 layers a session check on top.
    std::size_t pump();

    /// @brief Total responses received since construction.
    [[nodiscard]] std::size_t responsesReceived() const noexcept {
        return responsesReceived_;
    }

    /// @brief Total bytes received since construction. Useful for
    /// ST33's bandwidth panel.
    [[nodiscard]] std::size_t bytesReceived() const noexcept {
        return bytesReceived_;
    }

    /// @brief Last requestId issued by `requestEngineSnapshot`.
    /// Zero if none issued yet.
    [[nodiscard]] std::uint32_t lastEngineSnapshotRequestId() const noexcept {
        return lastEngineSnapshotRequestId_;
    }

    /// @brief Drop every cached value. The next accessor returns
    /// `nullopt` until a fresh pump.
    void invalidateCache() noexcept;

private:
    network::ITransport* transport_{nullptr};
    network::PeerId      agentPeer_{};
    std::uint32_t        nextRequestId_{1};

    mutable std::optional<EngineFrameSummary> engineSnapshotCache_;
    std::uint32_t lastEngineSnapshotRequestId_{0};

    std::uint32_t lastCommandRequestId_{0};
    bool          lastCommandAccepted_{false};
    std::size_t   commandResponsesReceived_{0};

    std::size_t responsesReceived_{0};
    std::size_t bytesReceived_{0};
};

} // namespace threadmaxx::studio
