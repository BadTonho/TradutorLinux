if(NOT DEFINED INPUT OR NOT DEFINED RUNTIME)
    message(FATAL_ERROR "INPUT e RUNTIME são obrigatórios")
endif()

execute_process(
    COMMAND "${RUNTIME}" --trace --report "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "--report da fixture unwind falhou: ${result}\n${stderr}")
endif()
if(NOT stdout MATCHES "mechanism: x64-unwind \\([1-9][0-9]* functions")
    message(FATAL_ERROR "relatório não expôs x64-unwind:\n${stdout}")
endif()
if(NOT stderr MATCHES "\\[tl\\]\\[pe\\]\\[info\\] unwind functions=\\\"")
    message(FATAL_ERROR "trace não expôs evento pe/unwind:\n${stderr}")
endif()
