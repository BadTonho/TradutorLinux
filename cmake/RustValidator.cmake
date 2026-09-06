function(tl_configure_rust_validator)
    find_program(TL_RUSTUP_EXECUTABLE NAMES rustup)
    if(NOT TL_RUSTUP_EXECUTABLE)
        message(FATAL_ERROR
            "TL_BUILD_RUST=ON requer rustup; instale o toolchain fixado ${TL_RUST_TOOLCHAIN}")
    endif()

    execute_process(
        COMMAND "${TL_RUSTUP_EXECUTABLE}" run "${TL_RUST_TOOLCHAIN}" cargo --version
        RESULT_VARIABLE TL_RUST_TOOLCHAIN_RESULT
        OUTPUT_VARIABLE TL_RUST_TOOLCHAIN_OUTPUT
        ERROR_VARIABLE TL_RUST_TOOLCHAIN_ERROR
    )
    if(NOT TL_RUST_TOOLCHAIN_RESULT EQUAL 0)
        message(FATAL_ERROR
            "toolchain Rust ${TL_RUST_TOOLCHAIN} não está disponível via rustup; "
            "instale com 'rustup toolchain install ${TL_RUST_TOOLCHAIN}'\n"
            "${TL_RUST_TOOLCHAIN_ERROR}")
    endif()

    execute_process(
        COMMAND "${TL_RUSTUP_EXECUTABLE}" run "${TL_RUST_TOOLCHAIN}" cargo metadata
            --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
            --locked --offline --no-deps --format-version 1
        RESULT_VARIABLE TL_RUST_METADATA_RESULT
        OUTPUT_VARIABLE TL_RUST_METADATA_OUTPUT
        ERROR_VARIABLE TL_RUST_METADATA_ERROR
    )
    if(NOT TL_RUST_METADATA_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Cargo não validou o manifesto/lockfile Rust com --locked --offline:\n"
            "${TL_RUST_METADATA_ERROR}")
    endif()

    find_package(Threads REQUIRED)
    set(TL_RUST_LIBRARY_DIRECTORY "${CMAKE_BINARY_DIR}/rust_ffi")
    set(TL_RUST_LIBRARY "${TL_RUST_LIBRARY_DIRECTORY}/libtl_rust_validator.a")
    set(TL_RUST_CARGO_TARGET_DIRECTORY "${TL_RUST_LIBRARY_DIRECTORY}/cargo-target")
    set(TL_RUST_CARGO_PROFILE_DIRECTORY "debug")
    set(TL_RUST_CARGO_PROFILE_ARGUMENT)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(TL_RUST_CARGO_PROFILE_DIRECTORY "release")
        set(TL_RUST_CARGO_PROFILE_ARGUMENT --release)
    endif()

    add_custom_command(
        OUTPUT "${TL_RUST_LIBRARY}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TL_RUST_LIBRARY_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E env MAKEFLAGS=
            "${TL_RUSTUP_EXECUTABLE}" run "${TL_RUST_TOOLCHAIN}" cargo build
            --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
            --target-dir "${TL_RUST_CARGO_TARGET_DIRECTORY}"
            --locked --offline ${TL_RUST_CARGO_PROFILE_ARGUMENT}
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${TL_RUST_CARGO_TARGET_DIRECTORY}/${TL_RUST_CARGO_PROFILE_DIRECTORY}/libtl_rust_validator.a"
            "${TL_RUST_LIBRARY}"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/Cargo.toml"
            "${PROJECT_SOURCE_DIR}/Cargo.lock"
            "${PROJECT_SOURCE_DIR}/rust-toolchain.toml"
            "${PROJECT_SOURCE_DIR}/src/rust/validator.rs"
            "${PROJECT_SOURCE_DIR}/src/rust/msix_contract.rs"
            "${PROJECT_SOURCE_DIR}/src/rust/msix_parser.rs"
            "${PROJECT_SOURCE_DIR}/src/rust/pe_parser.rs"
            "${PROJECT_SOURCE_DIR}/src/rust/profile_contract.rs"
            "${PROJECT_SOURCE_DIR}/src/rust/profile_parser.rs"
            "${PROJECT_SOURCE_DIR}/include/tradutorlinux/ffi/rust_validator.h"
            "${PROJECT_SOURCE_DIR}/include/tradutorlinux/ffi/rust_pe_parser.h"
            "${PROJECT_SOURCE_DIR}/include/tradutorlinux/ffi/rust_msix_parser.h"
            "${PROJECT_SOURCE_DIR}/include/tradutorlinux/ffi/rust_profile_parser.h"
        VERBATIM
        COMMENT "Generating Rust validator static library"
    )
    add_custom_target(tl_rust_validator_library DEPENDS "${TL_RUST_LIBRARY}")

    add_library(tl_rust_validator STATIC IMPORTED GLOBAL)
    set_target_properties(tl_rust_validator PROPERTIES
        IMPORTED_LOCATION "${TL_RUST_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/include"
    )
    add_dependencies(tl_rust_validator tl_rust_validator_library)
    target_link_libraries(tl_rust_validator INTERFACE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
endfunction()
