if(NOT DEFINED TL_RUNTIME OR NOT DEFINED TL_CORPUS OR NOT DEFINED TL_STAGING_ROOT)
    message(FATAL_ERROR "TL_RUNTIME, TL_CORPUS e TL_STAGING_ROOT são obrigatórios")
endif()

if(NOT EXISTS "${TL_RUNTIME}")
    message(FATAL_ERROR "runtime inexistente: ${TL_RUNTIME}")
endif()
if(NOT IS_DIRECTORY "${TL_CORPUS}")
    message(FATAL_ERROR "corpus inexistente: ${TL_CORPUS}")
endif()

# A matriz nativa não interage com janelas. Não herdar a sessão gráfica do
# desenvolvedor evita que um SFX GUI permaneça aguardando ação humana e torne o
# resultado dependente do ambiente que executou o CTest.
set(ENV{DISPLAY})
set(ENV{WAYLAND_DISPLAY})

# Estes são somente os PE32+ com uma ação direta segura já documentada no B2,
# mais rejeições controladas que devem parar antes do entry point. Instaladores,
# DLLs e cenários que exigem interação GUI têm smokes próprios e não são
# iniciados por esta matriz.
set(CASES
    "7z_x64.exe|0|exit exit-code=\"0\" explicit=\"sim\""
    "WinRAR_x64.exe|0|exit exit-code=\"0\" explicit=\"sim\""
    "winrar-x64-723.exe|0|exit exit-code=\"0\" explicit=\"sim\""
    "Rockstar-Games-Launcher.exe|3|exit exit-code=\"3\" explicit=\"sim\""
    "Rufus_x64.exe|4|diretório de exceções fora da imagem"
    "Notepad++/updater/GUP.exe|5|provider-rejected module=\"libcurl.dll\" provider=\"drive_c\" detail=\"WLDAP32.dll!ordinal(46): módulo não registrado\""
)

file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
file(MAKE_DIRECTORY "${TL_STAGING_ROOT}")
set(passed 0)
set(index 0)

foreach(case IN LISTS CASES)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 relative_path)
    list(GET fields 1 expected_exit)
    list(GET fields 2 expected_marker)
    set(input_path "${TL_CORPUS}/${relative_path}")
    if(NOT EXISTS "${input_path}")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR "caso ausente: ${relative_path}")
    endif()

    math(EXPR index "${index} + 1")
    set(prefix "${TL_STAGING_ROOT}/prefix-${index}")
    file(MAKE_DIRECTORY "${prefix}/appdata")
    set(ENV{TL_PREFIX} "${prefix}")
    set(ENV{APPDATA} "${prefix}/appdata")

    execute_process(
        COMMAND "${TL_RUNTIME}" --trace=loader,process --timeout 3 --cpu 3 --memory 512
            "${input_path}"
        TIMEOUT 30
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
    if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: exit esperado ${expected_exit}, obtido ${actual_exit}\n"
            "stdout:\n${run_stdout}\n"
            "stderr:\n${run_stderr}")
    endif()
    string(FIND "${run_stderr}" "${expected_marker}" marker_position)
    if(marker_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "${relative_path}: diagnóstico esperado não encontrado: ${expected_marker}\n"
            "stdout:\n${run_stdout}\n"
            "stderr:\n${run_stderr}")
    endif()
    if("${relative_path}" STREQUAL "Notepad++/updater/GUP.exe")
        string(FIND "${run_stderr}" "unmap base=\"" unmap_position)
        string(FIND "${run_stderr}" "ExitProcess symbol=\"" exit_process_position)
        string(FIND "${run_stderr}" "guest-signal" guest_signal_position)
        string(FIND "${run_stderr}" "guest-timeout" guest_timeout_position)
        if(unmap_position EQUAL -1 OR NOT exit_process_position EQUAL -1 OR
           NOT guest_signal_position EQUAL -1 OR NOT guest_timeout_position EQUAL -1)
            file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
            message(FATAL_ERROR
                "${relative_path}: rejeição não terminou antes do entry point:\n"
                "stdout:\n${run_stdout}\n"
                "stderr:\n${run_stderr}")
        endif()
    endif()
    math(EXPR passed "${passed} + 1")
endforeach()

file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
message(STATUS "matriz nativa do corpus: ${passed}/${passed} casos passaram")
