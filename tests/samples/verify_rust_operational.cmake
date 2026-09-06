if(NOT DEFINED RUNTIME OR NOT DEFINED COMPAT_FILE OR NOT DEFINED HELLO OR NOT DEFINED HANG
   OR NOT DEFINED PROTON_FIXTURE OR NOT DEFINED WORK OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR
        "RUNTIME, COMPAT_FILE, HELLO, HANG, PROTON_FIXTURE, WORK and RUST_ENABLED are required")
endif()

foreach(input IN ITEMS "${RUNTIME}" "${COMPAT_FILE}" "${HELLO}" "${HANG}" "${PROTON_FIXTURE}")
    if(NOT EXISTS "${input}")
        message(FATAL_ERROR "Required operational fixture is missing: ${input}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")

function(tl_assert_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${description}: '${needle}' was not found:\n${haystack}")
    endif()
endfunction()

function(tl_assert_no_path_validation trace description)
    string(FIND "${trace}" "path-validation" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${description}: Rust path-validation trace leaked into C++ mode:\n${trace}")
    endif()
endfunction()

function(tl_add_app fixture id prefix)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "HOME=${WORK}/home"
            "XDG_CONFIG_HOME=${WORK}/config"
            "${RUNTIME}" app add "${fixture}" --id "${id}" --name "${id}"
            --prefix "${prefix}"
        RESULT_VARIABLE add_result
        OUTPUT_VARIABLE add_stdout
        ERROR_VARIABLE add_stderr
    )
    if(NOT add_result EQUAL 0)
        message(FATAL_ERROR "app add ${id} failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
    endif()
endfunction()

function(tl_assert_path_validation trace phase checks rejected status component)
    if(RUST_ENABLED)
        tl_assert_contains(
            "${trace}"
            "[tl][${component}][info] path-validation phase=\"${phase}\" backend=\"rust\" handle-count=\"1\" checks=\"${checks}\" rejected=\"${rejected}\""
            "Rust ${phase} validation metrics")
        tl_assert_contains("${trace}" "status=\"${status}\"" "Rust ${phase} validation status")
    else()
        tl_assert_no_path_validation("${trace}" "${phase} validation")
    endif()
endfunction()

# Native backend: one profile is loaded and materialized twice. This proves
# that the Rust handle is scoped to each phase and does not retain prefix state.
set(native_prefix "${WORK}/native-prefix")
set(native_source "${native_prefix}/compat/files/injected.dat")
set(native_profile "${native_prefix}/compat/profile.json")
set(native_target "${native_prefix}/drive_c/Program Files/Compat Fixture/injected.dat")
set(native_parent "${native_prefix}/drive_c/Program Files/Compat Fixture")
tl_add_app("${COMPAT_FILE}" "rust-compat-file" "${native_prefix}")
file(WRITE "${native_source}" "operational native payload\n")
file(WRITE "${native_profile}"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"app_id\": \"rust-compat-file\",\n"
    "  \"files\": [{\"source\": \"injected.dat\", \"target\": \"C:\\\\Program Files\\\\Compat Fixture\\\\injected.dat\"}]\n"
    "}\n")

function(tl_run_native output_var error_var result_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "HOME=${WORK}/home"
            "XDG_CONFIG_HOME=${WORK}/config"
            "${RUNTIME}" app run rust-compat-file --trace=runtime,process
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
    set(${output_var} "${run_stdout}" PARENT_SCOPE)
    set(${error_var} "${run_stderr}" PARENT_SCOPE)
    set(${result_var} "${run_result}" PARENT_SCOPE)
endfunction()

tl_run_native(native_stdout_a native_trace_a native_result_a)
if(NOT native_result_a EQUAL 0 OR NOT native_stdout_a STREQUAL "operational native payload\n")
    message(FATAL_ERROR "native operational run A failed (${native_result_a})\nstdout:\n${native_stdout_a}\nstderr:\n${native_trace_a}")
endif()
foreach(needle
        "compat-profile status=\"loaded\""
        "compat-files status=\"applied\""
        "compat-files-cleanup status=\"cleaned\"")
    tl_assert_contains("${native_trace_a}" "${needle}" "native run A trace")
endforeach()
tl_assert_path_validation("${native_trace_a}" "profile" "2" "0" "completed" "runtime")
tl_assert_path_validation("${native_trace_a}" "files" "2" "0" "completed" "runtime")
if(NOT EXISTS "${native_source}" OR EXISTS "${native_target}" OR EXISTS "${native_parent}")
    message(FATAL_ERROR "native run A did not preserve the source and clean the target")
endif()
if(EXISTS "${native_prefix}/drive_c/compat")
    message(FATAL_ERROR "native compat directory leaked into drive_c after run A")
endif()

tl_run_native(native_stdout_b native_trace_b native_result_b)
if(NOT native_result_b EQUAL 0 OR NOT native_stdout_b STREQUAL "operational native payload\n")
    message(FATAL_ERROR "native operational run B failed (${native_result_b})\nstdout:\n${native_stdout_b}\nstderr:\n${native_trace_b}")
endif()
tl_assert_contains("${native_trace_b}" "compat-files-cleanup status=\"cleaned\"" "native run B cleanup")
tl_assert_path_validation("${native_trace_b}" "profile" "2" "0" "completed" "runtime")
tl_assert_path_validation("${native_trace_b}" "files" "2" "0" "completed" "runtime")
if(NOT EXISTS "${native_source}" OR EXISTS "${native_target}" OR EXISTS "${native_parent}")
    message(FATAL_ERROR "native run B did not preserve the source and clean the target")
endif()
file(READ "${native_source}" native_source_contents)
if(NOT native_source_contents STREQUAL "operational native payload\n")
    message(FATAL_ERROR "native source changed: '${native_source_contents}'")
endif()

# An invalid lexical mapping must retain the generic execution path and must
# never reach FileExposure::materialize.
set(invalid_prefix "${WORK}/invalid-prefix")
set(invalid_profile "${invalid_prefix}/compat/profile.json")
tl_add_app("${HELLO}" "rust-invalid-profile" "${invalid_prefix}")
file(WRITE "${invalid_profile}"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"app_id\": \"rust-invalid-profile\",\n"
    "  \"files\": [{\"source\": \"../escape.dat\", \"target\": \"C:\\\\escape.dat\"}]\n"
    "}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run rust-invalid-profile --trace=runtime,process
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_trace
)
if(NOT invalid_result EQUAL 0 OR NOT invalid_stdout MATCHES "Ola do Windows no Linux!")
    message(FATAL_ERROR "invalid profile did not use generic fallback (${invalid_result})\nstdout:\n${invalid_stdout}\nstderr:\n${invalid_trace}")
endif()
tl_assert_contains("${invalid_trace}" "compat-profile status=\"invalid\"" "invalid profile status")
tl_assert_contains("${invalid_trace}" "aviso: perfil de compatibilidade invalid" "invalid profile warning")
if(RUST_ENABLED)
    tl_assert_contains("${invalid_trace}" "path-validation phase=\"profile\"" "invalid profile Rust trace")
    tl_assert_contains("${invalid_trace}" "status=\"invalid-input\"" "invalid profile Rust status")
    tl_assert_contains("${invalid_trace}" "rejected=\"1\"" "invalid profile rejection count")
else()
    tl_assert_no_path_validation("${invalid_trace}" "invalid profile")
endif()
if(EXISTS "${invalid_prefix}/drive_c/escape.dat")
    message(FATAL_ERROR "invalid profile unexpectedly materialized a file")
endif()

# Timeout must still clean the temporary file and leave its source intact.
set(hang_prefix "${WORK}/hang-prefix")
set(hang_source "${hang_prefix}/compat/files/hang.dat")
set(hang_profile "${hang_prefix}/compat/profile.json")
set(hang_target "${hang_prefix}/drive_c/Program Files/Hang Fixture/hang.dat")
set(hang_parent "${hang_prefix}/drive_c/Program Files/Hang Fixture")
tl_add_app("${HANG}" "rust-hang" "${hang_prefix}")
file(WRITE "${hang_source}" "timeout payload\n")
file(WRITE "${hang_profile}"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"app_id\": \"rust-hang\",\n"
    "  \"files\": [{\"source\": \"hang.dat\", \"target\": \"C:\\\\Program Files\\\\Hang Fixture\\\\hang.dat\"}]\n"
    "}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run rust-hang --timeout 1 --trace=runtime,process
    RESULT_VARIABLE hang_result
    OUTPUT_VARIABLE hang_stdout
    ERROR_VARIABLE hang_trace
)
if(NOT hang_result EQUAL 72 OR NOT hang_stdout STREQUAL "")
    message(FATAL_ERROR "hang fixture did not return timeout 72 (${hang_result})\nstdout:\n${hang_stdout}\nstderr:\n${hang_trace}")
endif()
tl_assert_contains("${hang_trace}" "compat-files-cleanup status=\"cleaned\"" "timeout cleanup")
tl_assert_contains("${hang_trace}" "category=\"guest-timeout\"" "timeout diagnosis")
tl_assert_path_validation("${hang_trace}" "profile" "2" "0" "completed" "runtime")
tl_assert_path_validation("${hang_trace}" "files" "2" "0" "completed" "runtime")
if(NOT EXISTS "${hang_source}" OR EXISTS "${hang_target}" OR EXISTS "${hang_parent}")
    message(FATAL_ERROR "timeout left compatibility paths behind or removed the source")
endif()

# Proton mock: the same operational profile is loaded by the catalog path,
# materialized into the Proton prefix, and cleaned without exposing compat/.
set(proton_prefix "${WORK}/proton-prefix")
set(proton_app_dir "${proton_prefix}/drive_c/Program Files/Operational Proton")
set(proton_executable "${proton_app_dir}/tl_proton_probe.exe")
set(proton_profile "${proton_prefix}/compat/profile.json")
set(proton_source "${proton_prefix}/compat/files/proton-helper.dat")
set(proton_target "${proton_prefix}/proton/compatdata/pfx/drive_c/Program Files/Operational Proton/proton-helper.dat")
set(proton_root "${WORK}/proton")
set(proton_mock_output "${WORK}/proton-mock-output.txt")
file(MAKE_DIRECTORY "${proton_app_dir}")
file(COPY "${PROTON_FIXTURE}" DESTINATION "${proton_app_dir}")
tl_add_app("${proton_executable}" "rust-proton" "${proton_prefix}")
file(WRITE "${proton_source}" "operational proton payload\n")
file(WRITE "${proton_profile}"
    "{\n"
    "  \"schema\": 3,\n"
    "  \"app_id\": \"rust-proton\",\n"
    "  \"files\": [{\"source\": \"proton-helper.dat\", \"target\": \"C:\\\\Program Files\\\\Operational Proton\\\\proton-helper.dat\"}],\n"
    "  \"backend\": {\"kind\": \"proton\", \"min_version\": \"11.0\"}\n"
    "}\n")
file(MAKE_DIRECTORY
    "${proton_root}/files/bin"
    "${proton_root}/files/share/wine"
    "${proton_root}/files/share/default_pfx")
file(COPY_FILE "/bin/true" "${proton_root}/files/bin/wine")
file(COPY_FILE "/bin/true" "${proton_root}/files/bin/wineserver")
file(WRITE "${proton_root}/files/share/wine/wine.inf" "fake wine inf\n")
file(WRITE "${proton_root}/version" "11.0-1\n")
file(WRITE "${proton_root}/proton"
    "#!/bin/sh\n"
    "printf 'OPERATIONAL_PROTON_STDOUT\\n'\n"
    "helper_dir=$(dirname \"$2\")\n"
    "helper=\"$helper_dir/proton-helper.dat\"\n"
    "if [ \"$(cat \"$helper\")\" != 'operational proton payload' ]; then exit 24; fi\n"
    "printf 'mock proton operational stderr\\n' >&2\n"
    "printf 'arg1=%s\\narg2=%s\\ncompat=%s\\nwineprefix=%s\\nclient=%s\\ninstall=%s\\n' \"$1\" \"$2\" \"$STEAM_COMPAT_DATA_PATH\" \"$WINEPREFIX\" \"$STEAM_COMPAT_CLIENT_INSTALL_PATH\" \"$STEAM_COMPAT_INSTALL_PATH\" > \"$TL_PROTON_MOCK_OUTPUT\"\n"
    "exit 23\n")
file(CHMOD "${proton_root}/proton"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "TL_PROTON_ROOT=${proton_root}"
        "TL_PROTON_MOCK_OUTPUT=${proton_mock_output}"
        "${RUNTIME}" app run rust-proton --trace=runtime,proton,process
    RESULT_VARIABLE proton_result
    OUTPUT_VARIABLE proton_stdout
    ERROR_VARIABLE proton_trace
)
if(NOT proton_result EQUAL 23 OR NOT proton_stdout STREQUAL "OPERATIONAL_PROTON_STDOUT\n")
    message(FATAL_ERROR "operational Proton mock failed (${proton_result})\nstdout:\n${proton_stdout}\nstderr:\n${proton_trace}")
endif()
tl_assert_contains("${proton_trace}" "[tl][runtime][info] compat-profile status=\"loaded\"" "Proton profile trace")
if(RUST_ENABLED)
    tl_assert_contains("${proton_trace}" "[tl][proton][info] path-validation phase=\"files\"" "Proton Rust materialization trace")
endif()
tl_assert_contains("${proton_trace}" "[tl][proton][info] files-cleanup" "Proton cleanup trace")
tl_assert_contains("${proton_trace}" "[tl][proton] mock proton operational stderr" "Proton stderr forwarding")
tl_assert_path_validation("${proton_trace}" "profile" "2" "0" "completed" "runtime")
if(NOT RUST_ENABLED)
    tl_assert_no_path_validation("${proton_trace}" "Proton C++ baseline")
endif()
if(EXISTS "${proton_target}" OR NOT EXISTS "${proton_source}")
    message(FATAL_ERROR "Proton compatibility target was not cleaned or source disappeared")
endif()
if(EXISTS "${proton_prefix}/proton/compat" OR NOT EXISTS "${proton_prefix}/proton/application-manifest.json")
    message(FATAL_ERROR "Proton compatibility tree was exposed or manifest is missing")
endif()
if(NOT EXISTS "${proton_mock_output}")
    message(FATAL_ERROR "Proton mock did not receive the operational environment")
endif()
file(READ "${proton_mock_output}" proton_mock_contents)
tl_assert_contains("${proton_mock_contents}" "arg1=runinprefix" "Proton launcher mode")
tl_assert_contains("${proton_mock_contents}" "compat=${proton_prefix}/proton/compatdata" "Proton data path")
tl_assert_contains("${proton_mock_contents}" "wineprefix=${proton_prefix}/proton/compatdata/pfx" "Proton wine prefix")

file(REMOVE_RECURSE "${WORK}")
