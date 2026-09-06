if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED APP_ID OR NOT DEFINED WORK
   OR NOT DEFINED EXPECTED_HEX OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR
        "INPUT, RUNTIME, APP_ID, WORK, EXPECTED_HEX and RUST_ENABLED are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")
set(prefix "${WORK}/prefix")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${INPUT}" --id "${APP_ID}" --name "${APP_ID}"
        --prefix "${prefix}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR
        "app add ${APP_ID} failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

set(input_args)
if(DEFINED INPUT_TEXT)
    set(input_file "${WORK}/input.txt")
    file(WRITE "${input_file}" "${INPUT_TEXT}")
    list(APPEND input_args INPUT_FILE "${input_file}")
endif()

set(run_command "${CMAKE_COMMAND}" -E env
    "HOME=${WORK}/home"
    "XDG_CONFIG_HOME=${WORK}/config"
    "${RUNTIME}" app run "${APP_ID}"
    "--trace=pe,loader,imports,runtime,process")
if(DEFINED EXTRA_ARGS)
    list(APPEND run_command ${EXTRA_ARGS})
endif()

execute_process(
    COMMAND ${run_command}
    ${input_args}
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_trace
)
if(DEFINED SKIP_EXIT AND run_result EQUAL SKIP_EXIT)
    message(STATUS "Skipping ${INPUT}: runtime environment does not permit this fixture")
    file(REMOVE_RECURSE "${WORK}")
    return()
endif()
if(DEFINED EXPECTED_EXIT)
    if(NOT run_result EQUAL EXPECTED_EXIT)
        message(FATAL_ERROR
            "app run returned ${run_result} for ${INPUT}, expected ${EXPECTED_EXIT}\n${run_trace}")
    endif()
elseif(NOT run_result EQUAL 0)
    message(FATAL_ERROR "app run returned ${run_result} for ${INPUT}\n${run_trace}")
endif()

string(HEX "${run_stdout}" actual_hex)
if(NOT actual_hex STREQUAL EXPECTED_HEX)
    message(FATAL_ERROR
        "Unexpected app run stdout for ${INPUT}: ${actual_hex}, expected ${EXPECTED_HEX}\n${run_trace}")
endif()

string(REGEX MATCH "\\[tl\\]\\[pe\\]\\[info\\] image[^\n]*" pe_image_line
    "${run_trace}")
if(NOT pe_image_line)
    message(FATAL_ERROR "app run did not emit the PE image event:\n${run_trace}")
endif()
if(RUST_ENABLED)
    if(NOT pe_image_line MATCHES "backend=\\\"rust\\\"")
        message(FATAL_ERROR "native app run did not select the Rust PE backend:\n${run_trace}")
    endif()
else()
    if(pe_image_line MATCHES "backend=\\\"rust\\\"")
        message(FATAL_ERROR "TL_BUILD_RUST=OFF emitted a Rust PE backend:\n${run_trace}")
    endif()
endif()

function(tl_app_assert_contains needle description)
    string(FIND "${run_trace}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${description}: '${needle}' was not found:\n${run_trace}")
    endif()
endfunction()

tl_app_assert_contains("[tl][loader][info] mapped" "app run loader mapping")
tl_app_assert_contains("[tl][process]" "app run process event")

if(DEFINED REQUIRED_TRACE)
    string(REPLACE "," ";" required_events "${REQUIRED_TRACE}")
    foreach(required_event IN LISTS required_events)
        string(FIND "${run_trace}" "[tl][runtime][info] ${required_event}" event_position)
        if(event_position EQUAL -1)
            message(FATAL_ERROR
                "Runtime event ${required_event} was not found in app run trace:\n${run_trace}")
        endif()
    endforeach()
endif()

if(DEFINED REQUIRED_TRACE_CONTAINS)
    string(FIND "${run_trace}" "${REQUIRED_TRACE_CONTAINS}" required_trace_position)
    if(required_trace_position EQUAL -1)
        message(FATAL_ERROR
            "Required trace text '${REQUIRED_TRACE_CONTAINS}' was not found:\n${run_trace}")
    endif()
endif()

if(DEFINED MANIFEST)
    if(NOT EXISTS "${MANIFEST}")
        message(FATAL_ERROR "Manifest was not found: ${MANIFEST}")
    endif()
    file(READ "${MANIFEST}" manifest_json)
    string(JSON manifest_dll_count LENGTH "${manifest_json}" imports)
    if(manifest_dll_count GREATER 0)
        math(EXPR last_manifest_dll "${manifest_dll_count} - 1")
        foreach(manifest_dll_index RANGE 0 ${last_manifest_dll})
            string(JSON manifest_dll MEMBER "${manifest_json}" imports ${manifest_dll_index})
            string(JSON manifest_symbol_count LENGTH "${manifest_json}" imports "${manifest_dll}")
            if(manifest_symbol_count GREATER 0)
                math(EXPR last_manifest_symbol "${manifest_symbol_count} - 1")
                foreach(manifest_symbol_index RANGE 0 ${last_manifest_symbol})
                    string(JSON manifest_symbol GET "${manifest_json}" imports
                        "${manifest_dll}" ${manifest_symbol_index})
                    tl_app_assert_contains(
                        "resolved dll=\"${manifest_dll}\" symbol=\"${manifest_symbol}\""
                        "resolved import ${manifest_dll}!${manifest_symbol}")
                endforeach()
            endif()
        endforeach()
    endif()
endif()

if(DEFINED CLEANUP_FILE)
    get_filename_component(input_directory "${INPUT}" DIRECTORY)
    file(REMOVE "${input_directory}/${CLEANUP_FILE}")
endif()

file(REMOVE_RECURSE "${WORK}")
