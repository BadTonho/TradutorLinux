add_subdirectory(samples)

add_executable(tradutorlinux_unit_tests
    test_abi.cpp
    test_cli.cpp
    test_image_mapper.cpp
    test_import_resolver.cpp
    test_module.cpp
    test_pe_reader.cpp
    test_process.cpp
    test_runtime.cpp
    test_trace.cpp
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
gtest_discover_tests(tradutorlinux_unit_tests)

find_program(TL_XVFB_EXECUTABLE xvfb)
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
            "${CMAKE_CURRENT_BINARY_DIR}/runtime_gui_smoke"
    )
    set_tests_properties(runtime_gui_smoke PROPERTIES
        DEPENDS fixture_tl_win_metadata
        LABELS "runtime;pe32plus;gui"
        TIMEOUT 90
    )
endif()
