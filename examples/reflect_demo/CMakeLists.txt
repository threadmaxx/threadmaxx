add_executable(threadmaxx_reflect_demo main.cpp)
target_link_libraries(threadmaxx_reflect_demo PRIVATE threadmaxx::reflect)
set_target_properties(threadmaxx_reflect_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if (NOT MSVC)
    target_compile_options(threadmaxx_reflect_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
