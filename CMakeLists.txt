cmake_minimum_required(VERSION 3.28)

project(TradutorLinux VERSION 0.0.0 LANGUAGES C CXX ASM)

include(GNUInstallDirs)

include(CTest)
include(FetchContent)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ZLIB REQUIRED)

option(TL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TL_BUILD_SAMPLES "Build PE32+ integration fixtures" ON)
option(TL_BUILD_TARGET_APPS "Cross-compile real target applications (downloads pinned sources)" OFF)
option(TL_BUILD_RUST "Build and use the opt-in Rust validators" OFF)
set(TL_RUST_TOOLCHAIN "1.97.1" CACHE STRING "Pinned rustup toolchain for the Rust validators")
set(TL_PROTON_ROOT "" CACHE PATH "Proton root for optional real backend integration tests")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update fetched dependencies after the first download" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

if(TL_BUILD_RUST)
    include(RustValidator)
    tl_configure_rust_validator()
endif()

add_subdirectory(src)

install(TARGETS tradutorlinux tradutorlinux_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES resources/tradutorlinux.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES resources/tradutorlinux.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "tradutorlinux")
set(CPACK_PACKAGE_VENDOR "TradutorLinux")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Runtime de compatibilidade Win32 para Linux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "TradutorLinux")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libx11-6, libwayland-client0, libxkbcommon0, libqt6widgets6")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

if(BUILD_TESTING)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of GoogleTest" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "Do not build GoogleMock" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 063de7e9578f82b369302001269680b4b1553359
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()

include(CPack)
