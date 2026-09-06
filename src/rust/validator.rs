use std::alloc::{alloc, dealloc, Layout};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;
use std::str;

#[cfg(test)]
mod pe_contract;
mod msix_contract;
mod msix_parser;
mod pe_parser;
mod profile_contract;
mod profile_parser;
mod catalog_contract;
mod catalog_parser;

const STATUS_OK: u32 = 0;
const STATUS_INVALID_ARGUMENT: u32 = 1;
const STATUS_INVALID_UTF8: u32 = 2;
const STATUS_INVALID_UTF16: u32 = 3;
const STATUS_BUFFER_TOO_SMALL: u32 = 4;
const STATUS_INPUT_TOO_LARGE: u32 = 5;
const STATUS_INTERNAL: u32 = 6;
const STATUS_INVALID_PATH: u32 = 7;

#[repr(C)]
pub struct tl_rust_validator {
    max_input_bytes: u64,
}

struct InputError {
    status: u32,
    message: &'static [u8],
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

    let required = match message
        .len()
        .checked_add(1)
        .and_then(|value| u64::try_from(value).ok())
    {
        Some(value) => value,
        None => return STATUS_INTERNAL,
    };
    unsafe {
        *error_required = required;
    }

    if error_capacity != 0 && error_buffer.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    if error_capacity < required {
        if error_capacity != 0 {
            let writable = usize::try_from(error_capacity - 1)
                .unwrap_or(0)
                .min(message.len());
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

fn allocate_validator(max_input_bytes: u64) -> Option<*mut tl_rust_validator> {
    #[cfg(test)]
    if TEST_FAIL_NEXT_ALLOCATION.with(|fail| fail.replace(false)) {
        return None;
    }

    let layout = Layout::new::<tl_rust_validator>();
    let raw = unsafe { alloc(layout).cast::<tl_rust_validator>() };
    if raw.is_null() {
        return None;
    }
    unsafe {
        raw.write(tl_rust_validator { max_input_bytes });
    }
    Some(raw)
}

unsafe fn release_validator(validator: *mut tl_rust_validator) {
    let layout = Layout::new::<tl_rust_validator>();
    unsafe {
        std::ptr::drop_in_place(validator);
        dealloc(validator.cast::<u8>(), layout);
    }
}

unsafe fn checked_input_length(
    validator: *const tl_rust_validator,
    input_length: u64,
    element_bytes: u64,
) -> Result<usize, InputError> {
    if validator.is_null() {
        return Err(InputError {
            status: STATUS_INVALID_ARGUMENT,
            message: b"validator is null",
        });
    }

    let input_bytes = match input_length.checked_mul(element_bytes) {
        Some(value) => value,
        None => {
            return Err(InputError {
                status: STATUS_INPUT_TOO_LARGE,
                message: b"input length overflows byte count",
            })
        }
    };
    let validator_ref = unsafe { &*validator };
    if input_bytes > validator_ref.max_input_bytes {
        return Err(InputError {
            status: STATUS_INPUT_TOO_LARGE,
            message: b"input exceeds configured limit",
        });
    }

    usize::try_from(input_length).map_err(|_| InputError {
        status: STATUS_INPUT_TOO_LARGE,
        message: b"input length is not representable",
    })
}

unsafe fn input_bytes<'a>(
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
) -> Result<&'a [u8], InputError> {
    let length = unsafe { checked_input_length(validator, input_length, 1) }?;
    if input_length != 0 && input.is_null() {
        return Err(InputError {
            status: STATUS_INVALID_ARGUMENT,
            message: b"input is null",
        });
    }
    if length == 0 {
        Ok(&[])
    } else {
        Ok(unsafe { slice::from_raw_parts(input, length) })
    }
}

unsafe fn input_utf16<'a>(
    validator: *const tl_rust_validator,
    input: *const u16,
    input_length: u64,
) -> Result<&'a [u16], InputError> {
    let length = unsafe { checked_input_length(validator, input_length, 2) }?;
    if input_length != 0 && input.is_null() {
        return Err(InputError {
            status: STATUS_INVALID_ARGUMENT,
            message: b"input is null",
        });
    }
    if length == 0 {
        Ok(&[])
    } else {
        Ok(unsafe { slice::from_raw_parts(input, length) })
    }
}

fn validate_relative_path(bytes: &[u8]) -> bool {
    if bytes.is_empty() || bytes[0] == b'/' || bytes.contains(&0) {
        return false;
    }
    if bytes.contains(&b'\\') {
        return false;
    }

    for component in bytes.split(|byte| *byte == b'/') {
        if component == b"." || component == b".." {
            return false;
        }
    }
    true
}

fn validate_c_drive_path(bytes: &[u8]) -> bool {
    if bytes.len() < 3
        || (bytes[0] != b'C' && bytes[0] != b'c')
        || bytes[1] != b':'
        || (bytes[2] != b'/' && bytes[2] != b'\\')
        || bytes
            .last()
            .is_some_and(|byte| *byte == b'/' || *byte == b'\\')
        || bytes.contains(&0)
    {
        return false;
    }

    let mut depth = 0usize;
    for component in bytes[3..].split(|byte| *byte == b'/' || *byte == b'\\') {
        if component.is_empty() || component == b"." {
            continue;
        }
        if component == b".." {
            if depth == 0 {
                return false;
            }
            depth -= 1;
        } else {
            depth = depth.saturating_add(1);
        }
    }
    true
}

fn validate_utf16(units: &[u16]) -> bool {
    let mut index = 0;
    while index < units.len() {
        let unit = units[index];
        if (0xd800..=0xdbff).contains(&unit) {
            if index + 1 >= units.len() || !(0xdc00..=0xdfff).contains(&units[index + 1]) {
                return false;
            }
            index += 2;
        } else if (0xdc00..=0xdfff).contains(&unit) {
            return false;
        } else {
            index += 1;
        }
    }
    true
}

struct PathValidationRequest {
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
    predicate: fn(&[u8]) -> bool,
    message: &'static [u8],
}

unsafe fn validate_path_ffi(request: PathValidationRequest) -> u32 {
    let bytes = match unsafe { input_bytes(request.validator, request.input, request.input_length) }
    {
        Ok(bytes) => bytes,
        Err(error) => {
            return write_message(
                error.status,
                error.message,
                request.error_buffer,
                request.error_capacity,
                request.error_required,
            )
        }
    };
    if !(request.predicate)(bytes) {
        return write_message(
            STATUS_INVALID_PATH,
            request.message,
            request.error_buffer,
            request.error_capacity,
            request.error_required,
        );
    }
    write_message(
        STATUS_OK,
        b"",
        request.error_buffer,
        request.error_capacity,
        request.error_required,
    )
}

/// Creates an opaque validator handle.
///
/// # Safety
/// `out` must be null or point to writable storage for one handle pointer. On
/// success, the caller owns the returned handle and must destroy it exactly
/// once with `tl_rust_validator_destroy`.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_create(
    max_input_bytes: u64,
    out: *mut *mut tl_rust_validator,
) -> u32 {
    run_ffi(|| {
        if out.is_null() || max_input_bytes == 0 {
            return STATUS_INVALID_ARGUMENT;
        }
        unsafe {
            *out = std::ptr::null_mut();
        }
        let Some(validator) = allocate_validator(max_input_bytes) else {
            return STATUS_INTERNAL;
        };
        unsafe {
            *out = validator;
        }
        STATUS_OK
    })
}

/// Releases an opaque validator handle.
///
/// # Safety
/// `validator` must be null or a live handle returned by
/// `tl_rust_validator_create` that has not already been destroyed.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_destroy(validator: *mut tl_rust_validator) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !validator.is_null() {
            unsafe {
                release_validator(validator);
            }
        }
    }));
}

/// Validates a caller-owned UTF-8 byte buffer.
///
/// # Safety
/// Non-null input and output pointers must refer to valid memory for the
/// lengths supplied by the caller. The handle must remain live for the call.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_validate_utf8(
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| {
        let bytes = match unsafe { input_bytes(validator, input, input_length) } {
            Ok(bytes) => bytes,
            Err(error) => {
                return write_message(
                    error.status,
                    error.message,
                    error_buffer,
                    error_capacity,
                    error_required,
                )
            }
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

/// Validates a caller-owned UTF-16 code-unit buffer.
///
/// # Safety
/// Non-null input and output pointers must refer to valid memory for the
/// lengths supplied by the caller. The handle must remain live for the call.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_validate_utf16(
    validator: *const tl_rust_validator,
    input: *const u16,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| {
        let units = match unsafe { input_utf16(validator, input, input_length) } {
            Ok(units) => units,
            Err(error) => {
                return write_message(
                    error.status,
                    error.message,
                    error_buffer,
                    error_capacity,
                    error_required,
                )
            }
        };
        if !validate_utf16(units) {
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

/// Validates a caller-owned relative path byte buffer.
///
/// # Safety
/// Non-null input and output pointers must refer to valid memory for the
/// lengths supplied by the caller. The handle must remain live for the call.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_validate_relative_path(
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| unsafe {
        validate_path_ffi(PathValidationRequest {
            validator,
            input,
            input_length,
            error_buffer,
            error_capacity,
            error_required,
            predicate: validate_relative_path,
            message: b"relative path is invalid",
        })
    })
}

/// Validates a caller-owned `C:\\...` path byte buffer.
///
/// # Safety
/// Non-null input and output pointers must refer to valid memory for the
/// lengths supplied by the caller. The handle must remain live for the call.
#[no_mangle]
pub unsafe extern "C" fn tl_rust_validator_validate_c_drive_path(
    validator: *const tl_rust_validator,
    input: *const u8,
    input_length: u64,
    error_buffer: *mut c_char,
    error_capacity: u64,
    error_required: *mut u64,
) -> u32 {
    run_ffi(|| unsafe {
        validate_path_ffi(PathValidationRequest {
            validator,
            input,
            input_length,
            error_buffer,
            error_capacity,
            error_required,
            predicate: validate_c_drive_path,
            message: b"C drive path is invalid",
        })
    })
}

#[cfg(test)]
use std::cell::Cell;

#[cfg(test)]
thread_local! {
    static TEST_FAIL_NEXT_ALLOCATION: Cell<bool> = const { Cell::new(false) };
}

#[cfg(test)]
fn fail_next_allocation() {
    TEST_FAIL_NEXT_ALLOCATION.with(|fail| fail.set(true));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_validator(max_input_bytes: u64) -> *mut tl_rust_validator {
        let mut validator = std::ptr::null_mut();
        let status = unsafe { tl_rust_validator_create(max_input_bytes, &mut validator) };
        assert_eq!(status, STATUS_OK);
        assert!(!validator.is_null());
        validator
    }

    fn next_byte(state: &mut u64) -> u8 {
        *state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        (*state >> 32) as u8
    }

    fn validate_bytes(
        validator: *const tl_rust_validator,
        function: unsafe extern "C" fn(
            *const tl_rust_validator,
            *const u8,
            u64,
            *mut c_char,
            u64,
            *mut u64,
        ) -> u32,
        input: &[u8],
    ) -> u32 {
        let mut error = [0_i8; 64];
        let mut required = 0;
        unsafe {
            function(
                validator,
                input.as_ptr(),
                input.len() as u64,
                error.as_mut_ptr().cast::<c_char>(),
                error.len() as u64,
                &mut required,
            )
        }
    }

    #[test]
    fn relative_paths_match_profile_contract() {
        assert!(validate_relative_path(b"fixture.dat"));
        assert!(validate_relative_path("nested/arquivo-é.dat".as_bytes()));
        assert!(validate_relative_path(b"C:/compat-as-relative.dat"));
        assert!(!validate_relative_path(b""));
        assert!(!validate_relative_path(b"/absolute.dat"));
        assert!(!validate_relative_path(b"nested/../outside.dat"));
        assert!(!validate_relative_path(b"nested\\outside.dat"));
        assert!(!validate_relative_path(b"bad\0name"));
    }

    #[test]
    fn c_drive_paths_reject_escape_and_invalid_roots() {
        assert!(validate_c_drive_path(b"C:\\Fixture\\compat.dat"));
        assert!(validate_c_drive_path(b"c:/Fixture/compat.dat"));
        assert!(validate_c_drive_path(b"C:\\a\\..\\b.dat"));
        assert!(validate_c_drive_path(b"C:\\a//b.dat"));
        assert!(!validate_c_drive_path(b"D:\\Fixture\\file.dat"));
        assert!(!validate_c_drive_path(b"C:\\..\\outside.dat"));
        assert!(!validate_c_drive_path(b"C:\\bad\0name"));
        assert!(!validate_c_drive_path(b"C:\\"));
    }

    #[test]
    fn utf8_rejects_truncation_overlong_and_surrogate_sequences() {
        for valid in [
            b"".as_slice(),
            b"ascii".as_slice(),
            "é".as_bytes(),
            "𐍈".as_bytes(),
        ] {
            assert!(str::from_utf8(valid).is_ok());
        }
        for invalid in [
            [0xc2].as_slice(),
            [0xe0, 0x80, 0x80].as_slice(),
            [0xed, 0xa0, 0x80].as_slice(),
            [0xf0, 0x80, 0x80, 0x80].as_slice(),
            [0x80].as_slice(),
            [0xf4, 0x90, 0x80, 0x80].as_slice(),
        ] {
            assert!(str::from_utf8(invalid).is_err());
        }
    }

    #[test]
    fn utf16_validates_bmp_pairs_and_isolated_surrogates() {
        assert!(validate_utf16(&[]));
        assert!(validate_utf16(&[0x004c, 0x00e9]));
        assert!(validate_utf16(&[0xd83d, 0xde00]));
        assert!(!validate_utf16(&[0xd800]));
        assert!(!validate_utf16(&[0xdc00]));
        assert!(!validate_utf16(&[0xd800, 0x0041]));
        assert!(!validate_utf16(&[0xde00, 0xd83d]));
    }

    #[test]
    fn generated_inputs_preserve_path_invariants() {
        let mut state = 0x6c65_7869_6361_6c31;
        for _ in 0..2048 {
            let length = (next_byte(&mut state) as usize) % 96;
            let mut bytes = Vec::with_capacity(length);
            for _ in 0..length {
                let mut byte = next_byte(&mut state);
                if byte == 0 {
                    byte = b'x';
                }
                bytes.push(byte);
            }

            if validate_relative_path(&bytes) {
                assert!(!bytes.is_empty());
                assert_ne!(bytes[0], b'/');
                assert!(!bytes.contains(&b'\\'));
                assert!(!bytes.contains(&0));
                assert!(!bytes
                    .split(|byte| *byte == b'/')
                    .any(|component| component == b"." || component == b".."));
            }

            let mut c_drive = b"C:\\".to_vec();
            c_drive.extend_from_slice(&bytes);
            if validate_c_drive_path(&c_drive) {
                assert!(c_drive.len() >= 3);
                assert!(c_drive[0] == b'C' || c_drive[0] == b'c');
                assert_eq!(c_drive[1], b':');
                assert!(c_drive[2] == b'/' || c_drive[2] == b'\\');
                assert!(!c_drive.ends_with(b"/"));
                assert!(!c_drive.ends_with(b"\\"));
                assert!(!c_drive.contains(&0));
            }
        }
    }

    #[test]
    fn ffi_handles_limits_and_utf16_byte_overflow() {
        let validator = make_validator(4);
        let mut error = [0_i8; 64];
        let mut required = 0;
        let bytes = [b'a'; 4];
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    bytes.as_ptr(),
                    bytes.len() as u64,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_OK
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    bytes.as_ptr(),
                    u64::MAX,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_INPUT_TOO_LARGE
        );

        let units = [0x0041_u16, 0x0042_u16];
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf16(
                    validator,
                    units.as_ptr(),
                    units.len() as u64,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_OK
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf16(
                    validator,
                    units.as_ptr(),
                    u64::MAX,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_INPUT_TOO_LARGE
        );
        unsafe { tl_rust_validator_destroy(validator) };
    }

    #[test]
    fn ffi_preserves_null_pointer_precedence_and_statuses() {
        let validator = make_validator(8);
        let mut error = [0_i8; 64];
        let mut required = 0;
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    std::ptr::null(),
                    0,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_OK
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    std::ptr::null(),
                    1,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_INVALID_ARGUMENT
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    std::ptr::null(),
                    9,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    &mut required,
                )
            },
            STATUS_INPUT_TOO_LARGE
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    b"x".as_ptr(),
                    1,
                    std::ptr::null_mut(),
                    0,
                    &mut required,
                )
            },
            STATUS_BUFFER_TOO_SMALL
        );
        assert_eq!(
            unsafe {
                tl_rust_validator_validate_utf8(
                    validator,
                    b"x".as_ptr(),
                    1,
                    error.as_mut_ptr().cast::<c_char>(),
                    error.len() as u64,
                    std::ptr::null_mut(),
                )
            },
            STATUS_INVALID_ARGUMENT
        );
        unsafe { tl_rust_validator_destroy(validator) };
    }

    #[test]
    fn allocation_failure_is_closed_and_does_not_publish_a_handle() {
        fail_next_allocation();
        let mut validator = std::ptr::dangling_mut::<tl_rust_validator>();
        assert_eq!(
            unsafe { tl_rust_validator_create(64, &mut validator) },
            STATUS_INTERNAL
        );
        assert!(validator.is_null());
    }

    #[test]
    fn panics_are_converted_to_internal_status() {
        assert_eq!(run_ffi(|| panic!("test panic")), STATUS_INTERNAL);
    }

    #[test]
    fn ffi_path_functions_accept_and_reject_expected_inputs() {
        let validator = make_validator(64);
        assert_eq!(
            validate_bytes(validator, tl_rust_validator_validate_relative_path, b"a/b"),
            STATUS_OK
        );
        assert_eq!(
            validate_bytes(
                validator,
                tl_rust_validator_validate_relative_path,
                b"a/../b"
            ),
            STATUS_INVALID_PATH
        );
        assert_eq!(
            validate_bytes(
                validator,
                tl_rust_validator_validate_c_drive_path,
                b"C:\\a\\b"
            ),
            STATUS_OK
        );
        assert_eq!(
            validate_bytes(
                validator,
                tl_rust_validator_validate_c_drive_path,
                b"C:\\..\\b"
            ),
            STATUS_INVALID_PATH
        );
        unsafe { tl_rust_validator_destroy(validator) };
    }
}
