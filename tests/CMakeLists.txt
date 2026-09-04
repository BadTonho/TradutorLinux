add_subdirectory(samples)

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
endif()

if(TL_BUILD_TARGET_APPS)
    add_subdirectory(targets)
endif()

add_executable(tradutorlinux_unit_tests
    test_abi.cpp
    test_cli.cpp
    test_crash_context.cpp
    test_guest_context.cpp
    test_image_mapper.cpp
    test_import_resolver.cpp
    test_module.cpp
    test_msvcrt.cpp
    test_pe_reader.cpp
    test_process.cpp
    test_runtime.cpp
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
endif()
