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
    migration_bench
    grain_sweep
    # §3.10.4 batch 26 — Phase 8 gate: full-tick RPG-shaped stress
    rpg_stress_bench
    # §3.10.4 batch 27 — Phase 8 diagnostic probe (per-system / ablation / cmd-volume)
    rpg_stress_probe
    # §3.7 batch 31 diagnostic — wave dispatch overhead at high wave counts
    wave_dispatch_bench
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
