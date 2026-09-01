add_library(tradutorlinux_core STATIC
    catalog/app_catalog.cpp
    cli.cpp
    diagnostics/trace.cpp
    diagnostics/crash_context.cpp
    loader/dll_overrides.cpp
    loader/image_mapper.cpp
    loader/import_resolver.cpp
    loader/module.cpp
    loader/process.cpp
    package/msix.cpp
    pe/pe_reader.cpp
    prefix/prefix.cpp
    process/isolate.cpp
    runtime/memory_validator.cpp
    runtime/environment.cpp
    runtime/guest_context.cpp
    runtime/unwind.cpp
    runtime/ntdll.cpp
    runtime/kernel32.cpp
    runtime/legacy_winapi.cpp
    runtime/user32.cpp
    runtime/dialog_template.cpp
    runtime/gdi32.cpp
    runtime/shell32.cpp
    runtime/winapi.cpp
    runtime/gui_controls.cpp
    runtime/msvcrt.cpp
    runtime/advapi.cpp
    runtime/security.cpp
    runtime/ws2_32.cpp
    runtime/wininet.cpp
    runtime/wintrust.cpp
    runtime/crypt32.cpp
    runtime/ole32.cpp
    runtime/oleaut32.cpp
    runtime/shlwapi.cpp
    runtime/version.cpp
    runtime/winmm.cpp
    runtime/comctl32.cpp
    runtime/comdlg32.cpp
    runtime/imm32.cpp
    runtime/psapi.cpp
    runtime/gdiplus.cpp
    runtime/uxtheme.cpp
    runtime/dbghelp.cpp
    runtime/powrprof.cpp
    runtime/iphlpapi.cpp
    runtime/mpr.cpp
    runtime/dwmapi.cpp
    runtime/version.cpp
    runtime/guest_entry.S
    runtime/unwind_capture.S
    runtime/seh.S
    util/unicode.cpp
    gui/platform.cpp
    gui/wayland.cpp
    gui/x11.cpp
)

target_include_directories(tradutorlinux_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_compile_definitions(tradutorlinux_core
    PUBLIC TRADUTORLINUX_VERSION="${PROJECT_VERSION}-dev"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tradutorlinux_core PUBLIC -mstackrealign)
endif()
find_path(TL_X11_INCLUDE_DIR X11/Xlib.h REQUIRED)
find_library(TL_X11_LIBRARY X11 REQUIRED)
target_include_directories(tradutorlinux_core PRIVATE "${TL_X11_INCLUDE_DIR}")
target_link_libraries(tradutorlinux_core PUBLIC "${TL_X11_LIBRARY}")

find_path(TL_WAYLAND_INCLUDE_DIR wayland-client.h REQUIRED)
find_library(TL_WAYLAND_CLIENT_LIBRARY wayland-client REQUIRED)
find_path(TL_XKBCOMMON_INCLUDE_DIR xkbcommon/xkbcommon.h REQUIRED)
find_library(TL_XKBCOMMON_LIBRARY xkbcommon REQUIRED)
find_program(TL_WAYLAND_SCANNER wayland-scanner REQUIRED)
find_file(TL_XDG_SHELL_XML xdg-shell.xml
    PATHS /usr/share/wayland-protocols/stable/xdg-shell
    REQUIRED)
set(TL_WAYLAND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-generated")
file(MAKE_DIRECTORY "${TL_WAYLAND_GENERATED_DIR}")
add_custom_command(
    OUTPUT "${TL_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h"
           "${TL_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c"
    COMMAND "${TL_WAYLAND_SCANNER}" client-header "${TL_XDG_SHELL_XML}"
            "${TL_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h"
    COMMAND "${TL_WAYLAND_SCANNER}" private-code "${TL_XDG_SHELL_XML}"
            "${TL_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c"
    DEPENDS "${TL_XDG_SHELL_XML}"
    VERBATIM)
target_sources(tradutorlinux_core PRIVATE
    "${TL_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c")
target_include_directories(tradutorlinux_core PRIVATE
    "${TL_WAYLAND_INCLUDE_DIR}"
    "${TL_XKBCOMMON_INCLUDE_DIR}"
    "${TL_WAYLAND_GENERATED_DIR}")
target_link_libraries(tradutorlinux_core PUBLIC
    "${TL_WAYLAND_CLIENT_LIBRARY}"
    "${TL_XKBCOMMON_LIBRARY}")
if(CMAKE_DL_LIBS)
    target_link_libraries(tradutorlinux_core PUBLIC "${CMAKE_DL_LIBS}")
endif()
tl_enable_warnings(tradutorlinux_core)
tl_enable_sanitizers(tradutorlinux_core)

add_executable(tradutorlinux main.cpp)
target_link_libraries(tradutorlinux PRIVATE tradutorlinux_core)
tl_enable_warnings(tradutorlinux)
tl_enable_sanitizers(tradutorlinux)

add_executable(tradutorlinux_gui
    gui/main_window.cpp
    gui/main_window.hpp
    gui/qt_main.cpp
)
target_link_libraries(tradutorlinux_gui PRIVATE tradutorlinux_core Qt6::Widgets)
tl_enable_warnings(tradutorlinux_gui)
tl_enable_sanitizers(tradutorlinux_gui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tradutorlinux_gui PRIVATE -Wno-conversion -Wno-sign-conversion)
endif()
