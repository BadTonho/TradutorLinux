#![allow(dead_code)]

use std::os::raw::c_char;

#[repr(C)]
pub struct TlProfileIdentityV1 {
    pub app_id: *const u8,
    pub app_id_length: u64,
    pub app_sha256: *const u8,
    pub app_sha256_length: u64,
    pub app_version: *const u8,
    pub app_version_length: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TlProfileErrorV1 {
    pub code: u32,
    pub phase: u32,
    pub input_offset: u64,
    pub detail_value: u64,
}

pub const STATUS_SUCCESS: u32 = 0;
pub const STATUS_MALFORMED: u32 = 1;
pub const STATUS_UNSUPPORTED_FORMAT: u32 = 2;
pub const STATUS_INVALID_ARGUMENT: u32 = 3;
pub const STATUS_BUFFER_TOO_SMALL: u32 = 4;
pub const STATUS_INPUT_TOO_LARGE: u32 = 5;
pub const STATUS_OUTPUT_TOO_LARGE: u32 = 6;
pub const STATUS_INTERNAL: u32 = 7;

pub const ERROR_NONE: u32 = 0;
pub const ERROR_INVALID_ARGUMENT: u32 = 1;
pub const ERROR_BUFFER_TOO_SMALL: u32 = 2;
pub const ERROR_INPUT_TOO_LARGE: u32 = 3;
pub const ERROR_OUTPUT_TOO_LARGE: u32 = 4;
pub const ERROR_INTERNAL: u32 = 5;
pub const ERROR_JSON_SYNTAX: u32 = 16;
pub const ERROR_JSON_FIELD: u32 = 17;
pub const ERROR_SCHEMA: u32 = 18;
pub const ERROR_IDENTITY: u32 = 19;
pub const ERROR_PATH: u32 = 20;
pub const ERROR_BACKEND: u32 = 21;
pub const ERROR_WIRE_FORMAT: u32 = 22;

pub const PHASE_NONE: u32 = 0;
pub const PHASE_INPUT: u32 = 1;
pub const PHASE_JSON: u32 = 2;
pub const PHASE_SCHEMA: u32 = 3;
pub const PHASE_IDENTITY: u32 = 4;
pub const PHASE_PATHS: u32 = 5;
pub const PHASE_SERIALIZE: u32 = 6;
pub const PHASE_WIRE: u32 = 7;

pub const UNKNOWN_OFFSET: u64 = u64::MAX;

pub const WIRE_MAGIC: [u8; 4] = *b"TLPR";
pub const WIRE_MAJOR: u16 = 1;
pub const WIRE_MINOR: u16 = 0;
pub const WIRE_HEADER_SIZE: usize = 128;
pub const WIRE_DESCRIPTOR_SIZE: usize = 24;
pub const WIRE_DESCRIPTOR_OFFSET: usize = 32;
pub const WIRE_TABLE_COUNT: usize = 4;
pub const WIRE_VARIABLE_RECORDS: u32 = 1;
pub const WIRE_INFO_STRIDE: usize = 96;
pub const WIRE_FILE_STRIDE: usize = 32;
pub const WIRE_DLL_STRIDE: usize = 32;
pub const WIRE_STRING_RECORD_HEADER_SIZE: usize = 8;
pub const WIRE_STRING_REF_SIZE: usize = 16;

pub const WIRE_TABLE_INFO: usize = 0;
pub const WIRE_TABLE_FILES: usize = 1;
pub const WIRE_TABLE_DLLS: usize = 2;
pub const WIRE_TABLE_STRINGS: usize = 3;

pub const WIRE_INFO_SCHEMA_OFFSET: usize = 0;
pub const WIRE_INFO_BACKEND_OFFSET: usize = 4;
pub const WIRE_INFO_FLAGS_OFFSET: usize = 8;
pub const WIRE_INFO_RESERVED_OFFSET: usize = 12;
pub const WIRE_INFO_APP_ID_OFFSET: usize = 16;
pub const WIRE_INFO_SHA256_OFFSET: usize = 32;
pub const WIRE_INFO_VERSION_OFFSET: usize = 48;
pub const WIRE_INFO_MIN_VERSION_OFFSET: usize = 64;
pub const WIRE_INFO_EXTENSION_OFFSET: usize = 80;
pub const WIRE_INFO_FLAG_BACKEND_DECLARED: u32 = 1;
pub const WIRE_INFO_FLAG_EXTENSION_DECLARED: u32 = 2;
pub const WIRE_BACKEND_NATIVE: u32 = 0;
pub const WIRE_BACKEND_PROTON: u32 = 1;

pub const WIRE_FILE_SOURCE_OFFSET: usize = 0;
pub const WIRE_FILE_TARGET_OFFSET: usize = 16;
pub const WIRE_DLL_MODULE_OFFSET: usize = 0;
pub const WIRE_DLL_SOURCE_OFFSET: usize = 16;

pub const LIMIT_MAX_INPUT_BYTES: u64 = 1_048_576;
pub const LIMIT_MAX_FILES: u64 = 65_535;
pub const LIMIT_MAX_DLLS: u64 = 65_535;
pub const LIMIT_MAX_STRINGS: u64 = 262_144;
pub const LIMIT_MAX_STRING_BYTES: u64 = 1_048_576;
pub const LIMIT_MAX_SERIALIZED_BYTES: u64 = 67_108_864;

#[allow(dead_code)]
pub type ProfileChar = c_char;

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn identity_and_error_records_have_stable_c_layout() {
        assert_eq!(size_of::<TlProfileIdentityV1>(), 48);
        assert_eq!(align_of::<TlProfileIdentityV1>(), 8);
        assert_eq!(size_of::<TlProfileErrorV1>(), 24);
        assert_eq!(align_of::<TlProfileErrorV1>(), 8);
    }

    #[test]
    fn wire_constants_are_stable() {
        assert_eq!(WIRE_MAGIC, *b"TLPR");
        assert_eq!(WIRE_MAJOR, 1);
        assert_eq!(WIRE_MINOR, 0);
        assert_eq!(WIRE_HEADER_SIZE, 128);
        assert_eq!(WIRE_DESCRIPTOR_SIZE, 24);
        assert_eq!(WIRE_DESCRIPTOR_OFFSET, 32);
        assert_eq!(WIRE_TABLE_COUNT, 4);
        assert_eq!(WIRE_INFO_STRIDE, 96);
        assert_eq!(WIRE_FILE_STRIDE, 32);
        assert_eq!(WIRE_DLL_STRIDE, 32);
        assert_eq!(WIRE_STRING_RECORD_HEADER_SIZE, 8);
        assert_eq!(WIRE_STRING_REF_SIZE, 16);
        assert_eq!(LIMIT_MAX_INPUT_BYTES, 1_048_576);
        assert_eq!(LIMIT_MAX_SERIALIZED_BYTES, 67_108_864);
    }
}
