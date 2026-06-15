set(THREADMAXX_ASSETS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/threadmaxx_assets.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/data/audio.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/data/font.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/data/gltf.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/data/mesh.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/data/texture.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/detail/inflate.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/detail/io.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/detail/minimal_json.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/bmfont.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/bmp.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/gltf.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/obj.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/ply.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/png.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/tga.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/loaders/wav.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/async_loader.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/bundle.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/registry.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/watch.hpp
)

set(THREADMAXX_ASSETS_SOURCES
    AsyncLoader.cpp
    Bundle.cpp
    Io.cpp
    Registry.cpp
    Watch.cpp
    detail/Inflate.cpp
    loaders/Bmfont.cpp
    loaders/Bmp.cpp
    loaders/Gltf.cpp
    loaders/Obj.cpp
    loaders/Ply.cpp
    loaders/Png.cpp
    loaders/Tga.cpp
    loaders/Wav.cpp
)

add_library(threadmaxx_assets STATIC
    ${THREADMAXX_ASSETS_SOURCES}
    ${THREADMAXX_ASSETS_PUBLIC_HEADERS}
)
add_library(threadmaxx::assets ALIAS threadmaxx_assets)

target_include_directories(threadmaxx_assets
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_assets PUBLIC cxx_std_20)

set_target_properties(threadmaxx_assets PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_assets PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_assets PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_assets PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_assets PRIVATE -Werror)
    endif()
endif()

# Threads: AsyncLoader spawns a pool.
find_package(Threads REQUIRED)
target_link_libraries(threadmaxx_assets PUBLIC Threads::Threads)

# Opt-in engine bridge: compiles the engine-side IResourceLoader adapter
# when threadmaxx::threadmaxx is available. Exports
# THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1 so consumers can `#if`-gate.
if (TARGET threadmaxx::threadmaxx)
    target_sources(threadmaxx_assets PRIVATE
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_assets/engine_bridge.hpp)
    target_link_libraries(threadmaxx_assets PUBLIC threadmaxx::threadmaxx)
    target_compile_definitions(threadmaxx_assets PUBLIC
        THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1)
endif()
