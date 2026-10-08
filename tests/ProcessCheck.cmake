# Script mode (cmake -P), driven by logger_add_process_test() in tests/CMakeLists.txt: runs <EXE> <SCENARIO> in
# a fresh process and checks its exit code and output.
#   -DEXE=<path> -DSCENARIO=<name>
#   -DEXPECT=a|b|c                 substrings that must appear in stdout, in this order
#   -DEXPECT_COUNT=n|text          `text` must appear exactly n times in stdout
#   -DEXPECT_STDERR=text           a substring that must appear in stderr
#   -DEXPECT_STDERR_COUNT=n|text   `text` must appear exactly n times in stderr
#   -DFORBID=text                  must not appear in stdout

cmake_minimum_required(VERSION 3.28)   # a cmake -P script does not inherit the project's policies

foreach(var EXE SCENARIO)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "ProcessCheck.cmake requires -D${var}=...")
    endif()
endforeach()

execute_process(COMMAND "${EXE}" "${SCENARIO}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc TIMEOUT 120)
set(report "--- stdout ---\n${out}\n--- stderr ---\n${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${EXE} ${SCENARIO} exited with ${rc}\n${report}")
endif()

# count_occurrences(<haystack> <needle> <out-var>)
function(count_occurrences haystack needle out_var)
    set(count 0)
    set(rest "${haystack}")
    string(LENGTH "${needle}" len)
    while(TRUE)
        string(FIND "${rest}" "${needle}" idx)
        if(idx EQUAL -1)
            break()
        endif()
        math(EXPR count "${count} + 1")
        math(EXPR cut "${idx} + ${len}")
        string(SUBSTRING "${rest}" ${cut} -1 rest)
    endwhile()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# check_count(<haystack> <stream-name> <n|text>)
function(check_count haystack stream spec)
    string(FIND "${spec}" "|" sep)
    if(sep EQUAL -1)
        message(FATAL_ERROR "count spec '${spec}' must look like n|text")
    endif()
    string(SUBSTRING "${spec}" 0 ${sep} expected)
    math(EXPR start "${sep} + 1")
    string(SUBSTRING "${spec}" ${start} -1 needle)
    count_occurrences("${haystack}" "${needle}" actual)
    if(NOT actual EQUAL expected)
        message(FATAL_ERROR "'${needle}' appears ${actual} times in ${stream} of ${SCENARIO}, expected ${expected}\n${report}")
    endif()
endfunction()

if(DEFINED EXPECT AND NOT EXPECT STREQUAL "")
    string(REPLACE "|" ";" needles "${EXPECT}")
    set(rest "${out}")
    foreach(needle IN LISTS needles)
        string(FIND "${rest}" "${needle}" idx)
        if(idx EQUAL -1)
            message(FATAL_ERROR "'${needle}' not found (in order) in stdout of ${SCENARIO}\n${report}")
        endif()
        string(LENGTH "${needle}" len)
        math(EXPR cut "${idx} + ${len}")
        string(SUBSTRING "${rest}" ${cut} -1 rest)
    endforeach()
endif()

if(DEFINED EXPECT_COUNT AND NOT EXPECT_COUNT STREQUAL "")
    check_count("${out}" stdout "${EXPECT_COUNT}")
endif()

if(DEFINED EXPECT_STDERR AND NOT EXPECT_STDERR STREQUAL "")
    string(FIND "${err}" "${EXPECT_STDERR}" idx)
    if(idx EQUAL -1)
        message(FATAL_ERROR "'${EXPECT_STDERR}' not found in stderr of ${SCENARIO}\n${report}")
    endif()
endif()

if(DEFINED EXPECT_STDERR_COUNT AND NOT EXPECT_STDERR_COUNT STREQUAL "")
    check_count("${err}" stderr "${EXPECT_STDERR_COUNT}")
endif()

if(DEFINED FORBID AND NOT FORBID STREQUAL "")
    string(FIND "${out}" "${FORBID}" idx)
    if(NOT idx EQUAL -1)
        message(FATAL_ERROR "'${FORBID}' must not appear in stdout of ${SCENARIO}\n${report}")
    endif()
endif()
