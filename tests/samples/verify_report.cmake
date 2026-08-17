if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME)
    message(FATAL_ERROR "INPUT and RUNTIME are required")
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
set(required_items "result: supported;execution: not-attempted")
if(DEFINED EXPECTED_SYMBOL)
    list(APPEND required_items "${EXPECTED_SYMBOL} status=resolved")
elseif(DEFINED EXPECT_USER32)
    list(APPEND required_items "import: MessageBoxA status=resolved")
else()
    list(APPEND required_items "import: VirtualAlloc status=resolved")
endif()
foreach(needle IN LISTS required_items)
    string(FIND "${report_output}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Report does not contain '${needle}':\n${report_output}")
    endif()
endforeach()
string(FIND "${report_output}" "fase5" guest_output_position)
if(NOT guest_output_position EQUAL -1)
    message(FATAL_ERROR "Report executed guest code:\n${report_output}")
endif()
