# Standalone benchmark binaries. Opt-in via -DTHREADMAXX_BUILD_BENCHMARKS=ON.
# These are NOT registered with CTest — they print timings to stdout and
# exit, which is useful for ad-hoc profiling but noisy in CI.

set(THREADMAXX_BENCHMARKS
    commit_bench
    event_channel_bench
    hierarchy_bench
    cull_bench
    foreach_bench
    resource_handle_bench
    pack_instances_bench
    job_stealing_bench
    # §3.9.1 batch 16 — workload-realistic harness (the gate)
    chunk_iter_bench
    commit_path_bench
    # SHARDED_OPTIMISATION.md S0 — Pass A/B/C breakdown
    commit_pass_breakdown
    migration_bench
    grain_sweep
    # §3.10.4 batch 26 — Phase 8 gate: full-tick RPG-shaped stress
    rpg_stress_bench
    # §3.10.4 batch 27 — Phase 8 diagnostic probe (per-system / ablation / cmd-volume)
    rpg_stress_probe
    # §3.7 batch 31 diagnostic — wave dispatch overhead at high wave counts
    wave_dispatch_bench
    # §3.11.8 batch D8 — height-field + slope query workload
    terrain_query_bench
    # §3.11.9 batch D9 — particle burst-spawn / destroy stress
    particle_storm_bench
    # ADAPTIVE_TUNING.md T4 — observe-only overhead gate
    adaptive_tuning_bench
)

foreach(bench ${THREADMAXX_BENCHMARKS})
    add_executable(${bench} ${bench}.cpp)
    target_link_libraries(${bench} PRIVATE threadmaxx::threadmaxx)
    set_target_properties(${bench} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${bench} PRIVATE
            -Wall -Wextra -Wpedantic -O2)
    endif()
endforeach()

# §3.11.8 batch D8 — `terrain_query_bench` needs the header-only
# `Heightmap` from `examples/rpg_demo/`. Add the include dir for
# that bench only (it doesn't link `rpg_demo_core`, just consumes
# the header).
target_include_directories(terrain_query_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/rpg_demo)

# threadmaxx_navmesh N6 — batch path solver throughput. Links the
# sibling library directly; separated from the main bench list so it
# doesn't compile when navmesh is disabled.
if (TARGET threadmaxx::navmesh)
    add_executable(navmesh_batch_bench navmesh_batch_bench.cpp)
    target_link_libraries(navmesh_batch_bench PRIVATE threadmaxx::navmesh)
    set_target_properties(navmesh_batch_bench PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(navmesh_batch_bench PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# threadmaxx_animation A7 — crowd-evaluation throughput. Links the
# sibling library directly; separated from the main bench list so it
# doesn't compile when animation is disabled.
if (TARGET threadmaxx::animation)
    add_executable(animation_crowd_bench animation_crowd_bench.cpp)
    target_link_libraries(animation_crowd_bench PRIVATE threadmaxx::animation)
    set_target_properties(animation_crowd_bench PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(animation_crowd_bench PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# threadmaxx_physics P9 — Jolt-backed rigid-body throughput. Gated on
# Jolt availability (see src/threadmaxx_physics/CMakeLists.txt); when
# Jolt is absent the bench target is silently not created.
if (THREADMAXX_PHYSICS_HAS_JOLT)
    add_executable(physics_jolt_bench physics_jolt_bench.cpp)
    target_link_libraries(physics_jolt_bench PRIVATE threadmaxx::physics)
    set_target_properties(physics_jolt_bench PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(physics_jolt_bench PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# §S4 close-out — SIMD kernels bench. Separate from the core bench
# list because it needs `-mavx2 -mfma` on x86_64 to actually exercise
# the AVX2 path (the core library stays portable, so the AVX2 code
# only compiles when the consumer opts in via its own target flags).
if (TARGET threadmaxx::simd)
    add_executable(simd_kernels simd_kernels.cpp)
    target_link_libraries(simd_kernels PRIVATE threadmaxx::simd)
    set_target_properties(simd_kernels PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(simd_kernels PRIVATE
            -Wall -Wextra -Wpedantic -O3)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            target_compile_options(simd_kernels PRIVATE -mavx2 -mfma)
        endif()
    endif()
endif()
