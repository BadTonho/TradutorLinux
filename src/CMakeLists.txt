add_library(tradutorlinux_core STATIC
    catalog/app_catalog.cpp
    cli/options.cpp
    cli/report.cpp
    cli/runner.cpp
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
    runtime/core/memory_validator.cpp
    runtime/core/environment.cpp
    runtime/core/guest_context.cpp
    runtime/core/winapi.cpp
    runtime/core/guest_entry.S
    runtime/seh/unwind.cpp
    runtime/seh/unwind_capture.S
    runtime/seh/seh.S
    runtime/ntdll.cpp
    runtime/dlls/kernel32/file.cpp
    runtime/dlls/kernel32/process.cpp
    runtime/dlls/kernel32/thread.cpp
    runtime/dlls/kernel32/sync.cpp
    runtime/dlls/kernel32/memory.cpp
    runtime/dlls/kernel32/time.cpp
    runtime/dlls/kernel32/console.cpp
    runtime/dlls/kernel32/locale.cpp
    runtime/dlls/kernel32/module.cpp
    runtime/dlls/user32/window.cpp
    runtime/dlls/user32/message.cpp
    runtime/dlls/user32/dialog.cpp
    runtime/dlls/user32/menu.cpp
    runtime/dlls/user32/clipboard.cpp
    runtime/dlls/user32/misc.cpp
    runtime/dlls/user32/module.cpp
    runtime/dialog_template.cpp
    runtime/gdi32.cpp
    runtime/shell32.cpp
    runtime/gui_controls.cpp
    runtime/msvcrt.cpp
    runtime/advapi.cpp
    runtime/security.cpp
    runtime/dlls/net/ws2_32.cpp
    runtime/dlls/net/wininet.cpp
    runtime/dlls/net/iphlpapi.cpp
    runtime/dlls/net/mpr.cpp
    runtime/dlls/crypto/crypt32.cpp
    runtime/dlls/crypto/wintrust.cpp
    runtime/dlls/com/ole32.cpp
    runtime/dlls/com/oleaut32.cpp
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
    runtime/dwmapi.cpp
    runtime/function_trace.cpp
    util/unicode.cpp
    gui/platform.cpp
    gui/wayland.cpp
    gui/x11.cpp
)

target_include_directories(tradutorlinux_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/cli"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/core"
)
target_compile_definitions(tradutorlinux_core
    PUBLIC TRADUTORLINUX_VERSION="${PROJECT_VERSION}-dev"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tradutorlinux_core PUBLIC -mstackrealign)
    set_source_files_properties(
        runtime/dlls/kernel32/file.cpp runtime/dlls/kernel32/process.cpp runtime/dlls/kernel32/thread.cpp
        runtime/dlls/kernel32/sync.cpp runtime/dlls/kernel32/memory.cpp runtime/dlls/kernel32/time.cpp
        runtime/dlls/kernel32/console.cpp runtime/dlls/kernel32/locale.cpp runtime/dlls/kernel32/module.cpp
        runtime/dlls/user32/window.cpp runtime/dlls/user32/message.cpp runtime/dlls/user32/dialog.cpp
        runtime/dlls/user32/menu.cpp runtime/dlls/user32/clipboard.cpp runtime/dlls/user32/misc.cpp
        runtime/dlls/user32/module.cpp runtime/ntdll.cpp
        runtime/gdi32.cpp runtime/gui_controls.cpp runtime/shell32.cpp
        runtime/core/winapi.cpp runtime/msvcrt.cpp runtime/advapi.cpp runtime/security.cpp
        runtime/dlls/net/ws2_32.cpp runtime/dlls/net/wininet.cpp runtime/dlls/crypto/wintrust.cpp runtime/dlls/crypto/crypt32.cpp
        runtime/dlls/com/ole32.cpp runtime/dlls/com/oleaut32.cpp runtime/shlwapi.cpp runtime/version.cpp
        runtime/winmm.cpp runtime/comctl32.cpp runtime/comdlg32.cpp runtime/imm32.cpp
        runtime/psapi.cpp runtime/gdiplus.cpp runtime/uxtheme.cpp runtime/dbghelp.cpp
        runtime/powrprof.cpp runtime/dlls/net/iphlpapi.cpp runtime/dlls/net/mpr.cpp runtime/dwmapi.cpp
        util/unicode.cpp catalog/app_catalog.cpp package/msix.cpp
        diagnostics/crash_context.cpp gui/platform.cpp gui/wayland.cpp gui/x11.cpp
        loader/dll_overrides.cpp loader/image_mapper.cpp loader/import_resolver.cpp
        loader/module.cpp loader/process.cpp pe/pe_reader.cpp prefix/prefix.cpp
        runtime/core/memory_validator.cpp runtime/core/environment.cpp runtime/core/guest_context.cpp
        runtime/dialog_template.cpp runtime/seh/unwind.cpp process/isolate.cpp
        runtime/function_trace.cpp
        PROPERTIES COMPILE_OPTIONS "-finstrument-functions")
endif()
find_path(TL_X11_INCLUDE_DIR X11/Xlib.h REQUIRED)
find_library(TL_X11_LIBRARY X11 REQUIRED)
target_include_directories(tradutorlinux_core PRIVATE "${TL_X11_INCLUDE_DIR}")
target_link_libraries(tradutorlinux_core PUBLIC "${TL_X11_LIBRARY}")
target_link_libraries(tradutorlinux_core PUBLIC ZLIB::ZLIB)

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
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties("${TL_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c"
        PROPERTIES COMPILE_OPTIONS "-finstrument-functions")
endif()
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
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tradutorlinux PRIVATE -finstrument-functions)
    target_link_options(tradutorlinux PRIVATE -rdynamic)
endif()
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
    target_compile_options(tradutorlinux_gui PRIVATE -finstrument-functions)
    target_link_options(tradutorlinux_gui PRIVATE -rdynamic)
endif()
