set(THREADMAXX_NAVMESH_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/threadmaxx_navmesh.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/mesh.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/query.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_navmesh/crowd.hpp
)

set(THREADMAXX_NAVMESH_SOURCES
    NavMeshRegistry.cpp
    Solver.cpp
    PathQueryService.cpp
    BatchPathSolver.cpp
)

add_library(threadmaxx_navmesh STATIC
    ${THREADMAXX_NAVMESH_SOURCES}
    ${THREADMAXX_NAVMESH_PUBLIC_HEADERS}
)
add_library(threadmaxx::navmesh ALIAS threadmaxx_navmesh)

target_include_directories(threadmaxx_navmesh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_navmesh PUBLIC cxx_std_20)

# Reuses threadmaxx::Vec3 from the core lib — public link so consumers
# pick up the math PODs transitively.
target_link_libraries(threadmaxx_navmesh PUBLIC threadmaxx::threadmaxx)

set_target_properties(threadmaxx_navmesh PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_navmesh PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_navmesh PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_navmesh PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_navmesh PRIVATE -Werror)
    endif()
endif()
