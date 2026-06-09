# threadmaxx_navmesh test suite (N1+).
#
# Each test links `threadmaxx::navmesh` (which transitively pulls in
# `threadmaxx::threadmaxx` for the math PODs) and uses the tiny
# `Check.hpp` harness from the parent `tests/` directory. Registered
# with CTest under the `navmesh.*` name prefix.

set(THREADMAXX_NAVMESH_TESTS
    test_navmesh_registry_load
    test_navmesh_registry_unload
    test_navmesh_registry_invalid
)

foreach(test ${THREADMAXX_NAVMESH_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::navmesh)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME navmesh.${test} COMMAND ${test})
endforeach()
