if(NOT DEFINED RUNTIME OR NOT DEFINED HELLO OR NOT DEFINED WORK OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR "RUNTIME, HELLO, WORK and RUST_ENABLED are required")
endif()

if(NOT EXISTS "${RUNTIME}" OR NOT EXISTS "${HELLO}")
    message(FATAL_ERROR "Runtime or hello fixture is missing")
endif()

function(tl_assert_profile_parser trace expected_status description)
    string(REGEX MATCH "\\[tl\\]\\[runtime\\]\\[[^]]+\\] compat-profile[^\n]*" profile_line
        "${trace}")
    if(NOT profile_line)
        message(FATAL_ERROR "${description}: compat-profile trace was not emitted:\n${trace}")
    endif()
    if(RUST_ENABLED)
        if(expected_status STREQUAL "not-attempted")
            if(profile_line MATCHES "backend=\\\"rust\\\"" OR profile_line MATCHES "parser-status=")
                message(FATAL_ERROR "${description}: missing profile unexpectedly called Rust:\n${profile_line}")
            endif()
        elseif(NOT profile_line MATCHES "backend=\\\"rust\\\"" OR
               NOT profile_line MATCHES "parser-status=\\\"${expected_status}\\\"")
            message(FATAL_ERROR "${description}: Rust profile parser status was not traced:\n${profile_line}")
        endif()
        if(NOT expected_status STREQUAL "success" AND NOT expected_status STREQUAL "not-attempted")
            foreach(field IN ITEMS code phase input-offset detail-value)
                if(NOT profile_line MATCHES "${field}=\\\"[0-9]+\\\"")
                    message(FATAL_ERROR "${description}: structured field ${field} is missing:\n${profile_line}")
                endif()
            endforeach()
        endif()
    elseif(profile_line MATCHES "backend=\\\"rust\\\"" OR profile_line MATCHES "parser-status=")
        message(FATAL_ERROR "${description}: Rust profile fields leaked into the C++ baseline:\n${profile_line}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/config")
set(prefix "${WORK}/prefix")
set(profile "${prefix}/compat/profile.json")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app add "${HELLO}" --id compatfixture --name "Compat Fixture"
        --prefix "${prefix}"
    RESULT_VARIABLE add_result
    OUTPUT_VARIABLE add_stdout
    ERROR_VARIABLE add_stderr
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "app add failed (${add_result})\nstdout:\n${add_stdout}\nstderr:\n${add_stderr}")
endif()

file(WRITE "${profile}" "{\n  \"schema\": 1,\n  \"app_id\": \"compatfixture\",\n  \"files\": []\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compatfixture --trace=runtime
    RESULT_VARIABLE loaded_result
    OUTPUT_VARIABLE loaded_stdout
    ERROR_VARIABLE loaded_stderr
)
if(NOT loaded_result EQUAL 0 OR NOT loaded_stdout MATCHES "Ola do Windows no Linux!")
    message(FATAL_ERROR "loaded profile run failed (${loaded_result})\nstdout:\n${loaded_stdout}\nstderr:\n${loaded_stderr}")
endif()
if(NOT loaded_stderr MATCHES "compat-profile status=\"loaded\"" OR
   NOT loaded_stderr MATCHES "app-id=\"compatfixture\"" OR
   NOT loaded_stderr MATCHES "files=\"0\"")
    message(FATAL_ERROR "loaded profile was not traced:\n${loaded_stderr}")
endif()
tl_assert_profile_parser("${loaded_stderr}" "success" "loaded profile")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compatfixture --report --trace=runtime
    RESULT_VARIABLE report_result
    OUTPUT_VARIABLE report_stdout
    ERROR_VARIABLE report_stderr
)
if(NOT report_result EQUAL 0 OR NOT report_stdout MATCHES "TradutorLinux compatibility report")
    message(FATAL_ERROR "app run --report failed (${report_result})\nstdout:\n${report_stdout}\nstderr:\n${report_stderr}")
endif()
tl_assert_profile_parser("${report_stderr}" "success" "app run --report profile")

file(REMOVE "${profile}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compatfixture --trace=runtime
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_stdout
    ERROR_VARIABLE missing_stderr
)
if(NOT missing_result EQUAL 0 OR NOT missing_stdout MATCHES "Ola do Windows no Linux!")
    message(FATAL_ERROR "missing profile run failed (${missing_result})\nstdout:\n${missing_stdout}\nstderr:\n${missing_stderr}")
endif()
if(NOT missing_stderr MATCHES "compat-profile status=\"missing\"" OR
   NOT missing_stderr MATCHES "aviso: perfil de compatibilidade missing")
    message(FATAL_ERROR "missing profile fallback was not diagnosed:\n${missing_stderr}")
endif()
tl_assert_profile_parser("${missing_stderr}" "not-attempted" "missing profile")

file(WRITE "${profile}" "{\n  \"schema\": 1,\n  \"app_id\": \"other-app\",\n  \"files\": []\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${WORK}/home"
        "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" app run compatfixture --trace=runtime
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(NOT invalid_result EQUAL 0 OR NOT invalid_stdout MATCHES "Ola do Windows no Linux!")
    message(FATAL_ERROR "invalid profile run failed (${invalid_result})\nstdout:\n${invalid_stdout}\nstderr:\n${invalid_stderr}")
endif()
if(NOT invalid_stderr MATCHES "compat-profile status=\"invalid\"" OR
   NOT invalid_stderr MATCHES "aviso: perfil de compatibilidade invalid")
    message(FATAL_ERROR "invalid profile fallback was not diagnosed:\n${invalid_stderr}")
endif()
tl_assert_profile_parser("${invalid_stderr}" "malformed" "identity-mismatched profile")

file(REMOVE_RECURSE "${WORK}")
