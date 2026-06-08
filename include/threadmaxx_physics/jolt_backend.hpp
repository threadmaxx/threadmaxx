#pragma once

#include "threadmaxx_physics/backend.hpp"

#include <memory>

/// Factory for the bundled `JoltBackend` — the recommended real solver
/// adapter, gated by `find_package(Jolt)` (or the opt-in
/// `THREADMAXX_PHYSICS_FETCH_JOLT` FetchContent fallback) at configure
/// time.
///
/// **Availability.** The factory ALWAYS links — the header itself never
/// includes any Jolt symbol — but `makeJoltBackend()` returns `nullptr`
/// when the CMake gate found no Jolt. Callers branch on
/// `joltBackendAvailable()` (or simply on the returned pointer) and
/// fall back to `makeStubBackend()` in that case.
///
/// **Determinism profile.** When `Jolt` was built with
/// `CROSS_PLATFORM_DETERMINISTIC=ON` (the default in our FetchContent
/// path) and the host uses a fixed timestep with
/// `PhysicsConfig::allowSolverThreading = false`, two side-by-side
/// JoltBackend instances driven with identical inputs reach
/// bit-identical state. This is the configuration the
/// `test_physics_jolt_conformance` gate exercises. Outside that
/// profile, conformance tolerances are documented per-test.
///
/// **Recommended Jolt version.** Pinned to upstream `v5.3.0` in
/// `src/threadmaxx_physics/CMakeLists.txt`. Bumping the tag is a
/// documented upgrade: rerun the conformance + smoke tests against the
/// new tag, adjust the per-test tolerances if real-physics drift crosses
/// the bounds, bump the pin in the same PR.
namespace threadmaxx::physics {

/// Construct a fresh JoltBackend instance. Returns `nullptr` when the
/// library was configured without Jolt; otherwise returns a
/// `unique_ptr` suitable for handing to `PhysicsScene` (or for direct
/// use through the `IPhysicsBackend` surface).
///
/// The factory eagerly initializes Jolt's global registry on first
/// call (`JPH::RegisterDefaultAllocator()`, `JPH::Factory::sInstance`,
/// `JPH::RegisterTypes()`) and is safe to call multiple times — the
/// init is idempotent under an internal `std::call_once`. Backends
/// share the global registry but otherwise own per-instance allocators,
/// job systems, and worlds.
std::unique_ptr<IPhysicsBackend> makeJoltBackend();

/// Compile-time hint for whether the Jolt backend is wired into this
/// build. `true` when `THREADMAXX_PHYSICS_HAS_JOLT` was defined at
/// library-compile time, `false` otherwise. Tests / games can use this
/// to skip Jolt-only setup paths without paying a runtime null check.
constexpr bool joltBackendAvailable() noexcept {
#if defined(THREADMAXX_PHYSICS_HAS_JOLT)
    return true;
#else
    return false;
#endif
}

} // namespace threadmaxx::physics
