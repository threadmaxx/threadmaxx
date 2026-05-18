# `threadmaxx_simd` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

## Conventions

Each batch is independently shippable:

- **Test gate** — the assertions that prove the batch landed.
- **Files** — what's added/modified.
- **Risks** — what could break (mostly compile-time on different
  toolchains).
- **Out of scope** — what's explicitly deferred to a later batch.

Batches always start with the tests (red), then implement (green),
then refactor. The library stays header-only throughout; the CMake
target is `INTERFACE`-only and the kernels live entirely in
`include/threadmaxx_simd/`.

## Library structure (target end-state)

```
include/threadmaxx_simd/
  threadmaxx_simd.hpp     # umbrella include
  config.hpp              # ISA enum + capabilities POD
  traits.hpp              # simd_batchable concept
  views.hpp               # span_view<T> adapter
  lanes.hpp               # lane-count helpers (S4+)
  vec3_ops.hpp            # public Vec3 kernels
  quat_ops.hpp            # public Quat kernels
  transform_ops.hpp       # public Transform / integration kernels
  aabb_ops.hpp            # public AABB + frustum kernels
  simd_math.hpp           # rsqrt / clamp / min / max
  cpu.hpp                 # one-time runtime CPU probe (S5)
  detail/
    scalar.hpp            # scalar kernel impls (always present)
    avx2.hpp              # AVX2 kernel impls (S3+)
    sse2.hpp              # SSE2 kernel impls (S6)
    neon.hpp              # NEON kernel impls (S6 alternate)
    dispatch.hpp          # compile-time + runtime dispatch (S5)
```

The public-facing `*_ops.hpp` headers are the user contract; everything
under `detail/` is implementation surface that can churn between
batches.

## Batch S1 — Foundations  ← this batch

**Goal**: header-only library + opt-in CMake + scalar Vec3 kernels.
First end-to-end test of the `view → kernel` pattern. No SIMD
intrinsics yet.

**Test gate**:

- `test_simd_config` — `compile_time_capabilities()` is constexpr;
  `.scalar == true`; `preferred_isa()` returns a valid enum value;
  `isa` has at least `scalar`/`sse2`/`avx2`/`neon` members.
- `test_simd_traits` — `simd_batchable<T>` accepts `Vec3`, `Quat`,
  `Transform`, `Velocity`, `BoundingVolume`; rejects
  `std::string` (not trivially copyable) and over-aligned types.
- `test_simd_views` — `view(span<T>)` and `view(span<const T>)`
  produce `span_view` with matching `data()` / `size()` /
  `empty()`; empty span → empty view.
- `test_simd_vec3_ops` — scalar `add`/`sub`/`scale`/`madd`/`normalize`
  /`dot` produce correct numerical output for fixed-input cases AND
  for tail-size cases (1, 3, 7, 13 elements). Empty spans are no-ops
  for the writers. Zero-vector `normalize` stays zero (no NaN).

**Files**:

- `include/threadmaxx_simd/{config,traits,views,simd_math,vec3_ops,threadmaxx_simd}.hpp`
- `include/threadmaxx_simd/detail/scalar.hpp`
- Top-level `CMakeLists.txt` — `option(THREADMAXX_BUILD_SIMD ...)`
  default ON; `add_library(threadmaxx_simd INTERFACE)`;
  `threadmaxx::simd` alias.
- `tests/simd/CMakeLists.txt` + four test executables.

**Risks**:

- The `simd_batchable` concept may need tweaking on older toolchains.
- `std::is_standard_layout_v<Transform>` should be true (it's a POD)
  but the test will catch a regression.

**Out of scope**:

- All non-Vec3 kernels (Quat / Transform / AABB / Frustum) — S2.
- Any SIMD intrinsics — S3.
- Runtime dispatch — S5.

## Batch S2 — Scalar Quat / Transform / AABB / Frustum kernels  ✅ landed 2026-05-17

**Shipped** — the full scalar surface from DESIGN_NOTES §5.2 and
§5.3 plus the §5.3 frustum culler. Every public kernel now has a
working scalar implementation; SIMD backends come in S3/S4.

**Files added**:

- `include/threadmaxx_simd/quat_ops.hpp` — public `normalize` /
  `slerp` (span + span_view overloads).
- `include/threadmaxx_simd/transform_ops.hpp` — public
  `apply_transforms` / `integrate_positions` /
  `integrate_linear_motion`.
- `include/threadmaxx_simd/aabb_ops.hpp` — public `transform_aabb` /
  `frustum_cull`. Uses `BoundingVolume` (the engine's existing AABB
  POD) and `Frustum` from `threadmaxx/render/Visibility.hpp`.
- `include/threadmaxx_simd/detail/scalar.hpp` — extended with the
  scalar implementations:
  - Quat helpers: `quat_mul_one`, `quat_normalize_one`,
    `quat_from_axis_angle`, `quat_rotate_vec_one`, `quat_slerp_one`.
    Slerp picks shortest path automatically (flips `b` if
    `dot(a, b) < 0`); near-parallel inputs fall back to lerp+normalize
    to avoid `sin(theta)/0` blowup.
  - Quat span kernels: `quat_normalize` (in-place), `quat_slerp`.
  - Transform span kernels: `apply_transforms`, `integrate_positions`
    (composes `Velocity.angular` as axis-angle into orientation),
    `integrate_linear_motion`.
  - AABB span kernels: `transform_aabb` (8-corner method),
    `frustum_cull` (sphere-based broad-phase writing 1-bit mask).
- `include/threadmaxx_simd/threadmaxx_simd.hpp` — umbrella updated
  to include the three new headers.

**Tests added**:

- `tests/simd/test_simd_quat_ops.cpp` — normalize basics including
  zero→identity fallback; slerp endpoints exact at α=0/1; midpoint
  matches the closed-form 45°-around-Y interpolation; shortest-path
  branch fires when `dot < 0`; tail handling (1, 7, 13); mismatched
  + empty spans.
- `tests/simd/test_simd_transform_ops.cpp` — `apply_transforms`
  with identity / translation / 90°-Y rotation / non-uniform scale;
  `integrate_linear_motion` advances position only;
  `integrate_positions` advances both linear and angular state
  (π rad/s around Y for 1s → 180° rotation); zero angular velocity
  leaves orientation untouched; tail / mismatched / empty.
- `tests/simd/test_simd_aabb_ops.cpp` — `transform_aabb` identity /
  translation / 45°-Y rotation (extent grows from 1 → √2) /
  non-uniform scale; `frustum_cull` in/out/straddling cases against
  a manually-constructed axis-aligned frustum; empty + mismatched.

**Conventions clarified for future batches**:

- Writers stop at the shorter span; no exception thrown for size
  mismatch. Same policy as the Vec3 kernels (S1).
- Zero-norm `Quat` collapses to identity (`0, 0, 0, 1`); matches
  the `rsqrt(0) = 1.0f` policy in `simd_math.hpp`.
- `Velocity.angular` interpreted as axis-angle per second (direction
  = axis, magnitude = ω rad/s). Per-tick angle = `|ω| * dt`.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 101/101 on both trees** (was 98; +3 from new tests).

**Files**: 4 new (`quat_ops.hpp`, `transform_ops.hpp`,
`aabb_ops.hpp`, plus the three new test files), 3 modified
(`detail/scalar.hpp`, `threadmaxx_simd.hpp`, `tests/simd/CMakeLists.txt`).

**Effort**: ~1.5 hours actual.

## Batch S3 — AVX2 backend for Vec3 ops  ✅ landed 2026-05-18

**Shipped** — first vectorized backend for the 5 element-wise Vec3
kernels (`add` / `sub` / `scale` / `madd` / `dot`). Compile-time
selected via the `THREADMAXX_SIMD_HAS_AVX2` macro from `config.hpp`
(set automatically when the translation unit is built with
`__AVX2__`). The main `threadmaxx::simd` INTERFACE library stays
portable — users opt in to AVX2 by setting `-mavx2` (and optionally
`-mfma`) on their own compile target. Without those flags, the
AVX2 header preprocesses out and the dispatcher resolves to scalar.

**Files added**:

- `include/threadmaxx_simd/detail/avx2.hpp` — AVX2 implementations
  of `add` / `sub` / `scale` / `madd` / `dot`. Flat-float stride-8
  loop with scalar tail. FMA used for `madd` / `dot` when
  `__FMA__` is set. Standard 2-step horizontal-sum for `dot`'s
  epilogue. Aliasing rationale documented in the header preamble.
- `tests/simd/test_simd_vec3_avx2_equivalence.cpp` — RNG-seeded
  scalar-vs-AVX2 equivalence across a sweep of 20 array sizes
  including every interesting alignment boundary (0, 1..8, 13,
  16, 17, 23, 24, 31, 32, 64, 127, 256, 1024). Tolerances picked
  per-kernel: tight absolute for `add` / `sub` / `scale` /
  `normalize`-style; bumped relative for `madd` (FMA's last-bit
  drift); n-proportional for `dot` accumulation.

**Files modified**:

- `include/threadmaxx_simd/vec3_ops.hpp` — compile-time dispatcher.
  `#if THREADMAXX_SIMD_HAS_AVX2` routes the 5 element-wise kernels
  to `detail::avx2`; `normalize` stays on the scalar path even
  when AVX2 is built (see S3.5 below).
- `tests/simd/CMakeLists.txt` — added `test_simd_vec3_avx2_equivalence`;
  attached `-mavx2 -mfma` to that test only (x86_64 + non-MSVC
  hosts). Other SIMD test targets stay portable.

**Test gate met**:

- `simd.test_simd_vec3_avx2_equivalence` — PASS. Manual run
  confirmed the AVX2 branch fired: `[simd_vec3_avx2_equivalence]
  AVX2 == scalar across 20 size points`. (The skip-branch message
  would have read "AVX2 not built; test skipped".)
- `simd.test_simd_vec3_ops` (S1) — continues to PASS. The same
  semantic assertions hold whether the path is scalar or AVX2
  because the dispatcher's signature is invariant.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 102/102 on both trees** (was 101; +1 from
  `test_simd_vec3_avx2_equivalence`).

**Effort**: ~1 hour actual.

## Batch S3.5 — AVX2 `normalize` (Vec3)  ✅ landed 2026-05-18

**Shipped** — completes AVX2 coverage for the Vec3 kernel family
(6/6). The implementation took the gather-based deinterleave
approach rather than the permute sequence I sketched earlier;
gather is simpler to get right and decently fast on Skylake+
hardware (the dev-target class).

**Implementation choices**:

- **Deinterleave via `_mm256_i32gather_ps`** with pre-built
  strided indices `{0,3,6,9,12,15,18,21}` (×3 for x/y/z). Loads
  8 lanes of x/y/z from the 24-float chunk in one instruction
  each. The earlier-sketched permute sequence would be faster
  but ~8 instructions; gather is ~3 instructions total for
  deinterleave.
- **Full-precision `sqrt + div`** instead of `rsqrt + Newton`.
  The decision: `rsqrt` is 12-bit accurate (~2.4e-4 relative
  error) — too loose for the 1e-6 tolerance the test gate
  demands. One Newton iteration tightens to ~6e-8 but adds 4
  cycles. Going straight `sqrt + div` is roughly the same
  cycle count on Skylake+ (~28 cycles total) AND gives
  bit-identical-to-scalar precision (within ulp), which makes
  the equivalence test trivial. Trade-off is fine because the
  win is over scalar (~25 cycles per Vec3 × 8 = 200 cycles)
  not over a hypothetical rsqrt+Newton path.
- **Zero-magnitude mask via `_mm256_cmp_ps + _mm256_andnot_ps`**.
  When `len² <= 0`, the comparison sets all bits in that lane
  of the mask; `andnot(mask, invLen)` zeros that lane's
  inverse-length, so the subsequent multiplication produces a
  zero output (matches the scalar policy — no NaN propagation
  from `1/sqrt(0)`).
- **Reinterleave via scalar extract-and-store**. Three
  `_mm256_store_ps` to stack-allocated arrays, then 8 iterations
  of 3 scalar writes each. Slower than a SIMD reinterleave but
  trivially correct; SIMD reinterleave can land later as a
  micro-optimization without changing semantics.

**Test gate met**:

- `simd.test_simd_vec3_avx2_equivalence` — extended with a
  `normalize` block at 1e-6 relative tolerance + an explicit
  zero-vector mixing test (zero vectors interspersed at every
  position within and across the 8-Vec3 lane boundary, plus in
  the tail). PASS. Confirmed via manual run: still 20 size
  points, all kernels equivalent.

**Files modified**:

- `include/threadmaxx_simd/detail/avx2.hpp` — added `normalize`.
- `include/threadmaxx_simd/vec3_ops.hpp` — `normalize` now
  dispatches like the other 5 kernels.
- `tests/simd/test_simd_vec3_avx2_equivalence.cpp` — normalize
  block + zero-mixing scenario.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 102/102 on both trees** (unchanged count — the
  equivalence test grew, no new files).

**Remaining Vec3 micro-opts** (low priority; not blocking S4):

- SIMD reinterleave to replace the scalar epilogue. Would save
  ~5 cycles per 8-Vec3 batch.
- Permute-based deinterleave instead of gather on platforms
  with slow gather (pre-Skylake).
- `rsqrt + Newton` variant for callers willing to trade a
  ~6e-8 last-bit error for ~30% wall-clock improvement.

**Effort**: ~30 min actual.

## Batch S4 — AVX2 for Transform / Quat / AABB kernels  ✅ partial 2026-05-18

**Shipped** — AVX2 paths for the two kernels where the win is
clean given the engine's existing POD layouts:

- `normalize(span<Quat>)` — 2 quats per __m256, with
  `_mm256_dp_ps` doing the per-quat length-squared reduction
  in one instruction. Zero-norm quats become identity (0,0,0,1)
  via blend with a fixed identity vector. Tail handles 0–1
  remaining quats via scalar.
- `frustum_cull(span<const Vec3>, span<const float>, Frustum, mask)`
  — 8 spheres per iteration via gather (centers, stride 3) +
  contiguous load (radii). 6-plane survival reduction
  accumulates an AND-mask; final 8-wide visibility mask
  collapsed to 8 bytes via `movemask` + bit-spread. FMA used
  per plane when available.

**Files**:

- `include/threadmaxx_simd/detail/avx2.hpp` — extended with
  `quat_normalize` and `frustum_cull`.
- `include/threadmaxx_simd/quat_ops.hpp` /
  `include/threadmaxx_simd/aabb_ops.hpp` — compile-time
  dispatcher added (same `#if THREADMAXX_SIMD_HAS_AVX2` pattern
  as Vec3 ops).
- `tests/simd/test_simd_avx2_equivalence_extended.cpp` — new
  equivalence test covering the two new kernels. Includes
  explicit zero-norm Quat injection at lane boundaries +
  in-tail positions. PASS.

**Second S4 batch landed 2026-05-18** — vectorized 3 more
kernels, benchmarked all 8 vs scalar, reverted dispatching
for the kernels where AVX2 lost on actual hardware. Honest
benchmark-driven scoping.

**Files added/modified (2nd batch)**:

- `include/threadmaxx_simd/detail/avx2.hpp` — added
  `apply_transforms`, `integrate_linear_motion`, `transform_aabb`,
  plus `hmin_ps` / `hmax_ps` helpers for the AABB
  horizontal reduction. The Transform-stride-10 gather pattern
  used by `apply_transforms` and `integrate_linear_motion`
  pre-builds 10 distinct __m256i index vectors outside the loop.
- `include/threadmaxx_simd/{transform,aabb}_ops.hpp` — public
  dispatch updated. `apply_transforms` and `transform_aabb`
  route to AVX2; `integrate_linear_motion` stays scalar (see
  bench results below).
- `tests/simd/test_simd_avx2_equivalence_extended.cpp` —
  extended with `apply_transforms` / `integrate_linear_motion`
  / `transform_aabb` equivalence blocks. All 5 blocks pass.
- `bench/simd_kernels.cpp` — new opt-in benchmark covering
  every vectorized kernel at 1k / 16k / 256k entity counts.
  Outputs CSV. Comparison column ("throughput") carries
  the scalar-over-AVX2 speedup factor.
- `bench/CMakeLists.txt` — registers `simd_kernels` with
  `-mavx2 -mfma -O3` on x86_64.

**Bench results at 256k entities** (medians; tolerance ~5%):

| kernel                       | scalar     | AVX2       | speedup |
|------------------------------|-----------:|-----------:|--------:|
| `vec3_add`                   | 476 µs     | 406 µs     |   1.17× |
| `vec3_madd`                  | 437 µs     | 405 µs     |   1.08× |
| `vec3_dot`                   | 864 µs     | 242 µs     | **3.57×** |
| `frustum_cull`               | 2,499 µs   | 709 µs     | **3.53×** |
| `apply_transforms`           | 3,300 µs   | 2,676 µs   |   1.23× |
| `transform_aabb`             | 22,739 µs  | 6,744 µs   | **3.37×** |
| `vec3_normalize` (AVX2)      | 1,284 µs   | 1,801 µs   |   0.71× ❌ |
| `quat_normalize` (AVX2)      | 1,435 µs   | 1,767 µs   |   0.81× ❌ |
| `integrate_linear_motion` (AVX2) | 2,539 µs | 2,690 µs |   0.94× ❌ |

**Decisions driven by these numbers**:

- The three ❌ kernels' public dispatchers now route to scalar
  even when AVX2 is built (previously they dispatched to AVX2).
  The AVX2 implementations remain in `detail/avx2.hpp` so the
  equivalence tests keep gating their correctness, and so a
  future micro-opt (permute-based deinterleave for normalize,
  per-quat `_mm_hadd_ps` instead of `dp_ps`, SoA-shaped
  Transform storage for `integrate_linear_motion`) can plug
  back in by flipping a single `#if`.
- `vec3_add` and `vec3_madd` and `apply_transforms` keep AVX2
  dispatching despite modest speedup — memory-bandwidth-bound
  workloads where the AVX2 saves a meaningful absolute number
  of cycles even if the ratio is small.

**Still-deferred S4 kernels** (would-be-vectorized but I judged
the implementation effort vs. expected payoff):

- `simd::integrate_positions` — Transform-stride gather PLUS
  per-element quaternion composition (orientation *= delta_q
  + renormalize). Two layers of complexity; benchmark of the
  isolated linear path already showed Transform-stride AVX2 is
  a wash. Composition would compound the regression. Stays
  scalar; documented as not benchmark-justified.
- `simd::slerp` — needs vectorized `sin` / `cos`. No AVX2
  intrinsic; clean polynomial approximation is a multi-page
  implementation that's its own project. Plus the shortest-
  path branch + lerp-fallback complicates the lane masking.
  Stays scalar.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 103/103 on both trees** (was 102; +1 from
  `test_simd_avx2_equivalence_extended`).

**Effort for the shipped subset**: ~1 hour actual.

## Batch S5 — Runtime CPU probe + dispatch  ✅ landed 2026-05-18

**Shipped** — proper CPUID-based runtime feature detection.
`runtime_capabilities()` previously aliased the compile-time set
(S1); it now does a real probe of the host CPU and caches the
result in a function-local static.

**Files**:

- `include/threadmaxx_simd/cpu.hpp` — new. CPUID probe via
  `__get_cpuid_count` on GCC/Clang and `__cpuidex` on MSVC.
  Leaf 1 EDX bit 26 = SSE2; leaf 7 subleaf 0 EBX bit 5 = AVX2.
  Result cached in `runtime_capabilities()`. Sibling
  `preferred_isa_runtime()` mirrors `preferred_isa()` but
  uses the runtime view.
- `include/threadmaxx_simd/config.hpp` — `runtime_capabilities`
  removed; replaced by a pointer-comment to `cpu.hpp`.
- `include/threadmaxx_simd/threadmaxx_simd.hpp` umbrella —
  now includes `cpu.hpp`.
- `tests/simd/test_simd_config.cpp` — updated to include
  `cpu.hpp` and reflect the new "runtime is independent of
  compile-time" contract (the S1 assertion that
  `rtc.X == ctc.X` no longer holds).
- `tests/simd/test_simd_runtime_dispatch.cpp` — new. Verifies
  scalar invariant, cache stability across calls,
  `preferred_isa_runtime` consistency, AVX2-implies-SSE2 on
  x86, and that AVX2-supporting hosts pick `isa::avx2` as the
  preferred runtime ISA. On the dev host the probe correctly
  reports `scalar=1 sse2=1 avx2=1 neon=0`.

**Out of scope for S5** — the dispatch-table rewiring (where
the kernels themselves pick the best runtime backend rather
than the build-time backend). That requires switching from
`inline` compile-time `#if` dispatch to indirect calls
through function pointers, which has its own perf tradeoff
(branch-target buffer misses on cold paths). Not pursued
because the current "one binary = one backend" model is what
~all single-platform projects ship.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 104/104 on both trees** (was 103; +1 from
  `test_simd_runtime_dispatch`).

**Effort**: ~45 min actual.

## Batch S6 — SSE2 (or NEON) second backend — DEFERRED

Decision: defer indefinitely.

On the project's target hosts (x86_64 Linux, modern desktop /
workstation CPUs), the available ISAs are: scalar (always),
SSE2 (always), AVX (~2011+), AVX2 (~2013+), AVX-512 (~2017+,
spotty). The library currently ships:

- scalar (always)
- AVX2 (when build target has `-mavx2`)

SSE2 fills a niche between scalar and AVX2: hosts that have
SSE2 but not AVX2. That's pre-2013 Haswell-era hardware. The
dev target is 2017+, so SSE2 has no marginal value over
"scalar fallback" for our use case. For a binary intended to
ship to a broad audience including older hardware, SSE2 would
be worthwhile — but that's not what threadmaxx is targeting.

NEON applies to ARM hosts; the project is Linux-x86_64. Not
applicable.

If a future user needs SSE2 (or NEON), the implementation
template is the AVX2 backend (`detail/avx2.hpp`) plus a
matching `detail/sse2.hpp` (or `detail/neon.hpp`) and the
appropriate `#if THREADMAXX_SIMD_HAS_X` arms in the
dispatchers. The library's structure supports this without
further refactoring.

## Batch S5 — Runtime CPU probe + dispatch

**Goal**: when multiple backends are built (e.g., scalar + AVX2),
the kernel picks the best one at first call via a one-time CPUID
probe. Useful for distributing a single binary that uses AVX2 on
capable hardware and falls back on older machines.

**Test gate**:

- `test_simd_runtime_dispatch` — `runtime_capabilities()` returns
  consistent results across calls; `preferred_isa()` matches
  `runtime_capabilities()` on the host.
- A force-scalar override via env var (`THREADMAXX_SIMD_FORCE=scalar`)
  works.

**Files**:

- `include/threadmaxx_simd/cpu.hpp`
- `include/threadmaxx_simd/detail/dispatch.hpp`
- All `*_ops.hpp` updated to route through the dispatcher.

**Risks**:

- CPUID on x86 needs `__cpuid_count`-style intrinsics; not
  portable to MSVC syntax without `#if`s.
- Dispatch must inline cleanly or it becomes a per-call branch.

## Batch S6 — SSE2 (or NEON) second backend

Same shape as S3/S4 but for a second ISA. Pick based on coverage
gap: SSE2 covers older x86 hardware; NEON covers ARM. On a Linux
x86_64 dev workstation SSE2 is the natural pick.

## Batch S7 — Chunk integration  ✅ landed 2026-05-18 (test gate); benchmarks deferred

**Shipped (integration test)**:

- `tests/simd/test_simd_chunk_integration.cpp` — drives a real
  `threadmaxx::Engine` through one integration step using
  `forEachChunk<Transform, Velocity>` to fan out parallel
  chunk work, and `simd::integrate_linear_motion` to do the
  math. Per-chunk scratch (`std::vector<Transform>` +
  `std::vector<Vec3>` on the worker's stack frame) keeps the
  parallel path race-free. Compares each entity's post-step
  position against a scalar `pre + linVel * dt` reference;
  256 entities through 4 workers, all match.

  Demonstrates the contract from DESIGN_NOTES §7: engine
  chunks → SIMD kernels via `std::span` produce
  equivalent results to a scalar reference, with the
  parallel chunk dispatch firing cleanly.

  Engine flow gotcha learned: the seed CommandBuffer commits
  inside `engine.initialize`, NOT on the first `step()`.
  Entities are alive immediately after initialize returns;
  one `step()` is one integrator pass.

**Benchmark shipped 2026-05-18** — `bench/simd_kernels.cpp`
covers every vectorized kernel at 1k / 16k / 256k entity
counts, side-by-side scalar vs AVX2 with `LatencyHistogram`-
backed percentile reporting. Outputs CSV-compatible rows;
optional path argument writes to file too. Required
`-DTHREADMAXX_BUILD_BENCHMARKS=ON` to build (default off).
The bench numbers drove the S4 close-out dispatch decisions —
see §S4 above. Re-run when porting new kernels.

**Verification**:
- Both `build/` and `build-werror/` clean.
- **ctest 105/105 on both trees** (was 104; +1 from
  `test_simd_chunk_integration`).

**Effort**: ~30 min actual.

## Out of scope for the whole library

Per the design notes §9 — none of this is in scope at any batch:

- Full matrix library
- Inverse kinematics
- Cloth
- Animation blending trees
- Physics solver math
- Memory allocation
- New component types
- New storage formats
- Mandatory SIMD on all platforms
- Public reliance on `reinterpret_cast`
