# Migrating from threadmaxx v1.2 to v1.3

## Summary

threadmaxx v1.3 amends the determinism contract on
`EngineStats::commitHash`. The hash semantic changed from "FNV-1a-64
mix of every committed command's bytes, in submission order"
(v1.x byte-mix path) to "FNV-1a-64 rollup of every archetype chunk's
content fingerprint, sorted by mask bits" (v1.3 state-rollup path).
The new hash is up to ~7× cheaper to compute at scale (commit phase
drops from ~9.8 ms to ~2.5 ms per tick at 100k entities) and is no
longer order-sensitive: two command streams that produce the same
final per-archetype state produce the same hash.

The two paths produce **different hash values for the same inputs**
by construction — they hash different things. The flag
`Config::legacyCommitHash = true` reverts to the v1.x byte-mix path
for the transition window. It is slated for removal one MINOR cycle
after v1.3.

## Who needs to migrate

You need to act if your code reads `EngineStats::commitHash` AND
**any** of the following applies:

- You compare published hashes between threadmaxx clients (e.g.
  authoritative server vs. replay client) using values originally
  recorded against a v1.2-or-earlier build.
- You persist `commitHash` values to disk and compare against them
  on later runs.
- Your test suite pins specific hex values produced by a v1.2
  build.

Internal-only diagnostic uses (logging, "did the hash stay constant
across this tick?", run-vs-run determinism checks within the same
binary version) need no action — the new hash is still deterministic
across runs and machines, just with different absolute values.

## The transition path

The recommended migration is the one-step approach: re-record your
reference hashes against the v1.3 default, and update your
verification code to use the new values. The new contract is also
easier to reason about for tools (the hash is a function of state,
not a function of command history).

If re-recording isn't feasible immediately, set
`Config::legacyCommitHash = true` to preserve the v1.x byte-mix
path. This stays available for the v1.3 MINOR cycle; the flag will
be removed in v1.4 per the threadmaxx deprecation policy.

```cpp
threadmaxx::Config cfg;
cfg.legacyCommitHash = true;   // preserve v1.x semantics
threadmaxx::Engine engine(cfg);
```

While the flag is set, every behavior of `commitHash` matches v1.2
bit-for-bit: empty steps produce the FNV basis sentinel, byte-mix
output is identical, and the test `tests/v1_2_legacy_commit_hash_test.cpp`
pins the invariants.

## What's actually different

### v1.2 (byte-mix hash, now opt-in via `legacyCommitHash = true`)

- Every committed command contributes (variant discriminator +
  entity handle + payload bytes) to a running FNV-1a-64.
- Reset to the FNV basis at the start of every `step()`.
- Empty step → hash stays at FNV basis (`0xcbf29ce484222325`).
- Order-sensitive: reordering commands within a tick changes the
  hash.
- Cost: ~70 ns per command per tick (serial).

### v1.3 (state-rollup hash, the new default)

- Every archetype chunk maintains a `cachedHash` updated lazily
  at end-of-step. When a chunk is dirty (any insert / removeSwapPop /
  migrate / per-handle setter touched it during the step), the
  end-of-step pass FNV-1a-64-mixes its full state (mask, count,
  entities, dense arrays, user columns) into a new fingerprint.
- The published `commitHash` is FNV-1a-64 over all chunk
  fingerprints, sorted by `mask.bits()` ascending.
- Empty step where no chunks were touched → previous tick's chunk
  fingerprints are reused; hash stays stable but is NOT the FNV
  basis (it's the rollup of the current state).
- Order-insensitive WITHIN A STEP for value-only commands: writing
  `setTransform` on entity A then B vs. B then A produces the
  same hash, because the final state is the same.
- Cost: scales with state bytes per tick × parallelism. At 100k
  entities in 5 archetypes, ~2.5 ms with 4 workers.

### Paused steps — unchanged

Both paths short-circuit paused steps to the FNV basis sentinel via
the same code path in `EngineImpl::step`. `engine.setPaused(true)`
followed by `engine.step()` still publishes
`commitHash == 0xcbf29ce484222325` regardless of `legacyCommitHash`.

## Verification

Both paths are deterministic across runs and machines.
`tests/archetype_hash_determinism_test.cpp` pins the v1.3 contract;
`tests/v1_2_legacy_commit_hash_test.cpp` pins the v1.2 contract. The
existing `tests/commit_hash_test` exercises the default path
(v1.3); the existing `tests/sharded_commit_test` cross-validates
single-threaded vs sharded commit under both paths.

If you need to verify that a v1.3 client agrees with a v1.2 client
on the SAME world state (e.g., during a phased migration), set
`legacyCommitHash = true` on the v1.3 side temporarily. Once all
clients have moved to v1.3, drop the flag and re-record reference
hashes.

## Why this changed

The v1.x byte-mix path was the only serial bottleneck left in the
commit phase. Sub-job dispatch (B28) drove `update` parallelism up
to 32× peak queue depth at 100k entities, but the per-command FNV
mix still ran on the sim thread one byte at a time, costing ~7 ms
of every 9.8 ms commit budget. The optimization path's §3.6 batch
30 attacked this by trading the "byte-identical hash across runs
given the same command stream" guarantee for "byte-identical hash
across runs given the same final per-archetype state" — a strictly
weaker invariant that still meets the original determinism use
cases (replay, lockstep, network diff) but admits the parallelism
the byte-mix path forbade.

A parallel byte-identical version of the v1.x hash was investigated
in §3.5 batch 29 and proved mathematically impossible under integer
multiplication mod 2^64 (FNV's recurrence is non-distributive over
XOR). The state-rollup path is the only viable route to the C2
budget.

See `OPTIMIZATION_PATH.md §3.6` for the design rationale and as-shipped
numbers; `CLAUDE.md §3.10.6` for the engineer-facing invariants on
the new hash path.
