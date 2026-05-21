# `threadmaxx_network` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **not started**. All batches are 📋 planned. Sequencing
follows the §10 "implementation order" of the design notes,
regrouped into shippable units that each carry their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

The library produces a static library `threadmaxx::network` plus
its public headers. Two transports ship at v1.0: `LoopbackTransport`
(in-process, deterministic, used by every test) and `UdpTransport`
(real sockets, gated by platform availability). Each batch's tests
run against `LoopbackTransport` so the suite stays hermetic and
deterministic.

The engine's deterministic `commitHash` (§3.10.6 batch 30) is the
load-bearing primitive this library leans on for desync detection,
rollback validation, and snapshot agreement. Every batch that
touches replication must preserve the contract that "same committed
state → same hash → same authoritative truth."

## Library structure (target end-state)

```
include/threadmaxx_network/
  threadmaxx_network.hpp   # umbrella
  config.hpp               # protocol settings
  ids.hpp                  # PeerId / SessionId / NetEntityId / TickId
  transport.hpp            # ITransport
  packets.hpp              # PacketHeader / PacketType
  bitstream.hpp            # BitWriter / BitReader
  clock_sync.hpp           # tick sync / RTT / drift
  authority.hpp            # OwnershipRule / Authority enum
  replication.hpp          # ReplicationRegistry / dirty tracking
  rollback.hpp             # RollbackConfig / history buffers
  prediction.hpp           # client prediction / reconciliation
  interest.hpp             # AOI / relevance filters
  serialization.hpp        # codec registry
  diagnostics.hpp          # desync reports / counters
  detail/
    crc32.hpp
    delta_encoder.hpp
    ring_buffer.hpp
    packet_pool.hpp
src/threadmaxx_network/
  ReplicationRegistry.cpp
  ServerSession.cpp
  ClientSession.cpp
  RollbackBuffer.cpp
  Reconciler.cpp
  InterestManager.cpp
  transport_loopback.cpp
  transport_udp.cpp        # POSIX sockets; Windows variant later
tests/network/
  test_network_*.cpp
bench/
  network_*.cpp
```

## Batch NW1 — Bitstream codec

**Goal**: `BitWriter` / `BitReader` with bit-packed writes,
variable-length integers, and byte-aligned blob writes. Used by
every subsequent batch.

**Test gate**:

- `test_network_bitstream_roundtrip` — write {bits=3, varuint,
  byte-blob, bits=11}, read back, recover the exact values.
- `test_network_bitstream_varuint` — varuint encoding of values
  spanning 1 / 2 / 3 / 4-byte boundaries matches the documented
  encoding.
- `test_network_bitstream_overflow` — reading past the end returns
  `exhausted() == true` and zero-valued reads.
- `test_network_bitstream_align` — byte-aligned write/read works
  regardless of preceding bit-level operations.

**Files**: `bitstream.hpp`. Header-only.

**Risks**: endianness. Specify **little-endian wire format**
across the board.

**Out of scope**: compression (deltas handle most of it; raw
compression of payloads is a v1.x topic if needed).

## Batch NW2 — Transport interface + LoopbackTransport

**Goal**: `ITransport` contract and the in-process loopback
backend used by every test. Loopback supports configurable loss,
reorder, and duplication via a `TransportProfile` to test
unreliable-network behaviors deterministically.

**Test gate**:

- `test_network_loopback_send_receive` — Peer A sends, Peer B
  receives the same bytes via the loopback hub.
- `test_network_loopback_loss` — `TransportProfile{ lossRate = 0.5 }`
  drops ~50% of packets across 1000 sends (binomial within ±5%).
- `test_network_loopback_reorder` — reorder profile produces
  out-of-order delivery while preserving payload integrity.
- `test_network_loopback_determinism` — fixed-seed profile produces
  the same delivery sequence across 2 runs.

**Files**: `transport.hpp`, `src/transport_loopback.cpp`.

**Risks**: loopback "hub" ownership — does each
`LoopbackTransport` know about its peers, or does a shared hub
mediate? Recommendation: shared `LoopbackHub` that transports
register with. Lets tests run multi-peer scenarios from one
process.

**Out of scope**: real sockets (NW10), transport encryption.

## Batch NW3 — Packet headers + session handshake

**Goal**: `PacketHeader` POD, Hello/Welcome handshake to establish
session, basic sequence + ack tracking.

**Test gate**:

- `test_network_handshake` — client sends Hello, server replies
  Welcome with a SessionId; both sides reach connected state.
- `test_network_session_unique` — two concurrent clients get
  different SessionIds.
- `test_network_sequence_wrap` — sequence numbers wrap from
  2^32-1 back to 0 without breaking ack tracking.

**Files**: `packets.hpp`, `ids.hpp`, `config.hpp`, partial
`src/ServerSession.cpp` + `src/ClientSession.cpp`.

**Out of scope**: encryption / cert exchange (out of scope per
DESIGN_NOTES §9 — game-layer concern).

## Batch NW4 — Input delivery + ack tracking

**Goal**: clients push per-tick input; server queues it; ack bits
travel both directions so retransmits stay bounded.

**Test gate**:

- `test_network_input_delivery` — client submits input for ticks
  100..120; server's input queue contains all 21 entries in order.
- `test_network_input_ack` — server acks tick 110; client drops
  ticks ≤110 from its retransmit queue.
- `test_network_input_under_loss` — `lossRate = 0.3` profile;
  input still arrives via retransmits within the configured
  window.

**Files**: extension to ServerSession / ClientSession.

**Out of scope**: input compression (v1.x), late-input handling
policy (game-side decision).

## Batch NW5 — Snapshot encoding (full-state)

**Goal**: server serializes `WorldSnapshot` into a `Snapshot`
packet stream. Client decodes back into a `WorldSnapshot`. No
delta yet — just full state, sent at a configurable cadence.

**Test gate**:

- `test_network_snapshot_roundtrip` — server world with 100
  entities; encode → decode → resulting WorldSnapshot equals
  source byte-for-byte (modulo deterministic snapshot layout).
- `test_network_snapshot_oversize` — snapshot bigger than one MTU
  fragments cleanly and reassembles.
- `test_network_snapshot_replicated_fields` — fields excluded by
  `ReplicationRegistry` are not present in the wire format.

**Files**: `replication.hpp`, partial
`src/ReplicationRegistry.cpp`, serialization helpers in
`serialization.hpp`.

**Risks**: this batch is where the library starts depending on
the engine's `WorldSnapshot` shape. Keep the wire schema **decoupled**
from `WorldSnapshot` — go through the `SimulationBridge`
encode/decode functors so the engine snapshot can evolve without
breaking the wire protocol.

**Out of scope**: delta compression (NW6).

## Batch NW6 — Delta encoding

**Goal**: server tracks dirty fields per-entity and sends deltas
relative to the last acked snapshot. Bandwidth win is the whole
point.

**Test gate**:

- `test_network_delta_smoke` — 100-entity scene where 10 entities
  move per tick; delta packet size is dramatically smaller than
  the equivalent full snapshot (target: ≥10× smaller in steady
  state).
- `test_network_delta_correctness` — delta-decode produces the
  same state as the equivalent full-snapshot-decode.
- `test_network_delta_under_loss` — packet loss recovery falls
  back to full snapshot when delta chain is broken.

**Files**: `detail/delta_encoder.hpp`, extension to ServerSession.

**Out of scope**: per-field bit packing tricks (v1.x optimization).

## Batch NW7 — Rollback buffers

**Goal**: input history + snapshot history + event history with
configurable depth. Required substrate for client prediction (NW8)
and lockstep / rollback netcode patterns.

**Test gate**:

- `test_network_rollback_input_history` — 120 ticks of input
  retained; `rewindToTick(60)` exposes ticks 60..120 for replay.
- `test_network_rollback_snapshot_history` — same shape for
  snapshots.
- `test_network_rollback_capacity_overflow` — exceeding
  `historyTicks` drops the oldest entry FIFO.

**Files**: `rollback.hpp`, `src/RollbackBuffer.cpp`,
`detail/ring_buffer.hpp`.

**Risks**: memory budget. 120 ticks × N-entity snapshot can be
sizeable; document the per-tick cost and let games tune
`historyTicks`.

**Out of scope**: actual rewind+replay against an engine (game
code does that via `SimulationBridge::rewindToTick`).

## Batch NW8 — Client prediction + reconciliation

**Goal**: client predicts authoritative state for its locally
owned entities; reconciles against server-confirmed snapshots
when they arrive. Mispredictions trigger rewind + replay.

**Test gate**:

- `test_network_prediction_basic` — client predicts forward 5
  ticks; server delivers confirmation; predicted state matched →
  zero reconciliation work.
- `test_network_prediction_misprediction` — client predicted state
  diverged from server's; reconcile rewinds to confirmed tick,
  replays with corrected input, ends in correct state.
- `test_network_prediction_smoothing` — reconciliation snap is
  smoothed visually (the smoothing layer is game-side, but the
  library exposes the corrected vs. predicted pair so the smoother
  has both endpoints).

**Files**: `prediction.hpp`, `src/Reconciler.cpp`.

**Out of scope**: anti-cheat (game-layer / server-layer concern).

## Batch NW9 — Interest management

**Goal**: per-client AOI filters. The server only sends entities
relevant to each client (distance-based, faction-based,
explicit-allow-list).

**Test gate**:

- `test_network_interest_distance` — 1000 entities scattered;
  client at world origin only receives entities within
  `interestRadius`.
- `test_network_interest_enter_exit` — entity crossing into AOI
  produces a "spawn" delta; crossing out produces a "despawn"
  delta.
- `test_network_interest_explicit` — entity in an
  explicit-allow-list bypasses distance filter.

**Files**: `interest.hpp`, `src/InterestManager.cpp`.

**Out of scope**: priority-based bandwidth scheduling (v1.x).

## Batch NW10 — Desync diagnostics + UdpTransport

**Goal**: hook `commitHash` into the protocol so server +
clients can detect divergence. Plus the real-network UdpTransport
(POSIX sockets) gated by configure-time platform detection.

**Test gate**:

- `test_network_commit_hash_match` — server and client running
  the same deterministic scenario report matching commitHash per
  tick.
- `test_network_commit_hash_mismatch` — inject a divergence
  (tweak client state); `DesyncReport` event fires with the
  diverging tick + both hashes.
- `test_network_udp_smoke` (gated) — UdpTransport bound to
  127.0.0.1; two peers exchange a Hello/Welcome handshake. No
  routing tests in CI; just no-crash + correct-bytes.

**Files**: `diagnostics.hpp`, `src/transport_udp.cpp`.

**Risks**: UdpTransport ABI portability (POSIX sockets vs.
Winsock). For v1.0, ship POSIX only; Winsock variant as v1.x.

**Out of scope**: TCP / QUIC transports (v1.x); Windows
UdpTransport (v1.x); NAT traversal (game-layer concern).

## v1.0 close-out criteria

- ✓ Every batch NW1–NW10 landed and tested.
- ✓ Loopback-only end-to-end soak: 1k ticks, 8 clients, 30%
  packet loss profile, no desync, no leaks.
- ✓ Snapshot-replication and delta-encoding scale to 256-entity
  scenes within a 1500-byte MTU (one delta packet per tick).
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE, plus a
  `NETCODE_PATTERNS.md` walking games through snapshot vs.
  lockstep vs. rollback choice.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_network/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — QUIC transport

QUIC is the right modern choice for game traffic (built-in
congestion control + multiplexed streams + encryption). Adapter
around `lsquic` or `msquic`; significant ABI work.

### v1.x — Windows UdpTransport (Winsock)

Mirror of `transport_udp.cpp` for Winsock 2. Already plumbed
behind `ITransport`; just needs the platform variant.

### v1.x — Deterministic lockstep helper

The library supports lockstep via the deterministic-commit
foundation, but a `LockstepSession` convenience class that bundles
input-gathering + commit-hash validation + late-input policy
would be ergonomic. Worth shipping after a real game tries it.

### v1.x — Bandwidth scheduler

Per-client priority queue for delta packets — when bandwidth is
tight, send high-priority entities (player, nearby NPCs) first,
defer low-priority (distant scenery).

### v1.x — Trace / replay tooling

Capture all packets to a log; replay deterministically against an
engine instance. Useful for cheat investigation and reproducing
production bugs.

### v1.x — Per-field bit packing

`ReplicationRegistry` learns per-field bit widths (e.g., "this
faction id is 4 bits"). Marginal bandwidth gain on top of NW6;
implementation cost is high. Profile first.

### v1.x — Reflection-driven schema codegen

Currently the engine's serialization hooks are hand-written. A
codegen step that emits replication codecs from C++ reflection
(once standardized) would close the "remember to update X in two
places" gap. Out of scope until C++26 reflection lands.

### v1.x — Cross-process loopback transport

For local-coop / split-screen. Same `ITransport` shape but uses
shared memory + named semaphores instead of in-process queue.

## Out of scope for the whole library

Per DESIGN_NOTES §9 — none of this lands at any batch:

- Physics authority model (game-layer concern)
- Animation blending
- Navmesh generation
- Matchmaking service
- Voice chat
- Account / auth backend
- Engine-side socket layer
- Direct renderer coupling
- Hardcoded game protocol
