add_subdirectory(samples)

add_executable(tradutorlinux_unit_tests
    test_cli.cpp
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
endif()

include(GoogleTest)
gtest_discover_tests(tradutorlinux_unit_tests)
