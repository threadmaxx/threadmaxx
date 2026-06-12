# threadmaxx_animation test suite (A1+).
#
# Each test links `threadmaxx::animation` (which transitively pulls in
# `threadmaxx::threadmaxx`) and uses the tiny `Check.hpp` harness from
# the parent `tests/` directory. Registered with CTest under the
# `animation.*` name prefix.

set(THREADMAXX_ANIMATION_TESTS
    test_animation_skeleton
    test_animation_clip
    test_animation_pose_buffer
    test_animation_pose_math
    test_animation_clip_sampling
    test_animation_event_tracks
    test_animation_graph_basic
    test_animation_graph_param
    test_animation_graph_dirty
    test_animation_blend1d
    test_animation_blend2d
    test_animation_additive
    test_animation_layer_mask
    test_animation_ik_2bone
    test_animation_ik_chain
    test_animation_ik_unreachable
    test_animation_ik_weight
    test_animation_warp_root
    test_animation_warp_window
    test_animation_warp_blend
    test_animation_crowd_smoke
    test_animation_crowd_lod
    test_animation_crowd_pool
    test_animation_diagnostics
    test_animation_retarget_smoke
    test_animation_serialization_roundtrip
    test_animation_animator_stats
)

foreach(test ${THREADMAXX_ANIMATION_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::animation)
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
    add_test(NAME animation.${test} COMMAND ${test})
endforeach()
