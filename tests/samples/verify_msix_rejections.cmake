cmake_policy(SET CMP0053 NEW)

if(NOT DEFINED RUNTIME OR NOT DEFINED HELLO OR NOT DEFINED WORK OR NOT DEFINED RUST_ENABLED)
    message(FATAL_ERROR "RUNTIME, HELLO, WORK and RUST_ENABLED are required")
endif()

if(NOT EXISTS "${RUNTIME}" OR NOT EXISTS "${HELLO}")
    message(FATAL_ERROR "Runtime or fixture executable is missing")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/source" "${WORK}/config" "${WORK}/home")

set(source "${WORK}/source")
file(MAKE_DIRECTORY "${source}/bin")
file(COPY_FILE "${HELLO}" "${source}/bin/tl_hello.exe")

function(make_zip output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${output}.zip" --format=zip ${ARGN}
        WORKING_DIRECTORY "${source}"
        RESULT_VARIABLE archive_result
    )
    if(NOT archive_result EQUAL 0)
        message(FATAL_ERROR "Could not create rejection fixture ${output}: ${archive_result}")
    endif()
    file(RENAME "${output}.zip" "${output}")
endfunction()

file(WRITE "${source}/AppxManifest.xml" [=[<?xml version="1.0"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="TradutorLinux.RejectionFixture" Publisher="CN=TradutorLinux" Version="1.0.0.0" />
  <Applications><Application Id="App" Executable="bin/tl_hello.exe" /></Applications>
</Package>
]=])
make_zip("${WORK}/valid.msix" AppxManifest.xml bin/tl_hello.exe)

file(READ "${WORK}/valid.msix" truncated LIMIT 12)
file(WRITE "${WORK}/truncated.msix" "${truncated}")

file(WRITE "${source}/AppxManifest.xml" "<Package><Applications>")
make_zip("${WORK}/malformed.msix" AppxManifest.xml bin/tl_hello.exe)

file(REMOVE "${source}/AppxManifest.xml")
make_zip("${WORK}/missing-manifest.msix" bin/tl_hello.exe)

file(WRITE "${source}/AppxBundleManifest.xml" "bundle")
make_zip("${WORK}/bundle.msixbundle" AppxBundleManifest.xml)
file(REMOVE "${source}/AppxBundleManifest.xml")

function(run_report input expected_exit expected_status label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "HOME=${WORK}/home" "XDG_CONFIG_HOME=${WORK}/config"
            "${RUNTIME}" --trace --report "${input}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE trace
    )
    if(NOT result EQUAL expected_exit)
        message(FATAL_ERROR "${label}: expected exit ${expected_exit}, got ${result}\nstdout:\n${output}\nstderr:\n${trace}")
    endif()
    if(NOT output STREQUAL "")
        message(FATAL_ERROR "${label}: rejected report wrote to stdout:\n${output}")
    endif()
    if(RUST_ENABLED)
        if(NOT trace MATCHES "package-parse.*backend=\\\"rust\\\".*status=\\\"${expected_status}\\\"")
            message(FATAL_ERROR "${label}: missing structured Rust rejection trace:\n${trace}")
        endif()
        if(NOT trace MATCHES "package-parse.*code=\\\"[0-9]+\\\".*phase=\\\"[0-9]+\\\".*input-offset=\\\"[0-9]+\\\".*detail-value=\\\"[0-9]+\\\"")
            message(FATAL_ERROR "${label}: missing structured Rust error fields:\n${trace}")
        endif()
    elseif(trace MATCHES "package-parse.*backend=\\\"rust\\\"")
        message(FATAL_ERROR "${label}: Rust backend leaked into OFF build:\n${trace}")
    endif()
endfunction()

if(RUST_ENABLED)
    set(expected_bundle_exit 5)
    set(expected_bundle_status unsupported-format)
else()
    set(expected_bundle_exit 4)
    set(expected_bundle_status malformed)
endif()

run_report("${WORK}/truncated.msix" 4 truncated truncated-package)
run_report("${WORK}/malformed.msix" 4 malformed malformed-manifest)
run_report("${WORK}/missing-manifest.msix" 4 malformed missing-manifest)
run_report("${WORK}/bundle.msixbundle" ${expected_bundle_exit} ${expected_bundle_status} bundle)

set(prefix "${WORK}/prefix")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${WORK}/home" "XDG_CONFIG_HOME=${WORK}/config"
        "${RUNTIME}" install "${WORK}/malformed.msix" --name "Rejected MSIX"
        --prefix "${prefix}" --trace
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_trace
)
if(NOT install_result EQUAL 4 OR NOT install_stdout STREQUAL "")
    message(FATAL_ERROR "malformed install did not fail before registration (${install_result})\nstdout:\n${install_stdout}\nstderr:\n${install_trace}")
endif()
if(RUST_ENABLED AND NOT install_trace MATCHES "package-parse.*backend=\\\"rust\\\".*status=\\\"malformed\\\"")
    message(FATAL_ERROR "malformed install did not expose Rust package parsing:\n${install_trace}")
endif()
if(EXISTS "${WORK}/config/tradutorlinux/library.json")
    message(FATAL_ERROR "malformed package created a catalog entry")
endif()
