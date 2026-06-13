add_executable(threadmaxx_migration_convert main.cpp)
target_link_libraries(threadmaxx_migration_convert PRIVATE
    threadmaxx::migration)
set_target_properties(threadmaxx_migration_convert PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if (NOT MSVC)
    target_compile_options(threadmaxx_migration_convert PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_migration_convert PRIVATE -Werror)
    endif()
endif()
