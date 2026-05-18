# `threadmaxx_simd` — Maintainer Guide

Internal documentation for engineers extending or debugging the
SIMD library. For external usage, see `USER_GUIDE.md`.

## Architecture overview

```
                 ┌──────────────────────────────────────┐
                 │  Consumer code                       │
                 │  #include <threadmaxx_simd/...hpp>   │
                 └────────────────┬─────────────────────┘
                                  ▼
            ┌───────────────────────────────────────────┐
            │  Public kernel headers (one per family)   │
            │    vec3_ops.hpp / quat_ops.hpp /          │
            │    transform_ops.hpp / aabb_ops.hpp       │
            │                                           │
            │  Each kernel is a thin dispatcher:        │
            │    #if THREADMAXX_SIMD_HAS_AVX2           │
            │      detail::avx2::kernel(...);           │
            │    #else                                  │
            │      detail::scalar::kernel(...);         │
            │    #endif                                 │
            └────────────────┬──────────────────────────┘
                             ▼
        ┌────────────────────────────────────┬─────────────────┐
        │  detail/scalar.hpp                 │  detail/avx2.hpp │
        │  (always available — fallback)     │  (#if-gated)     │
        └────────────────────────────────────┴─────────────────┘

       Foundation (all consumed by everything above):
       ┌──────────────────────────────────────────────────────┐
       │  config.hpp    — ISA enum, capability POD, macros    │
       │  traits.hpp    — simd_batchable concept              │
       │  views.hpp     — span_view<T> adapter                │
       │  simd_math.hpp — float utils (rsqrt, clamp, min/max) │
       │  cpu.hpp       — runtime CPUID probe                 │
       └──────────────────────────────────────────────────────┘
```

The whole library is header-only. The CMake target
`threadmaxx::simd` is an INTERFACE that carries only the include
directory + `cxx_std_20` + a transitive link to
`threadmaxx::threadmaxx`. There are no `.cpp` source files.

## Dispatch model

### Compile-time

`config.hpp` defines three macros from the compiler's built-in
ISA defines:

- `THREADMAXX_SIMD_HAS_SSE2`  (1 iff `__SSE2__` or x64 / `_M_IX86_FP >= 2`)
- `THREADMAXX_SIMD_HAS_AVX2`  (1 iff `__AVX2__`)
- `THREADMAXX_SIMD_HAS_NEON`  (1 iff `__ARM_NEON__`)

The kernel-family headers (`vec3_ops.hpp` etc.) conditionally
`#include "detail/avx2.hpp"` only when the AVX2 macro is set —
this keeps the AVX2 source out of compilation entirely on
non-AVX2 builds (it uses `<immintrin.h>` which would fail to
compile on, say, ARM).

Each public kernel body is then a compile-time `#if` selecting
either `detail::avx2::kernel(...)` or `detail::scalar::kernel(...)`.
The selection is per-kernel — the dispatcher is allowed to route
some kernels through AVX2 and others through scalar, based on
benchmark evidence.

### Runtime

`cpu.hpp::runtime_capabilities()` probes the host CPU via CPUID
on x86 (`__get_cpuid_count` for GCC/Clang, `__cpuidex` for MSVC).
The result is cached in a function-local `static const`
(C++11 magic-statics give us thread-safe one-shot init).

The runtime view is **NOT** currently wired into kernel dispatch.
It's exposed for:

- Diagnostic logging.
- Future fat-binary scenarios (build with `-mavx2` AND ship a
  scalar fallback path; runtime dispatch picks per call). The
  dispatcher rewiring for this is documented but not implemented.

On ARM, the runtime view is honestly a compile-time alias —
there's no portable user-space NEON probe. We document this in
`cpu.hpp`'s header preamble.

### Why no runtime function-pointer dispatch?

It was deliberately deferred. Indirect calls through function
pointers prevent inlining of the kernel body, costing the
optimizer's ability to hoist invariants and fuse with surrounding
loops. For single-platform builds (the dominant case for
threadmaxx), compile-time dispatch is strictly better. If a fat-
binary need arises, the dispatch-table layer is the right next
step — but it should be opt-in, not the default path.

## Layout of `detail/avx2.hpp`

The file is organized by helper-first, then kernel-by-kernel:

1. Aliasing note + header comment.
2. Horizontal-reduction helpers (`horizontal_sum_ps`, `hmin_ps`,
   `hmax_ps`).
3. Vec3 kernels (flat-float pattern).
4. Quat kernels.
5. Frustum kernel.
6. Transform kernels (AoS↔SoA gather pattern).
7. AABB kernel (1-per-iter broadcast pattern).

### Two vectorization patterns

**Pattern (1): Flat-float.** When the input is a contiguous array
of densely-packed structs (e.g., `Vec3[]`), cast `Vec3*` → `float*`
and process 8 floats per iteration. Used by Vec3 add / sub / scale
/ madd / dot. Works because element-wise ops don't care about
Vec3 boundaries — the sum of `a[i].x*b[i].x + a[i].y*b[i].y +
a[i].z*b[i].z` summed over all `i` is identical to the sum of
pairwise float products in the flat stream.

**Pattern (2): Gather-based AoS↔SoA.** When per-element work
crosses field boundaries (normalize needs `x² + y² + z²`,
transforms need rotation per Vec3, AABB transform needs 8 corners
per AABB), use `_mm256_i32gather_ps` with pre-built strided
indices to load N elements into separate SoA registers, do the
math 8-wide, then scalar-scatter the results back.

Both patterns end with a scalar tail. For tails that share
significant code with the scalar reference (e.g., transform
kernels), we call `scalar::kernel(t.subspan(i), …)` instead of
open-coding the per-element math twice.

### Aliasing note

We `reinterpret_cast<Vec3*>(span.data())` to `float*` (and
similar for `Transform*`). This is UB per a strict reading of the
C++ aliasing rules. In practice, GCC / Clang / MSVC all treat it
as safe because:

- The struct is `standard_layout` with `float x` as its first
  member (pointer-interconvertible).
- The cast lives in `detail::` — user code never sees it.

If a future toolchain ever flags this, the fix is `std::launder` +
`std::memcpy` into a local AVX2 register, which compilers will
optimize back to the load anyway. Not pursued without evidence.

## Adding a new kernel

Roughly 6 steps. Use `apply_transforms` as the canonical example.

### 1. Reference implementation in `detail/scalar.hpp`

```cpp
inline void my_kernel(std::span<const Foo> in,
                      std::span<Bar> out) noexcept {
    const std::size_t n = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = compute(in[i]);
    }
}
```

Follow the conventions:
- `noexcept`.
- Bounded by `std::min` of all span sizes.
- Empty-span tolerant (no special case needed).
- Zero / NaN policy explicit in comments.

### 2. Public dispatcher in the matching `*_ops.hpp`

If a new kernel family, create `foo_ops.hpp`. Otherwise, append
to the existing one.

```cpp
inline void my_kernel(std::span<const Foo> in,
                      std::span<Bar> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::my_kernel(in, out);
#else
    detail::scalar::my_kernel(in, out);
#endif
}

// Plus a span_view overload if needed:
inline void my_kernel(span_view<const Foo> in, span_view<Bar> out) noexcept {
    my_kernel(in.values, out.values);
}
```

If you don't yet have an AVX2 impl (initial scalar landing), the
dispatcher just unconditionally calls `detail::scalar::my_kernel`.

### 3. Scalar test (`tests/simd/test_simd_FAMILY_ops.cpp`)

Verify:
- Correctness on fixed inputs.
- Tail sizes (1, 3, 7, 13 — non-multiples of typical lane widths).
- Mismatched-span policy.
- Empty-span policy.
- Edge cases (zero, NaN, identity).
- `span_view` overload routes identically.

### 4. Register the test

Add to `tests/simd/CMakeLists.txt`'s `THREADMAXX_SIMD_TESTS` list.

### 5. AVX2 implementation in `detail/avx2.hpp`

Two design questions to answer first:

- **Pattern fit**: does the kernel cross field boundaries
  (gather/scatter) or stay element-wise (flat-float)?
- **Expected win**: scalar instruction count × N vs.
  vectorized cycle count per 8 elements (rough estimate). If
  gather latency × 6 ≈ scalar cost, expect a tie. Don't write
  AVX2 just to write it; the benchmark must justify it.

Implement, document the approach in the function's doc comment.

### 6. Equivalence test

Extend `tests/simd/test_simd_avx2_equivalence_extended.cpp`
(or `test_simd_vec3_avx2_equivalence.cpp` for Vec3 family):

```cpp
{
    // ---- N. my_kernel equivalence -------------------------
    const std::vector<std::size_t> sizes = {
        0, 1, 7, 8, 9, 16, 17, 31, 64, 257, 1024
    };
    for (std::size_t n : sizes) {
        std::vector<Foo> in = make_random_input(n, rng);
        std::vector<Bar> outScalar(n), outAvx2(n);
        scalar::my_kernel(in, outScalar);
        avx2::my_kernel  (in, outAvx2);
        for (std::size_t i = 0; i < n; ++i) {
            if (!approxEq(outScalar[i], outAvx2[i])) {
                CHECK(false);
            }
        }
    }
}
```

Cover sub-lane (n=1), exact-lane (n=8), mismatched-lane (n=7,
n=17), and large (n=1024) sizes so off-by-ones at the boundary
surface in the equivalence test, not in production.

### 7. Benchmark

Add a block to `bench/simd_kernels.cpp`:

```cpp
{
    LatencyHistogram hs, hv;
    runIters(hs, kWarmup, kIters,
        [&]{ sca::my_kernel(s.in, s.out); escape(s.out[0]); });
    runIters(hv, kWarmup, kIters,
        [&]{ vec::my_kernel(s.in, s.out); escape(s.out[0]); });
    emitPair(csv, "my_kernel", n, hs, hv);
}
```

Run it. **If AVX2 isn't faster, revert the public dispatcher to
scalar.** Document the regression in the dispatcher comment with
the measured numbers and what would need to change for AVX2 to
win. The AVX2 impl stays in `detail/avx2.hpp` — the equivalence
test continues to gate its correctness.

## Adding a new backend (e.g., SSE2 or NEON)

The library is structured to make this drop-in. Roughly:

1. Create `detail/<backend>.hpp` (e.g., `detail/sse2.hpp`).
2. Wrap the whole file in `#if THREADMAXX_SIMD_HAS_<BACKEND>` so
   it doesn't even open on non-target builds.
3. For each kernel you implement: ship in `detail::<backend>::`
   namespace with the same signature as the scalar version.
4. Extend the dispatcher in `*_ops.hpp`:

```cpp
inline void my_kernel(...) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::my_kernel(...);
#elif THREADMAXX_SIMD_HAS_NEON
    detail::neon::my_kernel(...);
#elif THREADMAXX_SIMD_HAS_SSE2
    detail::sse2::my_kernel(...);
#else
    detail::scalar::my_kernel(...);
#endif
}
```

The priority order (AVX2 > NEON > SSE2 > scalar) matches
`preferred_isa_from`.

5. Write an equivalence test (`test_simd_<backend>_equivalence.cpp`)
   following the AVX2 test pattern. Add `-m<backend>` flags to
   just that test in `tests/simd/CMakeLists.txt`.
6. Extend `bench/simd_kernels.cpp` with the new backend's kernels.

A backend that's slower than scalar on the measured workload
should NOT be wired into the dispatcher even if implemented.
Same rule as for individual kernels.

## Testing strategy

Three layers, all in `tests/simd/`:

1. **Reference correctness** (`test_simd_*_ops.cpp`) — verifies
   the scalar implementation against analytical results. Catches
   math bugs in the reference.
2. **Backend equivalence** (`test_simd_*_equivalence*.cpp`) —
   verifies AVX2 produces identical (within float tolerance)
   results to scalar across a sweep of input sizes including
   lane-boundary cases. Catches bugs in vectorization.
3. **Engine integration** (`test_simd_chunk_integration.cpp`) —
   verifies the contract from `DESIGN_NOTES.md §7` end-to-end on
   a real `threadmaxx::Engine`, including parallel chunk
   dispatch. Catches API contract violations.

All tests use the project-wide `Check.hpp` harness — one
executable per test, non-zero exit means failure.

### Test invariants you can rely on

- `simd.test_simd_*_ops` runs on every build (scalar or AVX2).
- `simd.test_simd_*_equivalence` is no-op-on-pass when AVX2 isn't
  built; runs the real comparison when `-mavx2` is set on the
  test target.
- `simd.test_simd_chunk_integration` exercises a 4-worker engine
  step and is the strongest gate against API regressions.

### Adding tolerance for new kernels

Equivalence test tolerances we've used:

- Element-wise ops (add/sub/scale): `1e-5` absolute, `1e-6` relative.
- FMA-using ops (madd): `1e-4` absolute, `1e-5` relative (FMA
  drift vs `mul+add`).
- Accumulators (dot): `1e-3 * scale` where scale = result magnitude.
- Quaternion ops: `1e-6` absolute + relative (full-precision sqrt+div).

If a new kernel has different precision characteristics, document
the chosen tolerance in the test file's comment block.

## Benchmark interpretation

`bench/simd_kernels.cpp` emits CSV rows with the shared `BenchRow`
schema from `bench/common.hpp`. The columns of interest:

| Column      | Meaning                                                       |
|-------------|---------------------------------------------------------------|
| `label`     | `kernel/scalar` or `kernel/avx2`                              |
| `entities`  | Array size for the kernel call                                |
| `mean_ns`   | Mean per-iteration wall-clock                                 |
| `p50/p95/p99_ns` | Latency percentiles (32 iterations after 4 warmup)        |
| `throughput`| For AVX2 rows: speedup factor over the matching scalar row    |

### Rule of thumb for the dispatcher

- **> 1.5× speedup**: dispatch AVX2.
- **1.05× – 1.5× speedup**: dispatch AVX2 (memory-bound wins are
  small but real).
- **< 1.05× speedup or regression**: dispatch scalar. Keep the
  AVX2 impl in `detail/avx2.hpp` for the equivalence test +
  future revisit.

### Sources of noise

- The mean can be unstable under load. Trust the median (`p50_ns`).
- The first run after a fresh boot can be slower (caches cold).
  The 4-iteration warmup before measurements helps but doesn't
  eliminate it.
- Frequency scaling: pin the CPU governor to `performance` (Linux)
  when measuring. The bench is sensitive to turbo state.

## Common pitfalls

### "I added an AVX2 kernel but the test passes and the bench shows it's slower"

That's the expected workflow — equivalence test gates correctness,
benchmark gates dispatch. Revert the dispatcher to scalar.

### "The equivalence test segfaults at large sizes"

Usually a gather-index bug. AVX2 gather doesn't bounds-check —
if your stride indices reach beyond the input span, you'll
crash. Verify the index pattern matches your stride model.

### "Output is correct for n=8 but wrong for n=7"

Tail handling. Either the scalar tail loop has different math
than the AVX2 main loop (FMA vs. mul+add ordering), or the main
loop processed one element too many. The equivalence test's
small-n cases catch both.

### "The dispatcher routes to AVX2 even though I didn't pass -mavx2"

Look for `add_compile_options(-mavx2)` somewhere up the CMake
hierarchy. Or the test target has `-mavx2` baked in (the SIMD
equivalence tests do — see `tests/simd/CMakeLists.txt`).

### "Building with -mavx2 changes the result of a kernel"

If the change is within the documented tolerance, the AVX2 path
is winning — that's correctness preservation, not a regression.
If it's a true mismatch, the equivalence test should catch it.

### "An x86 user reports SIGILL on launch"

The build was compiled with `-mavx2` but ran on a non-AVX2 CPU.
The library's `runtime_capabilities()` would correctly report
`avx2=false`, but by then it's too late — the binary loader
already executed the AVX2 instructions. Solution: either build
without `-mavx2` (scalar fallback) or implement runtime
dispatch (deferred).

### "The umbrella header pulls in `<immintrin.h>` even on non-AVX2 builds"

It shouldn't. `<immintrin.h>` is included only inside
`detail/avx2.hpp`, which is itself `#if`-gated. The umbrella
header conditionally includes `detail/avx2.hpp` only via the
`*_ops.hpp` headers, which guard on the same macro. If you see
`<immintrin.h>` opening unexpectedly, check whether someone
included `detail/avx2.hpp` directly without the guard.

## Repository layout (engineer-facing)

```
include/threadmaxx_simd/
├── README.md                  # Top-level overview / status badge
├── CHANGELOG.md               # Per-release notes
├── DESIGN_NOTES.md            # Original spec (don't edit unless re-scoping)
├── FUTURE_WORK.md             # Batch-by-batch landed work + v1.x candidates
├── USER_GUIDE.md              # User-facing docs
├── MAINTAINER_GUIDE.md        # This file
├── threadmaxx_simd.hpp        # Umbrella include
├── version.hpp                # Library version macros + version_string()
├── config.hpp                 # Compile-time feature detection
├── cpu.hpp                    # Runtime CPUID probe
├── traits.hpp                 # simd_batchable concept
├── views.hpp                  # span_view<T>
├── simd_math.hpp              # rsqrt / clamp / min / max
├── vec3_ops.hpp               # Public Vec3 kernels
├── quat_ops.hpp               # Public Quat kernels
├── transform_ops.hpp          # Public Transform kernels
├── aabb_ops.hpp               # Public AABB + Frustum kernels
└── detail/
    ├── scalar.hpp             # Reference scalar impls (all families)
    └── avx2.hpp               # AVX2 impls (all families, #if-gated)

tests/simd/
├── CMakeLists.txt             # Test registration + -mavx2 wiring
├── test_simd_config.cpp       # Capability struct + preferred_isa
├── test_simd_traits.cpp       # simd_batchable concept
├── test_simd_views.cpp        # span_view round-trip
├── test_simd_vec3_ops.cpp     # Vec3 scalar correctness
├── test_simd_quat_ops.cpp     # Quat scalar correctness
├── test_simd_transform_ops.cpp # Transform scalar correctness
├── test_simd_aabb_ops.cpp     # AABB + Frustum scalar correctness
├── test_simd_vec3_avx2_equivalence.cpp   # Vec3 scalar ↔ AVX2
├── test_simd_avx2_equivalence_extended.cpp # Other families scalar ↔ AVX2
├── test_simd_runtime_dispatch.cpp        # CPUID probe consistency
└── test_simd_chunk_integration.cpp       # forEachChunk × SIMD end-to-end

bench/
├── common.hpp                 # Shared BenchRow / LatencyHistogram / CsvWriter
└── simd_kernels.cpp           # Scalar vs AVX2 perf comparison
```

## Library version (`version.hpp`)

The library exposes a semver version via macros and a constexpr
function:

```cpp
#define THREADMAXX_SIMD_VERSION_MAJOR 1
#define THREADMAXX_SIMD_VERSION_MINOR 0
#define THREADMAXX_SIMD_VERSION_PATCH 0
#define THREADMAXX_SIMD_VERSION (MAJOR*10000 + MINOR*100 + PATCH)

constexpr const char* version_string() noexcept;  // → "1.0.0"
```

When bumping, update **both** the macros AND the string literal
returned by `version_string()`. Also append a section to
`CHANGELOG.md`.

## Versioning / ABI

The library is header-only, so there's no binary ABI to break.
Source ABI considerations:

- **Public signatures (`*_ops.hpp`)** are stable. Adding a new
  overload is fine; changing an existing one is not.
- **`span_view<T>`** is a public type with a stable shape
  (`.values` member, `.data() / .size() / .empty()` methods).
- **`capabilities` POD + `isa` enum** are stable.
- **`detail::scalar::*` and `detail::avx2::*`** are internal —
  consumers should not call them directly (the equivalence tests
  and bench are the only allowed exceptions, and they live in
  the repo).
- **`simd_math.hpp` utilities** are stable.

When evolving:
- Add new kernels via the workflow above. Adding never breaks.
- Removing a kernel requires a deprecation cycle. Mark with
  `[[deprecated]]`, ship one release, then remove.
- Layout changes to `capabilities` or `isa` are breaking; bump
  to a major version.

## See also

- `DESIGN_NOTES.md` — original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + open follow-ups.
- `USER_GUIDE.md` — user-facing API reference.
- `/CLAUDE.md` (repo root) — meta-instructions for AI-assisted
  development of `threadmaxx` and its sibling libraries.
