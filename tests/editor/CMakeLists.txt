set(THREADMAXX_EDITOR_TESTS
    test_editor_version
    test_editor_session_attach
    test_editor_headless_backend_capture
    test_editor_session_destroy_order
    test_editor_inspector_entities
    test_editor_inspector_resources
    test_editor_inspector_systems
    test_editor_inspector_entity_specific
    test_editor_command_basic
    test_editor_command_stack_redo
    test_editor_command_stack_new_after_undo
    test_editor_command_clear
    test_editor_hotreload_request
    test_editor_hotreload_event_observed
    test_editor_hotreload_cancel
    test_editor_telemetry_basic
    test_editor_telemetry_config_off
    test_editor_telemetry_system_stats
    test_editor_selection_set
    test_editor_selection_clear
    test_editor_selection_stale_entity
    test_editor_property_read
    test_editor_property_edit
    test_editor_property_custom_type
    test_editor_gizmo_handle_positions
    test_editor_gizmo_hit_test
    test_editor_gizmo_drag_translate
    test_editor_diff_identical
    test_editor_diff_added
    test_editor_diff_modified
    test_editor_diff_removed
    test_editor_layout_roundtrip
    test_editor_console_exec
    test_editor_console_history
    test_editor_hierarchy_flat
    test_editor_hierarchy_chain
    test_editor_hierarchy_collapse
)

foreach(test ${THREADMAXX_EDITOR_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::editor)
    target_include_directories(${test} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME editor.${test} COMMAND ${test})
endforeach()

# E11 — ImGui backend tests only when the backend is compiled in.
get_target_property(_editor_compile_defs threadmaxx::editor
    INTERFACE_COMPILE_DEFINITIONS)
if (_editor_compile_defs MATCHES "THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1")
    foreach(test
            test_editor_imgui_backend_smoke
            test_editor_imgui_overlay)
        add_executable(${test} ${test}.cpp)
        target_link_libraries(${test} PRIVATE
            threadmaxx::editor imgui)
        target_include_directories(${test} PRIVATE
            ${CMAKE_SOURCE_DIR}/tests)
        set_target_properties(${test} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        if (NOT MSVC)
            target_compile_options(${test} PRIVATE
                -Wall -Wextra -Wpedantic -Wshadow)
        endif()
        add_test(NAME editor.${test} COMMAND ${test})
    endforeach()
endif()
