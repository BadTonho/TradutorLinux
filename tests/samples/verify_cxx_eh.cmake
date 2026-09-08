if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED EXPECTED_EXIT)
    message(FATAL_ERROR "INPUT, RUNTIME and EXPECTED_EXIT are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

execute_process(
    COMMAND "${RUNTIME}" --trace "${INPUT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(NOT runtime_result EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR
        "Runtime returned ${runtime_result} for ${INPUT}; expected ${EXPECTED_EXIT}\n${runtime_trace}")
endif()
if(NOT runtime_stdout STREQUAL "")
    message(FATAL_ERROR "C++ EH fixture wrote to stdout; trace must stay on stderr")
endif()

string(FIND "${runtime_trace}" "[tl][runtime][info] cxx-eh state=\"matched\"" matched_position)
if(matched_position EQUAL -1)
    message(FATAL_ERROR "C++ EH catch-all was not selected:\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}" "[tl][runtime][info] ExitProcess" exit_position)
if(exit_position EQUAL -1)
    message(FATAL_ERROR "C++ EH fixture did not reach ExitProcess:\n${runtime_trace}")
endif()
