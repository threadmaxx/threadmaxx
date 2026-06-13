set(THREADMAXX_MIGRATION_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/threadmaxx_migration.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/io.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/records.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/savefile.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/detail/binary_reader.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_migration/detail/binary_writer.hpp
)

set(THREADMAXX_MIGRATION_SOURCES
    RecordIo.cpp
)

add_library(threadmaxx_migration STATIC
    ${THREADMAXX_MIGRATION_SOURCES}
    ${THREADMAXX_MIGRATION_PUBLIC_HEADERS}
)
add_library(threadmaxx::migration ALIAS threadmaxx_migration)

target_include_directories(threadmaxx_migration
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_migration PUBLIC cxx_std_20)

set_target_properties(threadmaxx_migration PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_migration PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_migration PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_migration PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_migration PRIVATE -Werror)
    endif()
endif()
