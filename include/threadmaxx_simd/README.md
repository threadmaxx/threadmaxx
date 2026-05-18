# `threadmaxx_simd`

Header-only SIMD batch kernels for the `threadmaxx` engine.
**Status**: v1.0.0 — production-ready.

## What

Vectorized math kernels over `std::span<T>` of the engine's PODs
(`Vec3`, `Quat`, `Transform`, `BoundingVolume`). Designed to plug
into `forEachChunk`-style iteration without owning memory or
changing engine layouts.

Two execution paths share one public API surface:

- **Scalar fallback** — always available, always correct, no
  compile-time flags required.
- **AVX2** — opt-in via `-mavx2 -mfma` on the consumer's compile
  target. Kernels measured to win on the host workload route here
  automatically.

Eleven public kernels across four families; see
[`USER_GUIDE.md`](USER_GUIDE.md) for the inventory.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::simd)
# Optional: opt into AVX2 on x86_64 Linux/macOS.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT MSVC)
    target_compile_options(my_target PRIVATE -mavx2 -mfma)
endif()
```

```cpp
#include <threadmaxx_simd/threadmaxx_simd.hpp>

void integrate(std::span<threadmaxx::Transform> transforms,
               std::span<const threadmaxx::Vec3> velocities,
               float dt) {
    threadmaxx::simd::integrate_linear_motion(transforms, velocities, dt);
}
```

## Documentation

| Document | Audience | Purpose |
|----------|----------|---------|
| [`README.md`](README.md) | Everyone | Top-level overview (this file) |
| [`USER_GUIDE.md`](USER_GUIDE.md) | Consumers | Kernel inventory, integration patterns, perf expectations |
| [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) | Library devs | Architecture, dispatch, how to add kernels / backends |
| [`DESIGN_NOTES.md`](DESIGN_NOTES.md) | Anyone | Original spec (frozen reference) |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | Library devs | Batch history + v1.x candidates |
| [`CHANGELOG.md`](CHANGELOG.md) | Everyone | Per-release notes |

## Scope

- ✓ Batch math over contiguous arrays of POD types.
- ✓ Span-first API, no allocation, no ownership.
- ✓ Scalar fallback always present.
- ✓ Benchmark-driven dispatch choices (only ship AVX2 paths that
  actually win).

Out of scope (per `DESIGN_NOTES.md` §9): full matrix library, IK,
cloth, physics solvers, animation blending trees, allocation, new
component types.

## Status: production-ready

- 11 public kernels, all backed by equivalence + correctness tests.
- 8 dedicated test executables registered with CTest (100% passing
  on `build/` and `build-werror/` trees).
- Benchmark harness with empirical perf data backing every
  dispatch decision.
- Two user-facing docs (this README + `USER_GUIDE.md`) and one
  maintainer doc (`MAINTAINER_GUIDE.md`).
- Versioning policy documented (semver, lifecycle in
  `MAINTAINER_GUIDE.md`).

See [`FUTURE_WORK.md`](FUTURE_WORK.md) for v1.x candidate work
(none of which blocks v1.0 production use).

## License

Same as the parent `threadmaxx` project.
