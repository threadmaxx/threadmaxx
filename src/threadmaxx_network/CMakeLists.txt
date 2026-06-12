set(THREADMAXX_NETWORK_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_network/threadmaxx_network.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_network/bitstream.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_network/ids.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_network/version.hpp
)

# Init.cpp is a no-op TU that gives CMake / linkers a non-empty
# source set for the static library at the NW1 seed point. Future
# batches add real sources (ReplicationRegistry.cpp / ...).
set(THREADMAXX_NETWORK_SOURCES
    Init.cpp
)

add_library(threadmaxx_network STATIC
    ${THREADMAXX_NETWORK_SOURCES}
    ${THREADMAXX_NETWORK_PUBLIC_HEADERS}
)
add_library(threadmaxx::network ALIAS threadmaxx_network)

target_include_directories(threadmaxx_network
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_network PUBLIC cxx_std_20)

set_target_properties(threadmaxx_network PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# The library has no PUBLIC dependency on the engine — networking
# stays engine-agnostic per DESIGN_NOTES §2.1. The engine bridge
# enters via SimulationBridge functors that game code supplies.

if (MSVC)
    target_compile_options(threadmaxx_network PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_network PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_network PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_network PRIVATE -Werror)
    endif()
endif()
