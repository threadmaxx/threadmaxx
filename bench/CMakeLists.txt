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
