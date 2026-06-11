# examples/ui_demo — headless walkthrough of every threadmaxx_ui pillar.
# Skipped if the UI sibling library isn't built.
if (TARGET threadmaxx::ui)
    add_executable(threadmaxx_ui_demo main.cpp)
    target_link_libraries(threadmaxx_ui_demo PRIVATE threadmaxx::ui)
    set_target_properties(threadmaxx_ui_demo PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(threadmaxx_ui_demo PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()
