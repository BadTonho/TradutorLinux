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

execute_process(
    COMMAND "${RUNTIME}" --trace "${INPUT}"
    ${input_args}
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(NOT runtime_result EQUAL 0)
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

if(DEFINED CLEANUP_FILE)
    file(REMOVE "${CLEANUP_FILE}")
endif()
