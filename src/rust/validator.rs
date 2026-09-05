use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;
use std::str;

const STATUS_OK: u32 = 0;
const STATUS_INVALID_ARGUMENT: u32 = 1;
const STATUS_INVALID_UTF8: u32 = 2;
const STATUS_INVALID_UTF16: u32 = 3;
const STATUS_BUFFER_TOO_SMALL: u32 = 4;
const STATUS_INPUT_TOO_LARGE: u32 = 5;
const STATUS_INTERNAL: u32 = 6;

#[repr(C)]
pub struct tl_rust_validator {
    max_input_bytes: u64,
}

fn write_message(
    status: u32,
    message: &[u8],
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    if error_required.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }

    let required = match u64::try_from(message.len().saturating_add(1)) {
        Ok(value) => value,
        Err(_) => return STATUS_INTERNAL,
    };
    unsafe {
        *error_required = required;
    }

    if error_capacity != 0 && error_buffer.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if error_capacity < required {
        if error_capacity != 0 {
            let writable = usize::try_from(error_capacity - 1).unwrap_or(0).min(message.len());
            unsafe {
                std::ptr::copy_nonoverlapping(
                    message.as_ptr(),
                    error_buffer.cast::<u8>(),
                    writable,
                );
                *error_buffer.cast::<u8>().add(writable) = 0;
            }
        }
        return STATUS_BUFFER_TOO_SMALL;
    }

    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), error_buffer.cast::<u8>(), message.len());
        *error_buffer.cast::<u8>().add(message.len()) = 0;
    }
    status
}

fn run_ffi(function: impl FnOnce() -> u32) -> u32 {
    match catch_unwind(AssertUnwindSafe(function)) {
        Ok(status) => status,
        Err(_) => STATUS_INTERNAL,
    }
}

#[no_mangle]
pub extern "C" fn tl_rust_validator_create(
    max_input_bytes: u64,
    out: *mut *mut tl_rust_validator,
) -> u32 {
    run_ffi(|| {
        if out.is_null() || max_input_bytes == 0 {
            return STATUS_INVALID_ARGUMENT;
        }
        let validator = Box::new(tl_rust_validator { max_input_bytes });
        unsafe {
            *out = Box::into_raw(validator);
        }
        STATUS_OK
    })
}

#[no_mangle]
pub extern "C" fn tl_rust_validator_destroy(validator: *mut tl_rust_validator) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !validator.is_null() {
            unsafe {
                drop(Box::from_raw(validator));
            }
        }
    }));
}

#[no_mangle]
pub extern "C" fn tl_rust_validator_validate_utf8(
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| {
        if validator.is_null() {
            return write_message(
                STATUS_INVALID_ARGUMENT,
                b"validator is null",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        let validator_ref = unsafe { &*validator };
        if input_length > validator_ref.max_input_bytes {
            return write_message(
                STATUS_INPUT_TOO_LARGE,
                b"input exceeds configured limit",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        if input_length != 0 && input.is_null() {
            return write_message(
                STATUS_INVALID_ARGUMENT,
                b"input is null",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        let length = match usize::try_from(input_length) {
            Ok(value) => value,
            Err(_) => {
                return write_message(
                    STATUS_INPUT_TOO_LARGE,
                    b"input length is not representable",
                    error_buffer,
                    error_capacity,
                    error_required,
                )
            }
        };
        let bytes = if length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(input, length) }
        };
        if str::from_utf8(bytes).is_err() {
            return write_message(
                STATUS_INVALID_UTF8,
                b"input is not valid UTF-8",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        write_message(STATUS_OK, b"", error_buffer, error_capacity, error_required)
    })
}

#[no_mangle]
pub extern "C" fn tl_rust_validator_validate_utf16(
    validator: *const tl_rust_validator,
    input: *const u16,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| {
        if validator.is_null() {
            return write_message(
                STATUS_INVALID_ARGUMENT,
                b"validator is null",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        let validator_ref = unsafe { &*validator };
        let input_bytes = match input_length.checked_mul(2) {
            Some(value) => value,
            None => {
                return write_message(
                    STATUS_INPUT_TOO_LARGE,
                    b"input length overflows byte count",
                    error_buffer,
                    error_capacity,
                    error_required,
                )
            }
        };
        if input_bytes > validator_ref.max_input_bytes {
            return write_message(
                STATUS_INPUT_TOO_LARGE,
                b"input exceeds configured limit",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        if input_length != 0 && input.is_null() {
            return write_message(
                STATUS_INVALID_ARGUMENT,
                b"input is null",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        let length = match usize::try_from(input_length) {
            Ok(value) => value,
            Err(_) => {
                return write_message(
                    STATUS_INPUT_TOO_LARGE,
                    b"input length is not representable",
                    error_buffer,
                    error_capacity,
                    error_required,
                )
            }
        };
        let units = if length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(input, length) }
        };
        if String::from_utf16(units).is_err() {
            return write_message(
                STATUS_INVALID_UTF16,
                b"input is not valid UTF-16",
                error_buffer,
                error_capacity,
                error_required,
            );
        }
        write_message(STATUS_OK, b"", error_buffer, error_capacity, error_required)
    })
}
