# threadmaxx_network

**Networking, replication, and rollback sibling library for `threadmaxx`.**

`threadmaxx_network` is the wire protocol + runtime that turns a
deterministic single-process `threadmaxx` simulation into one that
runs across the network. It targets the full spectrum from 1v1
fighters to small co-op (≤16 peers) to large-scale MMORPGs (≥1000
concurrent clients per shard).

## Status

**v1.0.0 shipped 2026-06-12.** Every batch NW1-NW10 landed; full
network suite is 33/33 green on `build/` and `build-werror/`.

## What ships at v1.0

| Header | Purpose |
|---|---|
| `bitstream.hpp` | `BitWriter` / `BitReader` — little-endian, bit-packed varuint codec. |
| `ids.hpp` | `PeerId` / `SessionId` / `TickId` / `NetEntityId` — wire identifier POD types. |
| `packets.hpp` | `PacketType`, 30-byte `PacketHeader`, `HelloPayload` / `WelcomePayload` / `InputPayload` writers. |
| `config.hpp` | `NetworkConfig` — `maxPeers`, `ackWindow`, server seed. |
| `transport.hpp` | `ITransport`, `LoopbackTransport` + `LoopbackHub` (deterministic in-process testing). |
| `udp_transport.hpp` | POSIX `UdpTransport` (opt-in via `THREADMAXX_NETWORK_HAS_UDP=1`). |
| `session.hpp` | `ServerSession` / `ClientSession` — handshake, input dispatch, ack tracking. |
| `replication.hpp` | `SnapshotEncoder` / `SnapshotDecoder`, `ReplicationRegistry`, MTU-aware fragmentation. |
| `delta.hpp` | `DeltaEncoder` / `DeltaDecoder` — entity-level deltas vs. acked baseline. |
| `rollback.hpp` | `RollbackBuffer` — input / snapshot / event history rings. |
| `prediction.hpp` | `Reconciler` — client prediction + rewind/replay. |
| `interest.hpp` | `InterestManager` — per-client AOI filter with enter/exit deltas. |
| `diagnostics.hpp` | `SyncTracker` — commit-hash desync detection. |
| `version.hpp` | `THREADMAXX_NETWORK_VERSION` + `version_string()`. |

## Game scale support

The library composes; you pick the layers that suit your game:

- **1v1 fighter (rollback netcode):** `RollbackBuffer` +
  `Reconciler` + `ClientSession::sendInput` over `LoopbackTransport`
  (LAN / single host) or `UdpTransport` (remote). No snapshot stream
  needed — peers exchange inputs and rewind locally on misprediction.
- **Small co-op (≤16 peers, P2P-ish):** Same as above, plus a
  `ServerSession`-style authoritative host (one peer also runs the
  server session) for cheat prevention and tie-break.
- **Medium-scale (≤256 peers, lobby shooters):** Snapshot +
  `DeltaEncoder` over UDP. Server-authoritative; each client predicts
  its own avatar via `Reconciler`. No interest management needed at
  this scale (everyone can see everyone).
- **Large-scale MMORPG (≥1000 peers per shard):**
  `InterestManager::buildVisibleSet(peer, world)` filters the
  per-client snapshot to the ~100-entity slice each player cares about,
  keeping outbound bandwidth flat with player count rather than world
  size. `DeltaEncoder` sustains the per-client stream; `SyncTracker`
  flags desync cases for forensics.

See `USER_GUIDE.md` for full code walkthroughs and
`NETCODE_PATTERNS.md` for which netcode model to pick.

## Building

`threadmaxx_network` is opted in by `-DTHREADMAXX_BUILD_NETWORK=ON`
(the default). Static library `threadmaxx::network`; no PUBLIC
dependency on the engine (per DESIGN_NOTES §2.1 — engine-agnostic by
design). Game code bridges via the `SimulationBridge` functors and
`EntityWriter` / `EntityReader` callbacks.
