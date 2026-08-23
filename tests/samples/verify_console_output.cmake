if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED EXPECTED_HEX)
    message(FATAL_ERROR "INPUT, RUNTIME and EXPECTED_HEX are required")
endif()

set(input_file "")
set(input_args)
if(DEFINED INPUT_TEXT)
    set(input_file "${INPUT}.input")
    file(WRITE "${input_file}" "${INPUT_TEXT}")
    set(input_args INPUT_FILE "${input_file}")
endif()

set(runtime_command "${RUNTIME}" --trace "${INPUT}")
if(DEFINED PREFIX_DIR)
    file(REMOVE_RECURSE "${PREFIX_DIR}")
    set(runtime_command "${CMAKE_COMMAND}" -E env "TL_PREFIX=${PREFIX_DIR}" "${RUNTIME}" --trace "${INPUT}")
endif()

execute_process(
    COMMAND ${runtime_command}
    ${input_args}
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(DEFINED SKIP_EXIT AND runtime_result EQUAL SKIP_EXIT)
    message(STATUS "Skipping ${INPUT}: runtime environment does not permit this fixture")
    return()
endif()
if(DEFINED EXPECTED_EXIT)
    if(NOT runtime_result EQUAL EXPECTED_EXIT)
        message(FATAL_ERROR "Runtime returned ${runtime_result} for ${INPUT}, expected ${EXPECTED_EXIT}\n${runtime_trace}")
    endif()
elseif(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime returned ${runtime_result} for ${INPUT}\n${runtime_trace}")
endif()

string(HEX "${runtime_stdout}" actual_hex)
if(NOT actual_hex STREQUAL EXPECTED_HEX)
    message(FATAL_ERROR "Unexpected stdout for ${INPUT}: ${actual_hex}, expected ${EXPECTED_HEX}\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}" "resolved dll=\"KERNEL32.dll\"" position)
if(position EQUAL -1)
    message(FATAL_ERROR "Resolved KERNEL32 imports not found in trace:\n${runtime_trace}")
endif()

if(DEFINED REQUIRED_TRACE)
    string(REPLACE "," ";" trace_events "${REQUIRED_TRACE}")
    foreach(trace_event IN LISTS trace_events)
        string(FIND "${runtime_trace}" "[tl][runtime][info] ${trace_event}" trace_position)
        if(trace_position EQUAL -1)
            message(FATAL_ERROR "Runtime event ${trace_event} not found in trace:\n${runtime_trace}")
        endif()
    endforeach()
endif()

if(DEFINED CLEANUP_FILE)
    file(REMOVE "${CLEANUP_FILE}")
endif()
