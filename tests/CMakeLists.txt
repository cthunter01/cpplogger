include(GoogleTest)

# The library reads LOGGER_LEVEL once per process: a value from the developer's shell must not reach a test.
set(logger_test_properties ENVIRONMENT_MODIFICATION "LOGGER_LEVEL=unset:" TIMEOUT 120)

# The demo must run through; its output is not checked beyond the exit code.
if(TARGET logger_demo)
    add_test(NAME demo_runs COMMAND logger_demo debug ${CMAKE_CURRENT_BINARY_DIR}/logger-demo.log)
    set_tests_properties(demo_runs PROPERTIES ${logger_test_properties})
endif()

# The suite assumes every level is compiled in. A build that strips levels (LOGGER_MIN_LEVEL set) runs the
# demo only: the stripping itself is proven by logger_min_level_tests in a normal build.
if(NOT LOGGER_MIN_LEVEL STREQUAL "")
    message(STATUS "LOGGER_MIN_LEVEL=${LOGGER_MIN_LEVEL}: the unit tests assume every level is compiled in "
                   "and are not built")
    return()
endif()

add_executable(logger_tests
    CaptureSink.h
    ConsoleSinkTests.cpp
    FileSinkTests.cpp
    FormatStringTests.cpp
    LevelTests.cpp
    RecordTests.cpp
    logger_tests.cpp
    macro_collision_tests.cpp)
target_link_libraries(logger_tests PRIVATE logger::lib GTest::gtest_main)
logger_configure_target(logger_tests)
# PRE_TEST: list the tests when ctest runs, not at build time (the binary may be sanitized or cross-compiled).
gtest_discover_tests(logger_tests DISCOVERY_MODE PRE_TEST PROPERTIES ${logger_test_properties})

# LOGGER_MIN_LEVEL acts inside the public headers, so proving the stripping needs code compiled with it. The
# definition is per source file, not per target: default_level_unit.cpp keeps the default, and the two must not
# influence each other although they instantiate the same templates.
add_executable(logger_min_level_tests
    CaptureSink.h
    default_level_unit.cpp
    default_level_unit.h
    min_level_tests.cpp)
set_source_files_properties(min_level_tests.cpp PROPERTIES
    COMPILE_DEFINITIONS LOGGER_MIN_LEVEL=LOGGER_LEVEL_WARN)
target_link_libraries(logger_min_level_tests PRIVATE logger::lib GTest::gtest_main)
logger_configure_target(logger_min_level_tests)
gtest_discover_tests(logger_min_level_tests DISCOVERY_MODE PRE_TEST PROPERTIES ${logger_test_properties})

# Process-level behaviour: LOGGER_LEVEL, the once-per-process default error handler, draining at exit and
# logging from static destructors. Each case runs the executable in a fresh process through ProcessCheck.cmake.
add_executable(logger_process_tests process_main.cpp)
target_link_libraries(logger_process_tests PRIVATE logger::lib)
logger_configure_target(logger_process_tests)

# logger_add_process_test(<name> <scenario> [ENV <LOGGER_LEVEL value>] [EXPECT a|b|c] [EXPECT_COUNT n|text]
#                         [EXPECT_STDERR text] [EXPECT_STDERR_COUNT n|text] [FORBID text])
function(logger_add_process_test name scenario)
    cmake_parse_arguments(arg "" "ENV;EXPECT;EXPECT_COUNT;EXPECT_STDERR;EXPECT_STDERR_COUNT;FORBID" "" ${ARGN})
    add_test(NAME process.${name}
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:logger_process_tests> -DSCENARIO=${scenario}
                "-DEXPECT=${arg_EXPECT}" "-DEXPECT_COUNT=${arg_EXPECT_COUNT}"
                "-DEXPECT_STDERR=${arg_EXPECT_STDERR}" "-DEXPECT_STDERR_COUNT=${arg_EXPECT_STDERR_COUNT}"
                "-DFORBID=${arg_FORBID}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/ProcessCheck.cmake)
    set(modification "LOGGER_LEVEL=unset:")
    if(DEFINED arg_ENV)
        list(APPEND modification "LOGGER_LEVEL=set:${arg_ENV}")
    endif()
    set_tests_properties(process.${name} PROPERTIES ENVIRONMENT_MODIFICATION "${modification}" TIMEOUT 120)
endfunction()

logger_add_process_test(default_level_is_info level EXPECT "level=INFO")
logger_add_process_test(env_sets_the_level level ENV WaRn EXPECT "level=WARN")
logger_add_process_test(env_off level ENV off EXPECT "level=OFF" FORBID "[INFO ]")
logger_add_process_test(code_wins_over_env level-override ENV debug EXPECT "level=ERROR")
logger_add_process_test(bad_env_reaches_the_handler bad-env-handler ENV loud
    EXPECT "handler: LOGGER_LEVEL=|is not one of error, warn, info, debug, perf or off|level=INFO")
logger_add_process_test(bad_env_reaches_stderr bad-env-default ENV loud
    EXPECT "level=INFO" EXPECT_STDERR "logger: LOGGER_LEVEL=")
logger_add_process_test(default_handler_reports_once default-handler
    EXPECT "done" EXPECT_STDERR_COUNT "1|logger: a sink failed to write: disk on fire")
logger_add_process_test(queue_is_drained_at_exit exit-drain
    EXPECT "drain i=0|drain i=199" EXPECT_COUNT "200|drain i=")
logger_add_process_test(queue_is_drained_on_std_exit std-exit
    EXPECT "drain i=199" EXPECT_COUNT "200|drain i=")
logger_add_process_test(static_destructors_can_log exit-order
    EXPECT "hello from main|late exit handler ran|early static destroyed")
logger_add_process_test(exit_inside_a_sink_reports_the_loss exit-in-sink
    EXPECT_STDERR "std::exit was called from inside a sink or the error handler; up to 3 queued record(s)")
