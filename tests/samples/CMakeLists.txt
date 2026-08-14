if(NOT TL_BUILD_SAMPLES)
    return()
endif()

find_program(TL_MINGW_CC
    NAMES x86_64-w64-mingw32-gcc-win32 x86_64-w64-mingw32-gcc
    REQUIRED
)
find_program(TL_LLVM_READOBJ
    NAMES llvm-readobj llvm-readobj-18 llvm-readobj-17
    REQUIRED
)

set(TL_FIXTURE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

function(tl_add_pe_fixture target_name source_file manifest_file)
    set(options LINK_KERNEL32 RELOCATABLE)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    set(output_file "${TL_FIXTURE_OUTPUT_DIRECTORY}/${target_name}.exe")
    set(link_libraries)
    set(link_flags)
    if(ARG_LINK_KERNEL32)
        list(APPEND link_libraries -lkernel32)
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
            ${link_flags}
            ${link_libraries}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${manifest_file}"
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

    if(TARGET tradutorlinux)
        add_test(
            NAME runtime_${target_name}_matches_readobj
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_runtime_output.cmake"
        )
        set_tests_properties(runtime_${target_name}_matches_readobj PROPERTIES
            DEPENDS fixture_${target_name}_metadata
            LABELS "runtime;pe32plus"
        )
    endif()
endfunction()

add_custom_target(tradutorlinux_samples ALL)

tl_add_pe_fixture(tl_nop src/tl_nop.c manifests/tl_nop.json)
tl_add_pe_fixture(tl_hello src/tl_hello.c manifests/tl_hello.json LINK_KERNEL32)
tl_add_pe_fixture(tl_reloc src/tl_reloc.c manifests/tl_reloc.json RELOCATABLE)
