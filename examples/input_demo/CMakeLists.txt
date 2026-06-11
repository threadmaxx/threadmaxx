# examples/input_demo — headless walkthrough of every threadmaxx_input
# pillar (keyboard / mouse / gamepad / actions / picking / trace).
# Skipped when the input sibling library isn't built.
if (TARGET threadmaxx::input)
    add_executable(threadmaxx_input_demo main.cpp)
    target_link_libraries(threadmaxx_input_demo PRIVATE threadmaxx::input)
    set_target_properties(threadmaxx_input_demo PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(threadmaxx_input_demo PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()
