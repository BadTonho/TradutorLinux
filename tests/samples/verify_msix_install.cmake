if(NOT DEFINED RUNTIME OR NOT DEFINED HELLO OR NOT DEFINED WORK OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR "RUNTIME, HELLO, WORK and RUST_ENABLED are required")
endif()

file(REMOVE_RECURSE "${WORK}")
set(package_source "${WORK}/package-source")
set(package_zip "${WORK}/native-fixture.zip")
set(package "${WORK}/native-fixture.msix")
set(prefix "${WORK}/prefix")
set(config "${WORK}/config")
set(home "${WORK}/home")
file(MAKE_DIRECTORY "${package_source}/bin" "${home}")
file(COPY_FILE "${HELLO}" "${package_source}/bin/tl_hello.exe")
file(WRITE "${package_source}/AppxManifest.xml" [=[<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="TradutorLinux.NativeFixture" Publisher="CN=TradutorLinux" Version="1.0.0.0" />
  <Applications>
    <Application Id="App" Executable="bin/tl_hello.exe" EntryPoint="Windows.FullTrustApplication" />
  </Applications>
</Package>
]=])

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${package_zip}" --format=zip AppxManifest.xml bin/tl_hello.exe
    WORKING_DIRECTORY "${package_source}"
    RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "could not create native MSIX fixture: ${archive_result}")
endif()
file(RENAME "${package_zip}" "${package}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${home}" "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" --report "${package}"
    RESULT_VARIABLE report_result
    OUTPUT_VARIABLE report_stdout
    ERROR_VARIABLE report_stderr
)
if(NOT report_result EQUAL 0 OR NOT report_stdout MATCHES "result: package-recognized")
    message(FATAL_ERROR "MSIX report failed (${report_result})\nstdout:\n${report_stdout}\nstderr:\n${report_stderr}")
endif()
if(NOT report_stdout MATCHES "main-executable: bin/tl_hello.exe")
    message(FATAL_ERROR "MSIX report did not select the manifest executable:\n${report_stdout}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${home}" "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" --trace --report "${package}"
    RESULT_VARIABLE traced_report_result
    OUTPUT_VARIABLE traced_report_stdout
    ERROR_VARIABLE traced_report_stderr
)
if(NOT traced_report_result EQUAL 0 OR NOT traced_report_stdout STREQUAL "${report_stdout}")
    message(FATAL_ERROR
        "Traced MSIX report changed the report output (${traced_report_result})\n"
        "plain:\n${report_stdout}\ntraced:\n${traced_report_stdout}\n"
        "stderr:\n${traced_report_stderr}")
endif()
if(RUST_ENABLED)
    if(NOT traced_report_stderr MATCHES "\\[tl\\]\\[cli\\]\\[info\\] package-parse.*backend=\\\"rust\\\".*status=\\\"success\\\"")
        message(FATAL_ERROR "Rust package report trace was not emitted:\n${traced_report_stderr}")
    endif()
elseif(traced_report_stderr MATCHES "package-parse.*backend=\\\"rust\\\"")
    message(FATAL_ERROR "Rust package backend leaked into the OFF report:\n${traced_report_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${home}" "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" install "${package}" --name "TL MSIX Fixture"
        --prefix "${prefix}" --trace
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0 OR NOT install_stdout STREQUAL "")
    message(FATAL_ERROR "MSIX install failed (${install_result})\nstdout:\n${install_stdout}\nstderr:\n${install_stderr}")
endif()
if(NOT install_stderr MATCHES "\\[tl\\]\\[install\\]\\[info\\] extracted" OR
   NOT install_stderr MATCHES "\\[tl\\]\\[install\\]\\[info\\] registered")
    message(FATAL_ERROR "MSIX install did not report extraction and registration:\n${install_stderr}")
endif()
if(RUST_ENABLED)
    if(NOT install_stderr MATCHES "\\[tl\\]\\[install\\]\\[info\\] package-parse.*backend=\\\"rust\\\".*status=\\\"success\\\"")
        message(FATAL_ERROR "Rust package install trace was not emitted:\n${install_stderr}")
    endif()
elseif(install_stderr MATCHES "package-parse.*backend=\\\"rust\\\"")
    message(FATAL_ERROR "Rust package backend leaked into the OFF install:\n${install_stderr}")
endif()

set(extracted "${prefix}/drive_c/Program Files/tl_msix_fixture/bin/tl_hello.exe")
if(NOT EXISTS "${extracted}")
    message(FATAL_ERROR "MSIX executable was not extracted to the prefix: ${extracted}")
endif()
set(catalog "${config}/tradutorlinux/library.json")
if(NOT EXISTS "${catalog}")
    message(FATAL_ERROR "MSIX install did not create a catalog")
endif()
file(READ "${catalog}" catalog_content)
if(NOT catalog_content MATCHES "tl_msix_fixture" OR
   NOT catalog_content MATCHES "tl_hello.exe")
    message(FATAL_ERROR "catalog does not contain the extracted executable:\n${catalog_content}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${home}" "XDG_CONFIG_HOME=${config}"
        "${RUNTIME}" app run tl_msix_fixture --trace
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0 OR NOT run_stdout STREQUAL "Ola do Windows no Linux!\n")
    message(FATAL_ERROR "MSIX app run failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
