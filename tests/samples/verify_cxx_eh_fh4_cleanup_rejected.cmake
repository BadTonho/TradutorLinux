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
    message(FATAL_ERROR "Malformed FH4 fixture wrote to stdout")
endif()

string(FIND "${runtime_trace}"
    "[tl][runtime][warning] cxx-eh state=\"rejected\" detail=\"invalid-fh4-unwind-map\""
    rejection_position)
if(rejection_position EQUAL -1)
    message(FATAL_ERROR "FH4 cycle was not rejected by the metadata parser:\n${runtime_trace}")
endif()

foreach(uncontrolled "guest-signal" "guest-timeout")
    string(FIND "${runtime_trace}" "${uncontrolled}" uncontrolled_position)
    if(NOT uncontrolled_position EQUAL -1)
        message(FATAL_ERROR
            "Malformed FH4 fixture terminated uncontrollably (${uncontrolled}):\n${runtime_trace}")
    endif()
endforeach()
