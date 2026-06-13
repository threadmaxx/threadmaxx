/// @file test_studio_multi_shard_pick.cpp
/// @brief ST34 — ShardDirectory enumerates the available shards;
/// ShardPickerPanel selects one + the resulting RemoteDataSource
/// connects to the chosen shard's agent.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/panels/shard_picker.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>
#include <threadmaxx_studio/shard_directory.hpp>

#include <threadmaxx_editor/backends/headless.hpp>

#include <threadmaxx_network/transport.hpp>

#include <memory>
#include <optional>

namespace {

using namespace threadmaxx;

class StubDataSource final : public studio::IStudioDataSource {
public:
    explicit StubDataSource(std::uint64_t tick) : tick_(tick) {}
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
    std::optional<studio::EngineFrameSummary>
    engineSnapshot() const override {
        studio::EngineFrameSummary s{};
        s.tick = tick_;
        return s;
    }
private:
    std::uint64_t tick_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    auto hub = std::make_shared<network::LoopbackHub>();

    // Three shards, each with its own agent + data source.
    network::LoopbackTransport agentA{hub};
    network::LoopbackTransport agentB{hub};
    network::LoopbackTransport agentC{hub};
    StubDataSource sourceA{101};
    StubDataSource sourceB{202};
    StubDataSource sourceC{303};
    studio::StudioAgent shardA{agentA, sourceA}; shardA.setAttachEnabled(true);
    studio::StudioAgent shardB{agentB, sourceB}; shardB.setAttachEnabled(true);
    studio::StudioAgent shardC{agentC, sourceC}; shardC.setAttachEnabled(true);

    // The studio-side transport.
    network::LoopbackTransport studio{hub};

    // Populate the directory.
    studio::ShardDirectory dir;
    studio::ShardInfo infoA{}; infoA.name = "eu-west-01";
    infoA.agentPeer = agentA.localPeer(); infoA.alive = true;
    studio::ShardInfo infoB{}; infoB.name = "eu-east-01";
    infoB.agentPeer = agentB.localPeer(); infoB.alive = true;
    studio::ShardInfo infoC{}; infoC.name = "us-west-01";
    infoC.agentPeer = agentC.localPeer(); infoC.alive = false;

    CHECK_EQ(dir.addShard(infoA), 0u);
    CHECK_EQ(dir.addShard(infoB), 1u);
    CHECK_EQ(dir.addShard(infoC), 2u);
    CHECK_EQ(dir.shardCount(), 3u);
    CHECK_EQ(dir.aliveCount(), 2u);
    CHECK(!dir.selectedIndex().has_value());
    CHECK(dir.selected() == nullptr);

    // Out-of-range select returns false, selection unchanged.
    CHECK(!dir.select(7));
    CHECK(!dir.selectedIndex().has_value());

    // selectByName for a missing name: same.
    CHECK(!dir.selectByName("ap-south-01"));
    CHECK(!dir.selectedIndex().has_value());

    // Pick shard B through the panel.
    studio::ShardPickerPanel panel{dir};
    CHECK(panel.pickShardByName("eu-east-01"));
    CHECK(dir.selectedIndex().has_value());
    CHECK_EQ(*dir.selectedIndex(), 1u);
    CHECK(dir.selected() != nullptr);
    CHECK_EQ(dir.selected()->name, std::string{"eu-east-01"});

    // Build a RemoteDataSource against the selected shard. Verify the
    // round-trip reaches the right backend (sourceB → tick 202).
    studio::RemoteDataSource remote{studio, dir.selected()->agentPeer};
    remote.requestEngineSnapshot();
    CHECK_EQ(shardA.pump(), 0u);  // nothing for A
    CHECK_EQ(shardB.pump(), 1u);  // B handled it
    CHECK_EQ(shardC.pump(), 0u);
    CHECK_EQ(remote.pump(), 1u);
    auto snap = remote.engineSnapshot();
    CHECK(snap.has_value());
    CHECK_EQ(snap->tick, 202u);

    // Re-pick shard A and rebuild — the new RemoteDataSource talks to
    // the right backend (sourceA → tick 101).
    CHECK(panel.pickShard(0));
    studio::RemoteDataSource remoteA{studio, dir.selected()->agentPeer};
    remoteA.requestEngineSnapshot();
    CHECK_EQ(shardA.pump(), 1u);
    CHECK_EQ(shardB.pump(), 0u);
    CHECK_EQ(remoteA.pump(), 1u);
    auto snapA = remoteA.engineSnapshot();
    CHECK(snapA.has_value());
    CHECK_EQ(snapA->tick, 101u);

    // markAlive flips liveness; aliveCount reflects it.
    dir.markAlive(2, true);
    CHECK_EQ(dir.aliveCount(), 3u);
    dir.markAlive(0, false);
    CHECK_EQ(dir.aliveCount(), 2u);

    // clearSelection drops the active pick.
    panel.clearSelection();
    CHECK(!dir.selectedIndex().has_value());
    CHECK(dir.selected() == nullptr);

    // Smoke-render via headless backend.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    StubDataSource dummy{0};
    panel.render(headless, dummy);
    CHECK(panel.rowCount() > 0u);

    EXIT_WITH_RESULT();
}
