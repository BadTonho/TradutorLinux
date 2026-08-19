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
    set(options LINK_KERNEL32 LINK_USER32 LINK_GDI32 LINK_WS2_32 LINK_ADVAPI32 RELOCATABLE RUNTIME_FAILURE EXPECT_RESOLVED CONSOLE REPORT REPORT_ONLY NO_RUNTIME_CHECK)
    set(oneValueArgs REPORT_SYMBOL RESOURCE_FILE)
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
    if(ARG_LINK_WS2_32)
        list(APPEND link_libraries -lws2_32)
    endif()
    if(ARG_LINK_ADVAPI32)
        list(APPEND link_libraries -ladvapi32)
    endif()
    if(ARG_RELOCATABLE)
        list(APPEND link_flags -Wl,--dynamicbase)
    endif()

    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_FIXTURE_OUTPUT_DIRECTORY}"
        COMMAND "${TL_MINGW_CC}"
            -std=c17
            -ffreestanding
            -fno-stack-protector
            -fno-asynchronous-unwind-tables
            -fno-unwind-tables
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
            elseif(target_name STREQUAL "tl_thread")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=54687265616420646f6e650a54687265616420646f6e650a4d61696e20646f6e650a")
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
            elseif(target_name STREQUAL "tl_registry_unicode")
                set(runtime_extra_args
                    "-DEXPECTED_HEX=72656769737472790a")
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
            LABELS "runtime;pe32plus"
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
            LABELS "runtime;pe32plus"
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

tl_add_pe_fixture(tl_nop src/tl_nop.c manifests/tl_nop.json RELOCATABLE)
tl_add_pe_fixture(tl_hello src/tl_hello.c manifests/tl_hello.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)
tl_add_pe_fixture(tl_echo src/tl_echo.c manifests/tl_echo.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)
tl_add_pe_fixture(tl_file src/tl_file.c manifests/tl_file.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE)
tl_add_pe_fixture(tl_reloc src/tl_reloc.c manifests/tl_reloc.json RELOCATABLE)
tl_add_pe_fixture(tl_missing_dll src/tl_missing_dll.c manifests/tl_missing_dll.json
    LINK_USER32 RUNTIME_FAILURE RELOCATABLE)
tl_add_pe_fixture(tl_gui src/tl_gui.c manifests/tl_gui.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE)
tl_add_pe_fixture(tl_win src/tl_win.c manifests/tl_win.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExA")
tl_add_pe_fixture(tl_win2 src/tl_win2.c manifests/tl_win2.json
    LINK_KERNEL32 LINK_USER32 EXPECT_RESOLVED REPORT_ONLY RELOCATABLE
    REPORT_SYMBOL "RegisterClassExA")
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

tl_add_pe_fixture(tl_hang src/tl_hang.c manifests/tl_hang.json RELOCATABLE NO_RUNTIME_CHECK)

tl_add_pe_fixture(tl_thread src/tl_thread.c manifests/tl_thread.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE RELOCATABLE)

tl_add_pe_fixture(tl_resources src/tl_resources.c manifests/tl_resources.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "FindResourceW"
    RESOURCE_FILE src/tl_resources.rc)

tl_add_pe_fixture(tl_sync src/tl_sync.c manifests/tl_sync.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateEventA")

tl_add_pe_fixture(tl_files_wide src/tl_files_wide.c manifests/tl_files_wide.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "CreateFileW")

tl_add_pe_fixture(tl_process_child src/tl_process_child.c manifests/tl_process_child.json
    LINK_KERNEL32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
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

tl_add_pe_fixture(tl_registry_unicode src/tl_registry_unicode.c manifests/tl_registry_unicode.json
    LINK_KERNEL32 LINK_ADVAPI32 EXPECT_RESOLVED CONSOLE REPORT RELOCATABLE
    REPORT_SYMBOL "RegCreateKeyExW")

if(TARGET tradutorlinux)
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
