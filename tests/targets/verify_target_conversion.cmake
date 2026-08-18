if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED WORKING_DIRECTORY
   OR NOT DEFINED GUEST_INPUT OR NOT DEFINED GOLDEN_OUTPUT)
    message(FATAL_ERROR
        "INPUT, RUNTIME, WORKING_DIRECTORY, GUEST_INPUT and GOLDEN_OUTPUT are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Target app was not generated: ${INPUT}")
endif()
if(NOT IS_DIRECTORY "${WORKING_DIRECTORY}")
    message(FATAL_ERROR "Guest working directory is missing: ${WORKING_DIRECTORY}")
endif()
if(NOT EXISTS "${GOLDEN_OUTPUT}")
    message(FATAL_ERROR "Expected conversion output is missing: ${GOLDEN_OUTPUT}")
endif()

set(_arguments "${INPUT}")
if(DEFINED GUEST_ARGS AND NOT GUEST_ARGS STREQUAL "")
    foreach(_arg IN LISTS GUEST_ARGS)
        list(APPEND _arguments "${_arg}")
    endforeach()
endif()
list(APPEND _arguments "${GUEST_INPUT}")

string(MD5 _output_id "${INPUT}|${WORKING_DIRECTORY}|${GUEST_ARGS}|${GUEST_INPUT}")
set(_guest_output "${CMAKE_CURRENT_BINARY_DIR}/target_conversion_${_output_id}.out")

execute_process(
    COMMAND "${RUNTIME}" ${_arguments}
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    OUTPUT_FILE "${_guest_output}"
    ERROR_VARIABLE run_error
    RESULT_VARIABLE run_result
    TIMEOUT 30
)

if(NOT DEFINED EXPECTED_EXIT)
    set(EXPECTED_EXIT 0)
endif()
if(NOT run_result EQUAL "${EXPECTED_EXIT}")
    message(STATUS "execution-result: failed")
    message(FATAL_ERROR
        "Conversion returned code ${run_result} (esperado ${EXPECTED_EXIT}):\n${run_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${_guest_output}" "${GOLDEN_OUTPUT}"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    message(STATUS "execution-result: incorrect")
    message(FATAL_ERROR
        "Saída da conversão diverge do ouro (bytes): ${_guest_output} != ${GOLDEN_OUTPUT}")
endif()

message(STATUS "execution-result: supported")
