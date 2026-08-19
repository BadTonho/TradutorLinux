add_library(tradutorlinux_core STATIC
    cli.cpp
    diagnostics/trace.cpp
    loader/image_mapper.cpp
    loader/import_resolver.cpp
    loader/module.cpp
    loader/process.cpp
    pe/pe_reader.cpp
    process/isolate.cpp
    runtime/winapi.cpp
    runtime/gui_controls.cpp
    runtime/msvcrt.cpp
    runtime/advapi.cpp
    runtime/ws2_32.cpp
    runtime/guest_entry.S
    gui/x11.cpp
)

target_include_directories(tradutorlinux_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_compile_definitions(tradutorlinux_core
    PUBLIC TRADUTORLINUX_VERSION="${PROJECT_VERSION}-dev"
)
find_path(TL_X11_INCLUDE_DIR X11/Xlib.h REQUIRED)
find_library(TL_X11_LIBRARY X11 REQUIRED)
target_include_directories(tradutorlinux_core PRIVATE "${TL_X11_INCLUDE_DIR}")
target_link_libraries(tradutorlinux_core PUBLIC "${TL_X11_LIBRARY}")
tl_enable_warnings(tradutorlinux_core)
tl_enable_sanitizers(tradutorlinux_core)

add_executable(tradutorlinux main.cpp)
target_link_libraries(tradutorlinux PRIVATE tradutorlinux_core)
tl_enable_warnings(tradutorlinux)
tl_enable_sanitizers(tradutorlinux)

add_executable(tradutorlinux_gui gui/launcher.cpp)
target_include_directories(tradutorlinux_gui PRIVATE "${TL_X11_INCLUDE_DIR}")
target_link_libraries(tradutorlinux_gui PRIVATE "${TL_X11_LIBRARY}")
tl_enable_warnings(tradutorlinux_gui)
tl_enable_sanitizers(tradutorlinux_gui)
