set(THREADMAXX_REFLECT_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/threadmaxx_reflect.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/aggregate.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/field_info.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/macro.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/registry.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/type_info.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/attributes.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/enum.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/visit.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/binders/json.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/detail/aggregate_impl.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/detail/enum_impl.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/detail/macro_impl.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/detail/name_arena.hpp
)

set(THREADMAXX_REFLECT_SOURCES
    Init.cpp
    JsonBinder.cpp
    Registry.cpp
)

# R7 — engine bridge sources are opt-in; the file is only added when
# threadmaxx::threadmaxx is around, identical to threadmaxx_assets.

add_library(threadmaxx_reflect STATIC
    ${THREADMAXX_REFLECT_SOURCES}
    ${THREADMAXX_REFLECT_PUBLIC_HEADERS}
)
add_library(threadmaxx::reflect ALIAS threadmaxx_reflect)

target_include_directories(threadmaxx_reflect
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_reflect PUBLIC cxx_std_20)

set_target_properties(threadmaxx_reflect PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_reflect PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_reflect PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_reflect PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_reflect PRIVATE -Werror)
    endif()
endif()

# Opt-in engine bridge: compiles the engine-side adapter when
# threadmaxx::threadmaxx is available. Exports
# THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1 so consumers can `#if`-gate.
# R7 will add the EngineBridge.cpp source; the gate already lives here
# so the macro is exported in time for tests / examples.
if (TARGET threadmaxx::threadmaxx)
    target_sources(threadmaxx_reflect PRIVATE
        EngineBridge.cpp
        ${CMAKE_SOURCE_DIR}/include/threadmaxx_reflect/engine_bridge.hpp)
    target_link_libraries(threadmaxx_reflect PUBLIC threadmaxx::threadmaxx)
    target_compile_definitions(threadmaxx_reflect PUBLIC
        THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1)
endif()
