#![allow(dead_code)]

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TlAppCatalogErrorV1 {
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
pub const ERROR_VALUE: u32 = 19;
pub const ERROR_LIMIT: u32 = 20;
pub const ERROR_WIRE_FORMAT: u32 = 21;

pub const PHASE_NONE: u32 = 0;
pub const PHASE_INPUT: u32 = 1;
pub const PHASE_JSON: u32 = 2;
pub const PHASE_SCHEMA: u32 = 3;
pub const PHASE_SERIALIZE: u32 = 4;
pub const PHASE_WIRE: u32 = 5;

pub const UNKNOWN_OFFSET: u64 = u64::MAX;

pub const WIRE_MAGIC: [u8; 4] = *b"TLAC";
pub const WIRE_MAJOR: u16 = 1;
pub const WIRE_MINOR: u16 = 0;
pub const WIRE_HEADER_SIZE: usize = 128;
pub const WIRE_DESCRIPTOR_SIZE: usize = 24;
pub const WIRE_DESCRIPTOR_OFFSET: usize = 32;
pub const WIRE_TABLE_COUNT: usize = 4;
pub const WIRE_VARIABLE_RECORDS: u32 = 1;

pub const TABLE_INFO: usize = 0;
pub const TABLE_APPS: usize = 1;
pub const TABLE_ARGS: usize = 2;
pub const TABLE_STRINGS: usize = 3;

pub const INFO_STRIDE: usize = 32;
pub const APP_STRIDE: usize = 192;
pub const ARG_STRIDE: usize = 16;
pub const STRING_RECORD_HEADER_SIZE: usize = 8;
pub const STRING_REF_SIZE: usize = 16;

pub const INFO_VERSION_OFFSET: usize = 0;
pub const INFO_FLAGS_OFFSET: usize = 4;
pub const INFO_APP_COUNT_OFFSET: usize = 8;
pub const INFO_ARG_COUNT_OFFSET: usize = 16;
pub const INFO_RESERVED_OFFSET: usize = 24;

pub const APP_ID_OFFSET: usize = 0;
pub const APP_NAME_OFFSET: usize = 16;
pub const APP_EXECUTABLE_PATH_OFFSET: usize = 32;
pub const APP_PREFIX_PATH_OFFSET: usize = 48;
pub const APP_ICON_PATH_OFFSET: usize = 64;
pub const APP_WORKING_DIRECTORY_OFFSET: usize = 80;
pub const APP_SHA256_OFFSET: usize = 96;
pub const APP_VERSION_OFFSET: usize = 112;
pub const APP_CREATED_AT_OFFSET: usize = 128;
pub const APP_ARGS_INDEX_OFFSET: usize = 144;
pub const APP_ARGS_COUNT_OFFSET: usize = 152;
pub const APP_CPU_LIMIT_OFFSET: usize = 160;
pub const APP_MEMORY_LIMIT_OFFSET: usize = 168;
pub const APP_RESERVED_OFFSET: usize = 176;

pub const LIMIT_MAX_INPUT_BYTES: u64 = 4 * 1024 * 1024;
pub const LIMIT_MAX_APPS: u64 = 65_535;
pub const LIMIT_MAX_ARGS: u64 = 262_144;
pub const LIMIT_MAX_STRINGS: u64 = 1_048_576;
pub const LIMIT_MAX_STRING_BYTES: u64 = 1_048_576;
pub const LIMIT_MAX_DECODED_STRING_BYTES: u64 = 16 * 1024 * 1024;
pub const LIMIT_MAX_SERIALIZED_BYTES: u64 = 64 * 1024 * 1024;

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn error_record_has_stable_c_layout() {
        assert_eq!(size_of::<TlAppCatalogErrorV1>(), 24);
        assert_eq!(align_of::<TlAppCatalogErrorV1>(), 8);
    }

    #[test]
    fn wire_constants_are_stable() {
        assert_eq!(WIRE_MAGIC, *b"TLAC");
        assert_eq!(WIRE_HEADER_SIZE, 128);
        assert_eq!(WIRE_DESCRIPTOR_SIZE, 24);
        assert_eq!(WIRE_DESCRIPTOR_OFFSET, 32);
        assert_eq!(WIRE_TABLE_COUNT, 4);
        assert_eq!(INFO_STRIDE, 32);
        assert_eq!(APP_STRIDE, 192);
        assert_eq!(ARG_STRIDE, 16);
        assert_eq!(STRING_RECORD_HEADER_SIZE, 8);
        assert_eq!(LIMIT_MAX_INPUT_BYTES, 4 * 1024 * 1024);
        assert_eq!(LIMIT_MAX_SERIALIZED_BYTES, 64 * 1024 * 1024);
    }
}
