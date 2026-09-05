if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED PROTON_ROOT OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, FIXTURE, PROTON_ROOT and WORK are required")
endif()
if(NOT EXISTS "${RUNTIME}" OR NOT EXISTS "${FIXTURE}")
    message(FATAL_ERROR "Runtime or compatibility fixture is missing")
endif()
if(NOT IS_DIRECTORY "${PROTON_ROOT}")
    message(FATAL_ERROR "Proton root is not a directory: ${PROTON_ROOT}")
endif()

find_program(XVFB_RUN_EXECUTABLE NAMES xvfb-run)
if(NOT XVFB_RUN_EXECUTABLE)
    message(FATAL_ERROR "xvfb-run is required for the real Proton integration")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config/tradutorlinux")

set(prefix_a "${WORK}/prefix-a")
set(prefix_b "${WORK}/prefix-b")
set(app_dir_a "${prefix_a}/drive_c/Program Files/Compat Fixture")
set(app_dir_b "${prefix_b}/drive_c/Program Files/Compat Fixture")
set(executable_a "${app_dir_a}/tl_compat_file.exe")
set(executable_b "${app_dir_b}/tl_compat_file.exe")
set(source_a "${prefix_a}/compat/files/injected.dat")
set(source_b "${prefix_b}/compat/files/injected.dat")
set(profile_a "${prefix_a}/compat/profile.json")
set(profile_b "${prefix_b}/compat/profile.json")
set(target_a "${prefix_a}/proton/compatdata/pfx/drive_c/Program Files/Compat Fixture/injected.dat")
set(target_b "${prefix_b}/proton/compatdata/pfx/drive_c/Program Files/Compat Fixture/injected.dat")
set(staged_executable_a "${prefix_a}/proton/compatdata/pfx/drive_c/Program Files/Compat Fixture/tl_compat_file.exe")
set(staged_executable_b "${prefix_b}/proton/compatdata/pfx/drive_c/Program Files/Compat Fixture/tl_compat_file.exe")
set(wrapper "${WORK}/run_probe.sh")
set(stdout_file_a "${WORK}/stdout-a.txt")
set(stderr_file_a "${WORK}/stderr-a.txt")
set(stdout_file_b "${WORK}/stdout-b.txt")
set(stderr_file_b "${WORK}/stderr-b.txt")

file(MAKE_DIRECTORY "${app_dir_a}" "${app_dir_b}" "${prefix_a}/compat" "${prefix_b}/compat")
file(COPY "${FIXTURE}" DESTINATION "${app_dir_a}")
file(COPY "${FIXTURE}" DESTINATION "${app_dir_b}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${executable_a}" --id proton-isolation-a --name "Proton Isolation A"
        --prefix "${prefix_a}"
    RESULT_VARIABLE add_a_result
    OUTPUT_VARIABLE add_a_stdout
    ERROR_VARIABLE add_a_stderr
)
if(NOT add_a_result EQUAL 0)
    message(FATAL_ERROR "app add for proton-isolation-a failed (${add_a_result})\nstdout:\n${add_a_stdout}\nstderr:\n${add_a_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${executable_b}" --id proton-isolation-b --name "Proton Isolation B"
        --prefix "${prefix_b}"
    RESULT_VARIABLE add_b_result
    OUTPUT_VARIABLE add_b_stdout
    ERROR_VARIABLE add_b_stderr
)
if(NOT add_b_result EQUAL 0)
    message(FATAL_ERROR "app add for proton-isolation-b failed (${add_b_result})\nstdout:\n${add_b_stdout}\nstderr:\n${add_b_stderr}")
endif()

file(WRITE "${source_a}" "proton profile a\n")
file(WRITE "${source_b}" "proton profile b\n")

set(APP_ID proton-isolation-a)
string(CONFIGURE [=[{
  "schema": 3,
  "app_id": "@APP_ID@",
  "files": [
    {
      "source": "injected.dat",
      "target": "C:\\Program Files\\Compat Fixture\\injected.dat"
    }
  ],
  "backend": {"kind": "proton", "min_version": "11.0"}
}
]=] profile_a_contents @ONLY)
file(WRITE "${profile_a}" "${profile_a_contents}")

set(APP_ID proton-isolation-b)
string(CONFIGURE [=[{
  "schema": 3,
  "app_id": "@APP_ID@",
  "files": [
    {
      "source": "injected.dat",
      "target": "C:\\Program Files\\Compat Fixture\\injected.dat"
    }
  ],
  "backend": {"kind": "proton", "min_version": "11.0"}
}
]=] profile_b_contents @ONLY)
file(WRITE "${profile_b}" "${profile_b_contents}")

file(WRITE "${WORK}/config/tradutorlinux/backends.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"proton\": {\n"
    "    \"root\": \"${PROTON_ROOT}\"\n"
    "  }\n"
    "}\n"
)

# xvfb-run combines the command streams. The wrapper restores the separation
# so the test proves that guest stdout is unchanged and Proton diagnostics stay
# on stderr.
file(WRITE "${wrapper}" [=[#!/bin/sh
exec "$TL_ISOLATION_RUNTIME" app run "$TL_ISOLATION_APP_ID" --trace=proton,process >"$TL_ISOLATION_STDOUT" 2>"$TL_ISOLATION_STDERR"
]=])
file(CHMOD "${wrapper}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

function(run_isolation_app app_id stdout_file stderr_file result_var)
    execute_process(
        COMMAND "${XVFB_RUN_EXECUTABLE}" --auto-servernum -s "-screen 0 1280x720x24"
            "${CMAKE_COMMAND}" -E env
            "HOME=${WORK}/home"
            "XDG_CONFIG_HOME=${WORK}/config"
            "TL_PROTON_ROOT=${PROTON_ROOT}"
            "WINEDEBUG=-all"
            "TL_ISOLATION_RUNTIME=${RUNTIME}"
            "TL_ISOLATION_APP_ID=${app_id}"
            "TL_ISOLATION_STDOUT=${stdout_file}"
            "TL_ISOLATION_STDERR=${stderr_file}"
            "/bin/sh" "${wrapper}"
        TIMEOUT 180
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_wrapper_stdout
        ERROR_VARIABLE run_wrapper_stderr
    )
    if(NOT run_wrapper_stderr STREQUAL "")
        file(APPEND "${stderr_file}" "\n${run_wrapper_stderr}")
    endif()
    set(${result_var} "${run_result}" PARENT_SCOPE)
endfunction()

run_isolation_app(proton-isolation-a "${stdout_file_a}" "${stderr_file_a}" run_a_result)
if(NOT run_a_result EQUAL 0)
    file(READ "${stdout_file_a}" run_a_stdout ERROR_QUIET)
    file(READ "${stderr_file_a}" run_a_stderr ERROR_QUIET)
    message(FATAL_ERROR "real Proton run for A returned ${run_a_result}\nstdout:\n${run_a_stdout}\nstderr:\n${run_a_stderr}")
endif()
file(READ "${stdout_file_a}" run_a_stdout)
file(READ "${stderr_file_a}" run_a_stderr)
if(NOT run_a_stdout STREQUAL "proton profile a\n")
    message(FATAL_ERROR "prefix A did not observe its own profile: '${run_a_stdout}'\nstderr:\n${run_a_stderr}")
endif()
foreach(needle
        "[tl][proton][info] provider-selected"
        "[tl][proton][info] staging-complete"
        "[tl][proton][info] launch"
        "[tl][proton][info] files-cleanup"
        "[tl][proton][info] exit")
    string(FIND "${run_a_stderr}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "Proton diagnostic '${needle}' for A was not found:\n${run_a_stderr}")
    endif()
endforeach()

if(NOT EXISTS "${source_a}" OR NOT EXISTS "${source_b}")
    message(FATAL_ERROR "a native compatibility source disappeared after prefix A")
endif()
if(EXISTS "${target_a}")
    message(FATAL_ERROR "prefix A materialized target was not cleaned")
endif()
if(EXISTS "${target_b}")
    message(FATAL_ERROR "prefix B target appeared before prefix B ran")
endif()
if(EXISTS "${prefix_a}/proton/compat" OR EXISTS "${prefix_b}/proton/compat")
    message(FATAL_ERROR "native compat metadata leaked into a Proton tree after prefix A")
endif()
if(NOT EXISTS "${staged_executable_a}" OR NOT EXISTS "${prefix_a}/proton/application-manifest.json")
    message(FATAL_ERROR "prefix A did not retain its staged application and manifest")
endif()
file(SHA256 "${executable_a}" executable_a_hash)
file(SHA256 "${staged_executable_a}" staged_a_hash)
if(NOT executable_a_hash STREQUAL staged_a_hash)
    message(FATAL_ERROR "prefix A staged executable differs from its native source")
endif()
file(READ "${source_a}" source_a_contents)
file(READ "${source_b}" source_b_contents)
if(NOT source_a_contents STREQUAL "proton profile a\n" OR
   NOT source_b_contents STREQUAL "proton profile b\n")
    message(FATAL_ERROR "native profile sources were changed after prefix A")
endif()

run_isolation_app(proton-isolation-b "${stdout_file_b}" "${stderr_file_b}" run_b_result)
if(NOT run_b_result EQUAL 0)
    file(READ "${stdout_file_b}" run_b_stdout ERROR_QUIET)
    file(READ "${stderr_file_b}" run_b_stderr ERROR_QUIET)
    message(FATAL_ERROR "real Proton run for B returned ${run_b_result}\nstdout:\n${run_b_stdout}\nstderr:\n${run_b_stderr}")
endif()
file(READ "${stdout_file_b}" run_b_stdout)
file(READ "${stderr_file_b}" run_b_stderr)
if(NOT run_b_stdout STREQUAL "proton profile b\n")
    message(FATAL_ERROR "prefix B did not observe its own profile: '${run_b_stdout}'\nstderr:\n${run_b_stderr}")
endif()
foreach(needle
        "[tl][proton][info] provider-selected"
        "[tl][proton][info] staging-complete"
        "[tl][proton][info] launch"
        "[tl][proton][info] files-cleanup"
        "[tl][proton][info] exit")
    string(FIND "${run_b_stderr}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "Proton diagnostic '${needle}' for B was not found:\n${run_b_stderr}")
    endif()
endforeach()

if(EXISTS "${target_a}" OR EXISTS "${target_b}")
    message(FATAL_ERROR "a materialized target remained after both Proton runs")
endif()
if(EXISTS "${prefix_a}/proton/compat" OR EXISTS "${prefix_b}/proton/compat")
    message(FATAL_ERROR "native compat metadata leaked into a Proton tree after prefix B")
endif()
if(NOT EXISTS "${staged_executable_b}" OR NOT EXISTS "${prefix_b}/proton/application-manifest.json")
    message(FATAL_ERROR "prefix B did not retain its staged application and manifest")
endif()
file(SHA256 "${executable_b}" executable_b_hash)
file(SHA256 "${staged_executable_b}" staged_b_hash)
if(NOT executable_b_hash STREQUAL staged_b_hash)
    message(FATAL_ERROR "prefix B staged executable differs from its native source")
endif()
file(READ "${source_a}" source_a_contents_after)
file(READ "${source_b}" source_b_contents_after)
if(NOT source_a_contents_after STREQUAL "proton profile a\n" OR
   NOT source_b_contents_after STREQUAL "proton profile b\n")
    message(FATAL_ERROR "native profile sources were changed after both Proton runs")
endif()

file(READ "${prefix_a}/proton/application-manifest.json" manifest_a)
file(READ "${prefix_b}/proton/application-manifest.json" manifest_b)
if(NOT manifest_a MATCHES "proton-isolation-a" OR manifest_a MATCHES "proton-isolation-b")
    message(FATAL_ERROR "prefix A manifest is not isolated:\n${manifest_a}")
endif()
if(NOT manifest_b MATCHES "proton-isolation-b" OR manifest_b MATCHES "proton-isolation-a")
    message(FATAL_ERROR "prefix B manifest is not isolated:\n${manifest_b}")
endif()

file(REMOVE_RECURSE "${WORK}")
