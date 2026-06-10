# N9 — offline bake driver. Reads a triangle-soup text format and
# writes a v2 navmesh blob the runtime registry can load. Opt-in via
# `THREADMAXX_BUILD_EXAMPLES`; additionally guarded on the navmesh
# sibling library being available (same probe the test suite uses).

if (NOT TARGET threadmaxx::navmesh)
    return()
endif()

add_executable(threadmaxx_navmesh_bake main.cpp)

target_link_libraries(threadmaxx_navmesh_bake PRIVATE threadmaxx::navmesh)

set_target_properties(threadmaxx_navmesh_bake PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (NOT MSVC)
    target_compile_options(threadmaxx_navmesh_bake PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_navmesh_bake PRIVATE -Werror)
    endif()
endif()
