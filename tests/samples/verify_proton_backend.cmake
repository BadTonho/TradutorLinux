if(NOT DEFINED RUNTIME OR NOT DEFINED FIXTURE OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, FIXTURE and WORK are required")
endif()
if(NOT EXISTS "${RUNTIME}" OR NOT EXISTS "${FIXTURE}")
    message(FATAL_ERROR "Runtime or Proton fixture is missing")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")
set(prefix "${WORK}/prefix")
set(app_dir "${prefix}/drive_c/Program Files/Proton Probe")
set(executable "${app_dir}/tl_proton_probe.exe")
set(profile "${prefix}/compat/profile.json")
set(source "${prefix}/compat/files/proton-helper.dat")
set(target "${prefix}/proton/compatdata/pfx/drive_c/Program Files/Proton Probe/proton-helper.dat")
set(proton_root "${WORK}/proton")
set(mock_output "${WORK}/mock-output.txt")

# O app add inicializa o prefixo. O executável é colocado antes dele para que
# o catálogo registre um caminho já confinado ao drive_c.
file(MAKE_DIRECTORY "${app_dir}")
file(COPY "${FIXTURE}" DESTINATION "${app_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${executable}" --id protonprobe --name "Proton Probe"
        --prefix "${prefix}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "app add failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

file(WRITE "${source}" "proton helper\n")
file(WRITE "${profile}"
    "{\n"
    "  \"schema\": 3,\n"
    "  \"app_id\": \"protonprobe\",\n"
    "  \"files\": [\n"
    "    {\"source\": \"proton-helper.dat\", \"target\": \"C:\\\\Program Files\\\\Proton Probe\\\\proton-helper.dat\"}\n"
    "  ],\n"
    "  \"backend\": {\"kind\": \"proton\", \"min_version\": \"11.0\"}\n"
    "}\n"
)

file(MAKE_DIRECTORY
    "${proton_root}/files/bin"
    "${proton_root}/files/share/wine"
    "${proton_root}/files/share/default_pfx"
)
file(COPY_FILE "/bin/true" "${proton_root}/files/bin/wine")
file(COPY_FILE "/bin/true" "${proton_root}/files/bin/wineserver")
file(WRITE "${proton_root}/files/share/wine/wine.inf" "fake wine inf\n")
file(WRITE "${proton_root}/version" "11.0-1\n")
file(WRITE "${proton_root}/proton"
    "#!/bin/sh\n"
    "printf 'PROTON_STDOUT\\n'\n"
    "helper_dir=$(dirname \"$2\")\n"
    "helper=\"$helper_dir/proton-helper.dat\"\n"
    "if [ \"$(cat \"$helper\")\" != 'proton helper' ]; then exit 24; fi\n"
    "printf 'arg1=%s\\narg2=%s\\ncompat=%s\\nwineprefix=%s\\nclient=%s\\ninstall=%s\\nhelper=ok\\n' \"$1\" \"$2\" \"$STEAM_COMPAT_DATA_PATH\" \"$WINEPREFIX\" \"$STEAM_COMPAT_CLIENT_INSTALL_PATH\" \"$STEAM_COMPAT_INSTALL_PATH\" > \"$TL_PROTON_MOCK_OUTPUT\"\n"
    "printf 'mock proton stderr\\n' >&2\n"
    "exit 23\n"
)
file(CHMOD "${proton_root}/proton"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "TL_PROTON_ROOT=${proton_root}"
        "TL_PROTON_MOCK_OUTPUT=${mock_output}"
        "${RUNTIME}" app run protonprobe --trace=proton,process
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 23)
    message(FATAL_ERROR "mock Proton run returned ${run_result}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
if(NOT run_stdout STREQUAL "PROTON_STDOUT\n")
    message(FATAL_ERROR "Proton stdout was not preserved: '${run_stdout}'\nstderr:\n${run_stderr}")
endif()
foreach(needle
        "[tl][proton][info] provider-selected"
        "[tl][proton][info] staging-complete"
        "[tl][proton] mock proton stderr"
        "[tl][proton][info] files-cleanup"
        "[tl][proton][info] exit")
    string(FIND "${run_stderr}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "Proton diagnostic '${needle}' not found:\n${run_stderr}")
    endif()
endforeach()

file(READ "${mock_output}" mock_contents)
foreach(needle
        "arg1=run"
        "arg2=${prefix}/proton/compatdata/pfx/drive_c/Program Files/Proton Probe/tl_proton_probe.exe"
        "compat=${prefix}/proton/compatdata"
        "wineprefix=${prefix}/proton/compatdata/pfx"
        "client=${prefix}/proton/client"
        "install=${prefix}/proton"
        "helper=ok")
    string(FIND "${mock_contents}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "mock Proton did not receive '${needle}':\n${mock_contents}")
    endif()
endforeach()

if(EXISTS "${target}")
    message(FATAL_ERROR "Proton files[] target was not cleaned")
endif()
if(NOT EXISTS "${source}")
    message(FATAL_ERROR "Proton compatibility source disappeared")
endif()
if(EXISTS "${prefix}/proton/compat")
    message(FATAL_ERROR "compat directory became visible in Proton prefix")
endif()
if(NOT EXISTS "${prefix}/proton/application-manifest.json")
    message(FATAL_ERROR "Proton application manifest was not published")
endif()

file(SHA256 "${executable}" source_hash)
file(SHA256 "${prefix}/proton/compatdata/pfx/drive_c/Program Files/Proton Probe/tl_proton_probe.exe" staged_hash)
if(NOT source_hash STREQUAL staged_hash)
    message(FATAL_ERROR "staged executable differs from source")
endif()

# A Proton explicitly requested by the profile must not silently fall back to
# the native runtime when its configured provider is invalid.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "TL_PROTON_ROOT=${WORK}/missing-proton"
        "TL_PROTON_MOCK_OUTPUT=${mock_output}"
        "${RUNTIME}" app run protonprobe
    RESULT_VARIABLE unavailable_result
    OUTPUT_VARIABLE unavailable_stdout
    ERROR_VARIABLE unavailable_stderr
)
if(NOT unavailable_result EQUAL 5 OR NOT unavailable_stdout STREQUAL "")
    message(FATAL_ERROR "invalid Proton did not return Unsupported without native fallback (${unavailable_result})\nstdout:\n${unavailable_stdout}\nstderr:\n${unavailable_stderr}")
endif()
if(NOT unavailable_stderr MATCHES "backend Proton não suportado")
    message(FATAL_ERROR "invalid Proton was not diagnosed:\n${unavailable_stderr}")
endif()

file(REMOVE_RECURSE "${WORK}")
