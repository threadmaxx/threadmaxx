# threadmaxx_network — Maintainer Guide

For contributors landing changes to the library.

## Source layout

```
include/threadmaxx_network/
  threadmaxx_network.hpp   # umbrella
  bitstream.hpp            # codec primitives
  ids.hpp / packets.hpp / config.hpp
  transport.hpp / udp_transport.hpp
  session.hpp / replication.hpp / delta.hpp
  rollback.hpp / prediction.hpp / interest.hpp / diagnostics.hpp
src/threadmaxx_network/
  *.cpp (one per non-header-only public surface)
tests/network/
  test_network_*.cpp
```

## Build / test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build -R '^network\.'
```

Warnings-as-errors:

```
cmake -S . -B build-werror -DCMAKE_BUILD_TYPE=Release \
    -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
ctest --test-dir build-werror -R '^network\.'
```

Both trees must stay 100% green. The full network test suite is the
contract.

## Adding a new public symbol

1. Add it to the appropriate `include/threadmaxx_network/*.hpp` with
   a one-line `@brief`. Load-bearing methods get
   `@thread_safety` / `@pre` annotations.
2. If non-header-only, drop an impl under
   `src/threadmaxx_network/<Name>.cpp`.
3. Update `src/threadmaxx_network/CMakeLists.txt`'s
   `THREADMAXX_NETWORK_PUBLIC_HEADERS` / `THREADMAXX_NETWORK_SOURCES`.
4. Add to the umbrella `threadmaxx_network.hpp`.
5. Add at least one focused test in `tests/network/` and register it
   in `tests/network/CMakeLists.txt`.
6. Re-run both build trees + the network test slice.
7. If it's a new netcode-pattern primitive, add a section to
   `NETCODE_PATTERNS.md`.

## Versioning

`THREADMAXX_NETWORK_VERSION = MAJOR*10000 + MINOR*100 + PATCH`. Three
artifacts move together on every bump:

- `include/threadmaxx_network/version.hpp` macros + literal,
- `tests/network/test_network_version.cpp` pin.

## Wire-format stability

Anything in `packets.hpp`, `bitstream.hpp`, `replication.hpp`'s
encoding, `delta.hpp`'s encoding, or `diagnostics.hpp`'s `DesyncReport`
is part of the **wire ABI**. Backwards-incompatible changes bump the
MAJOR version of the library AND `kProtocolVersion` in
`packets.hpp`.

Adding new `PacketType` enum values is backwards compatible (old
clients ignore unknown types — but won't process the new feature
either). Removing or renumbering existing enum values is a
breaking change.

## Adding a new transport

See `transport.hpp` — implement `ITransport` over your transport
layer (QUIC, WebRTC, shared memory, etc.). Mirror
`LoopbackTransport` and `UdpTransport` for ownership / threading
shape. Gate the source file in CMakeLists with the appropriate
platform / dependency probe, and export
`THREADMAXX_NETWORK_HAS_<TYPE>=1` for downstream `#if` checks.

## Commit cadence

Each batch is independently shippable and lands as one commit.
Standing authorization (see CLAUDE.md, `feedback_auto_commit_batches`)
is to commit immediately when a batch ships green.
