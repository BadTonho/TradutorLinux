if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME OR NOT DEFINED APP_ID OR NOT DEFINED WORK
   OR NOT DEFINED EXPECTED_EXIT OR NOT DEFINED REQUIRED_TRACE OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR
        "INPUT, RUNTIME, APP_ID, WORK, EXPECTED_EXIT, REQUIRED_TRACE and RUST_ENABLED are required")
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
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_trace
)
if(NOT run_result EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR
        "app run returned ${run_result} for ${INPUT}, expected ${EXPECTED_EXIT}\n${run_trace}")
endif()
if(NOT run_stdout STREQUAL "")
    message(FATAL_ERROR "Rejected app run wrote to stdout: ${run_stdout}")
endif()

string(FIND "${run_trace}" "${REQUIRED_TRACE}" required_position)
if(required_position EQUAL -1)
    message(FATAL_ERROR "'${REQUIRED_TRACE}' was not found in app run trace:\n${run_trace}")
endif()

string(REGEX MATCH "\\[tl\\]\\[pe\\]\\[info\\] image[^\n]*" pe_image_line
    "${run_trace}")
string(FIND "${run_trace}" "[tl][pe][error] parse-failed" parse_failed_position)
if(RUST_ENABLED)
    if(parse_failed_position EQUAL -1)
        if(NOT pe_image_line MATCHES "backend=\\\"rust\\\"")
            message(FATAL_ERROR "native app run did not select the Rust PE backend:\n${run_trace}")
        endif()
    else()
        if(NOT run_trace MATCHES "\\[tl\\]\\[pe\\]\\[error\\] parse-failed[^\n]*backend=\\\"rust\\\"")
            message(FATAL_ERROR "Rust parse failure did not carry backend metadata:\n${run_trace}")
        endif()
        foreach(field IN ITEMS code phase input-offset detail-value)
            if(NOT run_trace MATCHES "${field}=\\\"[0-9]+\\\"")
                message(FATAL_ERROR "Rust parse failure omitted ${field}:\n${run_trace}")
            endif()
        endforeach()
    endif()
else()
    if(pe_image_line MATCHES "backend=\\\"rust\\\"")
        message(FATAL_ERROR "TL_BUILD_RUST=OFF emitted a Rust PE backend:\n${run_trace}")
    endif()
endif()

if(DEFINED EXPECT_NO_LOADER AND EXPECT_NO_LOADER)
    string(FIND "${run_trace}" "[tl][loader][info] mapped" mapped_position)
    if(NOT mapped_position EQUAL -1)
        message(FATAL_ERROR "Rejected parsing reached image mapping:\n${run_trace}")
    endif()
    string(FIND "${run_trace}" "[tl][process]" process_position)
    if(NOT process_position EQUAL -1)
        message(FATAL_ERROR "Rejected parsing reached process execution:\n${run_trace}")
    endif()
endif()

file(REMOVE_RECURSE "${WORK}")
