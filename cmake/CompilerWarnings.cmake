function(tl_enable_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Werror
        )
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /permissive-)
    endif()
endfunction()
