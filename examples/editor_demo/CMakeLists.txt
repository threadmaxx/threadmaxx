add_executable(threadmaxx_editor_demo main.cpp)
target_link_libraries(threadmaxx_editor_demo PRIVATE
    threadmaxx::threadmaxx
    threadmaxx::editor
    threadmaxx::reflect
)
set_target_properties(threadmaxx_editor_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if (NOT MSVC)
    target_compile_options(threadmaxx_editor_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
