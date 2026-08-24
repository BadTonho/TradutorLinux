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
    pe/pe_reader.cpp
    prefix/prefix.cpp
    process/isolate.cpp
    runtime/memory_validator.cpp
    runtime/environment.cpp
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
    runtime/ole32.cpp
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
    runtime/guest_entry.S
    runtime/unwind_capture.S
    runtime/seh.S
    util/unicode.cpp
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
