# `threadmaxx_network` — networking, replication, and rollback sibling library

## 1. Purpose

`threadmaxx_network` provides the wire protocol and runtime needed for:

* authoritative server play,
* client prediction and reconciliation,
* deterministic lockstep,
* snapshot / delta replication,
* rollback and replay,
* interest management,
* connection/session handling,
* desync detection.

It does **not** own game simulation. It consumes `threadmaxx` state, but it never replaces the engine’s commit model or storage model. That matches the roadmap’s boundary: the engine stays responsible for deterministic commit + stable entity IDs, while the networking layer owns the protocol.

## 2. Design principles

1. **Engine-agnostic.** No dependence on renderer internals, physics solver internals, or entity storage internals.
2. **Protocol over transport.** UDP, QUIC, loopback, and test transports are interchangeable.
3. **Snapshot/delta first.** Full-state snapshots exist, but deltas are the normal path.
4. **Rollback-capable.** Every replicated tick must be replayable from a saved input/snapshot history.
5. **Deterministic-friendly.** The library should integrate with `commitHash`-style validation rather than inventing its own notion of truth. The roadmap already uses deterministic commit as the engine-side foundation.
6. **No allocations in hot paths.** Packets, deltas, and command streams should use spans and fixed buffers.
7. **Transport and protocol are separate.** Transport sends bytes; protocol defines meaning.
8. **Interest management is explicit.** Replication rules are opt-in and data-driven.
9. **Component-aware, not component-owning.** It should serialize known component data through hooks, not duplicate the engine’s component system.
10. **Small public surface.** Add only the pieces needed for real gameplay networking.

## 3. Suggested package layout

```text id="zq1w9m"
include/threadmaxx_network/
  threadmaxx_network.hpp  // umbrella include
  config.hpp              // protocol and runtime settings
  ids.hpp                 // PeerId, SessionId, NetEntityId, TickId
  transport.hpp           // transport interface
  packets.hpp             // packet headers, opcodes, envelopes
  bitstream.hpp           // bit-level read/write helpers
  clock_sync.hpp          // tick sync, RTT, drift correction
  authority.hpp           // server authority / ownership rules
  replication.hpp         // snapshot, delta, dirty tracking
  rollback.hpp            // history buffers, rewind/replay
  prediction.hpp          // client prediction / reconciliation
  interest.hpp            // visibility / relevance / AOI
  serialization.hpp      // codec registry and field packing
  diagnostics.hpp         // desync reports, traces, counters
  detail/
    crc32.hpp
    delta_encoder.hpp
    ring_buffer.hpp
    packet_pool.hpp
    transport_udp.hpp
    transport_loopback.hpp
```

If you want a separate transport backend package, split it like this:

```text
src/threadmaxx_network/
  transport_udp.cpp
  transport_quic.cpp
  transport_loopback.cpp
```

The core protocol layer can stay header-first, while platform/network-specific sockets live in source files.

## 4. Core abstractions

### 4.1 Stable identities

`threadmaxx` already gives you deterministic commit and stable entity IDs, which is the foundation this library needs. `threadmaxx_network` should wrap those IDs in network-facing handles rather than inventing its own world model.

```cpp id="d4n2qf"
namespace threadmaxx::network {

struct PeerId { std::uint32_t value; };
struct SessionId { std::uint64_t value; };
struct TickId { std::uint32_t value; };

// Opaque network-facing handle for a game entity that is already stable in threadmaxx.
struct NetEntityId {
    std::uint64_t value;
};

} // namespace threadmaxx::network
```

### 4.2 Transport interface

Keep transport simple and replaceable.

```cpp id="k7p3lv"
namespace threadmaxx::network {

struct PacketView {
    const std::byte* data{};
    std::size_t size{};
};

struct ReceivedPacket {
    PeerId peer{};
    PacketView payload{};
    std::uint64_t receiveTimeNs{};
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool send(PeerId peer, PacketView packet) = 0;
    virtual std::size_t receive(std::span<ReceivedPacket> out) = 0;
    virtual void poll() = 0;
    virtual void shutdown() = 0;
};

} // namespace threadmaxx::network
```

This keeps UDP, QUIC, loopback, and deterministic test transport all behind the same contract.

### 4.3 Protocol envelopes

Packets should be small, typed, and versioned.

```cpp id="t8h0qp"
namespace threadmaxx::network {

enum class PacketType : std::uint8_t {
    Hello,
    Welcome,
    Input,
    Snapshot,
    Delta,
    Ack,
    ResyncRequest,
    ResyncReply,
    Ping,
    Pong,
    DesyncReport,
    Disconnect
};

struct PacketHeader {
    std::uint8_t version = 1;
    PacketType type{};
    SessionId session{};
    TickId tick{};
    std::uint32_t sequence{};
    std::uint32_t ack{};
    std::uint32_t ackBits{};
};

} // namespace threadmaxx::network
```

### 4.4 Bitstream codec

Use a compact bit writer/reader for packet payloads.

```cpp id="y2r1ub"
namespace threadmaxx::network {

class BitWriter {
public:
    void writeBits(std::uint64_t value, std::uint32_t bitCount);
    void writeBytes(std::span<const std::byte> bytes);
    void writeVarUInt(std::uint64_t value);
    std::span<const std::byte> finish() const;
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> payload);

    std::uint64_t readBits(std::uint32_t bitCount);
    std::uint64_t readVarUInt();
    void readBytes(std::span<std::byte> out);
    bool exhausted() const;
};

} // namespace threadmaxx::network
```

## 5. Game-state model

The library should not mirror the full engine state. It should work with a **replication model**:

* some entities are replicated,
* some components are replicated,
* some fields are predicted locally,
* some state is server-owned only,
* some data is transient and never serialized.

### 5.1 Replication registry

```cpp id="o6m4ed"
namespace threadmaxx::network {

struct ComponentCodecId {
    std::uint32_t value{};
};

struct ReplicatedFieldMask {
    std::uint64_t bits{};
};

class ReplicationRegistry {
public:
    template<class T>
    void registerComponent(ReplicatedFieldMask fields,
                           std::string_view name);

    template<class T>
    bool isRegistered() const noexcept;

    template<class T>
    ComponentCodecId codecId() const noexcept;
};

} // namespace threadmaxx::network
```

This registry tells the network layer how to encode known component types. It should use the engine’s existing component serialization hooks where possible. The roadmap already says serialization hooks belong in the engine, not a full migration system, so the network layer should build on that instead of replacing it.

### 5.2 Ownership rules

Ownership should be explicit:

* server owns authoritative simulation state,
* client may own prediction for local player input,
* remote clients own nothing except their input queue,
* some entities can be transferred temporarily during host migration or scripted control.

```cpp id="u9x4cs"
namespace threadmaxx::network {

enum class Authority : std::uint8_t {
    Server,
    ClientPredicted,
    ClientInterpolated,
    Shared,
    Transient
};

struct OwnershipRule {
    NetEntityId entity{};
    Authority authority{};
};

} // namespace threadmaxx::network
```

## 6. Runtime modes

### 6.1 Snapshot replication

This is the simplest production mode.

* Server simulates.
* Server sends periodic snapshots and smaller deltas.
* Clients interpolate between received states.
* Client-side prediction is optional for locally controlled entities.

### 6.2 Deterministic lockstep

This is the mode that benefits most from `threadmaxx`’s deterministic commit foundation. The engine already supplies the stable side of that equation; the network library just distributes inputs and confirms tick progression.

* All peers exchange inputs for tick N.
* Simulation for tick N only advances when all required inputs are present or timeout policy permits a fallback.
* Each peer can validate the resulting tick against a commit hash.
* Divergence triggers resync or rollback.

### 6.3 Rollback netcode

Rollback needs three buffers:

* input history,
* snapshot history,
* event history or command history.

```cpp id="f4r1ds"
namespace threadmaxx::network {

struct RollbackConfig {
    std::uint32_t historyTicks = 120;
    std::uint32_t maxRollbackTicks = 12;
    bool keepEventHistory = true;
    bool keepCommandHistory = true;
};

} // namespace threadmaxx::network
```

The goal is not to own simulation. The goal is to capture enough history to rewind `threadmaxx` world state and replay forward.

## 7. Public session types

### 7.1 Server session

```cpp id="q3s8kr"
namespace threadmaxx::network {

class ServerSession {
public:
    explicit ServerSession(ReplicationRegistry registry,
                           std::unique_ptr<ITransport> transport,
                           RollbackConfig config = {});

    void stepTick(const threadmaxx::WorldSnapshot& snapshot,
                  TickId tick);

    void pushInput(PeerId peer, TickId tick, std::span<const std::byte> input);
    void disconnect(PeerId peer, std::string_view reason);
};

} // namespace threadmaxx::network
```

### 7.2 Client session

```cpp id="j8m1pa"
namespace threadmaxx::network {

class ClientSession {
public:
    explicit ClientSession(ReplicationRegistry registry,
                           std::unique_ptr<ITransport> transport,
                           RollbackConfig config = {});

    void submitLocalInput(std::span<const std::byte> input);
    void receivePacket(PacketView packet);
    std::optional<TickId> predictedTick() const noexcept;
    std::optional<TickId> confirmedTick() const noexcept;
};

} // namespace threadmaxx::network
```

### 7.3 Shared simulation bridge

This is the glue the game uses.

```cpp id="l5n7vh"
namespace threadmaxx::network {

struct SimulationBridge {
    // encode/decode from threadmaxx world state
    std::function<void(BitWriter&, const threadmaxx::WorldSnapshot&)> writeSnapshot;
    std::function<void(BitReader&, threadmaxx::WorldSnapshot&)> readSnapshot;

    // for local rollback/replay
    std::function<void(TickId)> rewindToTick;
    std::function<void(TickId)> replayFromTick;
};

} // namespace threadmaxx::network
```

## 8. Integration with `threadmaxx`

This library should plug into the engine, not patch it.

### 8.1 Use the engine’s snapshot and serialization hooks

The roadmap already says `threadmaxx` has snapshot serialization and stable entity IDs available as engine-side foundations. `threadmaxx_network` should use those for state capture and replay instead of inventing a second serialization model.

### 8.2 Use commit hashes for desync checks

The roadmap’s commit hash work gives you a built-in divergence detector. `threadmaxx_network` should record the authoritative hash per tick and compare it during replay / lockstep / rollback validation.

### 8.3 Keep the engine unaware of transport

No sockets in the engine. No packets in ECS. No replication policy inside `threadmaxx` core. The engine should remain the simulation backend; networking lives above it, exactly as the roadmap intends.

## 9. What the library should not do

* no physics authority model,
* no animation blending,
* no navmesh generation,
* no matchmaking service,
* no voice chat,
* no account/auth backend,
* no engine-side socket layer,
* no direct renderer coupling,
* no hardcoded game protocol.

Those are either separate application concerns or sibling libraries above the engine boundary. The roadmap is explicit that animation, physics, navmesh, and editor tooling stay out of the core library; networking belongs in that same “above the engine” layer.

## 10. Implementation order

A sane build order is:

1. packet headers and bitstream codec,
2. transport interface + loopback transport,
3. session handshake,
4. input delivery and ack tracking,
5. snapshot encoding,
6. delta compression,
7. rollback buffers,
8. client prediction and reconciliation,
9. desync diagnostics,
10. interest management.

## 11. Tests to add

* packet encode/decode round-trips,
* loss / reorder / duplication transport simulation,
* deterministic replay over 1k+ ticks,
* rollback correctness on locally predicted entities,
* delta compression correctness,
* reconnect and resync behavior,
* commit-hash mismatch detection,
* interest-management coverage with large worlds.
