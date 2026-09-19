if(NOT DEFINED TL_RUNTIME OR NOT DEFINED TL_CORPUS)
    message(FATAL_ERROR "TL_RUNTIME e TL_CORPUS são obrigatórios")
endif()

if(NOT EXISTS "${TL_RUNTIME}")
    message(FATAL_ERROR "runtime inexistente: ${TL_RUNTIME}")
endif()
if(NOT IS_DIRECTORY "${TL_CORPUS}")
    message(FATAL_ERROR "corpus inexistente: ${TL_CORPUS}")
endif()

# Todos os PE/ZIP executáveis encontrados recursivamente no corpus atual.
# DLLs e pacotes são analisados, mas nunca iniciados. Os exits são o contrato
# do --report: 0 = concluído, 4 = formato estrutural rejeitado e 5 =
# arquitetura ou mecanismo não suportado.
set(CASES
    "7-Zip/7-zip.dll|0"
    "7-Zip/7-zip32.dll|5"
    "7-Zip/7z.dll|0"
    "7-Zip/7z.exe|0"
    "7-Zip/7zFM.exe|0"
    "7-Zip/7zG.exe|0"
    "7-Zip/Uninstall.exe|5"
    "7-Zip_x64_Installer.exe|5"
    "7z.dll|0"
    "7zFM_x64.exe|0"
    "7z_x64.exe|0"
    "Affinity x64.msix|0"
    "CPU-Z_2.18_en.exe|5"
    "CapCut_7677236283084898320_installer.exe|5"
    "Creative_Cloud_Set-Up_7474.exe|5"
    "EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe|5"
    "Everything_Search_x64.exe|5"
    "GPU-Z_2.70.0.exe|5"
    "HWMonitor_1.67.exe|5"
    "HWiNFO64.exe|4"
    "Logitech_GHUB_x64.exe|0"
    "Notepad++/$PLUGINSDIR/InstallOptions.dll|5"
    "Notepad++/$PLUGINSDIR/LangDLL.dll|5"
    "Notepad++/$PLUGINSDIR/System.dll|5"
    "Notepad++/$PLUGINSDIR/UserInfo.dll|5"
    "Notepad++/$PLUGINSDIR/nsDialogs.dll|5"
    "Notepad++/$_15_/NppConverter/NppConverter.dll|5"
    "Notepad++/$_15_/NppExport/NppExport.dll|0"
    "Notepad++/$_15_/mimeTools/mimeTools.dll|0"
    "Notepad++/$_17_/nppPluginList.dll|0"
    "Notepad++/contextMenu/NppShell.dll|5"
    "Notepad++/contextMenu/NppShell.msix|0"
    "Notepad++/notepad++.exe|0"
    "Notepad++/uninstall.exe|5"
    "Notepad++/updater/GUP.exe|0"
    "Notepad++/updater/libcurl.dll|0"
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
    "npp_extracted/$PLUGINSDIR/InstallOptions.dll|5"
    "npp_extracted/$PLUGINSDIR/LangDLL.dll|5"
    "npp_extracted/$PLUGINSDIR/System.dll|5"
    "npp_extracted/$PLUGINSDIR/UserInfo.dll|5"
    "npp_extracted/$PLUGINSDIR/nsDialogs.dll|5"
    "npp_extracted/$_15_/NppConverter/NppConverter.dll|5"
    "npp_extracted/$_15_/NppExport/NppExport.dll|0"
    "npp_extracted/$_15_/mimeTools/mimeTools.dll|0"
    "npp_extracted/$_17_/nppPluginList.dll|0"
    "npp_extracted/contextMenu/NppShell.dll|5"
    "npp_extracted/contextMenu/NppShell.msix|0"
    "npp_extracted/notepad++.exe|0"
    "npp_extracted/uninstall.exe|5"
    "npp_extracted/updater/GUP.exe|0"
    "npp_extracted/updater/libcurl.dll|0"
    "officedeploymenttool_20228-20124.exe|5"
    "putty_x64.exe|0"
    "winrar-x64-723.exe|0"
)

set(passed 0)
set(total 0)
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
    math(EXPR total "${total} + 1")
endforeach()

message(STATUS "matriz recursiva --report do corpus: ${passed}/${total} casos passaram")
