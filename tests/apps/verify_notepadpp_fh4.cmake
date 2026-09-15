if(NOT DEFINED TL_RUNTIME OR NOT DEFINED TL_TARGET OR NOT DEFINED TL_STAGING_ROOT)
    message(FATAL_ERROR "TL_RUNTIME, TL_TARGET e TL_STAGING_ROOT são obrigatórios")
endif()
if(NOT DEFINED TL_CPU_SECONDS)
    set(TL_CPU_SECONDS 3)
endif()

if(NOT EXISTS "${TL_RUNTIME}")
    message(FATAL_ERROR "runtime inexistente: ${TL_RUNTIME}")
endif()
if(NOT EXISTS "${TL_TARGET}")
    message(FATAL_ERROR "alvo inexistente: ${TL_TARGET}")
endif()
get_filename_component(TL_TARGET_DIR "${TL_TARGET}" DIRECTORY)

# Este cenário valida o despacho C++/SEH do executável real sem depender de um
# servidor X11. O próprio Notepad++ percorre os catches FH4 durante a falha
# controlada de criação da janela e deve sair pelo ExitProcess convidado.
set(ENV{DISPLAY})
set(ENV{WAYLAND_DISPLAY})
set(prefix "${TL_STAGING_ROOT}/prefix")
set(appdata "${TL_STAGING_ROOT}/appdata")
file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
file(MAKE_DIRECTORY "${prefix}" "${appdata}")
set(ENV{TL_PREFIX} "${prefix}")
set(ENV{APPDATA} "${appdata}")

execute_process(
    COMMAND "${TL_RUNTIME}" --trace=runtime,process --timeout 8 --cpu "${TL_CPU_SECONDS}" --memory 512
        "${TL_TARGET}"
    WORKING_DIRECTORY "${TL_TARGET_DIR}"
    TIMEOUT 30
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)

if(NOT "${actual_exit}" STREQUAL "0")
    file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
    message(FATAL_ERROR
        "Notepad++ FH4: exit esperado 0, obtido ${actual_exit}\n"
        "stdout:\n${run_stdout}\n"
        "stderr:\n${run_stderr}")
endif()
if(NOT "${run_stdout}" STREQUAL "")
    file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
    message(FATAL_ERROR "Notepad++ FH4: stdout inesperado:\n${run_stdout}")
endif()

foreach(marker
        "cxx-eh state=\"matched\" detail=\"fh4-catch-typed\""
        "ExitProcess symbol=\"ExitProcess\" exit-code=\"0\""
        "cxx-eh state=\"search\" detail=\"fh4-cleanup-limit\"")
    string(FIND "${run_stderr}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "Notepad++ FH4: diagnóstico esperado não encontrado: ${marker}\n"
            "stderr:\n${run_stderr}")
    endif()
endforeach()

string(REGEX MATCHALL "cxx-eh state=\"matched\" detail=\"fh4-catch-typed\"" typed_catches
    "${run_stderr}")
list(LENGTH typed_catches typed_catch_count)
if(NOT typed_catch_count EQUAL 2)
    file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
    message(FATAL_ERROR
        "Notepad++ FH4: esperados dois catches tipados, obtidos ${typed_catch_count}\n"
        "stderr:\n${run_stderr}")
endif()

foreach(uncontrolled "guest-signal" "guest-timeout")
    string(FIND "${run_stderr}" "${uncontrolled}" uncontrolled_position)
    if(NOT uncontrolled_position EQUAL -1)
        file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
        message(FATAL_ERROR
            "Notepad++ FH4: término não controlado (${uncontrolled}):\n${run_stderr}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TL_STAGING_ROOT}")
message(STATUS "Notepad++ FH4 headless: catches tipados e saída controlada confirmados")
