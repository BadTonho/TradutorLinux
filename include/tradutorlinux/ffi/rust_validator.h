#ifndef TRADUTORLINUX_FFI_RUST_VALIDATOR_H
#define TRADUTORLINUX_FFI_RUST_VALIDATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_rust_validator tl_rust_validator_t;
typedef uint32_t tl_rust_status_t;

#define TL_RUST_STATUS_OK UINT32_C(0)
#define TL_RUST_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define TL_RUST_STATUS_INVALID_UTF8 UINT32_C(2)
#define TL_RUST_STATUS_INVALID_UTF16 UINT32_C(3)
#define TL_RUST_STATUS_BUFFER_TOO_SMALL UINT32_C(4)
#define TL_RUST_STATUS_INPUT_TOO_LARGE UINT32_C(5)
#define TL_RUST_STATUS_INTERNAL UINT32_C(6)
#define TL_RUST_STATUS_INVALID_PATH UINT32_C(7)

/* The handle is allocated and released only by the Rust library. */
tl_rust_status_t tl_rust_validator_create(
    uint64_t max_input_bytes,
    tl_rust_validator_t** out);

void tl_rust_validator_destroy(tl_rust_validator_t* validator);

/*
 * Input and error_buffer are owned by the caller. Rust reads input_length
 * bytes/code units and writes at most error_capacity bytes, including the
 * terminating NUL. error_required is mandatory and reports the complete
 * message size including that NUL. No pointer returned by this API is owned
 * by Rust or may be retained by the caller.
 */
tl_rust_status_t tl_rust_validator_validate_utf8(
    const tl_rust_validator_t* validator,
    const uint8_t* input,
    uint64_t input_length,
    char* error_buffer,
    uint64_t error_capacity,
    uint64_t* error_required);

tl_rust_status_t tl_rust_validator_validate_utf16(
    const tl_rust_validator_t* validator,
    const uint16_t* input,
    uint64_t input_length,
    char* error_buffer,
    uint64_t error_capacity,
    uint64_t* error_required);

tl_rust_status_t tl_rust_validator_validate_relative_path(
    const tl_rust_validator_t* validator,
    const uint8_t* input,
    uint64_t input_length,
    char* error_buffer,
    uint64_t error_capacity,
    uint64_t* error_required);

tl_rust_status_t tl_rust_validator_validate_c_drive_path(
    const tl_rust_validator_t* validator,
    const uint8_t* input,
    uint64_t input_length,
    char* error_buffer,
    uint64_t error_capacity,
    uint64_t* error_required);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* TRADUTORLINUX_FFI_RUST_VALIDATOR_H */
