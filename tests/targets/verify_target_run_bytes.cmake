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
if(NOT EXISTS "${GOLDEN_INPUT}")
    message(FATAL_ERROR "Golden fixture is missing: ${GOLDEN_INPUT}")
endif()

# GUEST_ARGS: lista separada por ponto-e-vírgula de argumentos do convidado,
# encaminhados depois do executável. O caminho do arquivo de entrada é sempre
# o último argumento.
set(_arguments "${INPUT}")
if(DEFINED GUEST_ARGS AND NOT GUEST_ARGS STREQUAL "")
    foreach(_arg IN LISTS GUEST_ARGS)
        list(APPEND _arguments "${_arg}")
    endforeach()
endif()
list(APPEND _arguments "${GOLDEN_INPUT}")

set(_guest_output "${CMAKE_CURRENT_BINARY_DIR}/bzip2_guest_output.bin")

execute_process(
    COMMAND "${RUNTIME}" ${_arguments}
    OUTPUT_FILE "${_guest_output}"
    ERROR_VARIABLE run_error
    RESULT_VARIABLE run_result
)

if(NOT DEFINED EXPECTED_EXIT)
    set(EXPECTED_EXIT 0)
endif()
if(NOT run_result EQUAL "${EXPECTED_EXIT}")
    message(STATUS "execution-result: failed")
    message(FATAL_ERROR
        "Run returned code ${run_result} (esperado ${EXPECTED_EXIT}):\n${run_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${_guest_output}" "${GOLDEN_OUTPUT}"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    message(STATUS "execution-result: incorrect")
    message(FATAL_ERROR
        "Saída do convidado diverge do ouro (bytes): ${_guest_output} != ${GOLDEN_OUTPUT}")
endif()

message(STATUS "execution-result: supported")