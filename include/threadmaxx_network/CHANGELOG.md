# threadmaxx_network — Changelog

## v1.0.0 — 2026-06-12

First stable release. Every NW-batch (NW1-NW10) landed; full network
suite (33 tests) green on `build/` and `build-werror/`.

### Added

- `BitWriter` / `BitReader` — bit-packed little-endian wire codec
  with varuint + byte-aligned blob support.
- `PeerId` / `SessionId` / `TickId` / `NetEntityId` — wire identifier
  PODs.
- `ITransport` + `LoopbackTransport` (deterministic in-process) +
  POSIX `UdpTransport`.
- `LoopbackHub` with `TransportProfile` (loss / reorder / duplication
  rates + RNG seed) for hermetic, deterministic network simulation
  in tests.
- `PacketHeader` (30 bytes, fixed layout), 12-variant `PacketType`
  enum, Hello/Welcome handshake.
- `ServerSession` / `ClientSession` — handshake, per-peer sequence +
  ack tracking, `sendInput(tick, bytes)` with retransmit window,
  server-side input dispatch via `inputsFor` / `releaseInputsUpTo`.
- `ReplicationRegistry` — schema-hash-based detect of server/client
  schema drift.
- `SnapshotEncoder` / `SnapshotDecoder` — MTU-aware fragmentation
  (~1200 bytes per fragment), reassembly by `(snapshotId, fragmentIdx)`.
- `DeltaEncoder` / `DeltaDecoder` — entity-level deltas against an
  acked baseline; ≥5x bandwidth win at 100-entity scenes with 10%
  per-tick mutation.
- `RollbackBuffer` — input / snapshot / event history rings,
  capacity-bounded by `historyTicks`.
- `Reconciler` — client prediction; on misprediction, rewinds via
  `applyConfirmed` + replays `simulate(t, inputs)` for ticks in the
  (confirmed, predicted] window; `OutOfWindow` for confirmations
  past the history horizon.
- `InterestManager` — distance-based AOI filter + per-client
  explicit allow-list + enter/exit deltas.
- `SyncTracker` — per-tick commit-hash desync detection.
- `version.hpp` — `THREADMAXX_NETWORK_VERSION` + `version_string()`.

### Library scope

| Game shape | Recommended stack |
|---|---|
| 1v1 fighter | `RollbackBuffer` + `Reconciler` |
| Small co-op (≤16) | `ServerSession` + snapshots |
| Mid-scale (≤256) | + `DeltaEncoder` |
| MMORPG (≥1000) | + `InterestManager` (per-client AOI) |

### Out of v1.0 (deferred to v1.x)

- QUIC transport (`v1.x` — modern multiplexed alternative to UDP).
- Windows UdpTransport (`v1.x` — Winsock 2 mirror).
- `LockstepSession` convenience wrapper for deterministic-lockstep
  games.
- Bandwidth scheduler (priority queue per-client).
- Trace / replay tooling (record packets, replay deterministically).
- Per-field bit packing on top of NW6 deltas.
- Reflection-driven schema codegen (waits on C++26 reflection).
- Cross-process loopback transport for split-screen / local-coop.

These are scope, not stability, decisions; the v1.0 wire format is
the contract.
