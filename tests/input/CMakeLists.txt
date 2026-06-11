# threadmaxx_input test suite (I1+).
#
# Each test links `threadmaxx::input` and uses the tiny `Check.hpp` harness
# from the parent `tests/` directory. Registered with CTest under the
# `input.*` name prefix.

set(THREADMAXX_INPUT_TESTS
    test_input_state_pod
    test_input_event_variant
    test_input_context_lifecycle
    test_input_no_allocations
    test_input_no_engine_link
    test_input_keys_held
    test_input_keys_released
    test_input_mouse_buttons
    test_input_mouse_motion
    test_input_char_queue
    test_input_modifier_mask
    test_input_action_id
    test_input_binding_single_source
    test_input_binding_multi_source
    test_input_modifier_exact_match
    test_input_binding_serialize
    test_input_action_no_allocations
    test_input_gamepad_connect
    test_input_gamepad_buttons
    test_input_gamepad_axis_deadzone
    test_input_gamepad_axis_pair_radial
    test_input_gamepad_action_binding
    test_input_gamepad_hotunplug
    test_input_cursor_mode_default
    test_input_cursor_locked_relative
    test_input_capture_mouse
    test_input_capture_keyboard
    test_input_capture_clear_on_release
    test_input_picking_identity_camera
    test_input_picking_round_trip
    test_input_picking_viewport_offset
    test_input_picking_far_plane
    test_input_picking_no_allocations
)

# UI-bridge tests opt in when threadmaxx::ui is available — the bridge
# itself is compiled only in that case (see src/threadmaxx_input/CMakeLists.txt).
if (TARGET threadmaxx::ui)
    list(APPEND THREADMAXX_INPUT_TESTS
        test_input_ui_bridge_mouse
        test_input_ui_bridge_keys
        test_input_ui_bridge_capture
        test_input_ui_bridge_no_allocations
    )
endif()

foreach(test ${THREADMAXX_INPUT_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::input)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
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
    add_test(NAME input.${test} COMMAND ${test})
endforeach()
