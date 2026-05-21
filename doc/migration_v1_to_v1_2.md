# Migrating from threadmaxx v1.1 to v1.2

## Summary

threadmaxx v1.2 is an additive minor release. No public symbols were
removed; no on-disk formats changed. The single behavior change is
the `commitHash` semantics, which is opt-out via a flag during the
transition window. Most projects can update the dependency
constraint and rebuild with no source changes.

```cmake
find_package(threadmaxx 1.2 CONFIG REQUIRED)
```

If you do not read `EngineStats::commitHash`, you are done.

## What changed

### 1. `commitHash` semantics — opt-out

The published `commitHash` value now reflects per-archetype state
instead of the byte-mixed command stream. Two command streams that
produce the same final per-archetype state now produce the same
hash. The new path costs roughly one-quarter of the v1.1 path at
100k entities.

The flag is `Config::legacyCommitHash`, default `false` (new path).
Set to `true` to preserve the v1.1 byte-mix path bit-for-bit for the
transition window:

```cpp
threadmaxx::Config cfg;
cfg.legacyCommitHash = true;   // preserve v1.1 semantics
threadmaxx::Engine engine(cfg);
```

The flag is slated for removal one MINOR cycle later per the
threadmaxx deprecation policy. Use it as a transition tool, not as a
long-term solution.

The detailed contract change — what the hash hashes, the
order-sensitivity rules, paused-step sentinel behavior, when to
re-record reference hashes — is in
[`doc/migration_v1_2_to_v1_3.md`](migration_v1_2_to_v1_3.md). The
two docs cover the same change from different angles: this one is
the v1.1-user upgrade checklist; the other is the contract
reference any external client comparing hashes across builds will
need to read.

### 2. `SystemContext::workerCount()` — new pure virtual

`SystemContext` gained a pure virtual `workerCount()` returning the
engine's worker pool size. `forEachChunk` uses it to size the
batch-28 sub-job dispatch budget.

`SystemContextImpl` (the only `SystemContext` derived class
threadmaxx ships) implements it as a cheap pass-through to
`Engine::workerCount()`. The change is transparent unless your
code directly subclasses `SystemContext`, which is unsupported per
the public-surface contract (every derived class outside the
library would be relying on engine internals).

If you have a mock `SystemContext` in a test fixture, implement
`workerCount()` returning `1` (or however many workers your test
mocks).

### 3. New opt-in knobs

None are required to upgrade. Listed for awareness:

- **`Config::legacyCommitHash`** (above).
- **`ISystem::preferredGrain()` / `parallelFor` grain hint** — was
  present in v1.1 (batch 11) but is the recommended tuning knob now
  that the §3.4 sub-job dispatch in `forEachChunk` lets one chunk's
  rows fan out across workers. See
  [`doc/performance_tuning.md`](performance_tuning.md).
- **`THREADMAXX_BUILD_LONG_SOAK`** CMake option — opt-in 10,000-tick
  concurrency soak (`tests/concurrency_soak_long.cpp`). Not
  registered with ctest by default. ~6 min runtime in Release.

## What did NOT change

- **Public headers under `include/threadmaxx/`** — no removals; no
  layout changes on any exposed POD.
- **`WorldSnapshot` magic / version** —
  `kWorldSnapshotVersion` is unchanged; v1.1 snapshots load on
  v1.2 unchanged.
- **The 16 component slots and `ComponentSet::all()`'s mask** —
  unchanged.
- **`ISystem` virtual table** beyond the additive
  `workerCount()` on `SystemContext` (note that `workerCount()`
  is on `SystemContext`, not `ISystem`).
- **Determinism within a single threadmaxx version** — both the
  v1.1 byte-mix path (via `legacyCommitHash = true`) and the v1.2
  state-rollup path are deterministic across runs and machines.

## Verification

After upgrading, the following ctest invariants should hold:

- `tests/commit_hash_test` and `tests/sharded_commit_test` pass
  (new contract).
- `tests/v1_2_legacy_commit_hash_test` passes under the
  `legacyCommitHash = true` opt-out path.
- `tests/archetype_hash_determinism_test` verifies the new
  contract's properties.

If you have recorded reference hashes against v1.1, re-record them
once against v1.2 and remove the temporary `legacyCommitHash`
override. The v1.1 reference hashes will not match v1.2's default
path by construction.

## Pointers

- [`CHANGELOG.md`](../CHANGELOG.md) — the v1.2.0 release notes.
- [`doc/migration_v1_2_to_v1_3.md`](migration_v1_2_to_v1_3.md) —
  the detailed hash contract reference.
- [`doc/performance_tuning.md`](performance_tuning.md) — new in
  v1.2; covers the knobs that batches 26–32 exposed or expanded.
- `OPTIMIZATION_PATH.md` (top-level) — the as-shipped writeup for
  batches 26–32 with bench numbers.
