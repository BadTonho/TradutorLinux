if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, FIXTURE and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")

set(prefix_a "${WORK}/prefix-a")
set(prefix_b "${WORK}/prefix-b")
set(source_a "${prefix_a}/compat/files/injected.dat")
set(source_b "${prefix_b}/compat/files/injected.dat")
set(profile_a "${prefix_a}/compat/profile.json")
set(profile_b "${prefix_b}/compat/profile.json")
set(target_a "${prefix_a}/drive_c/Program Files/Compat Fixture/injected.dat")
set(target_b "${prefix_b}/drive_c/Program Files/Compat Fixture/injected.dat")
set(target_parent_a "${prefix_a}/drive_c/Program Files/Compat Fixture")
set(target_parent_b "${prefix_b}/drive_c/Program Files/Compat Fixture")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${FIXTURE}" --id compat-a --name "Compat A"
        --prefix "${prefix_a}"
    RESULT_VARIABLE add_a_result
    OUTPUT_VARIABLE add_a_stdout
    ERROR_VARIABLE add_a_stderr
)
if(NOT add_a_result EQUAL 0)
    message(FATAL_ERROR "app add for compat-a failed (${add_a_result})\nstdout:\n${add_a_stdout}\nstderr:\n${add_a_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${FIXTURE}" --id compat-b --name "Compat B"
        --prefix "${prefix_b}"
    RESULT_VARIABLE add_b_result
    OUTPUT_VARIABLE add_b_stdout
    ERROR_VARIABLE add_b_stderr
)
if(NOT add_b_result EQUAL 0)
    message(FATAL_ERROR "app add for compat-b failed (${add_b_result})\nstdout:\n${add_b_stdout}\nstderr:\n${add_b_stderr}")
endif()

file(WRITE "${source_a}" "profile-a\n")
file(WRITE "${source_b}" "profile-b\n")

set(APP_ID compat-a)
string(CONFIGURE [=[{
  "schema": 1,
  "app_id": "@APP_ID@",
  "files": [
    {
      "source": "injected.dat",
      "target": "C:\\Program Files\\Compat Fixture\\injected.dat"
    }
  ]
}
]=] profile_a_contents @ONLY)
file(WRITE "${profile_a}" "${profile_a_contents}")

set(APP_ID compat-b)
string(CONFIGURE [=[{
  "schema": 1,
  "app_id": "@APP_ID@",
  "files": [
    {
      "source": "injected.dat",
      "target": "C:\\Program Files\\Compat Fixture\\injected.dat"
    }
  ]
}
]=] profile_b_contents @ONLY)
file(WRITE "${profile_b}" "${profile_b_contents}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compat-a --trace=runtime
    RESULT_VARIABLE run_a_result
    OUTPUT_VARIABLE run_a_stdout
    ERROR_VARIABLE run_a_stderr
)
if(NOT run_a_result EQUAL 0)
    message(FATAL_ERROR "compat-a run failed (${run_a_result})\nstdout:\n${run_a_stdout}\nstderr:\n${run_a_stderr}")
endif()
string(HEX "${run_a_stdout}" run_a_hex)
if(NOT run_a_hex STREQUAL "70726f66696c652d610a")
    message(FATAL_ERROR "unexpected compat-a guest output: ${run_a_hex}\nstderr:\n${run_a_stderr}")
endif()
foreach(needle
        "compat-profile status=\"loaded\""
        "app-id=\"compat-a\""
        "compat-files status=\"applied\""
        "compat-file status=\"copied\""
        "compat-files-cleanup status=\"cleaned\"")
    if(NOT run_a_stderr MATCHES "${needle}")
        message(FATAL_ERROR "trace entry ${needle} for compat-a was not found:\n${run_a_stderr}")
    endif()
endforeach()

if(NOT EXISTS "${source_a}" OR NOT EXISTS "${source_b}")
    message(FATAL_ERROR "a compat source disappeared after compat-a")
endif()
if(EXISTS "${target_a}" OR EXISTS "${target_parent_a}")
    message(FATAL_ERROR "compat-a materialized target was not cleaned")
endif()
if(EXISTS "${target_b}" OR EXISTS "${target_parent_b}")
    message(FATAL_ERROR "compat-b target appeared before compat-b ran")
endif()
if(EXISTS "${prefix_a}/drive_c/compat" OR EXISTS "${prefix_b}/drive_c/compat")
    message(FATAL_ERROR "compat directory leaked into drive_c after compat-a")
endif()
file(READ "${source_a}" source_a_contents)
file(READ "${source_b}" source_b_contents)
if(NOT source_a_contents STREQUAL "profile-a\n")
    message(FATAL_ERROR "compat-a source was modified: ${source_a_contents}")
endif()
if(NOT source_b_contents STREQUAL "profile-b\n")
    message(FATAL_ERROR "compat-b source was modified before its run: ${source_b_contents}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compat-b --trace=runtime
    RESULT_VARIABLE run_b_result
    OUTPUT_VARIABLE run_b_stdout
    ERROR_VARIABLE run_b_stderr
)
if(NOT run_b_result EQUAL 0)
    message(FATAL_ERROR "compat-b run failed (${run_b_result})\nstdout:\n${run_b_stdout}\nstderr:\n${run_b_stderr}")
endif()
string(HEX "${run_b_stdout}" run_b_hex)
if(NOT run_b_hex STREQUAL "70726f66696c652d620a")
    message(FATAL_ERROR "unexpected compat-b guest output: ${run_b_hex}\nstderr:\n${run_b_stderr}")
endif()
foreach(needle
        "compat-profile status=\"loaded\""
        "app-id=\"compat-b\""
        "compat-files status=\"applied\""
        "compat-file status=\"copied\""
        "compat-files-cleanup status=\"cleaned\"")
    if(NOT run_b_stderr MATCHES "${needle}")
        message(FATAL_ERROR "trace entry ${needle} for compat-b was not found:\n${run_b_stderr}")
    endif()
endforeach()

if(EXISTS "${target_a}" OR EXISTS "${target_b}")
    message(FATAL_ERROR "a materialized target remained after both runs")
endif()
if(EXISTS "${target_parent_a}" OR EXISTS "${target_parent_b}")
    message(FATAL_ERROR "a materialized parent remained after both runs")
endif()
if(EXISTS "${prefix_a}/drive_c/compat" OR EXISTS "${prefix_b}/drive_c/compat")
    message(FATAL_ERROR "compat directory leaked into drive_c after compat-b")
endif()

file(READ "${source_a}" source_a_contents_after)
file(READ "${source_b}" source_b_contents_after)
if(NOT source_a_contents_after STREQUAL "profile-a\n")
    message(FATAL_ERROR "compat-a source was modified: ${source_a_contents_after}")
endif()
if(NOT source_b_contents_after STREQUAL "profile-b\n")
    message(FATAL_ERROR "compat-b source was modified: ${source_b_contents_after}")
endif()

file(REMOVE_RECURSE "${WORK}")
