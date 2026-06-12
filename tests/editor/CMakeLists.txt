set(THREADMAXX_EDITOR_TESTS
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
