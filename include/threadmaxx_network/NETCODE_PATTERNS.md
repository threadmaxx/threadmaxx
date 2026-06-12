# Netcode patterns — when to use what

`threadmaxx_network` ships the primitives. This doc maps game shapes
to netcode choices.

## Decision tree

```
┌──────────────────────────┐
│ How many concurrent peers│
│ touch one simulation?    │
└────────────┬─────────────┘
             │
   ┌─────────┴──────────┬──────────┐
  2-8                  8-256       1000+
  (fighter/co-op)   (shooter/RTS) (MMORPG)
   │                  │            │
┌──┴─────────────┐ ┌──┴──────────┐ ┌──┴─────────────────┐
│ deterministic  │ │ snapshot +  │ │ snapshot + delta + │
│ rollback       │ │ delta       │ │ interest management│
│ + input share  │ │ + prediction│ │ + sharding         │
└────────────────┘ └─────────────┘ └────────────────────┘
```

## 1v1 fighter — rollback netcode

**Use:** `RollbackBuffer` + `Reconciler` + `ClientSession`. Two peers,
each runs the same deterministic engine. They exchange inputs every
tick. On misprediction (peer's actual input arrived later than
expected), rewind locally and replay.

**Why rollback, not snapshot:** fighting games can't tolerate the
50-150ms snapshot interpolation lag. Rollback hides latency by
predicting + correcting.

**Lock to:** `Engine::stats().commitHash` validation. If hashes
diverge after a confirmed input sequence, you have a determinism
bug (not a network bug) — find and fix; the netcode is doing its job.

**Library wiring:**

```cpp
RollbackConfig cfg{};
cfg.historyTicks = 30;     // ~half a second at 60 Hz
cfg.maxRollbackTicks = 8;  // hard cap on a single rewind
Reconciler reconciler{cfg};
// ... wire simulate/applyConfirmed/capture hooks, sendInput on each tick.
```

## Small co-op (2-16 peers) — server-authoritative snapshots

**Use:** server-authoritative `ServerSession` + `ClientSession`,
periodic snapshots (no delta needed at this scale), per-client
prediction for the local avatar.

**Why server-authoritative:** prevents trivial cheats (clients can't
declare their own state). `ClientSession::sendInput` is the only
upstream traffic; `SnapshotEncoder` runs server-side at 20-30 Hz.

**Library wiring:**

```cpp
SnapshotEncoder enc;
enc.begin(TickId{tick});
for (auto& e : world.allReplicatedEntities()) {
    enc.addEntity(NetEntityId{e.netId()},
        [&](NetEntityId, BitWriter& w) { encodeEntity(w, e); });
}
auto fragments = enc.finishFragments(session, seq);
```

`InterestManager` is overkill here — every client sees every entity.
Skip it.

## Medium-scale (16-256 peers) — snapshot + delta

**Use:** add `DeltaEncoder` on top of the snapshot model. Server
sends one full snapshot every N ticks (the "keyframe"); deltas every
tick in between.

**Why delta:** at 256 peers, full snapshots become bandwidth-bound.
Delta drops the per-tick payload by 5-50x for typical scenes where
1-5% of entities mutate per tick.

**Library wiring:**

```cpp
DeltaEncoder enc;
enc.setBaseline(lastAckedTick, lastAckedSnapshot);
auto packet = enc.produceDelta(session, TickId{tick},
    {current.data(), current.size()}, seq);
// If client's baseline goes stale (no ack within window), fall back
// to a full snapshot.
```

Add a periodic full-snapshot keyframe (every ~2s) so newly-connected
clients sync in bounded time.

## Large-scale MMORPG (1000+ peers per shard) — full stack

**Use:** the entire kit:

1. `ServerSession` + `UdpTransport` per shard.
2. `InterestManager` per client — *critical*. Without it, downstream
   bandwidth scales with world size; with it, it scales with viewport
   size.
3. `DeltaEncoder` per client. Each client has its own baseline and
   its own per-tick delta.
4. `RollbackBuffer` server-side for forensics and replay.
5. `SyncTracker` server-wide for desync detection (forensics; you
   don't reconcile in an MMORPG — you kick the desyncing peer and
   request resync).

**Sharding:** the library is per-`PeerId` keyed. To scale beyond a
single thread, partition peers across worker threads:

```cpp
// One worker owns peer p:
auto& worker = workers[p.value % workers.size()];
worker.queue.push({p, packet});
```

Each worker holds its own `ServerSession`, `InterestManager`, and
per-client `DeltaEncoder` slot. Cross-shard entities replicate via a
small "boundary" set you copy between shards each tick.

**Bandwidth budget per client (typical):**

- snapshot keyframe every 2s: ~50KB (compressed delta: ~20-50 bytes
  per entity).
- per-tick delta at 20Hz: ~2-5KB visible entities × ~30-50 bytes
  changed per tick × 0.05 dirty rate ≈ 5-15KB/s downstream per
  client.
- upstream: 1-2KB/s of input packets.

These are healthy enough that a single 1Gbps egress link handles
~50-100K concurrent clients (the real bottleneck is CPU, not
bandwidth).

## Cheating considerations

The library doesn't ship anti-cheat. But the architecture makes
cheating harder:

- All client edits go through `ClientSession::sendInput`. The server
  validates inputs against game rules in its simulation pass; clients
  can't fabricate world state.
- `SyncTracker` catches state divergence — a client running modified
  code drifts from the server's commitHash quickly.
- The wire protocol has no "tell server my position" path. Position
  is only ever computed by the server from validated inputs.

Game-layer concerns (account auth, signed clients, server-side
validation rules) live above `threadmaxx_network`.
