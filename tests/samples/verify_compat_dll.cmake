if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED DLL_A OR NOT DEFINED DLL_B
   OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, FIXTURE, DLL_A, DLL_B and WORK are required")
endif()

foreach(input IN ITEMS "${RUNTIME}" "${FIXTURE}" "${DLL_A}" "${DLL_B}")
    if(NOT EXISTS "${input}")
        message(FATAL_ERROR "Required fixture is missing: ${input}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")
set(prefix_a "${WORK}/prefix-a")
set(prefix_b "${WORK}/prefix-b")
set(dll_source_a "${prefix_a}/compat/dlls/compat-a.dll")
set(dll_source_b "${prefix_b}/compat/dlls/compat-b.dll")
set(profile_a "${prefix_a}/compat/profile.json")
set(profile_b "${prefix_b}/compat/profile.json")

function(tl_add_fixture_app id prefix result_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "HOME=${WORK}/home"
            "XDG_CONFIG_HOME=${WORK}/config"
            "${RUNTIME}" app add "${FIXTURE}" --id "${id}" --name "${id}"
            --prefix "${prefix}"
        RESULT_VARIABLE add_result
        OUTPUT_VARIABLE add_stdout
        ERROR_VARIABLE add_stderr
    )
    if(NOT add_result EQUAL 0)
        message(FATAL_ERROR "app add ${id} failed (${add_result})\n${add_stdout}\n${add_stderr}")
    endif()
    set(${result_var} TRUE PARENT_SCOPE)
endfunction()

tl_add_fixture_app(compat-dll-a "${prefix_a}" added_a)
tl_add_fixture_app(compat-dll-b "${prefix_b}" added_b)
file(COPY_FILE "${DLL_A}" "${dll_source_a}")
file(COPY_FILE "${DLL_B}" "${dll_source_b}")

set(APP_ID compat-dll-a)
set(DLL_SOURCE compat-a.dll)
string(CONFIGURE [=[{
  "schema": 2,
  "app_id": "@APP_ID@",
  "files": [],
  "dlls": [
    {
      "module": "compat.dll",
      "source": "@DLL_SOURCE@"
    }
  ]
}
]=] profile_a_contents @ONLY)
file(WRITE "${profile_a}" "${profile_a_contents}")

set(APP_ID compat-dll-b)
set(DLL_SOURCE compat-b.dll)
string(CONFIGURE [=[{
  "schema": 2,
  "app_id": "@APP_ID@",
  "files": [],
  "dlls": [
    {
      "module": "compat.dll",
      "source": "@DLL_SOURCE@"
    }
  ]
}
]=] profile_b_contents @ONLY)
file(WRITE "${profile_b}" "${profile_b_contents}")

function(tl_run_fixture id expected_hex result_var stdout_var stderr_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "HOME=${WORK}/home"
            "XDG_CONFIG_HOME=${WORK}/config"
            "${RUNTIME}" app run "${id}" --trace
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
    string(HEX "${run_stdout}" run_hex)
    if(NOT run_result EQUAL 0 OR NOT run_hex STREQUAL "${expected_hex}")
        message(FATAL_ERROR "${id} failed (${run_result}), output=${run_hex}\n${run_stderr}")
    endif()
    set(${result_var} TRUE PARENT_SCOPE)
    set(${stdout_var} "${run_stdout}" PARENT_SCOPE)
    set(${stderr_var} "${run_stderr}" PARENT_SCOPE)
endfunction()

tl_run_fixture(compat-dll-a "636f6d7061742d610a" ran_a run_a_stdout run_a_trace)
foreach(needle
        "provider-selected module=\"compat.dll\""
        "dll-mapped module=\"compat.dll\""
        "import-resolved module=\"KERNEL32.dll\""
        "dll-attach module=\"compat.dll\""
        "dll-detach module=\"compat.dll\"")
    if(NOT run_a_trace MATCHES "${needle}")
        message(FATAL_ERROR "A trace did not contain '${needle}':\n${run_a_trace}")
    endif()
endforeach()
if(NOT EXISTS "${dll_source_a}" OR EXISTS "${prefix_a}/drive_c/compat.dll"
   OR EXISTS "${prefix_a}/drive_c/compat")
    message(FATAL_ERROR "A DLL source was lost or leaked into drive_c")
endif()

tl_run_fixture(compat-dll-b "636f6d7061742d620a" ran_b run_b_stdout run_b_trace)
if(NOT run_b_trace MATCHES "provider-selected module=\"compat.dll\"" OR
   NOT run_b_trace MATCHES "provider=\"profile\"")
    message(FATAL_ERROR "B profile provider was not selected:\n${run_b_trace}")
endif()
if(NOT EXISTS "${dll_source_b}" OR EXISTS "${prefix_b}/drive_c/compat.dll"
   OR EXISTS "${prefix_b}/drive_c/compat")
    message(FATAL_ERROR "B DLL source was lost or leaked into drive_c")
endif()

file(REMOVE "${profile_a}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compat-dll-a --trace
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_stdout
    ERROR_VARIABLE missing_trace
)
if(missing_result EQUAL 0 OR missing_stdout MATCHES "compat-a")
    message(FATAL_ERROR "missing profile unexpectedly executed custom DLL\n${missing_trace}")
endif()

file(WRITE "${profile_a}" "{\n  \"schema\": 1,\n  \"app_id\": \"compat-dll-a\",\n  \"files\": []\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compat-dll-a --trace
    RESULT_VARIABLE schema1_result
    OUTPUT_VARIABLE schema1_stdout
    ERROR_VARIABLE schema1_trace
)
if(schema1_result EQUAL 0 OR schema1_stdout MATCHES "compat-a")
    message(FATAL_ERROR "schema v1 unexpectedly executed custom DLL\n${schema1_trace}")
endif()

file(WRITE "${profile_a}" "{\n  \"schema\": 2,\n  \"app_id\": \"compat-dll-a\",\n  \"files\": [],\n  \"dlls\": [{\"module\": \"compat.dll\", \"source\": \"missing.dll\"}]\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compat-dll-a --trace
    RESULT_VARIABLE invalid_dll_result
    OUTPUT_VARIABLE invalid_dll_stdout
    ERROR_VARIABLE invalid_dll_trace
)
if(invalid_dll_result EQUAL 0 OR invalid_dll_stdout MATCHES "compat-a")
    message(FATAL_ERROR "invalid DLL unexpectedly executed custom code\n${invalid_dll_trace}")
endif()
if(NOT invalid_dll_trace MATCHES "provider-rejected")
    message(FATAL_ERROR "invalid DLL rejection was not traced:\n${invalid_dll_trace}")
endif()

file(REMOVE_RECURSE "${WORK}")
