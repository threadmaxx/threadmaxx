/// @file test_studio_agent_send_snapshot.cpp
/// @brief ST29 — round-trip: studio side sends GetEngineSnapshot
/// across a LoopbackHub; StudioAgent pumps it, calls the bound
/// IStudioDataSource, ships the response back. Studio side decodes
/// and asserts every field.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>
#include <optional>

namespace {

using namespace threadmaxx;

class StubDataSource final : public studio::IStudioDataSource {
public:
    explicit StubDataSource(studio::EngineFrameSummary s) : summary_(s) {}

    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }

    std::optional<studio::EngineFrameSummary>
    engineSnapshot() const override {
        return summary_;
    }

private:
    studio::EngineFrameSummary summary_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    studio::EngineFrameSummary expected{};
    expected.tick = 1234;
    expected.lastStepSeconds = 0.012345;
    expected.paused = true;
    expected.systemCount = 7;
    expected.workerCount = 4;

    StubDataSource source{expected};

    auto hub = std::make_shared<network::LoopbackHub>();
    network::LoopbackTransport agentTransport{hub};
    network::LoopbackTransport studioTransport{hub};

    studio::StudioAgent agent{agentTransport, source};

    // Studio side issues the request.
    constexpr std::uint32_t kRequestId = 0xC0FFEEu;
    auto requestBytes = studio::encodeGetEngineSnapshotRequest(kRequestId);
    network::PacketView reqView{requestBytes.data(), requestBytes.size()};
    CHECK(studioTransport.send(agentTransport.localPeer(), reqView));

    // Agent pumps the request and ships a response.
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.requestsHandled(), 1u);
    CHECK(agent.bytesSent() > 0u);

    // Studio side drains the response.
    std::array<network::ReceivedPacket, 4> inbox{};
    const auto got = studioTransport.receive(inbox);
    CHECK_EQ(got, 1u);
    CHECK(inbox[0].peer == agentTransport.localPeer());

    auto decoded = studio::decodeAgentResponse(
        std::span<const std::byte>{inbox[0].payload.data(),
                                   inbox[0].payload.size()});
    CHECK(decoded.has_value());
    CHECK(decoded->tag == studio::AgentResponseTag::EngineSnapshot);
    CHECK_EQ(decoded->requestId, kRequestId);
    CHECK(decoded->ok);

    const auto& summary = decoded->engineSnapshot;
    CHECK_EQ(summary.tick, expected.tick);
    CHECK_EQ(summary.systemCount, expected.systemCount);
    CHECK_EQ(summary.workerCount, expected.workerCount);
    CHECK(summary.paused == expected.paused);
    // double comparison: tight tolerance, host-endian round-trip is exact
    CHECK(summary.lastStepSeconds == expected.lastStepSeconds);

    // Empty pump returns 0.
    CHECK_EQ(agent.pump(), 0u);

    // Nullopt source path: response carries ok=0, no payload.
    class NullSource final : public studio::IStudioDataSource {
    public:
        studio::AttachMode mode() const noexcept override {
            return studio::AttachMode::Direct;
        }
    };
    NullSource nullSource;
    studio::StudioAgent nullAgent{agentTransport, nullSource};
    auto req2 = studio::encodeGetEngineSnapshotRequest(0x42u);
    CHECK(studioTransport.send(agentTransport.localPeer(),
                               network::PacketView{req2.data(), req2.size()}));
    CHECK_EQ(nullAgent.pump(), 1u);

    const auto got2 = studioTransport.receive(inbox);
    CHECK_EQ(got2, 1u);
    auto decoded2 = studio::decodeAgentResponse(
        std::span<const std::byte>{inbox[0].payload.data(),
                                   inbox[0].payload.size()});
    CHECK(decoded2.has_value());
    CHECK_EQ(decoded2->requestId, 0x42u);
    CHECK(!decoded2->ok);

    EXIT_WITH_RESULT();
}
