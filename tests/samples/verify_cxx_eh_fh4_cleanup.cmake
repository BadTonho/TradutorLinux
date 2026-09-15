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
    message(FATAL_ERROR "FH4 C++ EH cleanup fixture wrote to stdout")
endif()

string(FIND "${runtime_trace}"
    "[tl][runtime][info] cxx-eh state=\"search\" detail=\"fh4-termination-cleanup\""
    cleanup_position)
if(cleanup_position EQUAL -1)
    message(FATAL_ERROR "FH4 termination cleanup was not selected:\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}"
    "[tl][runtime][info] ExitProcess symbol=\"ExitProcess\" exit-code=\"0\""
    exit_position)
if(exit_position EQUAL -1)
    message(FATAL_ERROR "FH4 cleanup did not reach the successful catch:\n${runtime_trace}")
endif()
