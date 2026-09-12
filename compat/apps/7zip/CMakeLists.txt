add_library(tradutorlinux_7zip STATIC
    src/extension.cpp
)

target_include_directories(tradutorlinux_7zip
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/runtime"
        "${PROJECT_SOURCE_DIR}/src/runtime/core"
)

tl_enable_warnings(tradutorlinux_7zip)
tl_enable_sanitizers(tradutorlinux_7zip)
