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
