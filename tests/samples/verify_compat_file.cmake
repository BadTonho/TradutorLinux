if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, FIXTURE and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")
set(prefix "${WORK}/prefix")
set(source "${prefix}/compat/files/injected.dat")
set(profile "${prefix}/compat/profile.json")
set(target "${prefix}/drive_c/Program Files/Compat Fixture/injected.dat")
set(target_parent "${prefix}/drive_c/Program Files/Compat Fixture")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${FIXTURE}" --id compatfiles --name "Compat Files"
        --prefix "${prefix}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "app add failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

file(WRITE "${source}" "compat payload\n")
file(WRITE "${profile}" "{\n  \"schema\": 1,\n  \"app_id\": \"compatfiles\",\n  \"files\": [\n    {\n      \"source\": \"injected.dat\",\n      \"target\": \"C:\\\\Program Files\\\\Compat Fixture\\\\injected.dat\"\n    }\n  ]\n}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compatfiles --trace=runtime
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "compat file run failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

string(HEX "${run_stdout}" output_hex)
if(NOT output_hex STREQUAL "636f6d706174207061796c6f61640a")
    message(FATAL_ERROR "unexpected guest output: ${output_hex}\nstderr:\n${run_stderr}")
endif()
foreach(needle
        "compat-profile status=\"loaded\""
        "compat-files status=\"applied\""
        "compat-file status=\"copied\""
        "compat-files-cleanup status=\"cleaned\"")
    if(NOT run_stderr MATCHES "${needle}")
        message(FATAL_ERROR "trace entry '${needle}' was not found:\n${run_stderr}")
    endif()
endforeach()

if(NOT EXISTS "${source}")
    message(FATAL_ERROR "compat source disappeared")
endif()
if(EXISTS "${target}" OR EXISTS "${target_parent}")
    message(FATAL_ERROR "materialized target was not cleaned")
endif()
if(EXISTS "${prefix}/drive_c/compat")
    message(FATAL_ERROR "compat directory leaked into drive_c")
endif()

file(READ "${source}" source_contents)
if(NOT source_contents STREQUAL "compat payload\n")
    message(FATAL_ERROR "compat source was modified: '${source_contents}'")
endif()

file(REMOVE_RECURSE "${WORK}")
