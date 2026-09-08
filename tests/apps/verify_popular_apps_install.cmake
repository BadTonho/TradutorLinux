if(NOT DEFINED TL_RUNTIME OR NOT DEFINED TL_CORPUS OR
   NOT DEFINED TL_STAGING_ROOT OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR
        "TL_RUNTIME, TL_CORPUS, TL_STAGING_ROOT e RUST_ENABLED são obrigatórios")
endif()

if(NOT EXISTS "${TL_RUNTIME}")
    message(FATAL_ERROR "runtime inexistente: ${TL_RUNTIME}")
endif()
if(NOT IS_DIRECTORY "${TL_CORPUS}")
    message(FATAL_ERROR "corpus inexistente: ${TL_CORPUS}")
endif()

# Os quatro primeiros casos são instalações autorizadas. Os quatro últimos
# exercitam somente a rejeição pré-extração dos novos aplicativos x86 ou
# empacotados; nenhum deles é iniciado nem cadastrado.
set(CASES
    "RobloxPlayerInstaller.exe|roblox|3"
    "Logitech_GHUB_x64.exe|ghub|1"
    "lghub_installer.exe|ghub_alias|1"
    "Affinity x64.msix|affinity|4"
    "CPU-Z_2.18_en.exe|cpuz|5"
    "GPU-Z_2.70.0.exe|gpuz|5"
    "HWMonitor_1.67.exe|hwmonitor|5"
    "HWiNFO64.exe|hwinfo|4"
)

file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
file(MAKE_DIRECTORY "${TL_STAGING_ROOT}")
set(passed 0)
set(index 0)

foreach(case IN LISTS CASES)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 relative_path)
    list(GET fields 1 app_id)
    list(GET fields 2 expected_exit)
    set(input_path "${TL_CORPUS}/${relative_path}")
    if(NOT EXISTS "${input_path}")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR "caso ausente: ${relative_path}")
    endif()

    math(EXPR index "${index} + 1")
    set(case_root "${TL_STAGING_ROOT}/case-${index}")
    set(prefix "${case_root}/prefix")
    set(home "${case_root}/home")
    set(config "${case_root}/config")
    set(appdata "${case_root}/appdata")
    file(MAKE_DIRECTORY "${prefix}" "${home}" "${config}" "${appdata}")
    set(ENV{HOME} "${home}")
    set(ENV{XDG_CONFIG_HOME} "${config}")
    set(ENV{APPDATA} "${appdata}")

    execute_process(
        COMMAND "${TL_RUNTIME}" install "${input_path}" --name "${app_id}"
            --prefix "${prefix}" --trace --cpu 3 --memory 512
        TIMEOUT 30
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE install_stdout
        ERROR_VARIABLE install_trace
    )
    if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: exit esperado ${expected_exit}, obtido ${actual_exit}\n"
            "stdout:\n${install_stdout}\n"
            "stderr:\n${install_trace}")
    endif()
    if(NOT "${install_stdout}" STREQUAL "")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: instalação rejeitada escreveu em stdout:\n${install_stdout}")
    endif()
    if(NOT RUST_ENABLED AND install_trace MATCHES "backend=\"rust\"")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR "${relative_path}: backend Rust vazou no build OFF:\n${install_trace}")
    endif()

    if("${app_id}" STREQUAL "affinity")
        set(expected_stage "package-parse")
    elseif("${app_id}" STREQUAL "cpuz" OR "${app_id}" STREQUAL "gpuz" OR
           "${app_id}" STREQUAL "hwmonitor" OR "${app_id}" STREQUAL "hwinfo")
        set(expected_stage "parse")
    else()
        set(expected_stage "setup")
    endif()
    string(FIND "${install_trace}" "${expected_stage}" failed_position)
    if(failed_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: falha de instalação sem o estágio esperado "
            "${expected_stage}:\n${install_trace}")
    endif()
    string(FIND "${install_trace}" "extracted" extracted_position)
    string(FIND "${install_trace}" "registered" registered_position)
    if(NOT extracted_position EQUAL -1 OR NOT registered_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: instalação rejeitada deixou extração/cadastro:\n${install_trace}")
    endif()

    if("${app_id}" STREQUAL "roblox")
        set(expected_marker "RBXCRASH: FatalRuntimeError")
    elseif("${app_id}" STREQUAL "ghub" OR "${app_id}" STREQUAL "ghub_alias")
        set(expected_marker "ExitProcess symbol=\"ExitProcess\" exit-code=\"1\" status=\"success\"")
    elseif("${app_id}" STREQUAL "affinity")
        if(RUST_ENABLED)
            set(expected_marker "package-parse format=\"MSIX / AppX\" backend=\"rust\" status=\"malformed\"")
        else()
            set(expected_marker "failed stage=\"package-parse\"")
        endif()
    elseif("${app_id}" STREQUAL "cpuz" OR "${app_id}" STREQUAL "gpuz" OR
           "${app_id}" STREQUAL "hwmonitor" OR "${app_id}" STREQUAL "hwinfo")
        set(expected_marker "failed stage=\"parse\"")
    endif()
    string(FIND "${install_trace}" "${expected_marker}" marker_position)
    if(marker_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: diagnóstico esperado não encontrado: ${expected_marker}\n"
            "stderr:\n${install_trace}")
    endif()

    # O prefixo contém symlinks padrão em drive_c/dosdevices. Use find sem
    # -L para contar somente arquivos regulares e não seguir esses links.
    execute_process(
        COMMAND find "${prefix}" "${config}" "${appdata}" -type f -print
        RESULT_VARIABLE residual_result
        OUTPUT_VARIABLE residual_files
        ERROR_VARIABLE residual_error
    )
    if(NOT residual_result EQUAL 0)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: não foi possível verificar arquivos residuais:\n"
            "${residual_error}")
    endif()
    string(STRIP "${residual_files}" residual_files)
    if(NOT "${residual_files}" STREQUAL "")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: arquivos residuais após rejeição:\n"
            "${residual_files}")
    endif()
    math(EXPR passed "${passed} + 1")
endforeach()

file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
message(STATUS "matriz de instalação do corpus: ${passed}/${passed} casos passaram")
