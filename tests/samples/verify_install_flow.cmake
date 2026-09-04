if(NOT DEFINED RUNTIME OR NOT DEFINED SETUP OR NOT DEFINED MULTI_SETUP OR NOT DEFINED FAIL_SETUP OR
   NOT DEFINED MISSING_IMPORTS OR NOT DEFINED HANG OR NOT DEFINED HELLO OR NOT DEFINED LEGACY_APP OR
   NOT DEFINED INSTALLED_APP OR NOT DEFINED WORK)
    message(FATAL_ERROR "RUNTIME, SETUP, MULTI_SETUP, FAIL_SETUP, MISSING_IMPORTS, HANG, HELLO, LEGACY_APP, INSTALLED_APP and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
set(prefix "${WORK}/prefix")
set(config "${WORK}/config")
file(MAKE_DIRECTORY "${prefix}/drive_c/windows/temp")
file(COPY_FILE "${INSTALLED_APP}" "${prefix}/drive_c/windows/temp/tl_install_app.exe")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${SETUP}" --name "TL Install Fixture" --prefix "${prefix}" --trace
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "install returned ${install_result}\nstdout:\n${install_stdout}\nstderr:\n${install_stderr}")
endif()
if(NOT install_stdout STREQUAL "installed-app\ninstaller\n")
    message(FATAL_ERROR "unexpected installer stdout: ${install_stdout}")
endif()
string(FIND "${install_stderr}" "[tl][install][info] registered" registered_position)
if(registered_position EQUAL -1)
    message(FATAL_ERROR "installation was not registered:\n${install_stderr}")
endif()

set(catalog "${config}/tradutorlinux/library.json")
if(NOT EXISTS "${catalog}")
    message(FATAL_ERROR "catalog was not created")
endif()
file(READ "${catalog}" catalog_content)
string(FIND "${catalog_content}" "tl_install_fixture" catalog_id_position)
string(FIND "${catalog_content}" "${prefix}" catalog_prefix_position)
if(catalog_id_position EQUAL -1 OR catalog_prefix_position EQUAL -1)
    message(FATAL_ERROR "catalog does not preserve the installed app and prefix:\n${catalog_content}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" app run tl_install_fixture --trace
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "app run returned ${run_result}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
if(NOT run_stdout STREQUAL "installed-app\n")
    message(FATAL_ERROR "unexpected installed-app stdout: ${run_stdout}")
endif()
set(state_file "${prefix}/drive_c/users/guest/AppData/Local/tl-install-state.txt")
if(NOT EXISTS "${state_file}")
    message(FATAL_ERROR "installed app did not write state inside its prefix")
endif()
file(READ "${state_file}" state_content)
if(NOT state_content STREQUAL "installed\n")
    message(FATAL_ERROR "unexpected state file content: ${state_content}")
endif()

# O nome do arquivo contém aspas para garantir que a extração não seja
# montada como uma linha de shell. O arquivo é um tar simples que o 7z abre.
set(archive_source "${WORK}/quoted-archive-source")
file(MAKE_DIRECTORY "${archive_source}")
file(COPY_FILE "${INSTALLED_APP}" "${archive_source}/tl_install_app.exe")
set(quoted_archive "${WORK}/setup\"quoted.tar")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${quoted_archive}" tl_install_app.exe
    WORKING_DIRECTORY "${archive_source}"
    RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "could not create quoted-path archive: ${archive_result}")
endif()
set(quoted_prefix "${WORK}/quoted-prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${quoted_archive}" --name "TL Quoted Archive"
        --prefix "${quoted_prefix}" --trace
    RESULT_VARIABLE quoted_result
    OUTPUT_VARIABLE quoted_stdout
    ERROR_VARIABLE quoted_stderr
)
if(NOT quoted_result EQUAL 0 OR NOT quoted_stdout STREQUAL "")
    message(FATAL_ERROR "quoted-path archive install failed (${quoted_result})\nstdout:\n${quoted_stdout}\nstderr:\n${quoted_stderr}")
endif()
string(FIND "${quoted_stderr}" "[tl][install][info] registered" quoted_registered_position)
if(quoted_registered_position EQUAL -1)
    message(FATAL_ERROR "quoted-path archive was not registered:\n${quoted_stderr}")
endif()

# --app-exe evita a descoberta e ainda exige que o caminho escolhido pertença
# ao prefixo da instalação.
set(explicit_prefix "${WORK}/explicit-prefix")
file(MAKE_DIRECTORY "${explicit_prefix}/drive_c/windows/temp")
file(COPY_FILE "${INSTALLED_APP}" "${explicit_prefix}/drive_c/windows/temp/tl_install_app.exe")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${SETUP}" --name "TL Explicit Fixture" --prefix "${explicit_prefix}"
        --app-exe "C:\\Program Files\\TL Install Fixture\\tl_install_app.exe" --trace
    RESULT_VARIABLE explicit_result
    OUTPUT_VARIABLE explicit_stdout
    ERROR_VARIABLE explicit_stderr
)
if(NOT explicit_result EQUAL 0 OR NOT explicit_stdout STREQUAL "installed-app\ninstaller\n")
    message(FATAL_ERROR "explicit --app-exe failed (${explicit_result})\n${explicit_stderr}")
endif()
set(explicit_state_file "${explicit_prefix}/drive_c/users/guest/AppData/Local/tl-install-state.txt")
if(NOT EXISTS "${explicit_state_file}" OR NOT EXISTS "${state_file}")
    message(FATAL_ERROR "independent prefixes did not retain their own application state")
endif()

# Quando dois executáveis x64 são novos, o runtime não escolhe arbitrariamente.
set(multi_prefix "${WORK}/multi-prefix")
file(MAKE_DIRECTORY "${multi_prefix}/drive_c/windows/temp")
file(COPY_FILE "${INSTALLED_APP}" "${multi_prefix}/drive_c/windows/temp/tl_install_app.exe")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${MULTI_SETUP}" --name "TL Multi Fixture" --prefix "${multi_prefix}" --trace
    RESULT_VARIABLE multi_result
    OUTPUT_VARIABLE multi_stdout
    ERROR_VARIABLE multi_stderr
)
if(NOT multi_result EQUAL 6)
    message(FATAL_ERROR "multi-candidate install returned ${multi_result}\n${multi_stderr}")
endif()
string(REGEX MATCHALL "candidate" multi_candidates "${multi_stderr}")
list(LENGTH multi_candidates multi_candidate_count)
if(NOT multi_candidate_count EQUAL 2)
    message(FATAL_ERROR "expected two install candidates, got ${multi_candidate_count}\n${multi_stderr}")
endif()

# Falha do setup não pode gerar cadastro, mesmo que o prefixo seja preservado.
set(failed_prefix "${WORK}/failed-prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${FAIL_SETUP}" --name "TL Failed Fixture" --prefix "${failed_prefix}" --trace
    RESULT_VARIABLE failed_result
    OUTPUT_VARIABLE failed_stdout
    ERROR_VARIABLE failed_stderr
)
if(NOT failed_result EQUAL 9 OR NOT failed_stdout STREQUAL "installer-failed\n")
    message(FATAL_ERROR "failed setup returned ${failed_result}\n${failed_stderr}")
endif()
string(FIND "${failed_stderr}" "[tl][install][error] failed stage=\"setup\"" failed_trace_position)
if(failed_trace_position EQUAL -1)
    message(FATAL_ERROR "failed setup did not emit install trace:\n${failed_stderr}")
endif()

# Imports ainda não suportados e timeout também deixam o prefixo sem catálogo.
set(imports_prefix "${WORK}/imports-prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${MISSING_IMPORTS}" --name "TL Missing Imports" --prefix "${imports_prefix}" --trace
    RESULT_VARIABLE imports_result
    OUTPUT_VARIABLE imports_stdout
    ERROR_VARIABLE imports_stderr
)
if(NOT imports_result EQUAL 5 OR NOT imports_stdout STREQUAL "")
    message(FATAL_ERROR "unsupported imports install returned ${imports_result}\n${imports_stderr}")
endif()
string(FIND "${imports_stderr}" "[tl][install][error] failed stage=\"imports\"" imports_trace_position)
if(imports_trace_position EQUAL -1)
    message(FATAL_ERROR "unsupported imports did not emit install trace:\n${imports_stderr}")
endif()

set(timeout_prefix "${WORK}/timeout-prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${HANG}" --name "TL Timeout Fixture" --prefix "${timeout_prefix}" --timeout 1 --trace
    RESULT_VARIABLE timeout_result
    OUTPUT_VARIABLE timeout_stdout
    ERROR_VARIABLE timeout_stderr
)
if(NOT timeout_result EQUAL 72 OR NOT timeout_stdout STREQUAL "")
    message(FATAL_ERROR "timeout install returned ${timeout_result}\n${timeout_stderr}")
endif()
string(FIND "${timeout_stderr}" "[tl][install][error] failed stage=\"setup-timeout\"" timeout_trace_position)
if(timeout_trace_position EQUAL -1)
    message(FATAL_ERROR "timeout did not emit install trace:\n${timeout_stderr}")
endif()

# Um executável que não altera drive_c conclui o setup, mas deixa o cadastro pendente.
set(empty_prefix "${WORK}/empty-prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${HELLO}" --name "TL No Candidate" --prefix "${empty_prefix}" --trace
    RESULT_VARIABLE empty_result
    OUTPUT_VARIABLE empty_stdout
    ERROR_VARIABLE empty_stderr
)
if(NOT empty_result EQUAL 6 OR NOT empty_stdout STREQUAL "Ola do Windows no Linux!\n")
    message(FATAL_ERROR "no-candidate install returned ${empty_result}\n${empty_stderr}")
endif()
string(FIND "${empty_stderr}" "reason=\"no-candidate\"" no_candidate_position)
if(no_candidate_position EQUAL -1)
    message(FATAL_ERROR "no-candidate trace was not emitted:\n${empty_stderr}")
endif()
file(READ "${catalog}" final_catalog_content)
string(FIND "${final_catalog_content}" "tl_multi_fixture" multi_catalog_position)
string(FIND "${final_catalog_content}" "tl_no_candidate" empty_catalog_position)
string(FIND "${final_catalog_content}" "tl_failed_fixture" failed_catalog_position)
string(FIND "${final_catalog_content}" "tl_missing_imports" imports_catalog_position)
string(FIND "${final_catalog_content}" "tl_timeout_fixture" timeout_catalog_position)
if(NOT multi_catalog_position EQUAL -1 OR NOT empty_catalog_position EQUAL -1 OR
   NOT failed_catalog_position EQUAL -1 OR NOT imports_catalog_position EQUAL -1 OR
   NOT timeout_catalog_position EQUAL -1)
    message(FATAL_ERROR "pending installations must not be added to the catalog:\n${final_catalog_content}")
endif()

# Catálogos antigos usavam o prefixo compartilhado e guardavam um diretório
# externo. Esse comportamento continua disponível para não quebrar entradas já
# criadas antes dos prefixos por aplicativo.
set(legacy_config "${WORK}/legacy-config")
set(legacy_prefix "${WORK}/legacy-prefix")
set(legacy_working_directory "${WORK}/legacy-working-directory")
file(MAKE_DIRECTORY "${legacy_config}/tradutorlinux" "${legacy_working_directory}")
file(WRITE "${legacy_config}/tradutorlinux/library.json"
    "{\n  \"version\": 1,\n  \"apps\": [{\n"
    "    \"id\": \"legacy-file\",\n"
    "    \"name\": \"Legacy File\",\n"
    "    \"executable_path\": \"${LEGACY_APP}\",\n"
    "    \"prefix_path\": \"${legacy_prefix}\",\n"
    "    \"icon_path\": \"\",\n"
    "    \"working_directory\": \"${legacy_working_directory}\",\n"
    "    \"created_at\": \"2026-08-23T00:00:00Z\",\n"
    "    \"args\": []\n  }]\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${legacy_config}" "TL_PREFIX=${legacy_prefix}"
        "${RUNTIME}" app run legacy-file --trace
    RESULT_VARIABLE legacy_result
    OUTPUT_VARIABLE legacy_stdout
    ERROR_VARIABLE legacy_stderr
)
if(NOT legacy_result EQUAL 0 OR NOT legacy_stdout STREQUAL "fase5\n")
    message(FATAL_ERROR "legacy catalog entry failed (${legacy_result})\n${legacy_stderr}")
endif()
if(NOT EXISTS "${legacy_working_directory}/tl_phase5_data.bin")
    message(FATAL_ERROR "legacy working_directory was not preserved")
endif()
file(REMOVE "${legacy_working_directory}/tl_phase5_data.bin")
