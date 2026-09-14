if(NOT DEFINED TL_RUNTIME OR NOT DEFINED TL_CORPUS)
    message(FATAL_ERROR "TL_RUNTIME e TL_CORPUS são obrigatórios")
endif()

if(NOT EXISTS "${TL_RUNTIME}")
    message(FATAL_ERROR "runtime inexistente: ${TL_RUNTIME}")
endif()
if(NOT IS_DIRECTORY "${TL_CORPUS}")
    message(FATAL_ERROR "corpus inexistente: ${TL_CORPUS}")
endif()

# A matriz cobre os 26 PE escolhidos no B1 e o pacote MSIX. Os valores são
# exit codes do contrato CLI: 0 = análise concluída, 4 = imagem/formato
# estruturalmente rejeitado e 5 = arquitetura/formato não suportado.
set(CASES
    "7-Zip_x64_Installer.exe|5"
    "7z.dll|0"
    "7zFM_x64.exe|0"
    "7z_x64.exe|0"
    "CPU-Z_2.18_en.exe|5"
    "CapCut_7677236283084898320_installer.exe|5"
    "Creative_Cloud_Set-Up_7474.exe|5"
    "EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe|5"
    "Everything_Search_x64.exe|5"
    "GPU-Z_2.70.0.exe|5"
    "HWMonitor_1.67.exe|5"
    "HWiNFO64.exe|4"
    "Logitech_GHUB_x64.exe|0"
    "Notepad++_x64_Installer.exe|5"
    "RTSS.exe|5"
    "RTSSHooks64.dll|0"
    "RTSSSetup737.exe|5"
    "RobloxPlayerInstaller.exe|0"
    "Rockstar-Games-Launcher.exe|0"
    "Rufus_x64.exe|4"
    "WinRAR_x64.exe|0"
    "lghub_installer.exe|0"
    "notepad++.exe|0"
    "officedeploymenttool_20228-20124.exe|5"
    "putty_x64.exe|0"
    "winrar-x64-723.exe|0"
    "Affinity x64.msix|4"
)

set(passed 0)
foreach(case IN LISTS CASES)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 relative_path)
    list(GET fields 1 expected_exit)
    set(input_path "${TL_CORPUS}/${relative_path}")
    if(NOT EXISTS "${input_path}")
        message(FATAL_ERROR "caso ausente: ${relative_path}")
    endif()

    execute_process(
        COMMAND "${TL_RUNTIME}" --report "${input_path}"
        TIMEOUT 30
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE report_stdout
        ERROR_VARIABLE report_stderr
    )
    if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR
            "${relative_path}: exit esperado ${expected_exit}, obtido ${actual_exit}\n"
            "stdout:\n${report_stdout}\n"
            "stderr:\n${report_stderr}")
    endif()
    math(EXPR passed "${passed} + 1")
endforeach()

message(STATUS "matriz --report do corpus: ${passed}/${passed} casos passaram")
