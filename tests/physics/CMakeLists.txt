# threadmaxx_physics test suite (P1+).
#
# Each test links `threadmaxx::physics` (which transitively pulls in
# `threadmaxx::threadmaxx` for the math PODs) and uses the tiny
# `Check.hpp` harness from the parent `tests/` directory. Registered
# with CTest under the `physics.*` name prefix.

set(THREADMAXX_PHYSICS_TESTS
    test_physics_types_layout
    test_physics_stub_create_destroy
    test_physics_stub_step_noop
    test_physics_backend_conformance
    test_physics_shape_create
    test_physics_shape_share
    test_physics_shape_compound
    test_physics_body_lifecycle
    test_physics_body_kinematic_teleport
    test_physics_body_sync_batch
    test_physics_step_linear
    test_physics_step_angular
    test_physics_step_determinism
    test_physics_raycast_hit
    test_physics_raycast_miss
    test_physics_raycast_layer_filter
    test_physics_sweep_clear_path
    test_physics_overlap
    test_physics_constraint_create
    test_physics_constraint_destroy
    test_physics_constraint_disable_self_collision
    test_physics_character_move_flat
    test_physics_character_step_up
    test_physics_character_slope_limit
    test_physics_character_grounded
    test_physics_contact_begin
    test_physics_contact_end
    test_physics_contact_stable
)

# Jolt-backed tests are only registered when the CMake gate found Jolt
# (see src/threadmaxx_physics/CMakeLists.txt). Gate-off builds still
# compile the JoltBackend.cpp stub but `makeJoltBackend()` returns
# nullptr there — running the smoke/conformance tests in that mode
# would fail to assert and waste CTest cycles, so we skip registration.
if (THREADMAXX_PHYSICS_HAS_JOLT)
    list(APPEND THREADMAXX_PHYSICS_TESTS
        test_physics_jolt_smoke
        test_physics_jolt_conformance
    )
endif()

foreach(test ${THREADMAXX_PHYSICS_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::physics)
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
    add_test(NAME physics.${test} COMMAND ${test})
endforeach()
