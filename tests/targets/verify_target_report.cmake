if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED MANIFEST)
    message(FATAL_ERROR "INPUT, RUNTIME and MANIFEST are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Target app was not generated: ${INPUT}")
endif()

execute_process(
    COMMAND "${RUNTIME}" --report "${INPUT}"
    RESULT_VARIABLE report_result
    OUTPUT_VARIABLE report_output
    ERROR_VARIABLE report_error
)
if(NOT report_result EQUAL 0 AND NOT report_result EQUAL 5)
    message(FATAL_ERROR
        "Report returned unexpected code ${report_result} (esperado 0 ou 5 = Unsupported):\n${report_output}\n${report_error}")
endif()

string(FIND "${report_output}" "result: unsupported" result_position)
string(FIND "${report_output}" "execution: not-attempted" execution_position)
if(result_position EQUAL -1 OR execution_position EQUAL -1)
    message(FATAL_ERROR
        "Expected result: unsupported and execution: not-attempted:\n${report_output}")
endif()

file(READ "${MANIFEST}" manifest_json)
string(JSON expected_dll_count LENGTH "${manifest_json}" imports)
math(EXPR last_dll_index "${expected_dll_count} - 1")
foreach(dll_index RANGE 0 ${last_dll_index})
    string(JSON dll_name MEMBER "${manifest_json}" imports ${dll_index})
    string(JSON expected_symbol_count LENGTH "${manifest_json}" imports "${dll_name}")
    if(expected_symbol_count EQUAL 0)
        message(FATAL_ERROR "Manifest has no symbols for ${dll_name}")
    endif()
    math(EXPR last_symbol_index "${expected_symbol_count} - 1")
    foreach(symbol_index RANGE 0 ${last_symbol_index})
        string(JSON symbol_name GET "${manifest_json}" imports "${dll_name}" ${symbol_index})
        string(FIND "${report_output}" "import: ${dll_name}!${symbol_name}" symbol_position)
        if(symbol_position EQUAL -1)
            message(FATAL_ERROR
                "Report does not list ${dll_name}!${symbol_name}:\n${report_output}")
        endif()
    endforeach()
endforeach()
