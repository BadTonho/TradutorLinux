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
find_program(TL_MINGW_WINDRES
    NAMES x86_64-w64-mingw32-windres windres
    REQUIRED
)
find_program(TL_LLVM_READOBJ
    NAMES llvm-readobj llvm-readobj-19 llvm-readobj-18 llvm-readobj-17
    REQUIRED
)
find_program(TL_PATCH_EXECUTABLE patch REQUIRED)

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

# Simple Todo C no commit bcdf3d5fcebb8c0b445edb791d54511194c1b6ca. O alvo
# recebe um overlay pequeno mantido pelo TradutorLinux: remove a opção de
# autorun exclusiva do Windows e reorganiza a tela para o fluxo Linux. A fonte
# upstream continua pinada; o app.rc é gerado no diretório de build porque o
# script original o cria durante a compilação.
set(_simple_todo_archive "${TL_TARGETS_SOURCE_DIR}/simple-todo-c-bcdf3d5.tar.gz")
if(NOT EXISTS "${_simple_todo_archive}")
    file(DOWNLOAD
        "https://github.com/Efeckc17/simple-todo-c/archive/bcdf3d5fcebb8c0b445edb791d54511194c1b6ca.tar.gz"
        "${_simple_todo_archive}"
        EXPECTED_HASH SHA256=fda0e4ebe292a8bdad2a72ccd02ba1c1c08a2b0610c42fee87389ac29a9565a5
        TLS_VERIFY ON
    )
endif()
set(_simple_todo_source_dir "${TL_TARGETS_SOURCE_DIR}/simple-todo-c-bcdf3d5fcebb8c0b445edb791d54511194c1b6ca")
if(NOT EXISTS "${_simple_todo_source_dir}/src/main.c")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_simple_todo_archive}"
        WORKING_DIRECTORY "${TL_TARGETS_SOURCE_DIR}"
        RESULT_VARIABLE _simple_todo_extract_result
    )
    if(NOT _simple_todo_extract_result EQUAL 0)
        message(FATAL_ERROR "Falha ao extrair Simple Todo C pinado")
    endif()
endif()
set(_simple_todo_rc "${_simple_todo_source_dir}/src/app.rc")
if(NOT EXISTS "${_simple_todo_rc}")
    file(WRITE "${_simple_todo_rc}" "1 24 \"app.manifest\"\n400 ICON \"../img/todo.ico\"\n")
endif()
set(_simple_todo_patch_stamp "${_simple_todo_source_dir}/.tradutorlinux-linux-overlay-applied")
if(NOT EXISTS "${_simple_todo_patch_stamp}")
    execute_process(
        COMMAND "${TL_PATCH_EXECUTABLE}" --batch --forward -p1
            -i "${CMAKE_CURRENT_SOURCE_DIR}/patches/simple_todo_linux.patch"
        WORKING_DIRECTORY "${_simple_todo_source_dir}"
        RESULT_VARIABLE _simple_todo_ui_patch_result
    )
    if(NOT _simple_todo_ui_patch_result EQUAL 0)
        message(FATAL_ERROR "Falha ao aplicar overlay visual Linux do Simple Todo")
    endif()
    execute_process(
        COMMAND "${TL_PATCH_EXECUTABLE}" --batch --forward -p1
            -i "${CMAKE_CURRENT_SOURCE_DIR}/patches/simple_todo_linux_autorun.patch"
        WORKING_DIRECTORY "${_simple_todo_source_dir}"
        RESULT_VARIABLE _simple_todo_autorun_patch_result
    )
    if(NOT _simple_todo_autorun_patch_result EQUAL 0)
        message(FATAL_ERROR "Falha ao remover autorun Windows do Simple Todo")
    endif()
    file(WRITE "${_simple_todo_patch_stamp}" "linux-overlay-v1\n")
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

# Simple Todo C (C puro, Win32, commit pinado).
set(_simple_todo_sources
    "${_simple_todo_source_dir}/src/main.c"
    "${_simple_todo_source_dir}/src/todo.c"
    "${_simple_todo_source_dir}/src/gui.c"
    "${_simple_todo_source_dir}/src/utils/search.c"
)
set(_simple_todo_resource "${TL_TARGETS_OUTPUT_DIR}/simple_todo.res")
add_custom_command(
    OUTPUT "${_simple_todo_resource}"
    COMMAND "${TL_MINGW_WINDRES}" -i "${_simple_todo_rc}" -O coff -o "${_simple_todo_resource}"
    DEPENDS
        "${_simple_todo_rc}"
        "${_simple_todo_source_dir}/src/app.manifest"
        "${_simple_todo_source_dir}/img/todo.ico"
    VERBATIM
    COMMENT "Compilando recursos do Simple Todo C"
)
add_custom_command(
    OUTPUT "${TL_TARGETS_OUTPUT_DIR}/simple_todo.exe"
    COMMAND "${TL_MINGW_CC}" ${_simple_todo_sources} "${_simple_todo_resource}"
        -o "${TL_TARGETS_OUTPUT_DIR}/simple_todo.exe"
        -mwindows -lcomctl32 -luxtheme -lshell32 -ladvapi32 -Os -s
    DEPENDS ${_simple_todo_sources} "${_simple_todo_resource}"
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/simple_todo_linux.patch"
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/simple_todo_linux_autorun.patch"
    VERBATIM
    COMMENT "Cross-compilando simple_todo.exe (Simple Todo C pinado)"
)

add_custom_target(tradutorlinux_target_apps ALL
    DEPENDS
        "${TL_TARGETS_OUTPUT_DIR}/xxd.exe"
        "${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
        "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
        "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
        "${TL_TARGETS_OUTPUT_DIR}/simple_todo.exe"
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
if(TARGET tradutorlinux)
    add_test(
        NAME targetapp_bzip2_run_compress
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DGOLDEN_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/golden/bzip2/input.bin"
            "-DGOLDEN_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/golden/bzip2/expected.txt"
            "-DGUEST_ARGS=-c"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_run_bytes.cmake"
    )
    set_tests_properties(targetapp_bzip2_run_compress PROPERTIES
        DEPENDS targetapp_bzip2_report
        LABELS "targetapp;runtime;pe32plus"
    )
    add_test(
        NAME targetapp_bzip2_run_decompress
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_TARGETS_OUTPUT_DIR}/bzip2.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DGOLDEN_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/golden/bzip2_decompress/input.bin"
            "-DGOLDEN_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/golden/bzip2_decompress/expected.txt"
            "-DGUEST_ARGS=-d;-c"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_run.cmake"
    )
    set_tests_properties(targetapp_bzip2_run_decompress PROPERTIES
        DEPENDS targetapp_bzip2_report
        LABELS "targetapp;runtime;pe32plus"
    )
endif()
tl_add_target_app_test(dos2unix "${TL_TARGETS_OUTPUT_DIR}/dos2unix.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/dos2unix.json")
tl_add_target_app_test(unix2dos "${TL_TARGETS_OUTPUT_DIR}/unix2dos.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/unix2dos.json")
tl_add_target_app_test(simple_todo "${TL_TARGETS_OUTPUT_DIR}/simple_todo.exe"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/simple_todo.json")

if(TARGET simple_todo_gui_smoke AND TARGET tradutorlinux AND TL_XVFB_EXECUTABLE)
    add_test(
        NAME targetapp_simple_todo_gui_smoke
        COMMAND simple_todo_gui_smoke
            "$<TARGET_FILE:tradutorlinux>"
            "${TL_TARGETS_OUTPUT_DIR}/simple_todo.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/simple_todo_gui_smoke_work"
    )
    set_tests_properties(targetapp_simple_todo_gui_smoke PROPERTIES
        DEPENDS targetapp_simple_todo_report
        LABELS "targetapp;runtime;pe32plus;gui"
        TIMEOUT 120
        SKIP_RETURN_CODE 77
    )
endif()

# Os arquivos oficiais de teste do dos2unix são pinados junto com a fonte do
# alvo. Eles fornecem entradas CRLF/LF e um nome UTF-8 sem duplicar fixtures do
# projeto; o diretório de teste vira o CWD do convidado para preservar caminhos
# relativos Windows.
function(tl_add_target_conversion_test target_name scenario guest_input golden_output working_directory)
    if(NOT TARGET tradutorlinux)
        return()
    endif()
    add_test(
        NAME targetapp_${target_name}_${scenario}
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${TL_TARGETS_OUTPUT_DIR}/${target_name}.exe"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DWORKING_DIRECTORY=${working_directory}"
            "-DGUEST_INPUT=${guest_input}"
            "-DGOLDEN_OUTPUT=${golden_output}"
            "-DGUEST_ARGS=-O"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_target_conversion.cmake"
    )
    set_tests_properties(targetapp_${target_name}_${scenario} PROPERTIES
        DEPENDS targetapp_${target_name}_report
        LABELS "targetapp;runtime;pe32plus"
    )
endfunction()

if(TARGET tradutorlinux)
    set(_dos2unix_test_dir "${_dos2unix_source_dir}/test")
    tl_add_target_conversion_test(
        dos2unix eol eol_dos.txt
        "${_dos2unix_test_dir}/eol_unix.txt" "${_dos2unix_test_dir}")
    tl_add_target_conversion_test(
        unix2dos eol eol_unix.txt
        "${_dos2unix_test_dir}/eol_dos.txt" "${_dos2unix_test_dir}")
    tl_add_target_conversion_test(
        dos2unix unicode-glob "uni_el_*.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/golden/dos2unix/uni_el_expected.txt"
        "${_dos2unix_test_dir}")
endif()
