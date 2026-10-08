# 'coverage' target: runs the tests with Clang source-based coverage and writes a report.
#   cmake --workflow --preset coverage
# Report: build/coverage/coverage/summary.txt, HTML: build/coverage/coverage/html/index.html

if(NOT LOGGER_BUILD_TESTS)
    message(FATAL_ERROR "LOGGER_ENABLE_COVERAGE needs LOGGER_BUILD_TESTS=ON")
endif()

# macOS: Xcode's llvm-cov and llvm-profdata match Apple Clang but aren't on PATH; xcrun finds them.
set(llvm_tool_hints "")
if(APPLE)
    execute_process(COMMAND xcrun --find llvm-cov
        OUTPUT_VARIABLE xcode_llvm_cov OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(xcode_llvm_cov)
        cmake_path(GET xcode_llvm_cov PARENT_PATH llvm_tool_hints)
    endif()
endif()
find_program(LLVM_PROFDATA_PROGRAM llvm-profdata HINTS ${llvm_tool_hints} REQUIRED)
find_program(LLVM_COV_PROGRAM llvm-cov HINTS ${llvm_tool_hints} REQUIRED)

if(NOT TARGET logger_tests)
    message(FATAL_ERROR "LOGGER_ENABLE_COVERAGE: the test suite is not built when LOGGER_MIN_LEVEL is set "
                        "(LOGGER_MIN_LEVEL=${LOGGER_MIN_LEVEL}), so there is nothing to measure")
endif()

# Every executable the tests run, so they are built and their coverage is counted.
set(coverage_targets logger_tests logger_min_level_tests logger_process_tests)
if(TARGET logger_demo)
    list(APPEND coverage_targets logger_demo)   # runs as the demo_runs test
endif()

set(coverage_binaries "")
foreach(target IN LISTS coverage_targets)
    list(APPEND coverage_binaries "$<TARGET_FILE:${target}>")
endforeach()
list(JOIN coverage_binaries "\\;" coverage_binaries_arg)

add_custom_target(coverage
    COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARIES=${coverage_binaries_arg}"
        "-DIGNORE_REGEX=.*/(build|_deps|tests)/.*"
        "-DLLVM_PROFDATA=${LLVM_PROFDATA_PROGRAM}"
        "-DLLVM_COV=${LLVM_COV_PROGRAM}"
        "-DCTEST=${CMAKE_CTEST_COMMAND}"
        -P ${PROJECT_SOURCE_DIR}/cmake/RunCoverage.cmake
    DEPENDS ${coverage_targets}
    USES_TERMINAL
    VERBATIM
    COMMENT "Running tests and generating the coverage report")
