if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED LLVM_READOBJ)
    message(FATAL_ERROR "INPUT, RUNTIME and LLVM_READOBJ are required")
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
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime returned ${runtime_result} for ${INPUT}\n${runtime_trace}")
endif()

if(NOT runtime_stdout STREQUAL "")
    message(FATAL_ERROR "Runtime wrote to stdout; trace must go to stderr only")
endif()

execute_process(
    COMMAND "${LLVM_READOBJ}" --file-headers --sections --coff-imports "${INPUT}"
    RESULT_VARIABLE readobj_result
    OUTPUT_VARIABLE readobj_output
    ERROR_VARIABLE readobj_error
)
if(NOT readobj_result EQUAL 0)
    message(FATAL_ERROR "llvm-readobj failed for ${INPUT}: ${readobj_error}")
endif()

function(tl_check_trace_contains trace needle label)
    string(FIND "${trace}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: '${needle}' not found in runtime trace:\n${trace}")
    endif()
endfunction()

string(REGEX MATCH "AddressOfEntryPoint: 0x[0-9a-fA-F]+" entry_match "${readobj_output}")
if(NOT entry_match)
    message(FATAL_ERROR "AddressOfEntryPoint not found in llvm-readobj output")
endif()
string(REPLACE "AddressOfEntryPoint: " "" entry_value "${entry_match}")
tl_check_trace_contains("${runtime_trace}" "${entry_value}" "entry point")

string(REGEX MATCH "ImageBase: 0x[0-9a-fA-F]+" base_match "${readobj_output}")
if(NOT base_match)
    message(FATAL_ERROR "ImageBase not found in llvm-readobj output")
endif()
string(REPLACE "ImageBase: " "" base_value "${base_match}")
tl_check_trace_contains("${runtime_trace}" "${base_value}" "image base")

string(REGEX MATCHALL "VirtualAddress: 0x[0-9a-fA-F]+" section_va_matches "${readobj_output}")
if(NOT section_va_matches)
    message(FATAL_ERROR "No section virtual addresses found in llvm-readobj output")
endif()
foreach(section_va IN LISTS section_va_matches)
    string(REPLACE "VirtualAddress: " "" section_va_value "${section_va}")
    tl_check_trace_contains("${runtime_trace}" "virtual-address=\"${section_va_value}\"" "section VA")
endforeach()

string(REGEX MATCHALL "DLL: [^\n]+" dll_matches "${readobj_output}")
foreach(dll_match IN LISTS dll_matches)
    string(REPLACE "DLL: " "" dll_name "${dll_match}")
    tl_check_trace_contains("${runtime_trace}" "dll=\"${dll_name}\"" "imported DLL")
endforeach()

string(REGEX MATCHALL "Symbol: [^\n]+" symbol_matches "${readobj_output}")
foreach(symbol_match IN LISTS symbol_matches)
    string(REPLACE "Symbol: " "" symbol_token "${symbol_match}")
    string(REGEX MATCH "^[^ (]+" symbol_name "${symbol_token}")
    tl_check_trace_contains("${runtime_trace}" "${symbol_name}" "imported symbol")
endforeach()

if(DEFINED MANIFEST)
    file(READ "${MANIFEST}" manifest_json)
    string(JSON manifest_dll_count LENGTH "${manifest_json}" imports)
    if(manifest_dll_count GREATER 0)
        math(EXPR last_manifest_dll "${manifest_dll_count} - 1")
        foreach(manifest_dll_index RANGE 0 ${last_manifest_dll})
            string(JSON manifest_dll MEMBER "${manifest_json}" imports ${manifest_dll_index})
            string(JSON manifest_symbol_count LENGTH "${manifest_json}" imports "${manifest_dll}")
            math(EXPR last_manifest_symbol "${manifest_symbol_count} - 1")
            foreach(manifest_symbol_index RANGE 0 ${last_manifest_symbol})
                string(JSON manifest_symbol GET "${manifest_json}" imports
                    "${manifest_dll}" ${manifest_symbol_index})
                tl_check_trace_contains("${runtime_trace}"
                    "resolved dll=\"${manifest_dll}\" symbol=\"${manifest_symbol}\""
                    "resolved import ${manifest_dll}!${manifest_symbol}")
            endforeach()
        endforeach()
    endif()
endif()

if(DEFINED REQUIRE_SEH_HANDLER_METADATA)
    string(REGEX MATCH
        "seh state=\"handler\" code=\"[0-9]+\" detail=\"search\" mechanism=\"x64-seh\" function-index=\"[0-9]+\" handler-rva=\"[0-9]+\" handler-data-rva=\"[0-9]+\""
        seh_handler_metadata "${runtime_trace}")
    if(NOT seh_handler_metadata)
        message(FATAL_ERROR
            "SEH handler metadata is missing from the trace:\n${runtime_trace}")
    endif()
endif()
