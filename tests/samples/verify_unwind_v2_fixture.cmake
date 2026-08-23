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
    message(FATAL_ERROR "--report da fixture unwind V2 falhou: ${result}\n${stderr}")
endif()
if(NOT stdout MATCHES "mechanism: x64-unwind \\([1-9][0-9]* functions, v1=[0-9][0-9]*, v2=[1-9][0-9]*, [1-9][0-9]* epilogs")
    message(FATAL_ERROR "relatório não classificou UNWIND_INFO V2:\n${stdout}")
endif()
if(NOT stderr MATCHES "\\[tl\\]\\[pe\\]\\[info\\] unwind functions=\\\"[1-9][0-9]*\\\" v1=\\\"[0-9][0-9]*\\\" v2=\\\"[1-9][0-9]*\\\" epilogs=\\\"[1-9][0-9]*\\\"")
    message(FATAL_ERROR "trace não expôs os totais V2 de unwind:\n${stderr}")
endif()
