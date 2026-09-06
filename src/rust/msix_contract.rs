#![allow(dead_code)]

#[repr(C)]
pub struct TlMsixErrorV1 {
    pub code: u32,
    pub phase: u32,
    pub input_offset: u64,
    pub detail_value: u64,
}

pub const STATUS_SUCCESS: u32 = 0;
pub const STATUS_TRUNCATED: u32 = 1;
pub const STATUS_MALFORMED: u32 = 2;
pub const STATUS_UNSUPPORTED_FORMAT: u32 = 3;
pub const STATUS_UNSUPPORTED_MECHANISM: u32 = 4;
pub const STATUS_INVALID_ARGUMENT: u32 = 5;
pub const STATUS_BUFFER_TOO_SMALL: u32 = 6;
pub const STATUS_INPUT_TOO_LARGE: u32 = 7;
pub const STATUS_OUTPUT_TOO_LARGE: u32 = 8;
pub const STATUS_INTERNAL: u32 = 9;

pub const ERROR_NONE: u32 = 0;
pub const ERROR_INVALID_ARGUMENT: u32 = 1;
pub const ERROR_BUFFER_TOO_SMALL: u32 = 2;
pub const ERROR_INPUT_TOO_LARGE: u32 = 3;
pub const ERROR_OUTPUT_TOO_LARGE: u32 = 4;
pub const ERROR_INTERNAL: u32 = 5;
pub const ERROR_ZIP_EOCD: u32 = 16;
pub const ERROR_ZIP_CENTRAL_DIRECTORY: u32 = 17;
pub const ERROR_ZIP_LOCAL_HEADER: u32 = 18;
pub const ERROR_ZIP_ENTRY: u32 = 19;
pub const ERROR_ZIP_PATH: u32 = 20;
pub const ERROR_ZIP_LINK: u32 = 21;
pub const ERROR_ZIP_COMPRESSION: u32 = 22;
pub const ERROR_ZIP_CRC: u32 = 23;
pub const ERROR_XML_SYNTAX: u32 = 24;
pub const ERROR_XML_ENTITY: u32 = 25;
pub const ERROR_XML_LIMIT: u32 = 26;
pub const ERROR_MANIFEST: u32 = 27;
pub const ERROR_WIRE_FORMAT: u32 = 28;

pub const PHASE_NONE: u32 = 0;
pub const PHASE_INPUT: u32 = 1;
pub const PHASE_ZIP_EOCD: u32 = 2;
pub const PHASE_ZIP_CENTRAL_DIRECTORY: u32 = 3;
pub const PHASE_ZIP_ENTRY: u32 = 4;
pub const PHASE_ZIP_DECOMPRESS: u32 = 5;
pub const PHASE_XML: u32 = 6;
pub const PHASE_MANIFEST: u32 = 7;
pub const PHASE_SERIALIZE: u32 = 8;
pub const PHASE_WIRE: u32 = 9;

pub const UNKNOWN_OFFSET: u64 = u64::MAX;

pub const WIRE_MAGIC: [u8; 4] = *b"TLMS";
pub const WIRE_MAJOR: u16 = 1;
pub const WIRE_MINOR: u16 = 0;
pub const WIRE_HEADER_SIZE: usize = 128;
pub const WIRE_DESCRIPTOR_SIZE: usize = 24;
pub const WIRE_DESCRIPTOR_OFFSET: usize = 32;
pub const WIRE_TABLE_COUNT: usize = 4;
pub const WIRE_VARIABLE_RECORDS: u32 = 1;
pub const WIRE_INFO_STRIDE: usize = 64;
pub const WIRE_APPLICATION_STRIDE: usize = 64;
pub const WIRE_STRING_REF_SIZE: usize = 16;
pub const WIRE_STRING_RECORD_HEADER_SIZE: usize = 8;
pub const WIRE_INFO_PACKAGE_NAME_OFFSET: usize = 0;
pub const WIRE_INFO_PUBLISHER_OFFSET: usize = 16;
pub const WIRE_INFO_VERSION_OFFSET: usize = 32;
pub const WIRE_INFO_MAIN_EXECUTABLE_OFFSET: usize = 48;
pub const WIRE_APPLICATION_ID_OFFSET: usize = 0;
pub const WIRE_APPLICATION_EXECUTABLE_OFFSET: usize = 16;
pub const WIRE_APPLICATION_DISPLAY_NAME_OFFSET: usize = 32;
pub const WIRE_APPLICATION_ENTRY_POINT_OFFSET: usize = 48;

pub const LIMIT_MAX_PACKAGE_BYTES: u64 = 2_147_483_648;
pub const LIMIT_MAX_MANIFEST_BYTES: u64 = 16_777_216;
pub const LIMIT_MAX_MANIFEST_COMPRESSED_BYTES: u64 = 67_108_864;
pub const LIMIT_MAX_ZIP_ENTRIES: u64 = 10_000;
pub const LIMIT_MAX_ZIP_FILENAME_BYTES: u64 = 4_096;
pub const LIMIT_MAX_ENTRY_UNCOMPRESSED_BYTES: u64 = 536_870_912;
pub const LIMIT_MAX_PACKAGE_UNCOMPRESSED_BYTES: u64 = 536_870_912;
pub const LIMIT_MAX_XML_DEPTH: usize = 128;
pub const LIMIT_MAX_XML_ATTRIBUTES: usize = 256;
pub const LIMIT_MAX_STRINGS: u64 = 50_000;
pub const LIMIT_MAX_SERIALIZED_BYTES: u64 = 67_108_864;

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn error_record_has_c_layout() {
        let error = TlMsixErrorV1 {
            code: 1,
            phase: 2,
            input_offset: 3,
            detail_value: 4,
        };
        assert_eq!(size_of::<TlMsixErrorV1>(), 24);
        assert_eq!(align_of::<TlMsixErrorV1>(), 8);
        assert_eq!(error.code, 1);
        assert_eq!(error.phase, 2);
        assert_eq!(error.input_offset, 3);
        assert_eq!(error.detail_value, 4);
    }

    #[test]
    fn statuses_wire_and_limits_are_stable() {
        assert_eq!(STATUS_SUCCESS, 0);
        assert_eq!(STATUS_TRUNCATED, 1);
        assert_eq!(STATUS_MALFORMED, 2);
        assert_eq!(STATUS_UNSUPPORTED_FORMAT, 3);
        assert_eq!(STATUS_UNSUPPORTED_MECHANISM, 4);
        assert_eq!(STATUS_INVALID_ARGUMENT, 5);
        assert_eq!(STATUS_BUFFER_TOO_SMALL, 6);
        assert_eq!(STATUS_INPUT_TOO_LARGE, 7);
        assert_eq!(STATUS_OUTPUT_TOO_LARGE, 8);
        assert_eq!(STATUS_INTERNAL, 9);
        assert_eq!(WIRE_MAGIC, *b"TLMS");
        assert_eq!(WIRE_MAJOR, 1);
        assert_eq!(WIRE_MINOR, 0);
        assert_eq!(WIRE_HEADER_SIZE, 128);
        assert_eq!(WIRE_DESCRIPTOR_SIZE, 24);
        assert_eq!(WIRE_DESCRIPTOR_OFFSET, 32);
        assert_eq!(WIRE_TABLE_COUNT, 4);
        assert_eq!(WIRE_INFO_STRIDE, 64);
        assert_eq!(WIRE_APPLICATION_STRIDE, 64);
        assert_eq!(WIRE_STRING_REF_SIZE, 16);
        assert_eq!(WIRE_STRING_RECORD_HEADER_SIZE, 8);
        assert_eq!(LIMIT_MAX_PACKAGE_BYTES, 2_147_483_648);
        assert_eq!(LIMIT_MAX_SERIALIZED_BYTES, 67_108_864);
    }
}
