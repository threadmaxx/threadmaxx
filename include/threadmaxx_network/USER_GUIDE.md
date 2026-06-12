# threadmaxx_network — User Guide

## Boot a 1v1 / co-op session

```cpp
#include <threadmaxx_network/threadmaxx_network.hpp>
using namespace threadmaxx::network;

auto hub = std::make_shared<LoopbackHub>();
LoopbackTransport serverTransport{hub};
LoopbackTransport clientTransport{hub};

ServerSession server{&serverTransport};
ClientSession client{&clientTransport, serverTransport.localPeer()};

client.beginHandshake();
server.pumpReceive();   // handles Hello → posts Welcome
client.pumpReceive();   // latches sessionId + assignedPeer
assert(client.connected());
```

Switch from `LoopbackTransport` to `UdpTransport::bind("0.0.0.0", port)`
for real-network play; the protocol layer is transport-agnostic.

## Send inputs

```cpp
// Per-tick, on the client:
std::array<std::byte, 4> input = encodeInput(/* WASD bits */);
client.sendInput(TickId{tick}, input);

// Once per N ticks, re-send anything the server hasn't acked yet:
client.retransmitPending();

// Server-side pump consumes them; game code drains:
server.pumpReceive();
for (auto& in : server.inputsFor(client.localPeer())) {
    applyInput(in.tick, in.bytes);
}
server.releaseInputsUpTo(client.localPeer(), highWaterMark);
```

## Snapshot replication

```cpp
ReplicationRegistry registry;
registry.registerComponent("Transform", /*fieldCount=*/3);
registry.registerComponent("Health",    /*fieldCount=*/2);
// Server and client must agree on registry.schemaHash() before
// replication begins; an off-band Welcome exchange of hashes is
// the typical place to enforce this.

// Server: build a snapshot and send fragments.
SnapshotEncoder enc;
enc.begin(TickId{currentTick});
for (auto entity : world.replicatedEntities()) {
    enc.addEntity(NetEntityId{entity.netId()},
        [&](NetEntityId, BitWriter& w) {
            encodeEntity(w, entity);  // game-side
        });
}
std::uint32_t seq = clientPeerState.localSeq;
auto fragments = enc.finishFragments(clientPeerState.session, seq);
for (auto& f : fragments) {
    serverTransport.send(clientPeer,
        PacketView{f.data(), f.size()});
}

// Client: feed fragments into the decoder.
SnapshotDecoder dec;
std::array<ReceivedPacket, 32> inbox{};
auto n = clientTransport.receive(inbox);
for (std::size_t i = 0; i < n; ++i) {
    dec.feed({inbox[i].payload.data(), inbox[i].payload.size()},
        [&](NetEntityId id, BitReader& r) {
            decodeEntity(id, r);  // game-side
        });
}
```

`SnapshotEncoder` fragments at ~1200 bytes per packet so the wire
fits inside a 1500-byte ethernet MTU. The decoder reassembles
fragments by `snapshotId`; out-of-order arrival is fine.

## Delta encoding (bandwidth win)

```cpp
DeltaEncoder enc;
enc.setBaseline(lastAckedTick, lastAckedEntities);

// Each tick:
std::uint32_t seq = clientPeerState.localSeq;
auto deltaPacket = enc.produceDelta(session, TickId{tick},
    {currentEntities.data(), currentEntities.size()}, seq);
clientTransport.send(serverPeer,
    PacketView{deltaPacket.data(), deltaPacket.size()});

// Client decodes against its own baseline:
DeltaDecoder dec;
dec.setBaseline(lastAckedTick, lastAckedEntities);
dec.feed({packet.data(), packet.size()},
    [&](NetEntityId id, BitReader& r) {
        decodeEntity(id, r);
    });
```

When the delta chain breaks (`feed` returns false because the
decoder's baseline doesn't match the encoder's), fall back to a full
snapshot resync.

## Client prediction + rewind/replay

```cpp
Reconciler reconciler{};

reconciler.setSimulate([&](TickId t, std::span<const StoredInput> in) {
    advanceGameOneTick(t, in);   // game-side simulator
});
reconciler.setApplyConfirmed([&](const StoredSnapshot& s) {
    overwriteGameStateFrom(s);   // game-side
});
reconciler.setCapture([&](TickId t) {
    PredictedSnapshot ps{};
    ps.tick = t;
    ps.entities = sampleGameState();
    return ps;
});

// Per local tick:
StoredInput myInput{/*peer=self*/, TickId{tick}, encodeInput()};
advanceGameOneTick(TickId{tick}, {&myInput, 1});
reconciler.recordTick(TickId{tick}, {&myInput, 1});

// When a confirmation arrives from the server:
StoredSnapshot confirmed = receivedFromServer();
auto result = reconciler.onConfirmed(std::move(confirmed));
switch (result) {
    case ReconcileResult::Matched:     break; // perfect prediction
    case ReconcileResult::Reconciled:  break; // game state was rewound + replayed
    case ReconcileResult::OutOfWindow: requestFullResync(); break;
    default: break;
}
```

## Interest management (MMORPG-scale)

```cpp
InterestManager im;
ClientFocus focus{};
focus.peer = clientPeer;
focus.x = playerPos.x; focus.y = playerPos.y; focus.z = playerPos.z;
focus.config.radius = 100.0f;          // world units
focus.allowList.insert(partyMemberA.value);  // bypass distance filter
im.setFocus(focus);

// Per-tick, after positions update:
std::vector<EntityPosition> world = collectAllEntityPositions();
auto vis = im.buildVisibleSet(clientPeer,
    {world.data(), world.size()});

// Send only the entities in vis.visible to this client.
// For vis.entered → emit a spawn replication entry.
// For vis.exited  → emit a despawn replication entry.
```

`InterestManager` is the gate that keeps MMORPG bandwidth bounded by
*viewport size*, not by *world size*. Without it, a 10K-entity world
would explode every client's downstream link.

## Desync diagnostics

```cpp
SyncTracker tracker{};
tracker.onDesync([&](const DesyncReport& r) {
    logger.warn("desync tick={} local={:#x} remote={:#x}",
        r.tick.value, r.localHash, r.remoteHash);
});

// Every tick, after Engine::step():
tracker.recordLocal(TickId{engine.tick()}, engine.stats().commitHash);

// When a peer reports its hash for an earlier tick:
tracker.recordRemote(TickId{theirTick}, theirHash);
```

The `commitHash` from `Engine::stats()` is the engine's deterministic
fingerprint of committed state — exactly the right primitive for
desync detection.

## Threading

- `LoopbackHub` is mutex-guarded; safe across threads.
- `UdpTransport::receive` is non-blocking and meant for sim-thread
  polling.
- `ServerSession` / `ClientSession` / `Reconciler` /
  `InterestManager` are single-threaded per-instance by convention;
  the engine's wave model gives you a natural place to call them
  (e.g. a network system at the end of the wave graph).

## Threading model in MMORPG architectures

For shards above a few hundred concurrent clients, spawn one
`ServerSession` per shard but partition `InterestManager` and
`DeltaEncoder` per region — the library's per-client state is keyed
by `PeerId`, so sharding is just routing each peer's traffic to the
worker that owns it. See `NETCODE_PATTERNS.md` for the full sharding
discussion.
