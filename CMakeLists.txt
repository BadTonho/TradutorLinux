cmake_minimum_required(VERSION 3.28)

project(TradutorLinux VERSION 0.0.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

option(TL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TL_BUILD_SAMPLES "Build PE32+ integration fixtures" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update fetched dependencies after the first download" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

add_subdirectory(src)

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
