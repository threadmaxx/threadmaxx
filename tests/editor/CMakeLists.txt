set(THREADMAXX_EDITOR_TESTS
    test_editor_session_attach
    test_editor_headless_backend_capture
    test_editor_session_destroy_order
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
