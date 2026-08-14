add_library(tradutorlinux_core STATIC
    cli.cpp
    diagnostics/trace.cpp
    loader/image_mapper.cpp
    pe/pe_reader.cpp
)

target_include_directories(tradutorlinux_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_compile_definitions(tradutorlinux_core
    PUBLIC TRADUTORLINUX_VERSION="${PROJECT_VERSION}-dev"
)
tl_enable_warnings(tradutorlinux_core)
tl_enable_sanitizers(tradutorlinux_core)

add_executable(tradutorlinux main.cpp)
target_link_libraries(tradutorlinux PRIVATE tradutorlinux_core)
tl_enable_warnings(tradutorlinux)
tl_enable_sanitizers(tradutorlinux)
