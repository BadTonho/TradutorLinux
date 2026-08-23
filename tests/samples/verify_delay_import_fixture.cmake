if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME)
    message(FATAL_ERROR "INPUT and RUNTIME are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Delay-import fixture was not generated: ${INPUT}")
endif()

execute_process(
    COMMAND "${RUNTIME}" --trace "${INPUT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime returned ${runtime_result} for ${INPUT}:\n${runtime_trace}")
endif()
if(NOT runtime_stdout STREQUAL "")
    message(FATAL_ERROR "Delay-import fixture wrote unexpected stdout: ${runtime_stdout}")
endif()

foreach(needle
    "delay-import dll=\"KERNEL32.dll\" symbols=\"ExitProcess\""
    "resolved dll=\"KERNEL32.dll\" symbol=\"ExitProcess\""
    "mechanism=\"delay-import\""
)
    string(FIND "${runtime_trace}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Trace does not contain '${needle}':\n${runtime_trace}")
    endif()
endforeach()

if(DEFINED MISSING_INPUT)
    execute_process(
        COMMAND "${RUNTIME}" --trace "${MISSING_INPUT}"
        RESULT_VARIABLE missing_result
        OUTPUT_VARIABLE missing_stdout
        ERROR_VARIABLE missing_trace
    )
    if(NOT missing_result EQUAL 5)
        message(FATAL_ERROR "Missing delay import returned ${missing_result}:\n${missing_trace}")
    endif()
    if(NOT missing_stdout STREQUAL "")
        message(FATAL_ERROR "Missing delay import wrote stdout: ${missing_stdout}")
    endif()
    foreach(needle
        "unresolved dll=\"KERNEL32.dll\" symbol=\"TlMissingDelayImportW\""
        "status=\"unknown-symbol\""
        "mechanism=\"delay-import\""
    )
        string(FIND "${missing_trace}" "${needle}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Missing-import trace does not contain '${needle}':\n${missing_trace}")
        endif()
    endforeach()
endif()

execute_process(
    COMMAND "${RUNTIME}" --report "${INPUT}"
    RESULT_VARIABLE report_result
    OUTPUT_VARIABLE report_output
    ERROR_VARIABLE report_error
)
if(NOT report_result EQUAL 0)
    message(FATAL_ERROR "Report returned ${report_result}:\n${report_output}\n${report_error}")
endif()
foreach(needle
    "mechanism: delay-import (1/1 resolved)"
    "delay-import dll: KERNEL32.dll (1/1 resolved)"
    "import: ExitProcess status=resolved"
    "result: supported"
    "execution: not-attempted"
)
    string(FIND "${report_output}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Report does not contain '${needle}':\n${report_output}")
    endif()
endforeach()
