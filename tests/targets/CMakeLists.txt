if(NOT TL_BUILD_TARGET_APPS)
    return()
endif()

find_program(TL_MINGW_CC
    NAMES x86_64-w64-mingw32-gcc-win32 x86_64-w64-mingw32-gcc
    REQUIRED
)
find_program(TL_MINGW_AR
    NAMES x86_64-w64-mingw32-ar x86_64-w64-mingw32-gcc-ar
    REQUIRED
)
find_program(TL_LLVM_READOBJ
    NAMES llvm-readobj llvm-readobj-18 llvm-readobj-17
    REQUIRED
)

# Aplicativos-alvo reais (Fase 8). Cada aplicativo é fixado por versão e a
# fonte é baixada uma única vez no diretório de build, com hash SHA-256
# verificado. Os imports são pinados nos manifests em manifests/ e protegidos
# por teste: qualquer mudança de toolchain que altere os imports quebra o CI.
#
# O --report é a fonte de verdade do catálogo: aceita result: supported (exit 0)
# ou result: unsupported (exit 5). Quando o runtime suporta o CRT do alvo, uma
# fixture dourada em golden/<alvo>/ habilita também a execução de ponta a ponta
# (targetapp_<alvo>_run*, ver tl_add_target_app_run_test abaixo).

set(TL_TARGETS_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/source")
set(TL_TARGETS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/out")
file(MAKE_DIRECTORY "${TL_TARGETS_SOURCE_DIR}" "${TL_TARGETS_OUTPUT_DIR}")

# --- Fontes pinadas --------------------------------------------------------

# xxd, do vim v9.2.0957 (arquivo único, sem dependências além do CRT).
set(_xxd_source "${TL_TARGETS_SOURCE_DIR}/xxd.c")
if(NOT EXISTS "${_xxd_source}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/vim/vim/v9.2.0957/src/xxd/xxd.c"
        "${_xxd_source}"
        EXPECTED_HASH SHA256=f4f9812f9b83651e30040c83bd722651cc4656eb95b86a852edd1cd60e9b2fcd
        TLS_VERIFY ON
    )
endif()

# bzip2 1.0.8 (empacotamento simples).
set(_bzip2_archive "${TL_TARGETS_SOURCE_DIR}/bzip2-1.0.8.tar.gz")
if(NOT EXISTS "${_bzip2_archive}")
    file(DOWNLOAD
        "https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz"
        "${_bzip2_archive}"
        EXPECTED_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
        TLS_VERIFY ON
    )
endif()
if(NOT EXISTS "${TL_TARGETS_SOURCE_DIR}/bzip2-1.0.8/README")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_bzip2_archive}"
        WORKING_DIRECTORY "${TL_TARGETS_SOURCE_DIR}"
    )
endif()

# dos2unix 7.5.6 (conversão de fim de linha; também gera unix2dos).
set(_dos2unix_archive "${TL_TARGETS_SOURCE_DIR}/dos2unix-7.5.6.tar.gz")
if(NOT EXISTS "${_dos2unix_archive}")
    file(DOWNLOAD
        "https://downloads.sourceforge.net/project/dos2unix/dos2unix/7.5.6/dos2unix-7.5.6.tar.gz"
        "${_dos2unix_archive}"
        EXPECTED_HASH SHA256=63650acbd0c7fa8623429bcbf93a888e3351a1cad0f556cf41876f5673dd7d0b
        TLS_VERIFY ON
    )
endif()
if(NOT EXISTS "${TL_TARGETS_SOURCE_DIR}/dos2unix-7.5.6/README.txt")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_dos2unix_archive}"
        WORKING_DIRECTORY "${TL_TARGETS_SOURCE_DIR}"
    )
endif()

# --- Build cruzado ----------------------------------------------------------

# xxd
add_custom_command(
    OUTPUT "${TL_TARGETS_OUTPUT_DIR}/xxd.exe"
    COMMAND "${TL_MINGW_CC}" -O2 -s
        -o "${TL_TARGETS_OUTPUT_DIR}/xxd.exe" "${_xxd_source}"
    DEPENDS "${_xxd_source}"
    VERBATIM
    COMMENT "Cross-compilando xxd.exe (vim v9.2.0957)"
)

# bzip2 (objetos, libbz2.a e binário, sem depender de make)
set(_bzip2_source_dir "${TL_TARGETS_SOURCE_DIR}/bzip2-1.0.8")
set(_bzip2_library_sources blocksort huffman crctable randtable compress decompress bzlib)
set(_bzip2_objects)
foreach(_source IN LISTS _bzip2_library_sources)
    add_custom_command(
        OUTPUT "${_bzip2_source_dir}/${_source}.o"
        COMMAND "${TL_MINGW_CC}" -O2
            -c "${_bzip2_source_dir}/${_source}.c" -o "${_bzip2_source_dir}/${_source}.o"
        DEPENDS "${_bzip2_source_dir}/${_source}.c"
        VERBATIM
    )
    list(APPEND _bzip2_objects "${_bzip2_source_dir}/${_source}.o")
endforeach()
add_custom_command(
    OUTPUT "${_bzip2_source_dir}/bzip2.o"
    COMMAND "${TL_MINGW_CC}" -O2
        -c "${_bzip2_source_dir}/bzip2.c" -o "${_bzip2_source_dir}/bzip2.o"
    DEPENDS "${_bzip2_source_dir}/bzip2.c"
    VERBATIM
)
add_custom_command(
    OUTPUT "${_bzip2_source_dir}/libbz2.a"
    COMMAND "${TL_MINGW_AR}" rcs "${_bzip2_source_dir}/libbz2.a" ${_bzip2_objects}
    DEPENDS ${_bzip2_objects}
    VERBATIM
)
add_custom_command(
    OUTPUT "${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
    COMMAND "${TL_MINGW_CC}" -s
        -o "${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
        "${_bzip2_source_dir}/bzip2.o" "${_bzip2_source_dir}/libbz2.a"
    DEPENDS "${_bzip2_source_dir}/bzip2.o" "${_bzip2_source_dir}/libbz2.a"
    VERBATIM
    COMMENT "Cross-compilando bzip2.exe (bzip2 1.0.8)"
)

# dos2unix / unix2dos (mesmos objetos, dois binários)
set(_dos2unix_source_dir "${TL_TARGETS_SOURCE_DIR}/dos2unix-7.5.6")
set(_dos2unix_sources common querycp dos2unix unix2dos)
set(_dos2unix_objects)
foreach(_source IN LISTS _dos2unix_sources)
    add_custom_command(
        OUTPUT "${_dos2unix_source_dir}/${_source}.o"
        COMMAND "${TL_MINGW_CC}" -O2
            -DD2U_UNIFILE
            "-DVER_REVISION=\"7.5.6\""
            "-DVER_DATE=\"2026-05-28\""
            "-DVER_AUTHOR=\"'Erwin Waterlander'\""
            -DDEBUG=0
            -c "${_dos2unix_source_dir}/${_source}.c" -o "${_dos2unix_source_dir}/${_source}.o"
        DEPENDS
            "${_dos2unix_source_dir}/${_source}.c"
            "${_dos2unix_source_dir}/common.h"
            "${_dos2unix_source_dir}/dos2unix.h"
            "${_dos2unix_source_dir}/querycp.h"
            "${_dos2unix_source_dir}/unix2dos.h"
        VERBATIM
    )
    list(APPEND _dos2unix_objects "${_dos2unix_source_dir}/${_source}.o")
endforeach()
add_custom_command(
    OUTPUT "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
    COMMAND "${TL_MINGW_CC}" -s
        -o "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
        "${_dos2unix_source_dir}/dos2unix.o" "${_dos2unix_source_dir}/querycp.o"
        "${_dos2unix_source_dir}/common.o"
    DEPENDS "${_dos2unix_source_dir}/dos2unix.o" "${_dos2unix_source_dir}/querycp.o"
        "${_dos2unix_source_dir}/common.o"
    VERBATIM
    COMMENT "Cross-compilando dos2unix.exe (dos2unix 7.5.6)"
)
add_custom_command(
    OUTPUT "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
    COMMAND "${TL_MINGW_CC}" -s
        -o "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
        "${_dos2unix_source_dir}/unix2dos.o" "${_dos2unix_source_dir}/querycp.o"
        "${_dos2unix_source_dir}/common.o"
    DEPENDS "${_dos2unix_source_dir}/unix2dos.o" "${_dos2unix_source_dir}/querycp.o"
        "${_dos2unix_source_dir}/common.o"
    VERBATIM
    COMMENT "Cross-compilando unix2dos.exe (dos2unix 7.5.6)"
)

add_custom_target(tradutorlinux_target_apps ALL
    DEPENDS
        "${TL_TARGETS_OUTPUT_DIR}/xxd.exe"
        "${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
        "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
        "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
)

# --- Testes de verificação ---------------------------------------------------

function(tl_add_target_app_test target_name output_file manifest_file)
    add_test(
        NAME targetapp_${target_name}_metadata
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${output_file}"
            "-DMANIFEST=${manifest_file}"
            "-DLLVM_READOBJ=${TL_LLVM_READOBJ}"
            -P "${CMAKE_SOURCE_DIR}/tests/samples/verify_fixture.cmake"
    )
    set_tests_properties(targetapp_${target_name}_metadata PROPERTIES
        LABELS "targetapp;pe32plus"
    )

    if(TARGET tradutorlinux)
        add_test(
            NAME targetapp_${target_name}_report
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${output_file}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DMANIFEST=${manifest_file}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_report.cmake"
        )
        set_tests_properties(targetapp_${target_name}_report PROPERTIES
            DEPENDS targetapp_${target_name}_metadata
            LABELS "targetapp;runtime;pe32plus"
        )
    endif()
endfunction()

# Execução e2e de ponta a ponta. O convidado é executado de verdade e o stdout
# deve ser byte-idêntico ao ouro fixado em tests/targets/golden/. Requer que o
# runtime já suporte o CRT (Fase 9+) e só é adicionado para alvos com fixture
# dourada e manifesta de imports resolvidos (result: supported).
function(tl_add_target_app_run_test target_name output_file)
    set(_golden_dir "${CMAKE_CURRENT_SOURCE_DIR}/golden/${target_name}")
    if(NOT EXISTS "${_golden_dir}")
        return()
    endif()
    if(NOT TARGET tradutorlinux)
        return()
    endif()

    add_test(
        NAME targetapp_${target_name}_run
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${output_file}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DGOLDEN_INPUT=${_golden_dir}/input.bin"
            "-DGOLDEN_OUTPUT=${_golden_dir}/expected.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_run.cmake"
    )
    set_tests_properties(targetapp_${target_name}_run PROPERTIES
        DEPENDS targetapp_${target_name}_report
        LABELS "targetapp;runtime;pe32plus"
    )

    add_test(
        NAME targetapp_${target_name}_run_plain
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${output_file}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DGOLDEN_INPUT=${_golden_dir}/input.bin"
            "-DGOLDEN_OUTPUT=${_golden_dir}/expected_plain.txt"
            "-DGUEST_ARGS=-p"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_run.cmake"
    )
    set_tests_properties(targetapp_${target_name}_run_plain PROPERTIES
        DEPENDS targetapp_${target_name}_run
        LABELS "targetapp;runtime;pe32plus"
    )

    add_test(
        NAME targetapp_${target_name}_run_missing_input
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${output_file}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DGOLDEN_INPUT=${_golden_dir}/does-not-exist.bin"
            "-DGOLDEN_OUTPUT=${_golden_dir}/expected_missing.txt"
            "-DEXPECTED_EXIT=2"
            "-DALLOW_MISSING_GOLDEN_INPUT=TRUE"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_run.cmake"
    )
    set_tests_properties(targetapp_${target_name}_run_missing_input PROPERTIES
        DEPENDS targetapp_${target_name}_run
        LABELS "targetapp;runtime;pe32plus"
    )
endfunction()

tl_add_target_app_test(xxd      "${TL_TARGETS_OUTPUT_DIR}/xxd.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/xxd.json")
tl_add_target_app_run_test(xxd   "${TL_TARGETS_OUTPUT_DIR}/xxd.exe")
tl_add_target_app_test(bzip2    "${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/bzip2.json")
tl_add_target_app_test(dos2unix "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/dos2unix.json")
tl_add_target_app_test(unix2dos "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/unix2dos.json")
