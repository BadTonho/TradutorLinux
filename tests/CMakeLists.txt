add_subdirectory(samples)

add_executable(seven_zip_cli_smoke
    apps/7zip/7z_cli_smoke.cpp
)
tl_enable_warnings(seven_zip_cli_smoke)
tl_enable_sanitizers(seven_zip_cli_smoke)

set(TL_POPULAR_APPS_DIR "" CACHE PATH
    "Diretório opcional do corpus Aplicativos_Windows_Populares")
if(TL_POPULAR_APPS_DIR AND TARGET tradutorlinux)
    set(TL_7Z_EXECUTABLE "${TL_POPULAR_APPS_DIR}/7z_x64.exe")
    if(EXISTS "${TL_7Z_EXECUTABLE}" AND EXISTS "${TL_POPULAR_APPS_DIR}/7z.dll")
        add_test(
            NAME seven_zip_cli_smoke
            COMMAND seven_zip_cli_smoke
                "$<TARGET_FILE:tradutorlinux>"
                "${TL_7Z_EXECUTABLE}"
        )
        set_tests_properties(seven_zip_cli_smoke PROPERTIES
            LABELS "runtime;apps;7zip;dll;archive"
            TIMEOUT 60
        )
    endif()

    add_test(
        NAME popular_apps_report_matrix
        COMMAND ${CMAKE_COMMAND}
            "-DTL_RUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DTL_CORPUS=${TL_POPULAR_APPS_DIR}"
            -P "${PROJECT_SOURCE_DIR}/tests/apps/verify_popular_apps_report.cmake"
    )
    set_tests_properties(popular_apps_report_matrix PROPERTIES
        LABELS "runtime;apps;report;corpus"
        TIMEOUT 900
    )

    add_test(
        NAME popular_apps_native_matrix
        COMMAND ${CMAKE_COMMAND}
            "-DTL_RUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DTL_CORPUS=${TL_POPULAR_APPS_DIR}"
            "-DTL_STAGING_ROOT=${CMAKE_BINARY_DIR}/popular-apps-native-matrix"
            -P "${PROJECT_SOURCE_DIR}/tests/apps/verify_popular_apps_native.cmake"
    )
    set_tests_properties(popular_apps_native_matrix PROPERTIES
        LABELS "runtime;apps;native;corpus"
        TIMEOUT 180
    )

    add_test(
        NAME popular_apps_install_matrix
        COMMAND ${CMAKE_COMMAND}
            "-DTL_RUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DTL_CORPUS=${TL_POPULAR_APPS_DIR}"
            "-DTL_STAGING_ROOT=${CMAKE_BINARY_DIR}/popular-apps-install-matrix"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${PROJECT_SOURCE_DIR}/tests/apps/verify_popular_apps_install.cmake"
    )
    set_tests_properties(popular_apps_install_matrix PROPERTIES
        LABELS "runtime;apps;install;corpus"
        TIMEOUT 180
    )
endif()

add_executable(tl_rust_pe_contract_c rust_pe_contract_c.c)
target_include_directories(tl_rust_pe_contract_c PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_pe_contract_c)
tl_enable_sanitizers(tl_rust_pe_contract_c)
add_test(NAME rust_pe_contract_c COMMAND tl_rust_pe_contract_c)
set_tests_properties(rust_pe_contract_c PROPERTIES
    LABELS "ffi;rust;contract"
    TIMEOUT 30
)

add_executable(tl_rust_msix_contract_c rust_msix_contract_c.c)
target_include_directories(tl_rust_msix_contract_c PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_msix_contract_c)
tl_enable_sanitizers(tl_rust_msix_contract_c)
add_test(NAME rust_msix_contract_c COMMAND tl_rust_msix_contract_c)
set_tests_properties(rust_msix_contract_c PROPERTIES
    LABELS "ffi;rust;contract;msix"
    TIMEOUT 30
)

add_executable(tl_rust_msix_contract_cpp rust_msix_contract_cpp.cpp)
target_include_directories(tl_rust_msix_contract_cpp PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_msix_contract_cpp)
tl_enable_sanitizers(tl_rust_msix_contract_cpp)
add_test(NAME rust_msix_contract_cpp COMMAND tl_rust_msix_contract_cpp)
set_tests_properties(rust_msix_contract_cpp PROPERTIES
    LABELS "ffi;rust;contract;msix"
    TIMEOUT 30
)

add_executable(tl_rust_profile_contract_c rust_profile_contract_c.c)
target_include_directories(tl_rust_profile_contract_c PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_profile_contract_c)
tl_enable_sanitizers(tl_rust_profile_contract_c)
add_test(NAME rust_profile_contract_c COMMAND tl_rust_profile_contract_c)
set_tests_properties(rust_profile_contract_c PROPERTIES
    LABELS "ffi;rust;contract;profile"
    TIMEOUT 30
)

add_executable(tl_rust_profile_contract_cpp rust_profile_contract_cpp.cpp)
target_include_directories(tl_rust_profile_contract_cpp PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_profile_contract_cpp)
tl_enable_sanitizers(tl_rust_profile_contract_cpp)
add_test(NAME rust_profile_contract_cpp COMMAND tl_rust_profile_contract_cpp)
set_tests_properties(rust_profile_contract_cpp PROPERTIES
    LABELS "ffi;rust;contract;profile"
    TIMEOUT 30
)

add_executable(tl_rust_app_catalog_contract_c rust_app_catalog_contract_c.c)
target_include_directories(tl_rust_app_catalog_contract_c PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_app_catalog_contract_c)
tl_enable_sanitizers(tl_rust_app_catalog_contract_c)
add_test(NAME rust_app_catalog_contract_c COMMAND tl_rust_app_catalog_contract_c)
set_tests_properties(rust_app_catalog_contract_c PROPERTIES
    LABELS "ffi;rust;contract;catalog"
    TIMEOUT 30
)

add_executable(tl_rust_app_catalog_contract_cpp rust_app_catalog_contract_cpp.cpp)
target_include_directories(tl_rust_app_catalog_contract_cpp PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
tl_enable_warnings(tl_rust_app_catalog_contract_cpp)
tl_enable_sanitizers(tl_rust_app_catalog_contract_cpp)
add_test(NAME rust_app_catalog_contract_cpp COMMAND tl_rust_app_catalog_contract_cpp)
set_tests_properties(rust_app_catalog_contract_cpp PROPERTIES
    LABELS "ffi;rust;contract;catalog"
    TIMEOUT 30
)

if(TL_BUILD_RUST)
    add_executable(tl_rust_ffi_probe rust_ffi_probe.cpp)
    add_dependencies(tl_rust_ffi_probe tl_rust_validator_library)
    target_link_libraries(tl_rust_ffi_probe PRIVATE
        tl_rust_validator
        tl_rust_msix_deflate
    )
    tl_enable_warnings(tl_rust_ffi_probe)
    tl_enable_sanitizers(tl_rust_ffi_probe)
    add_test(NAME rust_ffi_probe COMMAND tl_rust_ffi_probe)
    set_tests_properties(rust_ffi_probe PROPERTIES
        LABELS "ffi;rust"
        TIMEOUT 30
    )

    find_program(TL_RUSTUP_TEST_EXECUTABLE NAMES rustup REQUIRED)
    set(TL_RUST_CARGO_TEST_TARGET_DIRECTORY
        "${CMAKE_BINARY_DIR}/rust_ffi/cargo-target")
    add_test(
        NAME rust_cargo_tests
        COMMAND "${TL_RUSTUP_TEST_EXECUTABLE}" run "${TL_RUST_TOOLCHAIN}" cargo test
            --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
            --target-dir "${TL_RUST_CARGO_TEST_TARGET_DIRECTORY}"
            --locked --offline
    )
    set_tests_properties(rust_cargo_tests PROPERTIES
        LABELS "ffi;rust;toolchain"
        TIMEOUT 120
        ENVIRONMENT "CARGO_TERM_COLOR=never"
    )
    add_test(
        NAME rust_cargo_clippy
        COMMAND "${TL_RUSTUP_TEST_EXECUTABLE}" run "${TL_RUST_TOOLCHAIN}" cargo clippy
            --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
            --target-dir "${TL_RUST_CARGO_TEST_TARGET_DIRECTORY}"
            --locked --offline --all-targets -- -D warnings
    )
    set_tests_properties(rust_cargo_clippy PROPERTIES
        LABELS "ffi;rust;toolchain;static-analysis"
        TIMEOUT 120
        ENVIRONMENT "CARGO_TERM_COLOR=never"
    )

    add_executable(tl_rust_path_validation
        test_rust_path_validation.cpp
        ../src/compat/path_rules.cpp
        ../src/compat/rust_path_validator.cpp
    )
    target_include_directories(tl_rust_path_validation PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src/compat"
    )
    target_link_libraries(tl_rust_path_validation PRIVATE
        tl_rust_validator
        tl_rust_msix_deflate
        GTest::gtest_main
    )
    tl_enable_warnings(tl_rust_path_validation)
    tl_enable_sanitizers(tl_rust_path_validation)
    add_test(NAME rust_path_validation COMMAND tl_rust_path_validation)
    set_tests_properties(rust_path_validation PROPERTIES
        LABELS "ffi;rust"
        TIMEOUT 30
    )
endif()

find_program(TL_XVFB_EXECUTABLE NAMES Xvfb xvfb)
if(TL_XVFB_EXECUTABLE)
    find_path(TL_X11_INCLUDE_DIR X11/Xlib.h REQUIRED)
    find_library(TL_X11_LIBRARY X11 REQUIRED)
    add_executable(simple_todo_gui_smoke
        gui/simple_todo_gui_smoke.cpp
    )
    target_link_libraries(simple_todo_gui_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(simple_todo_gui_smoke)

    add_executable(x11_popup_smoke
        gui/x11_popup_smoke.cpp
    )
    target_include_directories(x11_popup_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(x11_popup_smoke PRIVATE tradutorlinux_core "${TL_X11_LIBRARY}")
    tl_enable_warnings(x11_popup_smoke)
    tl_enable_sanitizers(x11_popup_smoke)
    add_test(NAME x11_popup_smoke COMMAND x11_popup_smoke)
    set_tests_properties(x11_popup_smoke PROPERTIES
        LABELS "runtime;gui;x11"
        TIMEOUT 20
        SKIP_RETURN_CODE 77
    )
    if(TL_ENABLE_SANITIZERS)
        set_tests_properties(x11_popup_smoke PROPERTIES
            ENVIRONMENT
                "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;LSAN_OPTIONS=detect_leaks=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        )
    endif()

    add_executable(seven_zip_smoke
        gui/seven_zip_smoke.cpp
    )
    target_include_directories(seven_zip_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(seven_zip_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(seven_zip_smoke)
    tl_enable_sanitizers(seven_zip_smoke)

    add_executable(putty_smoke
        apps/putty/putty_smoke.cpp
    )
    target_include_directories(putty_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(putty_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(putty_smoke)
    tl_enable_sanitizers(putty_smoke)

    add_executable(putty_ssh_smoke
        apps/putty/putty_ssh_smoke.cpp
    )
    target_include_directories(putty_ssh_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(putty_ssh_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(putty_ssh_smoke)
    tl_enable_sanitizers(putty_ssh_smoke)

    add_executable(winrar_sfx_smoke
        apps/winrar/winrar_sfx_smoke.cpp
    )
    target_include_directories(winrar_sfx_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(winrar_sfx_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(winrar_sfx_smoke)
    tl_enable_sanitizers(winrar_sfx_smoke)

    add_executable(notepadpp_smoke
        apps/notepadpp/notepadpp_smoke.cpp
    )
    target_include_directories(notepadpp_smoke PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(notepadpp_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(notepadpp_smoke)
    tl_enable_sanitizers(notepadpp_smoke)

    if(TL_POPULAR_APPS_DIR AND TARGET tradutorlinux)
        set(TL_7ZFM_EXECUTABLE "${TL_POPULAR_APPS_DIR}/7zFM_x64.exe")
        set(TL_PUTTY_EXECUTABLE "${TL_POPULAR_APPS_DIR}/putty_x64.exe")
        set(TL_WINRAR_EXECUTABLE "${TL_POPULAR_APPS_DIR}/WinRAR_x64.exe")
        set(TL_NOTEPADPP_EXECUTABLE "${TL_POPULAR_APPS_DIR}/Notepad++/notepad++.exe")

        if(EXISTS "${TL_7ZFM_EXECUTABLE}")
            add_test(
                NAME seven_zip_gui_smoke
                COMMAND seven_zip_smoke
                    "$<TARGET_FILE:tradutorlinux>"
                    "${TL_7ZFM_EXECUTABLE}"
            )
            set_tests_properties(seven_zip_gui_smoke PROPERTIES
                LABELS "runtime;apps;gui;7zip"
                TIMEOUT 60
                SKIP_RETURN_CODE 77
            )
        endif()

        if(EXISTS "${TL_PUTTY_EXECUTABLE}")
            add_test(
                NAME putty_real_gui_smoke
                COMMAND putty_smoke
                    "$<TARGET_FILE:tradutorlinux>"
                    "${TL_PUTTY_EXECUTABLE}"
            )
            set_tests_properties(putty_real_gui_smoke PROPERTIES
                LABELS "runtime;apps;gui;putty"
                TIMEOUT 60
                SKIP_RETURN_CODE 77
            )
            add_test(
                NAME putty_ssh_local_probe
                COMMAND putty_ssh_smoke
                    "$<TARGET_FILE:tradutorlinux>"
                    "${TL_PUTTY_EXECUTABLE}"
            )
            set_tests_properties(putty_ssh_local_probe PROPERTIES
                LABELS "runtime;apps;gui;network;putty"
                TIMEOUT 45
                SKIP_RETURN_CODE 77
            )
        endif()

        if(EXISTS "${TL_WINRAR_EXECUTABLE}")
            add_test(
                NAME winrar_sfx_real_smoke
                COMMAND winrar_sfx_smoke
                    "$<TARGET_FILE:tradutorlinux>"
                    "${TL_WINRAR_EXECUTABLE}"
            )
            set_tests_properties(winrar_sfx_real_smoke PROPERTIES
                LABELS "runtime;apps;gui;winrar"
                TIMEOUT 60
                SKIP_RETURN_CODE 77
            )
        endif()

        if(EXISTS "${TL_NOTEPADPP_EXECUTABLE}")
            add_test(
                NAME notepadpp_real_gui_smoke
                COMMAND notepadpp_smoke
                    "$<TARGET_FILE:tradutorlinux>"
                    "${TL_NOTEPADPP_EXECUTABLE}"
            )
            set_tests_properties(notepadpp_real_gui_smoke PROPERTIES
                LABELS "runtime;apps;gui;notepadpp"
                TIMEOUT 90
                SKIP_RETURN_CODE 77
            )
        endif()
    endif()
endif()

if(TL_BUILD_TARGET_APPS)
    add_subdirectory(targets)
endif()

add_executable(tradutorlinux_unit_tests
    test_abi.cpp
    test_backend_proton.cpp
    test_cli.cpp
    test_compat_materializer.cpp
    test_compat_profile.cpp
    test_crash_context.cpp
    test_guest_context.cpp
    test_image_mapper.cpp
    test_import_resolver.cpp
    test_module.cpp
    test_module_graph.cpp
    test_msvcrt.cpp
    test_pe_reader.cpp
    test_process.cpp
    test_runtime.cpp
    test_rust_pe_contract.cpp
    test_trace.cpp
    test_unwind.cpp
    test_win32.cpp
    test_win32_apps.cpp
    test_win32_external.cpp
    test_win32_gui.cpp
    test_gui_controls.cpp
    test_win32_security.cpp
    test_win32_stubs.cpp
)
target_link_libraries(tradutorlinux_unit_tests
    PRIVATE
        tradutorlinux_core
        GTest::gtest_main
)
if(TL_BUILD_RUST)
    target_sources(tradutorlinux_unit_tests PRIVATE
        test_rust_path_validation.cpp
        test_rust_pe_parser.cpp
        test_rust_msix_parser.cpp
        test_rust_profile_parser.cpp
        test_rust_app_catalog_parser.cpp
    )
    target_include_directories(tradutorlinux_unit_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/compat"
    )
    target_compile_definitions(tradutorlinux_unit_tests PRIVATE
        TRADUTORLINUX_RUST_PE_PARSER=1
        TRADUTORLINUX_RUST_PROFILE_PARSER=1
        TRADUTORLINUX_RUST_APP_CATALOG_PARSER=1
    )
    target_link_libraries(tradutorlinux_unit_tests PRIVATE tl_rust_validator)
endif()
tl_enable_warnings(tradutorlinux_unit_tests)
tl_enable_sanitizers(tradutorlinux_unit_tests)

if(TARGET tradutorlinux_samples)
    add_dependencies(tradutorlinux_unit_tests tradutorlinux_samples)
    target_compile_definitions(tradutorlinux_unit_tests PRIVATE
        TL_FIXTURE_OUTPUT_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/samples/generated"
    )
endif()

include(GoogleTest)
if(TL_ENABLE_SANITIZERS)
    # LeakSanitizer cannot run while CMake performs post-build discovery in
    # environments that trace child processes. Discover at ctest time instead,
    # after the sanitize test preset has supplied its runtime environment.
    gtest_discover_tests(tradutorlinux_unit_tests DISCOVERY_MODE PRE_TEST)
else()
    gtest_discover_tests(tradutorlinux_unit_tests)
endif()

if(TL_BUILD_RUST AND TARGET tradutorlinux_samples)
    add_test(
        NAME rust_pe_differential_corpus
        COMMAND tradutorlinux_unit_tests
            --gtest_filter=RustPeParserTest.DifferentiallyMatchesGeneratedPeCorpus
    )
    set_tests_properties(rust_pe_differential_corpus PROPERTIES
        LABELS "rust;pe32plus;differential;corpus"
        TIMEOUT 60
    )
    add_test(
        NAME rust_msix_differential
        COMMAND tradutorlinux_unit_tests
            --gtest_filter=RustMsixParserTest.*
    )
    set_tests_properties(rust_msix_differential PROPERTIES
        LABELS "rust;msix;differential;corpus"
        TIMEOUT 60
    )
    add_test(
        NAME rust_app_catalog_differential
        COMMAND tradutorlinux_unit_tests
            --gtest_filter=RustAppCatalogParserTest.*
    )
    set_tests_properties(rust_app_catalog_differential PROPERTIES
        LABELS "rust;catalog;differential;contract"
        TIMEOUT 60
    )
endif()

add_executable(tradutorlinux_qt_launcher_tests
    gui/qt_launcher_test.cpp
    ../src/gui/main_window.cpp
    ../src/gui/main_window.hpp
)
target_include_directories(tradutorlinux_qt_launcher_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/gui"
)
target_compile_definitions(tradutorlinux_qt_launcher_tests PRIVATE
    TL_GUI_TEST_RUNTIME_PATH="$<TARGET_FILE:tradutorlinux>"
)
target_link_libraries(tradutorlinux_qt_launcher_tests
    PRIVATE
        tradutorlinux_core
        Qt6::Widgets
)
tl_enable_warnings(tradutorlinux_qt_launcher_tests)
tl_enable_sanitizers(tradutorlinux_qt_launcher_tests)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tradutorlinux_qt_launcher_tests PRIVATE
        -Wno-conversion -Wno-sign-conversion
    )
endif()
if(TARGET tradutorlinux_samples)
    add_dependencies(tradutorlinux_qt_launcher_tests tradutorlinux_samples)
    target_compile_definitions(tradutorlinux_qt_launcher_tests PRIVATE
        TL_FIXTURE_OUTPUT_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/samples/generated"
    )
endif()
add_test(NAME qt_launcher_smoke COMMAND tradutorlinux_qt_launcher_tests)
set_tests_properties(qt_launcher_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "gui;qt"
)

if(TL_XVFB_EXECUTABLE AND TARGET tradutorlinux_samples AND TARGET tradutorlinux)
    find_library(TL_X11_LIBRARY X11 REQUIRED)
    add_executable(runtime_gui_smoke
        gui/runtime_gui_smoke.cpp
    )
    target_link_libraries(runtime_gui_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(runtime_gui_smoke)
    add_test(
        NAME runtime_gui_smoke
        COMMAND runtime_gui_smoke
            "$<TARGET_FILE:tradutorlinux>"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_win.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime_gui_smoke_work"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_win2.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_key.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_timer.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_gdi.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_paint.exe"
            "${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_dialog.exe"
    )
    set_tests_properties(runtime_gui_smoke PROPERTIES
        DEPENDS "fixture_tl_win_metadata;fixture_tl_win2_metadata;fixture_tl_key_metadata;fixture_tl_timer_metadata;fixture_tl_gdi_metadata;fixture_tl_paint_metadata;fixture_tl_dialog_metadata"
        LABELS "runtime;pe32plus;gui"
        TIMEOUT 90
        SKIP_RETURN_CODE 77
    )

    add_executable(proton_input_driver
        gui/proton_input_driver.cpp
    )
    find_library(TL_XTEST_LIBRARY Xtst REQUIRED)
    target_include_directories(proton_input_driver PRIVATE "${TL_X11_INCLUDE_DIR}")
    target_link_libraries(proton_input_driver PRIVATE "${TL_X11_LIBRARY}" "${TL_XTEST_LIBRARY}")
    tl_enable_warnings(proton_input_driver)
    tl_enable_sanitizers(proton_input_driver)
endif()

if(TARGET tradutorlinux_samples AND TARGET tradutorlinux)
    add_test(
        NAME integration_msix_install
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DHELLO=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_hello.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/msix_install_work"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_msix_install.cmake"
    )
    set_tests_properties(integration_msix_install PROPERTIES
        DEPENDS fixture_tl_hello_metadata
        LABELS "runtime;pe32plus;package;install"
        TIMEOUT 60
    )
    add_test(
        NAME integration_msix_rejections
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DHELLO=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_hello.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/msix_rejection_work"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_msix_rejections.cmake"
    )
    set_tests_properties(integration_msix_rejections PROPERTIES
        DEPENDS fixture_tl_hello_metadata
        LABELS "runtime;pe32plus;package;robustness"
        TIMEOUT 60
    )
    add_test(
        NAME integration_compat_profile
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DHELLO=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_hello.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/compat_profile_work"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_compat_profile.cmake"
    )
    set_tests_properties(integration_compat_profile PROPERTIES
        DEPENDS fixture_tl_hello_metadata
        LABELS "runtime;pe32plus;compat;profile"
        TIMEOUT 60
    )
    add_test(
        NAME integration_catalog_parser
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/catalog_parser_work"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_catalog_parser.cmake"
    )
    set_tests_properties(integration_catalog_parser PROPERTIES
        LABELS "runtime;catalog;rust;differential"
        TIMEOUT 60
    )
    add_test(
        NAME integration_compat_file_exposure
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_file.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/compat_file_exposure_work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_compat_file.cmake"
    )
    set_tests_properties(integration_compat_file_exposure PROPERTIES
        DEPENDS "fixture_tl_compat_file_metadata;report_tl_compat_file_support"
        LABELS "runtime;pe32plus;compat;profile;files"
        TIMEOUT 60
    )
    add_test(
        NAME integration_compat_profile_isolation
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_file.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/compat_profile_isolation_work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_compat_profile_isolation.cmake"
    )
    set_tests_properties(integration_compat_profile_isolation PROPERTIES
        DEPENDS "fixture_tl_compat_file_metadata;report_tl_compat_file_support"
        LABELS "runtime;pe32plus;compat;profile;files;isolation"
        TIMEOUT 60
    )
    add_test(
        NAME integration_rust_operational
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DCOMPAT_FILE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_file.exe"
            "-DHELLO=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_hello.exe"
            "-DHANG=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_hang.exe"
            "-DPROTON_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_proton_probe.exe"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rust_operational_work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_rust_operational.cmake"
    )
    set_tests_properties(integration_rust_operational PROPERTIES
        DEPENDS "fixture_tl_compat_file_metadata;fixture_tl_hello_metadata;fixture_tl_hang_metadata;fixture_tl_proton_probe_metadata;report_tl_compat_file_support;report_tl_proton_probe_support"
        LABELS "runtime;pe32plus;rust;compat;profile;files;backend;proton;operational"
        TIMEOUT 180
    )
    add_test(
        NAME integration_rust_app_run_malformed
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${CMAKE_SOURCE_DIR}/src/cli/report.cpp"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DAPP_ID=r21-4-malformed"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rust-app-run-malformed"
            "-DEXPECTED_EXIT=4"
            "-DREQUIRED_TRACE=parse-failed"
            "-DEXPECT_NO_LOADER=1"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_app_run_rejection.cmake"
    )
    set_tests_properties(integration_rust_app_run_malformed PROPERTIES
        LABELS "runtime;app-run;pe32plus;rust;robustness"
        TIMEOUT 60
    )
    add_test(
        NAME integration_compat_dll_profile
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_dll_app.exe"
            "-DDLL_A=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_dll_a.dll"
            "-DDLL_B=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_dll_b.dll"
            "-DDEP_DLL=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_dep.dll"
            "-DRUST_ENABLED=$<BOOL:${TL_BUILD_RUST}>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/compat_dll_profile_work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_compat_dll.cmake"
    )
    set_tests_properties(integration_compat_dll_profile PROPERTIES
        DEPENDS fixture_tl_compat_dll_app_metadata
        LABELS "runtime;pe32plus;compat;profile;dll;isolation"
        TIMEOUT 90
    )
    add_test(
        NAME integration_proton_backend
        COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
            "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_proton_probe.exe"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_backend_work"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_backend.cmake"
    )
    set_tests_properties(integration_proton_backend PROPERTIES
        DEPENDS "fixture_tl_proton_probe_metadata;report_tl_proton_probe_support"
        LABELS "runtime;pe32plus;backend;proton;mock"
        TIMEOUT 60
    )

    if(TL_PROTON_ROOT AND IS_DIRECTORY "${TL_PROTON_ROOT}")
        add_test(
            NAME integration_proton_graphics
            COMMAND "${CMAKE_COMMAND}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_graphics_probe.exe"
                "-DPROTON_ROOT=${TL_PROTON_ROOT}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_graphics_work"
                "-DAPP_ID=graphicsprobe"
                "-DPROBE_NAME=Graphics Probe"
                "-DEXPECTED_OUTPUT=D3D11 frame presented"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_graphics.cmake"
        )
        set_tests_properties(integration_proton_graphics PROPERTIES
            DEPENDS fixture_tl_graphics_probe_metadata
            LABELS "runtime;pe32plus;backend;proton;graphics;dxvk"
            TIMEOUT 240
        )
        add_test(
            NAME integration_proton_d3d12
            COMMAND "${CMAKE_COMMAND}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_d3d12_probe.exe"
                "-DPROTON_ROOT=${TL_PROTON_ROOT}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_d3d12_work"
                "-DAPP_ID=d3d12probe"
                "-DPROBE_NAME=D3D12 Probe"
                "-DEXPECTED_OUTPUT=D3D12 command path ready"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_graphics.cmake"
        )
        set_tests_properties(integration_proton_d3d12 PROPERTIES
            DEPENDS fixture_tl_d3d12_probe_metadata
            LABELS "runtime;pe32plus;backend;proton;graphics;vkd3d"
            TIMEOUT 240
        )
        add_test(
            NAME integration_proton_audio
            COMMAND "${CMAKE_COMMAND}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_audio_probe.exe"
                "-DPROTON_ROOT=${TL_PROTON_ROOT}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_audio_work"
                "-DAPP_ID=audioprobe"
                "-DPROBE_NAME=Audio Probe"
                "-DEXPECTED_OUTPUT=Proton audio ready"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_graphics.cmake"
        )
        set_tests_properties(integration_proton_audio PROPERTIES
            DEPENDS fixture_tl_audio_probe_metadata
            LABELS "runtime;pe32plus;backend;proton;audio;xaudio2"
            TIMEOUT 240
        )
        if(TARGET proton_input_driver)
            add_test(
                NAME integration_proton_input
                COMMAND "${CMAKE_COMMAND}"
                    "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                    "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_input_probe.exe"
                    "-DPROTON_ROOT=${TL_PROTON_ROOT}"
                    "-DINPUT_DRIVER=$<TARGET_FILE:proton_input_driver>"
                    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_input_work"
                    "-DAPP_ID=inputprobe"
                    "-DPROBE_NAME=Proton Input Probe"
                    "-DEXPECTED_OUTPUT=Proton input ready"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_input.cmake"
            )
            set_tests_properties(integration_proton_input PROPERTIES
                DEPENDS fixture_tl_input_probe_metadata
                LABELS "runtime;pe32plus;backend;proton;input"
                TIMEOUT 240
            )
        endif()
        add_test(
            NAME integration_proton_isolation
            COMMAND "${CMAKE_COMMAND}"
                "-DRUNTIME=$<TARGET_FILE:tradutorlinux>"
                "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/samples/generated/tl_compat_file.exe"
                "-DPROTON_ROOT=${TL_PROTON_ROOT}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/proton_isolation_work"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/samples/verify_proton_isolation.cmake"
        )
        set_tests_properties(integration_proton_isolation PROPERTIES
            DEPENDS "fixture_tl_compat_file_metadata;report_tl_compat_file_support"
            LABELS "runtime;pe32plus;backend;proton;compat;files;isolation"
            TIMEOUT 420
        )
    endif()
endif()
