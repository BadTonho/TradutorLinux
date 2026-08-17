if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED GOLDEN_INPUT
   OR NOT DEFINED GOLDEN_OUTPUT)
    message(FATAL_ERROR
        "INPUT, RUNTIME, GOLDEN_INPUT and GOLDEN_OUTPUT are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Target app was not generated: ${INPUT}")
endif()
if(NOT EXISTS "${GOLDEN_OUTPUT}")
    message(FATAL_ERROR "Golden fixture is missing: ${GOLDEN_OUTPUT}")
endif()
if(NOT DEFINED ALLOW_MISSING_GOLDEN_INPUT)
    set(ALLOW_MISSING_GOLDEN_INPUT FALSE)
endif()
if(NOT ALLOW_MISSING_GOLDEN_INPUT AND NOT EXISTS "${GOLDEN_INPUT}")
    message(FATAL_ERROR "Golden fixture is missing: ${GOLDEN_INPUT}")
endif()

set(_arguments "${INPUT}")
if(DEFINED GUEST_ARGS AND NOT GUEST_ARGS STREQUAL "")
    foreach(_arg IN LISTS GUEST_ARGS)
        list(APPEND _arguments "${_arg}")
    endforeach()
endif()
list(APPEND _arguments "${GOLDEN_INPUT}")

set(_execution_result "not-attempted")

execute_process(
    COMMAND "${RUNTIME}" ${_arguments}
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 30
)

if(NOT DEFINED EXPECTED_EXIT)
    set(EXPECTED_EXIT 0)
endif()

if(NOT run_result EQUAL "${EXPECTED_EXIT}")
    set(_execution_result "failed")
    message(STATUS "execution-result: failed (exit code ${run_result}, expected ${EXPECTED_EXIT})")
    message(FATAL_ERROR
        "Run returned code ${run_result} (esperado ${EXPECTED_EXIT}):\n${run_error}")
endif()

file(READ "${GOLDEN_OUTPUT}" golden_output)
if(NOT run_output STREQUAL golden_output)
    set(_execution_result "incorrect")
    message(STATUS "execution-result: incorrect (output mismatch)")
    message(FATAL_ERROR
        "Saída do convidado diverge do ouro:\n--- guest ---\n${run_output}\n--- golden ---\n${golden_output}")
endif()

set(_execution_result "supported")
message(STATUS "execution-result: supported")
