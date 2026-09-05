if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME)
    message(FATAL_ERROR "INPUT and RUNTIME are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

set(prefix_dir "${CMAKE_CURRENT_BINARY_DIR}/memory-limit-prefix")
file(REMOVE_RECURSE "${prefix_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "TL_PREFIX=${prefix_dir}"
        "${RUNTIME}" --trace --memory 128 "${INPUT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime returned ${runtime_result} for ${INPUT}\n${runtime_trace}")
endif()

string(HEX "${runtime_stdout}" actual_hex)
if(NOT actual_hex STREQUAL "6d656d6f72792d6c696d69740a")
    message(FATAL_ERROR
        "Unexpected stdout for ${INPUT}: ${actual_hex}, expected memory-limit\\n\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}" "resource-limits inheritance=\"fork\" memory-mib=\"128\"" limit_position)
if(limit_position EQUAL -1)
    message(FATAL_ERROR "Memory limit was not recorded in the trace:\n${runtime_trace}")
endif()

file(REMOVE_RECURSE "${prefix_dir}")
