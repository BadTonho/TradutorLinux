if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED EXPECTED_EXIT
   OR NOT DEFINED REQUIRED_TRACE)
    message(FATAL_ERROR "INPUT, RUNTIME, EXPECTED_EXIT and REQUIRED_TRACE are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

set(runtime_command "${RUNTIME}" --trace)
if(DEFINED EXTRA_ARGS)
    list(APPEND runtime_command ${EXTRA_ARGS})
endif()
list(APPEND runtime_command "${INPUT}")

execute_process(
    COMMAND ${runtime_command}
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime succeeded for ${INPUT}; expected exit ${EXPECTED_EXIT}\n${runtime_trace}")
endif()
if(NOT runtime_result EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "Runtime returned ${runtime_result}; expected ${EXPECTED_EXIT} for ${INPUT}\n${runtime_trace}")
endif()

if(NOT runtime_stdout STREQUAL "")
    message(FATAL_ERROR "Runtime wrote to stdout; trace must go to stderr only")
endif()

string(FIND "${runtime_trace}" "${REQUIRED_TRACE}" position)
if(position EQUAL -1)
    message(FATAL_ERROR "'${REQUIRED_TRACE}' not found in trace for ${INPUT}:\n${runtime_trace}")
endif()
