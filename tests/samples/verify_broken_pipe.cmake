if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME)
    message(FATAL_ERROR "INPUT and RUNTIME are required")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Fixture was not generated: ${INPUT}")
endif()

find_program(TL_BASH bash REQUIRED)

# O convidado escreve em um pipe cujo lado de leitura já foi fechado (true
# terminou antes do runtime iniciar), então o write falha com EPIPE de forma
# determinística. Antes da correção do SIGPIPE o convidado morria pelo sinal e
# o runtime retornava 71; o comportamento correto é a falha controlada do
# WriteFile e o término normal do convidado.
execute_process(
    COMMAND "${TL_BASH}" -c
        "exec 9> >(true); \"\$0\" --trace \"\$1\" 1>&9; exit \$?"
        "${RUNTIME}" "${INPUT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_trace
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR "Runtime returned ${runtime_result} (expected 0) for ${INPUT}\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}" "guest-signal" guest_signal_position)
if(NOT guest_signal_position EQUAL -1)
    message(FATAL_ERROR "Guest died by signal instead of a controlled write failure:\n${runtime_trace}")
endif()

string(FIND "${runtime_trace}" "win32-error=\"109\"" broken_pipe_position)
if(broken_pipe_position EQUAL -1)
    message(FATAL_ERROR "'win32-error=\"109\"' (ERROR_BROKEN_PIPE) not found in trace for ${INPUT}:\n${runtime_trace}")
endif()