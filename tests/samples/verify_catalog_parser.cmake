if(NOT DEFINED RUNTIME OR NOT DEFINED WORK OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR "RUNTIME, WORK and RUST_ENABLED are required")
endif()
if(NOT EXISTS "${RUNTIME}")
    message(FATAL_ERROR "Runtime is missing: ${RUNTIME}")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config/tradutorlinux")
set(catalog "${WORK}/config/tradutorlinux/library.json")
set(env_args
    "HOME=${WORK}/home"
    "XDG_CONFIG_HOME=${WORK}/config"
)

file(WRITE "${catalog}" [=[
{
  "version": 1,
  "apps": [
    {
      "id": "catalog-fixture",
      "name": "Catalog Fixture",
      "executable_path": "fixture.exe"
    }
  ]
}
]=])

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args}
        "${RUNTIME}" app list --trace=runtime
    RESULT_VARIABLE traced_result
    OUTPUT_VARIABLE traced_stdout
    ERROR_VARIABLE traced_stderr
)
if(NOT traced_result EQUAL 0 OR NOT traced_stdout MATCHES "catalog-fixture")
    message(FATAL_ERROR "traced catalog listing failed (${traced_result})\nstdout:\n${traced_stdout}\nstderr:\n${traced_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args}
        "${RUNTIME}" app list
    RESULT_VARIABLE plain_result
    OUTPUT_VARIABLE plain_stdout
    ERROR_VARIABLE plain_stderr
)
if(NOT plain_result EQUAL 0 OR NOT plain_stdout MATCHES "catalog-fixture")
    message(FATAL_ERROR "plain catalog listing failed (${plain_result})\nstdout:\n${plain_stdout}\nstderr:\n${plain_stderr}")
endif()
if(NOT traced_stdout STREQUAL plain_stdout)
    message(FATAL_ERROR "catalog stdout changed by trace\nwith trace:\n${traced_stdout}\nwithout trace:\n${plain_stdout}")
endif()
if(NOT plain_stderr STREQUAL "")
    message(FATAL_ERROR "catalog emitted stderr without trace:\n${plain_stderr}")
endif()

if(RUST_ENABLED)
    if(NOT traced_stderr MATCHES "catalog-parse" OR
       NOT traced_stderr MATCHES "backend=\"rust\"" OR
       NOT traced_stderr MATCHES "parser-status=\"success\"" OR
       NOT traced_stderr MATCHES "apps=\"1\"")
        message(FATAL_ERROR "Rust catalog success trace is incomplete:\n${traced_stderr}")
    endif()
else()
    if(traced_stderr MATCHES "catalog-parse" OR
       traced_stderr MATCHES "backend=\"rust\"" OR
       traced_stderr MATCHES "parser-status=")
        message(FATAL_ERROR "Rust catalog fields leaked into the C++ baseline:\n${traced_stderr}")
    endif()
endif()

file(WRITE "${catalog}" [=[{"apps":[{"id":"legacy","executable_path":"legacy.exe"}]}]=])
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args}
        "${RUNTIME}" app list --trace=runtime
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(NOT invalid_result EQUAL 0)
    message(FATAL_ERROR "invalid catalog listing returned ${invalid_result}\nstdout:\n${invalid_stdout}\nstderr:\n${invalid_stderr}")
endif()
if(RUST_ENABLED)
    if(invalid_stdout MATCHES "legacy" OR NOT invalid_stdout MATCHES "Nenhum aplicativo cadastrado")
        message(FATAL_ERROR "Rust invalid catalog fell back to permissive C++ entries:\n${invalid_stdout}")
    endif()
    foreach(field IN ITEMS "parser-status=\"malformed\"" "code=\"" "phase=\""
                           "input-offset=\"" "detail-value=\"")
        if(NOT invalid_stderr MATCHES "${field}")
            message(FATAL_ERROR "Rust invalid catalog trace is missing ${field}:\n${invalid_stderr}")
        endif()
    endforeach()
else()
    if(NOT invalid_stdout MATCHES "legacy")
        message(FATAL_ERROR "C++ baseline no longer accepts its legacy catalog input:\n${invalid_stdout}")
    endif()
    if(invalid_stderr MATCHES "catalog-parse" OR invalid_stderr MATCHES "backend=\"rust\"")
        message(FATAL_ERROR "Rust fields leaked into invalid C++ baseline:\n${invalid_stderr}")
    endif()
endif()

file(REMOVE "${catalog}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${env_args}
        "${RUNTIME}" app list --trace=runtime
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_stdout
    ERROR_VARIABLE missing_stderr
)
if(NOT missing_result EQUAL 0 OR NOT missing_stdout MATCHES "Nenhum aplicativo cadastrado")
    message(FATAL_ERROR "missing catalog listing failed (${missing_result})\nstdout:\n${missing_stdout}\nstderr:\n${missing_stderr}")
endif()
if(missing_stderr MATCHES "catalog-parse" OR missing_stderr MATCHES "backend=\"rust\"")
    message(FATAL_ERROR "missing catalog unexpectedly emitted a Rust parse event:\n${missing_stderr}")
endif()

file(REMOVE_RECURSE "${WORK}")
