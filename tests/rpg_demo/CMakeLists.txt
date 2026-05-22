# §3.11 batch D-audit — RPG demo test suite.
#
# Each test is a standalone executable linked against `rpg_demo_core`
# (the static library carrying every system + game-logic .cpp). Tests
# exercise the actual shipped code, not duplicated re-implementations.
# GLFW is a transitive dep of `rpg_demo_core` (for the global Input
# state); tests manipulate the input state directly without needing a
# window.

set(RPG_DEMO_TESTS
    test_round_trip
    test_combat
    test_npc_brain
    test_pickup
    test_input_edges
    test_skip_policy
    test_quests
    test_determinism
    test_animation
    test_preload
    test_asset_reload
    test_obj_loader
    # §3.11.8 batch D8 — heightmap + slope-reject contract.
    test_terrain_lookup
    # §3.11.9 batch D9 — particle aging + destroy contract.
    test_particle_lifetime
)

foreach(test ${RPG_DEMO_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE rpg_demo_core)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
    add_test(NAME rpg_demo.${test} COMMAND ${test})
endforeach()
