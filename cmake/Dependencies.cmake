include(FetchContent)

# FIND_PACKAGE_ARGS: an installed package (find_package) wins; otherwise the source is downloaded.
# SYSTEM: the dependency's headers are system headers, so our warnings and clang-tidy skip them.
# EXCLUDE_FROM_ALL: only the parts of the dependency we link against get built.

if(LOGGER_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    # MSVC: link the same C runtime as our targets (CMAKE_MSVC_RUNTIME_LIBRARY: the DLL one unless a preset
    # says otherwise) instead of GoogleTest's static default.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # A sanitized build never takes an installed GoogleTest: that one was built without the sanitizer, and
    # mixing it with our instrumented code gives false reports (see logger_sanitize_target). It builds the
    # source with our sanitizer flags instead, on every machine alike.
    set(gtest_find_package_args FIND_PACKAGE_ARGS NAMES GTest)
    if(LOGGER_SANITIZERS)
        set(gtest_find_package_args "")
    endif()
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.18.0
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
        ${gtest_find_package_args})   # must stay last: FIND_PACKAGE_ARGS takes every argument after it
    FetchContent_MakeAvailable(googletest)
    logger_sanitize_target(gtest)        # both do nothing unless LOGGER_SANITIZERS is set
    logger_sanitize_target(gtest_main)
endif()

# Adding another dependency (then link fmt::fmt):
#
# FetchContent_Declare(fmt
#     GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#     GIT_TAG        12.2.0
#     GIT_SHALLOW    TRUE
#     SYSTEM
#     EXCLUDE_FROM_ALL
#     FIND_PACKAGE_ARGS)
# FetchContent_MakeAvailable(fmt)
