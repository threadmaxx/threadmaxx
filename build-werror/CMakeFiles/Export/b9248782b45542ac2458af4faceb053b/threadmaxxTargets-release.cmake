#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "threadmaxx::threadmaxx" for configuration "Release"
set_property(TARGET threadmaxx::threadmaxx APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(threadmaxx::threadmaxx PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libthreadmaxx.a"
  )

list(APPEND _cmake_import_check_targets threadmaxx::threadmaxx )
list(APPEND _cmake_import_check_files_for_threadmaxx::threadmaxx "${_IMPORT_PREFIX}/lib/libthreadmaxx.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
