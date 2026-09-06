function(tl_enable_sanitizers target_name)
    if(NOT TL_ENABLE_SANITIZERS)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "TL_ENABLE_SANITIZERS requires GCC or Clang")
    endif()

    target_compile_options(${target_name} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_compile_definitions(${target_name} PRIVATE
        TRADUTORLINUX_HOST_SANITIZED=1
    )
    target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
endfunction()
