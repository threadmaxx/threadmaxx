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
)

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
