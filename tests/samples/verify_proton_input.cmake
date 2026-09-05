if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED PROTON_ROOT OR NOT DEFINED WORK OR
   NOT DEFINED INPUT_DRIVER OR NOT DEFINED APP_ID OR NOT DEFINED PROBE_NAME OR
   NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "RUNTIME, FIXTURE, PROTON_ROOT, WORK, INPUT_DRIVER, APP_ID, PROBE_NAME and EXPECTED_OUTPUT are required")
endif()
if(NOT EXISTS "${RUNTIME}" OR NOT EXISTS "${FIXTURE}" OR NOT EXISTS "${INPUT_DRIVER}")
    message(FATAL_ERROR "Runtime, input fixture or input driver is missing")
endif()
if(NOT IS_DIRECTORY "${PROTON_ROOT}")
    message(FATAL_ERROR "Proton root is not a directory: ${PROTON_ROOT}")
endif()

find_program(XVFB_RUN_EXECUTABLE NAMES xvfb-run)
if(NOT XVFB_RUN_EXECUTABLE)
    message(FATAL_ERROR "xvfb-run is required for the real Proton input integration")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config/tradutorlinux")
set(prefix "${WORK}/prefix")
get_filename_component(fixture_name "${FIXTURE}" NAME)
set(app_dir "${prefix}/drive_c/Program Files/${PROBE_NAME}")
set(executable "${app_dir}/${fixture_name}")
set(profile "${prefix}/compat/profile.json")
set(proton_prefix "${prefix}/proton/compatdata/pfx")
set(staged_executable "${proton_prefix}/drive_c/Program Files/${PROBE_NAME}/${fixture_name}")
set(run_wrapper "${WORK}/run_input_probe.sh")
set(run_stdout_file "${WORK}/input-stdout.txt")
set(run_stderr_file "${WORK}/input-stderr.txt")
set(driver_stderr_file "${WORK}/input-driver-stderr.txt")

file(MAKE_DIRECTORY "${app_dir}" "${prefix}/compat")
file(COPY "${FIXTURE}" DESTINATION "${app_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${executable}" --id "${APP_ID}" --name "${PROBE_NAME}"
        --prefix "${prefix}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "app add failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

file(WRITE "${profile}"
    "{\n"
    "  \"schema\": 3,\n"
    "  \"app_id\": \"${APP_ID}\",\n"
    "  \"files\": [],\n"
    "  \"backend\": {\"kind\": \"proton\", \"min_version\": \"11.0\"}\n"
    "}\n"
)
file(WRITE "${WORK}/config/tradutorlinux/backends.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"proton\": {\n"
    "    \"root\": \"${PROTON_ROOT}\"\n"
    "  }\n"
    "}\n"
)

# xvfb-run merges stderr into stdout. The wrapper keeps the application
# streams separate and drives the window while the Proton process is alive.
file(WRITE "${run_wrapper}" [=[#!/bin/sh
"$TL_INPUT_RUNTIME" app run "$TL_INPUT_APP_ID" --trace=proton,process >"$TL_INPUT_STDOUT" 2>"$TL_INPUT_STDERR" &
runtime_pid=$!
"$TL_INPUT_DRIVER" "$DISPLAY" "$TL_INPUT_WINDOW" > /dev/null 2>"$TL_INPUT_DRIVER_STDERR"
driver_result=$?
if [ "$driver_result" -ne 0 ]; then
    kill "$runtime_pid" 2>/dev/null || true
    wait "$runtime_pid" 2>/dev/null || true
    exit 125
fi
wait "$runtime_pid"
runtime_result=$?
exit "$runtime_result"
]=])
file(CHMOD "${run_wrapper}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

execute_process(
    COMMAND "${XVFB_RUN_EXECUTABLE}" --auto-servernum -s "-screen 0 1280x720x24"
        "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "TL_PROTON_ROOT=${PROTON_ROOT}"
        "PROTON_LOG=1"
        "TL_INPUT_RUNTIME=${RUNTIME}"
        "TL_INPUT_APP_ID=${APP_ID}"
        "TL_INPUT_WINDOW=${PROBE_NAME}"
        "TL_INPUT_DRIVER=${INPUT_DRIVER}"
        "TL_INPUT_STDOUT=${run_stdout_file}"
        "TL_INPUT_STDERR=${run_stderr_file}"
        "TL_INPUT_DRIVER_STDERR=${driver_stderr_file}"
        "/bin/sh" "${run_wrapper}"
    TIMEOUT 300
    RESULT_VARIABLE xvfb_result
    OUTPUT_VARIABLE xvfb_stdout
    ERROR_VARIABLE xvfb_stderr
)
set(run_stdout "")
set(run_stderr "")
set(driver_stderr "")
if(EXISTS "${run_stdout_file}")
    file(READ "${run_stdout_file}" run_stdout)
endif()
if(EXISTS "${run_stderr_file}")
    file(READ "${run_stderr_file}" run_stderr)
endif()
if(EXISTS "${driver_stderr_file}")
    file(READ "${driver_stderr_file}" driver_stderr)
endif()
if(NOT xvfb_stderr STREQUAL "")
    string(APPEND run_stderr "\n${xvfb_stderr}")
endif()
if(NOT xvfb_result EQUAL 0)
    message(FATAL_ERROR "real Proton input run returned ${xvfb_result}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}\ndriver stderr:\n${driver_stderr}")
endif()
if(NOT run_stdout STREQUAL "${EXPECTED_OUTPUT}\n")
    message(FATAL_ERROR "input fixture stdout was not preserved: '${run_stdout}'\nstderr:\n${run_stderr}")
endif()

foreach(needle
        "[tl][proton][info] provider-selected"
        "[tl][proton][info] staging-complete"
        "[tl][proton][info] launch"
        "[tl][proton][info] files-cleanup"
        "[tl][proton][info] exit")
    string(FIND "${run_stderr}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "Proton diagnostic '${needle}' not found:\n${run_stderr}")
    endif()
endforeach()

if(NOT EXISTS "${staged_executable}")
    message(FATAL_ERROR "staged input fixture is missing: ${staged_executable}")
endif()
if(NOT EXISTS "${prefix}/proton/application-manifest.json")
    message(FATAL_ERROR "Proton application manifest was not published")
endif()
if(EXISTS "${prefix}/proton/compat")
    message(FATAL_ERROR "compat directory became visible in Proton prefix")
endif()
if(EXISTS "${proton_prefix}/drive_c/compat")
    message(FATAL_ERROR "native compat metadata was copied into the Proton drive_c")
endif()
if(NOT EXISTS "${profile}")
    message(FATAL_ERROR "native compatibility profile disappeared")
endif()

file(REMOVE_RECURSE "${WORK}")
