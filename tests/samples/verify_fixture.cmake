if(NOT DEFINED INPUT OR NOT DEFINED MANIFEST OR NOT DEFINED LLVM_READOBJ)
    message(FATAL_ERROR "INPUT, MANIFEST and LLVM_READOBJ are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers --coff-imports "${INPUT}"
    RESULT_VARIABLE readobj_result
    OUTPUT_VARIABLE readobj_output
    ERROR_VARIABLE readobj_error
)
if(NOT readobj_result EQUAL 0)
    message(FATAL_ERROR "llvm-readobj failed for ${INPUT}: ${readobj_error}")
endif()

file(READ "${MANIFEST}" manifest_json)
string(JSON expected_machine GET "${manifest_json}" machine)
string(FIND "${readobj_output}" "${expected_machine}" machine_position)
if(machine_position EQUAL -1)
    message(FATAL_ERROR "Expected machine ${expected_machine} was not found in ${INPUT}")
endif()

string(JSON expected_dll_count LENGTH "${manifest_json}" imports)
string(REGEX MATCHALL "Import \\{" import_blocks "${readobj_output}")
list(LENGTH import_blocks actual_dll_count)
if(NOT actual_dll_count EQUAL expected_dll_count)
    message(FATAL_ERROR
        "Expected ${expected_dll_count} imported DLLs in ${INPUT}, found ${actual_dll_count}"
    )
endif()

if(expected_dll_count GREATER 0)
    math(EXPR last_dll_index "${expected_dll_count} - 1")
    foreach(dll_index RANGE 0 ${last_dll_index})
        string(JSON dll_name MEMBER "${manifest_json}" imports ${dll_index})
        string(FIND "${readobj_output}" "${dll_name}" dll_position)
        if(dll_position EQUAL -1)
            message(FATAL_ERROR "Expected imported DLL ${dll_name} was not found in ${INPUT}")
        endif()

        string(JSON expected_symbol_count LENGTH "${manifest_json}" imports "${dll_name}")
        if(expected_symbol_count GREATER 0)
            math(EXPR last_symbol_index "${expected_symbol_count} - 1")
            foreach(symbol_index RANGE 0 ${last_symbol_index})
                string(JSON symbol_name GET "${manifest_json}" imports "${dll_name}" ${symbol_index})
                string(FIND "${readobj_output}" "${symbol_name}" symbol_position)
                if(symbol_position EQUAL -1)
                    message(FATAL_ERROR
                        "Expected symbol ${dll_name}!${symbol_name} was not found in ${INPUT}"
                    )
                endif()
            endforeach()
        endif()
    endforeach()
endif()
