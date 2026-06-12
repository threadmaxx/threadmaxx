add_executable(threadmaxx_assets_demo main.cpp)
target_link_libraries(threadmaxx_assets_demo PRIVATE threadmaxx::assets)
target_compile_definitions(threadmaxx_assets_demo PRIVATE
    THREADMAXX_ASSETS_DEMO_CUBE_PATH="${CMAKE_SOURCE_DIR}/tests/assets/fixtures/cube.obj")
set_target_properties(threadmaxx_assets_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if (NOT MSVC)
    target_compile_options(threadmaxx_assets_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()
