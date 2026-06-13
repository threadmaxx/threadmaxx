add_executable(threadmaxx_studio_demo main.cpp)
target_link_libraries(threadmaxx_studio_demo PRIVATE
    threadmaxx::threadmaxx
    threadmaxx::editor
    threadmaxx::reflect
    threadmaxx::studio
)
if (TARGET threadmaxx::migration)
    target_link_libraries(threadmaxx_studio_demo PRIVATE threadmaxx::migration)
endif()
if (TARGET threadmaxx::network)
    target_link_libraries(threadmaxx_studio_demo PRIVATE threadmaxx::network)
endif()
set_target_properties(threadmaxx_studio_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if (NOT MSVC)
    target_compile_options(threadmaxx_studio_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
