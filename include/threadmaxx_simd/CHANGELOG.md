# Changelog

All notable changes to `threadmaxx_simd` are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/);
the project adheres to [Semantic Versioning](https://semver.org/).
See [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) for the bump rules.

## [1.0.0] — 2026-05-18 — Production-ready close-out

### Added

- **`nlerp(a, b, out, alpha)`** — normalized-linear interpolation for
  Quat, with shortest-path flip. Faster alternative to `slerp` when
  full constant-angular-velocity isn't needed (animation blending).
- **`version.hpp`** — `THREADMAXX_SIMD_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_SIMD_VERSION` integer, and
  `version_string()` for runtime logging.
- **`README.md`** — top-level library overview with quick start +
  doc cross-references.
- **`USER_GUIDE.md`** — user-facing documentation (kernel
  inventory, integration patterns, perf expectations).
- **`MAINTAINER_GUIDE.md`** — internal documentation (architecture,
  dispatch, how to add kernels / backends, common pitfalls).
- **`CHANGELOG.md`** — this file.

### Fixed (audit pass)

- Umbrella header now correctly includes `cpu.hpp` (was claimed in
  S5 batch notes but missing in code).
- Doc-lag comments throughout (`detail/avx2.hpp` header claiming
  `normalize` wasn't in the file; `*_ops.hpp` headers referencing
  outdated "S1 / S2 — always scalar" semantics).
- `hmin_ps` / `hmax_ps` hoisted next to `horizontal_sum_ps` —
  reduction helpers now live together at the top of the AVX2
  namespace.
- `compile_time_capabilities()` redundant `c.scalar = true;`
  removed (already default-initialized).
- `cpu.hpp` documents the deliberate skip of `XGETBV` (OS YMM
  state) and the honest ARM compile-time alias for
  `runtime_capabilities`.

### Closed-out roadmap

`FUTURE_WORK.md` v1.0 closure section documents what's shipped
vs. deferred to v1.x candidate batches.

---

## [0.5.0] — 2026-05-18 — Batch S4 close-out

### Added

- AVX2 implementations of `apply_transforms`,
  `integrate_linear_motion`, `transform_aabb` (in
  `detail/avx2.hpp`).
- `bench/simd_kernels.cpp` — opt-in scalar-vs-AVX2 benchmark at
  1k / 16k / 256k entity counts (CSV output).
- `hmin_ps` / `hmax_ps` horizontal-reduction helpers.
- Equivalence-test coverage for the three new kernels.

### Changed (benchmark-driven dispatch)

- `apply_transforms` → AVX2 (1.23× win).
- `transform_aabb` → AVX2 (3.37× win).
- `integrate_linear_motion` → reverted to scalar dispatch
  (AVX2 is 0.94× / a slight regression; Transform-stride gather
  overhead exceeds the integration body cost). AVX2 impl retained
  in `detail/avx2.hpp` for the equivalence test + future revisit.
- `vec3_normalize` → reverted to scalar (AVX2 is 0.71×).
- `quat_normalize` → reverted to scalar (AVX2 is 0.81×;
  `_mm256_dp_ps` latency dominates).

---

## [0.4.0] — 2026-05-18 — Batches S5 + S7

### Added

- **S5**: `cpu.hpp` with real CPUID-based runtime feature probe
  (`__get_cpuid_count` on GCC/Clang; `__cpuidex` on MSVC). Result
  cached in a function-local static. `preferred_isa_runtime()`
  mirrors the compile-time variant.
- **S5**: `runtime_capabilities()` moved from `config.hpp` to
  `cpu.hpp` (compile-time / runtime views are now independent).
- **S5**: `test_simd_runtime_dispatch.cpp` — probe consistency
  + AVX2-implies-SSE2 invariant.
- **S7**: `test_simd_chunk_integration.cpp` — drives a real
  `threadmaxx::Engine` through `forEachChunk<Transform, Velocity>`
  + `simd::integrate_linear_motion`, verifies parallel chunk
  dispatch produces the expected positions.

---

## [0.3.0] — 2026-05-18 — Batch S4 partial

### Added

- AVX2 implementations of `quat_normalize` (2 quats per `__m256`
  via `_mm256_dp_ps`) and `frustum_cull` (8 spheres per iteration
  via gather + 6-plane survival reduction + `movemask` pack).
- `test_simd_avx2_equivalence_extended.cpp` covering the new
  kernels.

### Documented

- 5 S4 kernels deferred with rationale (`integrate_positions`,
  `slerp`, `apply_transforms`, `transform_aabb`, plus the
  `integrate_linear_motion` whose AVX2 impl was contemplated but
  not yet shipped at this point).

---

## [0.2.5] — 2026-05-18 — Batch S3.5

### Added

- AVX2 `normalize(Vec3)` — gather-based AoS↔SoA + sqrt+div with
  zero-mask blend. Completes AVX2 coverage of the Vec3 family.

---

## [0.2.0] — 2026-05-18 — Batch S3

### Added

- `detail/avx2.hpp` — first vectorized backend. AVX2
  implementations of `add` / `sub` / `scale` / `madd` / `dot` for
  Vec3 via flat-float pattern.
- `test_simd_vec3_avx2_equivalence.cpp` — RNG-seeded scalar vs.
  AVX2 equivalence across a 20-point size sweep.
- Compile-time dispatch via `THREADMAXX_SIMD_HAS_AVX2` macro;
  build target stays portable.

---

## [0.1.0] — 2026-05-17 — Batches S1 + S2

### Added

- **S1**: Foundations — `config.hpp` (ISA enum + capability POD),
  `traits.hpp` (`simd_batchable` concept), `views.hpp`
  (`span_view<T>` adapter), `simd_math.hpp` (rsqrt / clamp /
  min / max), `detail/scalar.hpp` (scalar Vec3 kernels),
  `vec3_ops.hpp` (public API).
- **S2**: Scalar Quat / Transform / AABB / Frustum kernels
  (`quat_normalize`, `quat_slerp`, `apply_transforms`,
  `integrate_positions`, `integrate_linear_motion`,
  `transform_aabb`, `frustum_cull`).
- Top-level CMake target `threadmaxx::simd` (INTERFACE).
- 7 dedicated test executables.
