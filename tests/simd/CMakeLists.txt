# threadmaxx_simd test suite (S1+).
#
# Each test executable links against `threadmaxx::simd` (which itself
# transitively links `threadmaxx::threadmaxx`) and uses the tiny
# `Check.hpp` harness from the parent `tests/` directory. The suite
# is registered with CTest under the `simd.*` name prefix.

set(THREADMAXX_SIMD_TESTS
    test_simd_config
    test_simd_traits
    test_simd_views
    test_simd_vec3_ops
    test_simd_quat_ops
    test_simd_transform_ops
    test_simd_aabb_ops
    test_simd_vec3_avx2_equivalence
    test_simd_avx2_equivalence_extended
    test_simd_runtime_dispatch
    test_simd_chunk_integration
)

foreach(test ${THREADMAXX_SIMD_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::simd)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME simd.${test} COMMAND ${test})
endforeach()

# §S3 — the AVX2 equivalence test needs the AVX2 path actually
# compiled in. Force `-mavx2 -mfma` on it (and only it) when building
# for x86_64 with GCC / Clang. Without this flag, the test
# preprocesses the AVX2 code path out and prints a "skipped" line
# (still a PASS). With it, the test exercises the AVX2 path against
# the scalar reference. The main `threadmaxx::simd` INTERFACE
# library stays portable — users opt in by setting `-mavx2` on their
# own compile target.
if (NOT MSVC AND (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"))
    target_compile_options(test_simd_vec3_avx2_equivalence PRIVATE
        -mavx2 -mfma)
    target_compile_options(test_simd_avx2_equivalence_extended PRIVATE
        -mavx2 -mfma)
endif()
