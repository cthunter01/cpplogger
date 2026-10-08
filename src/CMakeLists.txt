# The library holds the code; the demo and the tests link against it.
add_library(logger_lib STATIC
    ConsoleSink.cpp
    Core.cpp
    FileSink.cpp
    Level.cpp
    Record.cpp
    logger.cpp)
add_library(logger::lib ALIAS logger_lib)
target_include_directories(logger_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
# The public headers need consteval, <format> and <source_location>: consumers get -std=c++23 too.
target_compile_features(logger_lib PUBLIC cxx_std_23)
# std::thread needs -pthread on FreeBSD (libthr) and older glibc; PUBLIC because consumers link the static library.
find_package(Threads REQUIRED)
target_link_libraries(logger_lib PUBLIC Threads::Threads)
# std::getenv, the one read of LOGGER_LEVEL, is deprecated by MSVC's CRT (C4996), which /WX turns into an error.
target_compile_definitions(logger_lib PRIVATE $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_WARNINGS>)
if(NOT LOGGER_MIN_LEVEL STREQUAL "")
    # PUBLIC: the macro acts inside the public headers, so consumers must see the same value as the library.
    target_compile_definitions(logger_lib PUBLIC LOGGER_MIN_LEVEL=LOGGER_LEVEL_${LOGGER_MIN_LEVEL})
endif()
logger_configure_target(logger_lib)

# Demo: every level, both shipped sinks, custom sinks and the error handler. Built only when this is
# the top-level project (a consumer gets the library alone), and never installed. The target has a
# prefixed name so that it cannot clash with a consumer's own target; the binary is bin/logger.
if(LOGGER_BUILD_DEMO)
    add_executable(logger_demo main.cpp)
    set_target_properties(logger_demo PROPERTIES OUTPUT_NAME logger)
    target_link_libraries(logger_demo PRIVATE logger::lib)
    logger_configure_target(logger_demo)
endif()

include(GNUInstallDirs)
install(TARGETS logger_lib ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/logger DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
