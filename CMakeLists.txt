cmake_minimum_required(VERSION 3.28)   # FetchContent SYSTEM + EXCLUDE_FROM_ALL + FIND_PACKAGE_ARGS
project(logger VERSION 0.1.0 DESCRIPTION "Thread-safe asynchronous C++23 logging library" LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset clang-debug")
endif()

# Presets set the build type; this covers a bare `cmake -B build`. A parent project keeps its own choice.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)     # no C++ modules; remove this line to use them
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

option(LOGGER_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(LOGGER_BUILD_DOCS "Add a 'docs' target when Doxygen is found" ${PROJECT_IS_TOP_LEVEL})
option(LOGGER_BUILD_DEMO "Build the demo executable (src/main.cpp)" ${PROJECT_IS_TOP_LEVEL})
option(LOGGER_WARNINGS_AS_ERRORS "Treat compiler and clang-tidy warnings as errors" OFF)
option(LOGGER_ENABLE_COVERAGE "Clang source-based coverage and a 'coverage' target" OFF)
option(LOGGER_ENABLE_CLANG_TIDY "Run clang-tidy on our sources while compiling" OFF)
option(LOGGER_ENABLE_IPO "Link-time optimisation for our targets" OFF)
set(LOGGER_SANITIZERS "" CACHE STRING "Sanitizers for our targets, e.g. address;undefined or thread")
# Compile-time stripping (include/logger/Level.h): log calls above this level compile to nothing.
set(LOGGER_MIN_LEVEL "" CACHE STRING
    "Most verbose level compiled into the library and its consumers: OFF, ERROR, WARN, INFO, DEBUG, PERF, or empty for all")
set_property(CACHE LOGGER_MIN_LEVEL PROPERTY STRINGS "" OFF ERROR WARN INFO DEBUG PERF)
# Tested with STREQUAL, never if(LOGGER_MIN_LEVEL): OFF is a false constant in CMake.
if(NOT LOGGER_MIN_LEVEL STREQUAL "")
    string(TOUPPER "${LOGGER_MIN_LEVEL}" LOGGER_MIN_LEVEL)
    set(logger_levels OFF ERROR WARN INFO DEBUG PERF)
    if(NOT LOGGER_MIN_LEVEL IN_LIST logger_levels)
        message(FATAL_ERROR "LOGGER_MIN_LEVEL='${LOGGER_MIN_LEVEL}' is not one of ${logger_levels} (or empty)")
    endif()
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)   # a launcher the user chose wins
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# Point clangd (in any editor) at the most recently configured preset.
# Windows allows symlinks only in Developer Mode (or as admin), so a failure is a note, not an error.
if(PROJECT_IS_TOP_LEVEL)
    file(CREATE_LINK ${PROJECT_BINARY_DIR}/compile_commands.json
         ${PROJECT_SOURCE_DIR}/compile_commands.json RESULT link_result SYMBOLIC)
    if(NOT link_result EQUAL 0)
        message(STATUS "Could not link compile_commands.json into the source tree (${link_result}). "
                       "On Windows, turn on Developer Mode to allow symlinks.")
    endif()
endif()

# Project modules are included by path, so a parent's CMAKE_MODULE_PATH cannot replace them.
include(${PROJECT_SOURCE_DIR}/cmake/ProjectOptions.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)

add_subdirectory(src)

if(LOGGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(LOGGER_ENABLE_COVERAGE)
    include(${PROJECT_SOURCE_DIR}/cmake/Coverage.cmake)
endif()
if(LOGGER_BUILD_DOCS)
    include(${PROJECT_SOURCE_DIR}/cmake/Docs.cmake)
endif()

# Release archives of what the install() rules install, named <project>-<version>-<CPACK_SYSTEM_NAME>.
# Made by the dist-* workflow presets (cmake --workflow --preset dist-linux) and the Release GitHub workflow.
if(PROJECT_IS_TOP_LEVEL)
    include(CPack)
endif()
