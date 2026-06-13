set(THREADMAXX_STUDIO_TESTS
    test_studio_version
    test_studio_panel_interface_canary
    test_studio_data_source_canary
    test_studio_no_engine_link_canary
    test_studio_app_lifecycle
    test_studio_panel_register_unregister
    test_studio_layout_persists_via_editor
    test_studio_menu_open_panel
    test_studio_status_bar_text
    test_studio_console_panel_renders_editor_console
    test_studio_direct_source_engine_snapshot
    test_studio_direct_source_world_snapshot
    test_studio_direct_source_submits_command
    test_studio_entity_inspector_list
    test_studio_property_edit_emits_command
    test_studio_gizmo_translate_emits_command
    test_studio_diff_render_summary
    test_studio_frame_snapshot_updates
    test_studio_task_graph_layout
    test_studio_tuning_patch_apply
    test_studio_resources_list
    test_studio_hierarchy_panel
    test_studio_profiler_panel
)

# M4 sibling-gated tests. Each runs only when the matching panel
# compiled in (same compile-definition gates the panel header).
get_target_property(_studio_compile_defs threadmaxx::studio
    INTERFACE_COMPILE_DEFINITIONS)
if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_ANIMATION_PANEL=1")
    list(APPEND THREADMAXX_STUDIO_TESTS
        test_studio_animation_panel_lists_clips)
endif()
if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_AUDIO_PANEL=1")
    list(APPEND THREADMAXX_STUDIO_TESTS
        test_studio_audio_bus_graph)
endif()
if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_INPUT_PANEL=1")
    list(APPEND THREADMAXX_STUDIO_TESTS
        test_studio_input_panel_records_trace)
endif()
if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_ASSETS_PANEL=1")
    list(APPEND THREADMAXX_STUDIO_TESTS
        test_studio_assets_panel_reload)
endif()

foreach(test ${THREADMAXX_STUDIO_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::studio)
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
    add_test(NAME studio.${test} COMMAND ${test})
endforeach()
