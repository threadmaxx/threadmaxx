set(THREADMAXX_NETWORK_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/threadmaxx_network.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/bitstream.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/ids.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/packets.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/session.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/delta.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/replication.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/interest.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/prediction.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/rollback.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/transport.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/udp_transport.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_network/version.hpp
)

set(THREADMAXX_NETWORK_SOURCES
    Delta.cpp
    Init.cpp
    InterestManager.cpp
    Reconciler.cpp
    ReplicationRegistry.cpp
    RollbackBuffer.cpp
    Session.cpp
    Snapshot.cpp
    SyncTracker.cpp
    Transport.cpp
    UdpTransport.cpp
)

add_library(threadmaxx_network STATIC
    ${THREADMAXX_NETWORK_SOURCES}
    ${THREADMAXX_NETWORK_PUBLIC_HEADERS}
)
add_library(threadmaxx::network ALIAS threadmaxx_network)

target_include_directories(threadmaxx_network
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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

# NW10 — detect POSIX socket headers at configure time. Sets
# THREADMAXX_NETWORK_HAS_UDP=1 on platforms where UdpTransport compiles.
include(CheckIncludeFileCXX)
check_include_file_cxx("sys/socket.h" THREADMAXX_NETWORK_HAS_POSIX_SOCKETS)
if (THREADMAXX_NETWORK_HAS_POSIX_SOCKETS)
    target_compile_definitions(threadmaxx_network PUBLIC
        THREADMAXX_NETWORK_HAS_UDP=1)
endif()

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
