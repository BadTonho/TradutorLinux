if(NOT TL_BUILD_SAMPLES)
    return()
endif()

find_program(TL_MINGW_CC
    NAMES x86_64-w64-mingw32-gcc-win32 x86_64-w64-mingw32-gcc
    REQUIRED
)
find_program(TL_LLVM_READOBJ
    NAMES llvm-readobj llvm-readobj-19 llvm-readobj-18 llvm-readobj-17
    REQUIRED
)
find_program(TL_MINGW_WINDRES
    NAMES x86_64-w64-mingw32-windres windres
    REQUIRED
)

set(TL_FIXTURE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

function(tl_add_pe_fixture target_name source_file manifest_file)
    set(options LINK_KERNEL32 LINK_USER32 LINK_GDI32 LINK_COMCTL32 LINK_GDIPLUS LINK_UXTHEME LINK_DBGHELP LINK_OLE32 LINK_OLEAUT32 LINK_POWRPROF LINK_IPHLPAPI LINK_WTSAPI32 LINK_WINMM LINK_WS2_32 LINK_WININET LINK_WINTRUST LINK_CRYPT32 LINK_ADVAPI32 LINK_SHELL32 LINK_SHLWAPI LINK_MSVCRT LINK_MPR LINK_COMDLG32 LINK_IMM32 LINK_DWMAPI LINK_VERSION LINK_D3D11 LINK_D3D12 LINK_DXGUID LINK_DXGI LINK_XAUDIO2_8 LINK_UUID RELOCATABLE UNWIND EXPECT_RESOLVED CONSOLE REPORT REPORT_ONLY NO_RUNTIME_CHECK RUNTIME_FAILURE)
    set(oneValueArgs REPORT_SYMBOL REPORT_SUPPORT RESOURCE_FILE IMPORT_DEF KNOWN_LIMITATION)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    set(output_file "${TL_FIXTURE_OUTPUT_DIRECTORY}/${target_name}.exe")
    set(resource_object)
    if(ARG_RESOURCE_FILE)
        set(resource_object "${TL_FIXTURE_OUTPUT_DIRECTORY}/${target_name}.rsrc.o")
        add_custom_command(
            OUTPUT "${resource_object}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
            COMMAND "${TL_MINGW_WINDRES}" -O coff
                -i "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_RESOURCE_FILE}"
                -o "${resource_object}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_RESOURCE_FILE}"
            COMMENT "Generating PE resources for ${target_name}.exe"
            VERBATIM
        )
    endif()
    set(link_libraries)
    set(link_flags)
    if(ARG_LINK_KERNEL32)
        list(APPEND link_libraries -lkernel32)
    endif()
    if(ARG_LINK_USER32)
        list(APPEND link_libraries -luser32)
    endif()
    if(ARG_LINK_GDI32)
        list(APPEND link_libraries -lgdi32)
    endif()
    if(ARG_LINK_COMCTL32)
        list(APPEND link_libraries -lcomctl32)
    endif()
    if(ARG_LINK_GDIPLUS)
        list(APPEND link_libraries -lgdiplus)
    endif()
    if(ARG_LINK_UXTHEME)
        list(APPEND link_libraries -luxtheme)
    endif()
    if(ARG_LINK_DBGHELP)
        list(APPEND link_libraries -ldbghelp)
    endif()
    if(ARG_LINK_OLE32)
        list(APPEND link_libraries -lole32)
    endif()
    if(ARG_LINK_OLEAUT32)
        list(APPEND link_libraries -loleaut32)
    endif()
    if(ARG_LINK_POWRPROF)
        list(APPEND link_libraries -lpowrprof)
    endif()
    if(ARG_LINK_IPHLPAPI)
        list(APPEND link_libraries -liphlpapi)
    endif()
    if(ARG_LINK_WTSAPI32)
        list(APPEND link_libraries -lwtsapi32)
    endif()
    if(ARG_LINK_WINMM)
        list(APPEND link_libraries -lwinmm)
    endif()
    if(ARG_LINK_WS2_32)
        list(APPEND link_libraries -lws2_32)
    endif()
    if(ARG_LINK_WININET)
        list(APPEND link_libraries -lwininet)
    endif()
    if(ARG_LINK_WINTRUST)
        list(APPEND link_libraries -lwintrust)
    endif()
    if(ARG_LINK_CRYPT32)
        list(APPEND link_libraries -lcrypt32)
    endif()
    if(ARG_LINK_ADVAPI32)
        list(APPEND link_libraries -ladvapi32)
    endif()
    if(ARG_LINK_SHELL32)
        list(APPEND link_libraries -lshell32)
    endif()
    if(ARG_LINK_SHLWAPI)
        list(APPEND link_libraries -lshlwapi)
    endif()
    if(ARG_LINK_MSVCRT)
        list(APPEND link_libraries -lmsvcrt)
    endif()
    if(ARG_LINK_MPR)
        list(APPEND link_libraries -lmpr)
    endif()
    if(ARG_LINK_COMDLG32)
        list(APPEND link_libraries -lcomdlg32)
    endif()
    if(ARG_LINK_IMM32)
        list(APPEND link_libraries -limm32)
    endif()
    if(ARG_LINK_DWMAPI)
        list(APPEND link_libraries -ldwmapi)
    endif()
    if(ARG_LINK_UXTHEME)
        list(APPEND link_libraries -luxtheme)
    endif()
    if(ARG_LINK_VERSION)
        list(APPEND link_libraries -lversion)
    endif()
    if(ARG_LINK_D3D11)
        list(APPEND link_libraries -ld3d11)
    endif()
    if(ARG_LINK_D3D12)
        list(APPEND link_libraries -ld3d12)
    endif()
    if(ARG_LINK_DXGUID)
        list(APPEND link_libraries -ldxguid)
    endif()
    if(ARG_LINK_DXGI)
        list(APPEND link_libraries -ldxgi)
    endif()
    if(ARG_LINK_XAUDIO2_8)
        list(APPEND link_libraries -lxaudio2_8)
    endif()
    if(ARG_LINK_UUID)
        list(APPEND link_libraries -luuid)
    endif()
    if(ARG_IMPORT_DEF)
        find_program(TL_MINGW_DLLTOOL
            NAMES x86_64-w64-mingw32-dlltool
            REQUIRED
        )
        set(import_lib "${TL_FIXTURE_OUTPUT_DIRECTORY}/${target_name}.imports.a")
        add_custom_command(
            OUTPUT "${import_lib}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
            COMMAND "${TL_MINGW_DLLTOOL}" -m i386:x86-64
                -d "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_IMPORT_DEF}"
                -l "${import_lib}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_IMPORT_DEF}"
            COMMENT "Generating import library for ${target_name}.exe"
            VERBATIM
        )
        # A biblioteca de imports declarada pela fixture deve preceder as
        # bibliotecas padrão: alguns símbolos (como __C_specific_handler)
        # também aparecem em import libs de KERNEL32 no toolchain mingw.
        list(PREPEND link_libraries "${import_lib}")
    endif()
    if(ARG_RELOCATABLE)
        list(APPEND link_flags -Wl,--dynamicbase)
    endif()
    set(unwind_flags -fno-asynchronous-unwind-tables -fno-unwind-tables)
    if(ARG_UNWIND)
        set(unwind_flags -funwind-tables)
    endif()

    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
        COMMAND "${TL_MINGW_CC}"
            -std=c17
            -ffreestanding
            -fno-stack-protector
            ${unwind_flags}
            -nostdlib
            -Wl,--entry=tl_entry
            -Wl,--subsystem,console
            -Wl,--no-insert-timestamp
            -o "${output_file}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}"
            ${resource_object}
            ${link_flags}
            ${link_libraries}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${manifest_file}"
            ${resource_object}
            ${import_lib}
        VERBATIM
        COMMENT "Generating PE32+ fixture ${target_name}.exe"
    )

    add_custom_target(${target_name}_fixture DEPENDS "${output_file}")
    add_dependencies(tradutorlinux_samples ${target_name}_fixture)

    add_test(
        NAME fixture_${target_name}_metadata
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${output_file}"
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/${manifest_file}"
            "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_fixture.cmake"
    )
    set_tests_properties(fixture_${target_name}_metadata PROPERTIES LABELS "fixture;pe32plus")

    if(TARGET tradutorlinux AND NOT ARG_RUNTIME_FAILURE AND NOT ARG_REPORT_ONLY
       AND NOT ARG_NO_RUNTIME_CHECK)
        set(runtime_manifest_args)
        if(ARG_EXPECT_RESOLVED)
            set(runtime_manifest_args
                "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/${manifest_file}")
        endif()
        set(runtime_script verify_runtime_output.cmake)
        set(runtime_extra_args)
        if(ARG_CONSOLE)
            set(runtime_script verify_console_output.cmake)
            if(target_name STREQUAL "tl_hello")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=4f6c6120646f2057696e646f7773206e6f204c696e7578210a")
            elseif(target_name STREQUAL "tl_echo")
                set(runtime_extra_args
                    "-DINPUT_TEXT=eco"
                    "-DEXPECTED_HEX=65636f")
            elseif(target_name STREQUAL "tl_file")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=66617365350a"
                    "-DCLEANUP_FILE=tl_phase5_data.bin")
            elseif(target_name STREQUAL "tl_virtual_query")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=7669727475616c2d71756572790a")
            elseif(target_name STREQUAL "tl_thread")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=54687265616420646f6e650a54687265616420646f6e650a4d61696e20646f6e650a")
            elseif(target_name STREQUAL "tl_tls_generic")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=746c732d67656e657269630a")
            elseif(target_name STREQUAL "tl_resources")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=546c207265736f75726365207061796c6f61640a")
            elseif(target_name STREQUAL "tl_sync")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=73796e630a")
            elseif(target_name STREQUAL "tl_files_wide")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=66696c65730a")
            elseif(target_name STREQUAL "tl_process_parent")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6368696c640a706172656e740a")
            elseif(target_name STREQUAL "tl_process_child")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6368696c640a"
                    "-DEXPECTED_EXIT=7")
            elseif(target_name STREQUAL "tl_network_loopback")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6e6574776f726b0a"
                    "-DSKIP_EXIT=77")
            elseif(target_name STREQUAL "tl_worker_rsl")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=776f726b65722d72736c0a"
                    "-DSKIP_EXIT=77")
            elseif(target_name STREQUAL "tl_registry_unicode")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=72656769737472790a")
            elseif(target_name STREQUAL "tl_dynload")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=64796e6c6f61640a")
            elseif(target_name STREQUAL "tl_version")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=76657273696f6e0a")
            elseif(target_name STREQUAL "tl_waitaddr")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=77616974616464720a")
            elseif(target_name STREQUAL "tl_fiber")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=66696265720a")
            elseif(target_name STREQUAL "tl_toolhelp")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=746f6f6c68656c700a")
            elseif(target_name STREQUAL "tl_shell")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=7368656c6c0a")
            elseif(target_name STREQUAL "tl_shell_heap_probe")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=7368656c6c2d686561702d70726f62650a")
            elseif(target_name STREQUAL "tl_notepad_startup_probe")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6e6f74657061642d737461727475702d70726f62650a"
                    "-DREQUIRED_TRACE=process-context,environment,locale,fls")
            elseif(target_name STREQUAL "tl_gdiex")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=67646965780a")
            elseif(target_name STREQUAL "tl_com")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=636f6d0a")
            elseif(target_name STREQUAL "tl_k32_gap")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6b33322d6761700a")
            elseif(target_name STREQUAL "tl_globalmem")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=676c6f62616c6d656d0a")
            elseif(target_name STREQUAL "tl_crypt32")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=637279707433320a")
            elseif(target_name STREQUAL "tl_certcontext")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=63657274636f6e746578740a")
            elseif(target_name STREQUAL "tl_user_ext")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=757365726578740a")
            elseif(target_name STREQUAL "tl_k32_system")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6b333273797374656d0a")
            elseif(target_name STREQUAL "tl_shell_path")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=7368656c6c706174680a")
            elseif(target_name STREQUAL "tl_oleaut_bstr")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6f6c65617574627374720a")
            elseif(target_name STREQUAL "tl_k32_gdi_link")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6b33326764696c696e6b0a")
            elseif(target_name STREQUAL "tl_winrar_kernel_shell")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=77696e7261726b65726e656c7368656c6c0a")
            elseif(target_name STREQUAL "tl_7z_cli_crt")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=377a636c696372740a")
            elseif(target_name STREQUAL "tl_rockstar_pipes_reg")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=726f636b7374617270697065730a")
            elseif(target_name STREQUAL "tl_7zfm_gui")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=375a464d204755492041504953204f4b0a")
            elseif(target_name STREQUAL "tl_putty")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=5055545459205353482041504953204f4b0a")
            elseif(target_name STREQUAL "tl_notepadpp")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=4e4f54455041442b2b2041504953204f4b0a")
            elseif(target_name STREQUAL "tl_roblox")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=524f424c4f582041504953204f4b0a")
            elseif(target_name STREQUAL "tl_stream")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6f6c652d73747265616d0a"
                    "-DREQUIRED_TRACE=ole-stream")
            elseif(target_name STREQUAL "tl_trust")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=74727573740a"
                    "-DREQUIRED_TRACE=wintrust")
            elseif(target_name STREQUAL "tl_wthelper")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=777468656c7065720a"
                    "-DREQUIRED_TRACE=wintrust")
            elseif(target_name STREQUAL "tl_unwind")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=756e77696e640a")
            elseif(target_name STREQUAL "tl_seh")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=7365680a")
            elseif(target_name STREQUAL "tl_powr")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=706f77720a"
                    "-DSKIP_EXIT=77")
            elseif(target_name STREQUAL "tl_locale_env_fls")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6c6f63616c652d656e762d666c730a"
                    "-DREQUIRED_TRACE=environment,fls,locale")
            elseif(target_name STREQUAL "tl_locale_extended")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=6c6f63616c652d657874656e6465640a"
                    "-DREQUIRED_TRACE=locale")
            elseif(target_name STREQUAL "tl_process_console")
                set(runtime_extra_args
                    "-DINPUT_TEXT=entrada"
                    "-DEXPECTED_HEX=70726f636573732d636f6e736f6c652dc3a90a"
                    "-DREQUIRED_TRACE=process-context,console")
            elseif(target_name STREQUAL "tl_file_metadata")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=66696c652d6d657461646174610a"
                    "-DREQUIRED_TRACE=filesystem"
                    "-DPREFIX_DIR=${CMAKE_CURRENT_BINARY_DIR}/file-metadata-prefix")
            endif()
        endif()
        add_test(
            NAME runtime_${target_name}_matches_readobj
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
                ${runtime_manifest_args}
                ${runtime_extra_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/${runtime_script}"
        )
        set_tests_properties(runtime_${target_name}_matches_readobj PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            LABELS "runtime;pe32plus"
        )

        # The catalog path is the production AppRun path.  Keep the direct
        # runtime test above as the C++/loader oracle, and run the same native
        # fixture through an isolated catalog so the selected PE backend is
        # exercised before the unchanged loader pipeline.
        set(app_runtime_extra_args)
        set(app_expected_hex_present FALSE)
        foreach(runtime_arg IN LISTS runtime_extra_args)
            if(runtime_arg MATCHES "^-D(INPUT_TEXT|CLEANUP_FILE|SKIP_EXIT|EXPECTED_EXIT|REQUIRED_TRACE)=")
                list(APPEND app_runtime_extra_args "${runtime_arg}")
            elseif(runtime_arg MATCHES "^-DEXPECTED_HEX=")
                list(APPEND app_runtime_extra_args "${runtime_arg}")
                set(app_expected_hex_present TRUE)
            endif()
        endforeach()
        if(NOT app_expected_hex_present)
            list(APPEND app_runtime_extra_args "-DEXPECTED_HEX=")
        endif()
        add_test(
            NAME app_run_${target_name}
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DAPP_ID=r21-4-${target_name}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-${target_name}"
                "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
                ${runtime_manifest_args}
                ${app_runtime_extra_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
        )
        set_tests_properties(app_run_${target_name} PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            LABELS "runtime;app-run;pe32plus"
        )
    endif()

    if(ARG_RUNTIME_FAILURE AND TARGET tradutorlinux)
        add_test(
            NAME runtime_${target_name}_rejected
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DEXPECTED_EXIT=5"
                "-DREQUIRED_TRACE=unknown-symbol"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_rejection.cmake"
        )
        set_tests_properties(runtime_${target_name}_rejected PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            LABELS "runtime;pe32plus"
        )
        add_test(
            NAME app_run_${target_name}_rejected
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DAPP_ID=r21-4-${target_name}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-${target_name}-rejected"
                "-DEXPECTED_EXIT=5"
                "-DREQUIRED_TRACE=unknown-symbol"
                "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
        )
        set_tests_properties(app_run_${target_name}_rejected PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            LABELS "runtime;app-run;pe32plus"
        )
    endif()

    if(ARG_KNOWN_LIMITATION)
        if(NOT ARG_RUNTIME_FAILURE)
            message(FATAL_ERROR
                "KNOWN_LIMITATION for ${target_name} requires RUNTIME_FAILURE"
            )
        endif()
        add_test(
            NAME fixture_${target_name}_limitation_metadata
            COMMAND "${CMAKE_COMMAND}"
                "-DFIXTURE=${target_name}.exe"
                "-DLIMITATION=${ARG_KNOWN_LIMITATION}"
                "-DMATRIX=${CMAKE_SOURCE_DIR}/docs/compatibilidade.md"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_known_limitation.cmake"
        )
        set_tests_properties(fixture_${target_name}_limitation_metadata PROPERTIES
            DEPENDS runtime_${target_name}_rejected
            LABELS "fixture;pe32plus;expectation"
        )
    endif()

    if((ARG_REPORT OR ARG_REPORT_ONLY) AND TARGET tradutorlinux)
        set(report_extra_args)
        if(ARG_REPORT_ONLY)
            set(report_extra_args -DEXPECT_USER32=1)
        endif()
        if(ARG_REPORT_SYMBOL)
            list(APPEND report_extra_args "-DEXPECTED_SYMBOL=${ARG_REPORT_SYMBOL}")
        endif()
        if(ARG_REPORT_SUPPORT)
            list(APPEND report_extra_args "-DEXPECTED_SUPPORT=${ARG_REPORT_SUPPORT}")
        endif()
        add_test(
            NAME report_${target_name}_support
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                ${report_extra_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_report.cmake"
        )
        set_tests_properties(report_${target_name}_support PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            LABELS "runtime;pe32plus"
        )
    endif()
endfunction()

add_custom_target(tradutorlinux_samples ALL)

add_executable(tl_delay_import_generator
    generate_delay_import_fixture.cpp
)
target_include_directories(tl_delay_import_generator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
tl_enable_warnings(tl_delay_import_generator)

set(TL_DELAY_IMPORT_FIXTURE "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_delay_import.exe")
set(TL_DELAY_IMPORT_MISSING_FIXTURE "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_delay_import_missing.exe")
add_custom_command(
    OUTPUT "${TL_DELAY_IMPORT_FIXTURE}" "${TL_DELAY_IMPORT_MISSING_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "$<TARGET_FILE:tl_delay_import_generator>" "${TL_DELAY_IMPORT_FIXTURE}" ExitProcess
    COMMAND "$<TARGET_FILE:tl_delay_import_generator>" "${TL_DELAY_IMPORT_MISSING_FIXTURE}" TlMissingDelayImportW
    DEPENDS tl_delay_import_generator
    VERBATIM
    COMMENT "Generating PE32+ delay-import fixture tl_delay_import.exe"
)
add_custom_target(tl_delay_import_fixture DEPENDS
    "${TL_DELAY_IMPORT_FIXTURE}" "${TL_DELAY_IMPORT_MISSING_FIXTURE}")
add_dependencies(tradutorlinux_samples tl_delay_import_fixture)

add_test(
    NAME fixture_tl_delay_import_metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${TL_DELAY_IMPORT_FIXTURE}"
        "-DMISSING_INPUT=${TL_DELAY_IMPORT_MISSING_FIXTURE}"
        "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_delay_import_fixture.cmake"
)
set_tests_properties(fixture_tl_delay_import_metadata PROPERTIES LABELS "fixture;pe32plus")

if(TARGET tradutorlinux)
    add_test(
        NAME app_run_tl_delay_import
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_DELAY_IMPORT_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-delay-import"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-delay-import"
            "-DEXPECTED_HEX="
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
    )
    set_tests_properties(app_run_tl_delay_import PROPERTIES
        DEPENDS fixture_tl_delay_import_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;imports;delay-import"
    )

    add_test(
        NAME app_run_tl_delay_import_missing
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_DELAY_IMPORT_MISSING_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-delay-import-missing"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-delay-import-missing"
            "-DEXPECTED_EXIT=5"
            "-DREQUIRED_TRACE=unknown-symbol"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
    )
    set_tests_properties(app_run_tl_delay_import_missing PROPERTIES
        DEPENDS fixture_tl_delay_import_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;imports;delay-import"
    )
endif()

tl_add_pe_fixture(tl_nop src/tl_nop.c manifests/tl_nop.json RELOCATABLE)
tl_add_pe_fixture(tl_hello src/tl_hello.c manifests/tl_hello.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)
tl_add_pe_fixture(tl_echo src/tl_echo.c manifests/tl_echo.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)
tl_add_pe_fixture(tl_file src/tl_file.c manifests/tl_file.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE)
tl_add_pe_fixture(tl_compat_file src/tl_compat_file.c manifests/tl_compat_file.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "CreateFileA")
tl_add_pe_fixture(tl_proton_probe src/tl_proton_probe.c manifests/tl_proton_probe.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "GetStdHandle")
tl_add_pe_fixture(tl_graphics_probe src/tl_graphics_probe.c manifests/tl_graphics_probe.json
    LINK_KERNEL32 LINK_USER32 LINK_D3D11 LINK_UUID RELOCATABLE NO_RUNTIME_CHECK)
tl_add_pe_fixture(tl_d3d12_probe src/tl_d3d12_probe.c manifests/tl_d3d12_probe.json
    LINK_KERNEL32 LINK_USER32 LINK_D3D12 LINK_DXGUID LINK_DXGI RELOCATABLE NO_RUNTIME_CHECK)
tl_add_pe_fixture(tl_input_probe src/tl_input_probe.c manifests/tl_input_probe.json
    LINK_KERNEL32 LINK_USER32 RELOCATABLE NO_RUNTIME_CHECK)
tl_add_pe_fixture(tl_audio_probe src/tl_audio_probe.c manifests/tl_audio_probe.json
    LINK_KERNEL32 LINK_XAUDIO2_8 RELOCATABLE NO_RUNTIME_CHECK)

set(TL_COMPAT_DLL_A "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_a.dll")
set(TL_COMPAT_DLL_B "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_b.dll")
set(TL_COMPAT_DLL_IMPORT_LIB "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll.imports.a")
set(TL_COMPAT_DEP_DLL "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dep.dll")
set(TL_COMPAT_DEP_IMPORT_LIB "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dep.imports.a")
find_program(TL_MINGW_DLLTOOL
    NAMES x86_64-w64-mingw32-dlltool
    REQUIRED
)
add_custom_command(
    OUTPUT "${TL_COMPAT_DEP_DLL}" "${TL_COMPAT_DEP_IMPORT_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "${TL_MINGW_CC}"
        -std=c17 -ffreestanding -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -nostdlib -shared
        -Wl,--entry=DllMain -Wl,--subsystem,console
        -Wl,--no-insert-timestamp -Wl,--dynamicbase
        -o "${TL_COMPAT_DEP_DLL}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dep.c"
    COMMAND "${TL_MINGW_DLLTOOL}" -m i386:x86-64
        -d "${CMAKE_CURRENT_SOURCE_DIR}/defs/tl_compat_dep.def"
        -l "${TL_COMPAT_DEP_IMPORT_LIB}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dep.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/defs/tl_compat_dep.def"
    VERBATIM
    COMMENT "Generating PE32+ compatibility dependency DLL"
)
add_custom_command(
    OUTPUT "${TL_COMPAT_DLL_A}" "${TL_COMPAT_DLL_IMPORT_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "${TL_MINGW_CC}"
        -std=c17 -ffreestanding -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -nostdlib -shared
        -Wl,--entry=DllMain -Wl,--subsystem,console
        -Wl,--no-insert-timestamp -Wl,--dynamicbase
        -o "${TL_COMPAT_DLL_A}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_a.c"
        "${TL_COMPAT_DEP_IMPORT_LIB}"
        -lkernel32
    COMMAND "${TL_MINGW_DLLTOOL}" -m i386:x86-64
        -d "${CMAKE_CURRENT_SOURCE_DIR}/defs/tl_compat_dll.def"
        -l "${TL_COMPAT_DLL_IMPORT_LIB}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_a.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/defs/tl_compat_dll.def"
        "${TL_COMPAT_DEP_IMPORT_LIB}"
    VERBATIM
    COMMENT "Generating PE32+ compatibility DLL variant A"
)
add_custom_command(
    OUTPUT "${TL_COMPAT_DLL_B}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "${TL_MINGW_CC}"
        -std=c17 -ffreestanding -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -nostdlib -shared
        -Wl,--entry=DllMain -Wl,--subsystem,console
        -Wl,--no-insert-timestamp -Wl,--dynamicbase
        -o "${TL_COMPAT_DLL_B}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_b.c"
        "${TL_COMPAT_DEP_IMPORT_LIB}"
        -lkernel32
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_b.c"
        "${TL_COMPAT_DEP_IMPORT_LIB}"
    VERBATIM
    COMMENT "Generating PE32+ compatibility DLL variant B"
)
add_custom_command(
    OUTPUT "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_app.exe"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "${TL_MINGW_CC}"
        -std=c17 -ffreestanding -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -nostdlib
        -Wl,--entry=tl_entry -Wl,--subsystem,console
        -Wl,--no-insert-timestamp -Wl,--dynamicbase
        -o "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_app.exe"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_app.c"
        "${TL_COMPAT_DLL_IMPORT_LIB}"
        -lkernel32
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_compat_dll_app.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_compat_dll_app.json"
        "${TL_COMPAT_DLL_IMPORT_LIB}"
    VERBATIM
    COMMENT "Generating PE32+ compatibility DLL application"
)
add_custom_target(tl_compat_dll_fixture
    DEPENDS
        "${TL_COMPAT_DEP_DLL}"
        "${TL_COMPAT_DEP_IMPORT_LIB}"
        "${TL_COMPAT_DLL_A}"
        "${TL_COMPAT_DLL_B}"
        "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_app.exe"
)
add_dependencies(tradutorlinux_samples tl_compat_dll_fixture)

add_test(
    NAME fixture_tl_compat_dll_app_metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_compat_dll_app.exe"
        "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_compat_dll_app.json"
        "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_fixture.cmake"
)
set_tests_properties(fixture_tl_compat_dll_app_metadata PROPERTIES LABELS "fixture;pe32plus;compat")
tl_add_pe_fixture(tl_virtual_query src/tl_virtual_query.c manifests/tl_virtual_query.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE)
tl_add_pe_fixture(tl_reloc src/tl_reloc.c manifests/tl_reloc.json RELOCATABLE)
tl_add_pe_fixture(tl_missing_dll src/tl_missing_dll.c manifests/tl_missing_dll.json
    IMPORT_DEF defs/tl_missing_dll.def RUNTIME_FAILURE RELOCATABLE
    KNOWN_LIMITATION "unknown-symbol")
tl_add_pe_fixture(tl_gui src/tl_gui.c manifests/tl_gui.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE)
tl_add_pe_fixture(tl_win src/tl_win.c manifests/tl_win.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExA")
tl_add_pe_fixture(tl_win2 src/tl_win2.c manifests/tl_win2.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExA")
tl_add_pe_fixture(tl_win_w src/tl_win_w.c manifests/tl_win_w.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExW")
tl_add_pe_fixture(tl_dialog src/tl_dialog.c manifests/tl_dialog.json
    LINK_KERNEL32 LINK_USER32 LINK_COMCTL32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "DialogBoxParamW" RESOURCE_FILE src/tl_dialog.rc)
tl_add_pe_fixture(tl_key src/tl_key.c manifests/tl_key.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExA")
tl_add_pe_fixture(tl_timer src/tl_timer.c manifests/tl_timer.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "SetTimer")
tl_add_pe_fixture(tl_gdi src/tl_gdi.c manifests/tl_gdi.json
    LINK_KERNEL32 LINK_USER32 LINK_GDI32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "TextOutA")
tl_add_pe_fixture(tl_paint src/tl_paint.c manifests/tl_paint.json
    LINK_KERNEL32 LINK_USER32 LINK_GDI32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "FillRect")

tl_add_pe_fixture(tl_crash src/tl_crash.c manifests/tl_crash.json RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_unwind src/tl_unwind.c manifests/tl_unwind.json
    LINK_KERNEL32 UNWIND EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "RtlVirtualUnwind")

tl_add_pe_fixture(tl_seh src/tl_seh.c manifests/tl_seh.json
    LINK_KERNEL32 IMPORT_DEF defs/tl_seh.def UNWIND EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "RaiseException")

tl_add_pe_fixture(tl_seh_v2_seed src/tl_seh.c manifests/tl_seh.json
    LINK_KERNEL32 IMPORT_DEF defs/tl_seh.def UNWIND EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)

# O compilador local ainda pode emitir somente UNWIND_INFO V1. Geramos uma
# imagem-semente normal e a promovemos de maneira determinística para V2,
# preservando os mesmos códigos de prólogo e inserindo um descritor de epílogo
# no espaço de padding já reservado pelo formato V1.
tl_add_pe_fixture(tl_unwind_v2_seed src/tl_unwind_v2.c manifests/tl_unwind_v2.json
    LINK_KERNEL32 UNWIND EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)
add_custom_command(
    OUTPUT "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_unwind_v2_seed.exe"
    APPEND
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/tl_unwind.c"
)

add_executable(tl_unwind_v2_generator generate_unwind_v2_fixture.cpp)
tl_enable_warnings(tl_unwind_v2_generator)

set(TL_UNWIND_V2_FIXTURE "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_unwind_v2.exe")
add_custom_command(
    OUTPUT "${TL_UNWIND_V2_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "$<TARGET_FILE:tl_unwind_v2_generator>"
        "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_unwind_v2_seed.exe"
        "${TL_UNWIND_V2_FIXTURE}"
    DEPENDS
        "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_unwind_v2_seed.exe"
        tl_unwind_v2_seed_fixture
        tl_unwind_v2_generator
    VERBATIM
    COMMENT "Promoting tl_unwind_v2.exe metadata to UNWIND_INFO V2"
)
add_custom_target(tl_unwind_v2_fixture DEPENDS "${TL_UNWIND_V2_FIXTURE}")
add_dependencies(tradutorlinux_samples tl_unwind_v2_fixture)

set(TL_SEH_V2_FIXTURE "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_seh_v2.exe")
add_custom_command(
    OUTPUT "${TL_SEH_V2_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
    COMMAND "$<TARGET_FILE:tl_unwind_v2_generator>"
        "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_seh_v2_seed.exe"
        "${TL_SEH_V2_FIXTURE}"
        "RaiseException"
    DEPENDS
        "${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_seh_v2_seed.exe"
        tl_seh_v2_seed_fixture
        tl_unwind_v2_generator
    VERBATIM
    COMMENT "Promoting tl_seh_v2.exe metadata to UNWIND_INFO V2"
)
add_custom_target(tl_seh_v2_fixture DEPENDS "${TL_SEH_V2_FIXTURE}")
add_dependencies(tradutorlinux_samples tl_seh_v2_fixture)

add_test(
    NAME fixture_tl_seh_v2_metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${TL_SEH_V2_FIXTURE}"
        "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_seh.json"
        "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_fixture.cmake"
)
set_tests_properties(fixture_tl_seh_v2_metadata PROPERTIES LABELS "fixture;pe32plus")

add_test(
    NAME fixture_tl_unwind_v2_metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${TL_UNWIND_V2_FIXTURE}"
        "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_unwind_v2.json"
        "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_fixture.cmake"
)
set_tests_properties(fixture_tl_unwind_v2_metadata PROPERTIES LABELS "fixture;pe32plus")

if(TARGET tradutorlinux)
    add_test(
        NAME unwind_tl_unwind_contract
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_unwind.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_unwind_fixture.cmake"
    )
    set_tests_properties(unwind_tl_unwind_contract PROPERTIES
        DEPENDS fixture_tl_unwind_metadata
        LABELS "fixture;runtime;pe32plus"
    )

    add_test(
        NAME runtime_tl_unwind_v2_matches_readobj
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_UNWIND_V2_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_unwind_v2.json"
            "-DEXPECTED_HEX=756e77696e642d76320a"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_console_output.cmake"
    )
    set_tests_properties(runtime_tl_unwind_v2_matches_readobj PROPERTIES
        DEPENDS fixture_tl_unwind_v2_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;pe32plus"
    )

    add_test(
        NAME unwind_tl_unwind_v2_contract
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_UNWIND_V2_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_unwind_v2_fixture.cmake"
    )
    set_tests_properties(unwind_tl_unwind_v2_contract PROPERTIES
        DEPENDS fixture_tl_unwind_v2_metadata
        LABELS "fixture;runtime;pe32plus"
    )

    add_test(
        NAME runtime_tl_seh_v2_matches_readobj
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_SEH_V2_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_seh.json"
            "-DEXPECTED_HEX=7365680a"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_console_output.cmake"
    )
    set_tests_properties(runtime_tl_seh_v2_matches_readobj PROPERTIES
        DEPENDS fixture_tl_seh_v2_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;pe32plus"
    )

    add_test(
        NAME app_run_tl_unwind_v2
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_UNWIND_V2_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-unwind-v2"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-unwind-v2"
            "-DEXPECTED_HEX=756e77696e642d76320a"
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_unwind_v2.json"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
    )
    set_tests_properties(app_run_tl_unwind_v2 PROPERTIES
        DEPENDS fixture_tl_unwind_v2_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;unwind"
    )

    add_test(
        NAME app_run_tl_seh_v2
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_SEH_V2_FIXTURE}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-seh-v2"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-seh-v2"
            "-DEXPECTED_HEX=7365680a"
            "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/manifests/tl_seh.json"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
    )
    set_tests_properties(app_run_tl_seh_v2 PROPERTIES
        DEPENDS fixture_tl_seh_v2_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;unwind;seh"
    )
endif()

tl_add_pe_fixture(tl_hang src/tl_hang.c manifests/tl_hang.json RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_memory_limit src/tl_memory_limit.c manifests/tl_memory_limit.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_process_limit_parent src/tl_process_limit_parent.c
    manifests/tl_process_limit_parent.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "CreateProcessW")

tl_add_pe_fixture(tl_thread src/tl_thread.c manifests/tl_thread.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)

tl_add_pe_fixture(tl_tls_generic src/tl_tls_generic.c manifests/tl_tls_generic.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetStdHandle")

tl_add_pe_fixture(tl_locale_env_fls src/tl_locale_env_fls.c manifests/tl_locale_env_fls.json
    LINK_KERNEL32 UNWIND EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "FlsAlloc")

tl_add_pe_fixture(tl_locale_extended src/tl_locale_extended.c manifests/tl_locale_extended.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "EnumSystemLocalesW")

tl_add_pe_fixture(tl_process_console src/tl_process_console.c manifests/tl_process_console.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetStartupInfoW")

tl_add_pe_fixture(tl_file_metadata src/tl_file_metadata.c manifests/tl_file_metadata.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SetFileInformationByHandle")

tl_add_pe_fixture(tl_security src/tl_security.c manifests/tl_security.json
    LINK_KERNEL32 LINK_ADVAPI32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "GetNamedSecurityInfoW")

if(TARGET tradutorlinux)
    add_test(
        NAME runtime_tl_security_prefix
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_security.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/security-prefix-work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_security_prefix.cmake"
    )
    set_tests_properties(runtime_tl_security_prefix PROPERTIES
        DEPENDS fixture_tl_security_metadata
        LABELS "runtime;pe32plus;security"
    )
endif()

tl_add_pe_fixture(tl_resources src/tl_resources.c manifests/tl_resources.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "FindResourceW"
    RESOURCE_FILE src/tl_resources.rc)

tl_add_pe_fixture(tl_sync src/tl_sync.c manifests/tl_sync.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateEventA")

tl_add_pe_fixture(tl_k32_gap src/tl_k32_gap.c manifests/tl_k32_gap.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "InitializeCriticalSectionEx")

tl_add_pe_fixture(tl_globalmem src/tl_globalmem.c manifests/tl_globalmem.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GlobalAlloc")

tl_add_pe_fixture(tl_crypt32 src/tl_crypt32.c manifests/tl_crypt32.json
    LINK_KERNEL32 LINK_CRYPT32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CertGetNameStringW")

tl_add_pe_fixture(tl_certcontext src/tl_certcontext.c manifests/tl_certcontext.json
    LINK_KERNEL32 LINK_CRYPT32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CertDuplicateCertificateContext")

tl_add_pe_fixture(tl_user_ext src/tl_user_ext.c manifests/tl_user_ext.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetDesktopWindow")

tl_add_pe_fixture(tl_k32_system src/tl_k32_system.c manifests/tl_k32_system.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetTimeZoneInformation")

tl_add_pe_fixture(tl_shell_path src/tl_shell_path.c manifests/tl_shell_path.json
    LINK_KERNEL32 LINK_SHELL32 LINK_SHLWAPI EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SHAutoComplete")

tl_add_pe_fixture(tl_oleaut_bstr src/tl_oleaut_bstr.c manifests/tl_oleaut_bstr.json
    LINK_KERNEL32 LINK_OLEAUT32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SysAllocString")

tl_add_pe_fixture(tl_k32_gdi_link src/tl_k32_gdi_link.c manifests/tl_k32_gdi_link.json
    LINK_KERNEL32 LINK_GDI32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateHardLinkW")

tl_add_pe_fixture(tl_winrar_kernel_shell src/tl_winrar_kernel_shell.c manifests/tl_winrar_kernel_shell.json
    LINK_KERNEL32 LINK_USER32 LINK_ADVAPI32 LINK_SHELL32 LINK_OLE32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetTickCount")

tl_add_pe_fixture(tl_7z_cli_crt src/tl_7z_cli_crt.c manifests/tl_7z_cli_crt.json
    LINK_KERNEL32 LINK_ADVAPI32 LINK_MSVCRT EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetVersion")

tl_add_pe_fixture(tl_rockstar_pipes_reg src/tl_rockstar_pipes_reg.c manifests/tl_rockstar_pipes_reg.json
    LINK_KERNEL32 LINK_ADVAPI32 LINK_USER32 LINK_GDI32 LINK_SHLWAPI LINK_COMCTL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "WaitNamedPipeW")

tl_add_pe_fixture(tl_7zfm_gui src/tl_7zfm_gui.c manifests/tl_7zfm_gui.json
    LINK_KERNEL32 LINK_ADVAPI32 LINK_USER32 LINK_COMCTL32 LINK_SHELL32 LINK_MSVCRT LINK_MPR EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "FindFirstChangeNotificationW")

tl_add_pe_fixture(tl_putty src/tl_putty.c manifests/tl_putty.json
    LINK_KERNEL32 LINK_WS2_32 LINK_GDI32 LINK_USER32 LINK_COMDLG32 LINK_IMM32 LINK_SHELL32 LINK_ADVAPI32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "WSAStartup")

tl_add_pe_fixture(tl_notepadpp src/tl_notepadpp.c manifests/tl_notepadpp.json
    LINK_KERNEL32 LINK_USER32 LINK_GDI32 LINK_COMCTL32 LINK_UXTHEME LINK_DWMAPI EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "DwmIsCompositionEnabled")

tl_add_pe_fixture(tl_roblox src/tl_roblox.c manifests/tl_roblox.json
    LINK_KERNEL32 LINK_ADVAPI32 LINK_WS2_32 LINK_USER32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CryptAcquireContextW")

tl_add_pe_fixture(tl_files_wide src/tl_files_wide.c manifests/tl_files_wide.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateFileW")

tl_add_pe_fixture(tl_install_app src/tl_install_app.c manifests/tl_install_app.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_install_setup src/tl_install_setup.c manifests/tl_install_setup.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)
add_dependencies(tl_install_setup_fixture tl_install_app_fixture)

tl_add_pe_fixture(tl_install_setup_multi src/tl_install_setup_multi.c manifests/tl_install_setup_multi.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)
add_dependencies(tl_install_setup_multi_fixture tl_install_app_fixture)

tl_add_pe_fixture(tl_install_setup_fail src/tl_install_setup_fail.c manifests/tl_install_setup_fail.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_process_child src/tl_process_child.c manifests/tl_process_child.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "ExitProcess")

tl_add_pe_fixture(tl_process_hang src/tl_process_hang.c manifests/tl_process_hang.json
    RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_process_parent src/tl_process_parent.c manifests/tl_process_parent.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateProcessW")
add_dependencies(tl_process_parent_fixture tl_process_child_fixture tl_process_hang_fixture)

tl_add_pe_fixture(tl_network_loopback src/tl_network_loopback.c manifests/tl_network_loopback.json
    LINK_KERNEL32 LINK_WS2_32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "socket")

tl_add_pe_fixture(tl_worker_rsl src/tl_worker_rsl.c manifests/tl_worker_rsl.json
    LINK_KERNEL32 LINK_WS2_32 LINK_IPHLPAPI LINK_CRYPT32 LINK_WTSAPI32
    EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetAdaptersAddresses")

tl_add_pe_fixture(tl_wininet src/tl_wininet.c manifests/tl_wininet.json
    LINK_KERNEL32 LINK_WININET EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE NO_RUNTIME_CHECK
    REPORT_SYMBOL "InternetOpenW")

if(TARGET tradutorlinux)
    find_program(TL_OPENSSL_EXECUTABLE NAMES openssl REQUIRED)
    find_program(TL_PYTHON_EXECUTABLE NAMES python3 REQUIRED)
    add_test(
        NAME runtime_tl_wininet_https_loopback
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_wininet.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DOPENSSL=${TL_OPENSSL_EXECUTABLE}"
            "-DPYTHON=${TL_PYTHON_EXECUTABLE}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/wininet-tls-work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_wininet_tls.cmake"
    )
    set_tests_properties(runtime_tl_wininet_https_loopback PROPERTIES
        DEPENDS fixture_tl_wininet_metadata
        LABELS "runtime;pe32plus;wininet"
        SKIP_REGULAR_EXPRESSION "Skipping WinINet TLS fixture"
        TIMEOUT 30
    )
endif()

tl_add_pe_fixture(tl_registry_unicode src/tl_registry_unicode.c manifests/tl_registry_unicode.json
    LINK_KERNEL32 LINK_ADVAPI32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "RegCreateKeyExW")

tl_add_pe_fixture(tl_dynload src/tl_dynload.c manifests/tl_dynload.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "LoadLibraryA")

tl_add_pe_fixture(tl_version src/tl_version.c manifests/tl_version.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "GetVersionExA")

tl_add_pe_fixture(tl_waitaddr src/tl_waitaddr.c manifests/tl_waitaddr.json
    EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "WaitOnAddress"
    IMPORT_DEF defs/tl_waitaddr.def)

tl_add_pe_fixture(tl_fiber src/tl_fiber.c manifests/tl_fiber.json
    EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateFiberEx"
    IMPORT_DEF defs/tl_fiber.def)

tl_add_pe_fixture(tl_powr src/tl_powr.c manifests/tl_powr.json
    LINK_KERNEL32 LINK_POWRPROF LINK_IPHLPAPI EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "PowerGetActiveScheme")

tl_add_pe_fixture(tl_toolhelp src/tl_toolhelp.c manifests/tl_toolhelp.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateToolhelp32Snapshot")

tl_add_pe_fixture(tl_shell src/tl_shell.c manifests/tl_shell.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SHGetKnownFolderPath"
    IMPORT_DEF defs/tl_shell.def)

tl_add_pe_fixture(tl_shell_heap_probe src/tl_shell_heap_probe.c manifests/tl_shell_heap_probe.json
    LINK_KERNEL32 LINK_SHELL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SHGetFolderPathW")

tl_add_pe_fixture(tl_notepad_startup_probe src/tl_notepad_startup_probe.c
    manifests/tl_notepad_startup_probe.json
    LINK_KERNEL32 LINK_SHELL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "SHGetFolderPathW")

tl_add_pe_fixture(tl_gdiex src/tl_gdiex.c manifests/tl_gdiex.json
    LINK_KERNEL32 LINK_USER32 LINK_GDI32 LINK_GDIPLUS LINK_UXTHEME LINK_WINMM LINK_DBGHELP EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "timeSetEvent" REPORT_SUPPORT "stub")

tl_add_pe_fixture(tl_com src/tl_com.c manifests/tl_com.json
    LINK_KERNEL32 LINK_OLE32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CoCreateInstance")

tl_add_pe_fixture(tl_stream src/tl_stream.c manifests/tl_stream.json
    LINK_KERNEL32 LINK_OLE32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateStreamOnHGlobal")

tl_add_pe_fixture(tl_trust src/tl_trust.c manifests/tl_trust.json
    LINK_KERNEL32 LINK_WINTRUST EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "WinVerifyTrust")

tl_add_pe_fixture(tl_wthelper src/tl_wthelper.c manifests/tl_wthelper.json
    LINK_KERNEL32 LINK_WINTRUST LINK_CRYPT32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "WTHelperGetProvCertFromChain")

if(TARGET tradutorlinux)
    add_test(
        NAME integration_install_prefix_catalog_run
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DSETUP=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_install_setup.exe"
            "-DMULTI_SETUP=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_install_setup_multi.exe"
            "-DFAIL_SETUP=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_install_setup_fail.exe"
            "-DMISSING_IMPORTS=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_missing_dll.exe"
            "-DHANG=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hang.exe"
            "-DHELLO=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hello.exe"
            "-DLEGACY_APP=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_file.exe"
            "-DINSTALLED_APP=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_install_app.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/install-prefix-catalog-run"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_install_flow.cmake"
    )
    set_tests_properties(integration_install_prefix_catalog_run PROPERTIES
        DEPENDS "fixture_tl_install_app_metadata;fixture_tl_install_setup_metadata"
        LABELS "runtime;pe32plus;install"
    )

    add_test(
        NAME runtime_tl_crash_guest_signal
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_crash.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DEXPECTED_EXIT=71"
            "-DREQUIRED_TRACE=terminated category=\"guest-signal\""
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_rejection.cmake"
    )
    set_tests_properties(runtime_tl_crash_guest_signal PROPERTIES
        DEPENDS fixture_tl_crash_metadata
        LABELS "runtime;pe32plus"
    )

    add_test(
        NAME app_run_tl_crash_guest_signal
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_crash.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-crash-guest-signal"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-crash-guest-signal"
            "-DEXPECTED_EXIT=71"
            "-DREQUIRED_TRACE=terminated category=\"guest-signal\""
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
    )
    set_tests_properties(app_run_tl_crash_guest_signal PROPERTIES
        DEPENDS fixture_tl_crash_metadata
        LABELS "runtime;app-run;pe32plus"
    )

    # O crash log enriquecido reporta o si_addr capturado no filho; a fixture
    # desreferencia o endereço 0, então o campo aparece com valor 0x0 e os
    # campos de contexto PE (rva/seção/importação) ficam ausentes.
    add_test(
        NAME runtime_tl_crash_fault_address
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_crash.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DEXPECTED_EXIT=71"
            "-DREQUIRED_TRACE=fault-address=\"0x0\""
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_rejection.cmake"
    )
    set_tests_properties(runtime_tl_crash_fault_address PROPERTIES
        DEPENDS fixture_tl_crash_metadata
        LABELS "runtime;pe32plus"
    )

    add_test(
        NAME app_run_tl_crash_fault_address
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_crash.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-crash-fault-address"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-crash-fault-address"
            "-DEXPECTED_EXIT=71"
            "-DREQUIRED_TRACE=fault-address=\"0x0\""
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
    )
    set_tests_properties(app_run_tl_crash_fault_address PROPERTIES
        DEPENDS fixture_tl_crash_metadata
        LABELS "runtime;app-run;pe32plus"
    )

    add_test(
        NAME runtime_tl_hang_timeout
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hang.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DEXPECTED_EXIT=72"
            "-DREQUIRED_TRACE=terminated category=\"guest-timeout\""
            "-DEXTRA_ARGS=--timeout;1"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_rejection.cmake"
    )
    set_tests_properties(runtime_tl_hang_timeout PROPERTIES
        DEPENDS fixture_tl_hang_metadata
        LABELS "runtime;pe32plus"
    )

    add_test(
        NAME app_run_tl_hang_timeout
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hang.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-hang-timeout"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-hang-timeout"
            "-DEXPECTED_EXIT=72"
            "-DREQUIRED_TRACE=terminated category=\"guest-timeout\""
            "-DEXTRA_ARGS=--timeout;1"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
    )
    set_tests_properties(app_run_tl_hang_timeout PROPERTIES
        DEPENDS fixture_tl_hang_metadata
        LABELS "runtime;app-run;pe32plus"
    )

    add_test(
        NAME runtime_tl_hang_cpu_limit
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hang.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DEXPECTED_EXIT=73"
            "-DREQUIRED_TRACE=terminated category=\"guest-resource-limit\" resource=\"cpu\""
            "-DEXTRA_ARGS=--cpu;1"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_rejection.cmake"
    )
    set_tests_properties(runtime_tl_hang_cpu_limit PROPERTIES
        DEPENDS fixture_tl_hang_metadata
        LABELS "runtime;pe32plus;resource-limit"
    )

    add_test(
        NAME app_run_tl_hang_cpu_limit
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hang.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-hang-cpu-limit"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-hang-cpu-limit"
            "-DEXPECTED_EXIT=73"
            "-DREQUIRED_TRACE=terminated category=\"guest-resource-limit\" resource=\"cpu\""
            "-DEXTRA_ARGS=--cpu;1"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run_rejection.cmake"
    )
    set_tests_properties(app_run_tl_hang_cpu_limit PROPERTIES
        DEPENDS fixture_tl_hang_metadata
        LABELS "runtime;app-run;pe32plus;resource-limit"
    )

    add_test(
        NAME runtime_tl_memory_limit
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_memory_limit.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_memory_limit.cmake"
    )
    set_tests_properties(runtime_tl_memory_limit PROPERTIES
        DEPENDS fixture_tl_memory_limit_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;pe32plus;resource-limit"
    )

    add_test(
        NAME app_run_tl_memory_limit
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_memory_limit.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-memory-limit"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-memory-limit"
            "-DEXPECTED_HEX=6d656d6f72792d6c696d69740a"
            "-DEXTRA_ARGS=--memory;128"
            "-DREQUIRED_TRACE_CONTAINS=resource-limits inheritance=\"fork\" memory-mib=\"128\""
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
    )
    set_tests_properties(app_run_tl_memory_limit PROPERTIES
        DEPENDS fixture_tl_memory_limit_metadata
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;resource-limit"
    )

    add_test(
        NAME runtime_tl_process_cpu_limit_inherited
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_process_limit_parent.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DEXTRA_ARGS=--cpu;1;--timeout;4"
            "-DEXPECTED_HEX=70726f636573732d6c696d69742d696e686572697465640a"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_console_output.cmake"
    )
    set_tests_properties(runtime_tl_process_cpu_limit_inherited PROPERTIES
        DEPENDS "fixture_tl_process_limit_parent_metadata;fixture_tl_process_hang_metadata"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;pe32plus;resource-limit;process"
    )

    add_test(
        NAME app_run_tl_process_cpu_limit_inherited
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_process_limit_parent.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-tl-process-cpu-limit-inherited"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/app-run-tl-process-cpu-limit-inherited"
            "-DEXTRA_ARGS=--cpu;1;--timeout;4"
            "-DEXPECTED_HEX=70726f636573732d6c696d69742d696e686572697465640a"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_app_run.cmake"
    )
    set_tests_properties(app_run_tl_process_cpu_limit_inherited PROPERTIES
        DEPENDS "fixture_tl_process_limit_parent_metadata;fixture_tl_process_hang_metadata"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        LABELS "runtime;app-run;pe32plus;resource-limit;process"
    )

    add_test(
        NAME runtime_tl_hello_broken_pipe
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_FIXTURE_OUTPUT_DIRECTORY}/tl_hello.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_broken_pipe.cmake"
    )
    set_tests_properties(runtime_tl_hello_broken_pipe PROPERTIES
        DEPENDS fixture_tl_hello_metadata
        LABELS "runtime;pe32plus"
    )
endif()
