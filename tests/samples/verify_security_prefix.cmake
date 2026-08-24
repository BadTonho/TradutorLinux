if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED WORK)
    message(FATAL_ERROR "INPUT, RUNTIME and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
set(prefix_a "${WORK}/prefix-a")
set(prefix_b "${WORK}/prefix-b")

function(tl_run_security prefix expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "TL_PREFIX=${prefix}" "${RUNTIME}" --trace "${INPUT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE trace
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "security runtime returned ${result}:\n${trace}")
    endif()
    if(NOT stdout STREQUAL "${expected}")
        message(FATAL_ERROR "unexpected security stdout '${stdout}', expected '${expected}':\n${trace}")
    endif()
    string(FIND "${trace}" "[tl][runtime][info] security" trace_position)
    if(trace_position EQUAL -1)
        message(FATAL_ERROR "security trace not found:\n${trace}")
    endif()
endfunction()

tl_run_security("${prefix_a}" "security-write\n")
tl_run_security("${prefix_a}" "security-read\n")
tl_run_security("${prefix_b}" "security-write\n")

file(REMOVE_RECURSE "${WORK}")
