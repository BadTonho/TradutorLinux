add_subdirectory(samples)

find_program(TL_XVFB_EXECUTABLE NAMES Xvfb xvfb)
if(TL_XVFB_EXECUTABLE)
    find_library(TL_X11_LIBRARY X11 REQUIRED)
    add_executable(simple_todo_gui_smoke
        gui/simple_todo_gui_smoke.cpp
    )
    target_link_libraries(simple_todo_gui_smoke PRIVATE "${TL_X11_LIBRARY}")
    tl_enable_warnings(simple_todo_gui_smoke)
endif()

if(TL_BUILD_TARGET_APPS)
    add_subdirectory(targets)
endif()

add_executable(tradutorlinux_unit_tests
    test_abi.cpp
    test_cli.cpp
    test_image_mapper.cpp
    test_import_resolver.cpp
    test_module.cpp
    test_msvcrt.cpp
    test_pe_reader.cpp
    test_process.cpp
    test_runtime.cpp
    test_trace.cpp
    test_win32.cpp
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
    )
    set_tests_properties(runtime_gui_smoke PROPERTIES
        DEPENDS "fixture_tl_win_metadata;fixture_tl_win2_metadata;fixture_tl_key_metadata;fixture_tl_timer_metadata;fixture_tl_gdi_metadata;fixture_tl_paint_metadata"
        LABELS "runtime;pe32plus;gui"
        TIMEOUT 90
        SKIP_RETURN_CODE 77
    )
endif()
