set(THREADMAXX_ASSETS_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/threadmaxx_assets.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/data/audio.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/data/font.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/data/mesh.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/data/texture.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/detail/inflate.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/detail/io.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/bmfont.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/bmp.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/obj.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/ply.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/png.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/tga.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_assets/loaders/wav.hpp
)

set(THREADMAXX_ASSETS_SOURCES
    Io.cpp
    detail/Inflate.cpp
    loaders/Bmfont.cpp
    loaders/Bmp.cpp
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
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
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
