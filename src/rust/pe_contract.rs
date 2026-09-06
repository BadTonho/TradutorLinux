#[repr(C)]
struct TlPeErrorV1 {
    code: u32,
    phase: u32,
    input_offset: u64,
    detail_value: u64,
}

const STATUS_SUCCESS: u32 = 0;
const STATUS_TRUNCATED: u32 = 1;
const STATUS_MALFORMED: u32 = 2;
const STATUS_UNSUPPORTED_ARCHITECTURE: u32 = 3;
const STATUS_UNSUPPORTED_FORMAT: u32 = 4;
const STATUS_UNSUPPORTED_MECHANISM: u32 = 5;
const STATUS_INVALID_ARGUMENT: u32 = 6;
const STATUS_BUFFER_TOO_SMALL: u32 = 7;
const STATUS_INPUT_TOO_LARGE: u32 = 8;
const STATUS_OUTPUT_TOO_LARGE: u32 = 9;
const STATUS_INTERNAL: u32 = 10;

const WIRE_HEADER_SIZE: u32 = 416;
const WIRE_TABLE_DESCRIPTOR_SIZE: u32 = 24;
const WIRE_TABLE_COUNT: u32 = 16;
const WIRE_INFO_STRIDE: u32 = 136;
const WIRE_SECTION_STRIDE: u32 = 40;
const WIRE_IMPORT_DLL_STRIDE: u32 = 40;
const WIRE_IMPORT_SYMBOL_STRIDE: u32 = 32;
const WIRE_EXPORT_STRIDE: u32 = 48;
const WIRE_TLS_CALLBACK_STRIDE: u32 = 8;
const WIRE_RUNTIME_FUNCTION_STRIDE: u32 = 24;
const WIRE_UNWIND_INFO_STRIDE: u32 = 72;
const WIRE_UNWIND_CODE_STRIDE: u32 = 8;
const WIRE_UNWIND_EPILOG_STRIDE: u32 = 8;
const WIRE_RELOC_BLOCK_STRIDE: u32 = 32;
const WIRE_RELOC_ENTRY_STRIDE: u32 = 8;
const WIRE_STRING_REF_SIZE: u32 = 16;
const WIRE_STRING_RECORD_HEADER_SIZE: u32 = 8;
const LIMIT_MAX_SERIALIZED_BYTES: u64 = 268_435_456;

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn error_record_has_c_layout() {
        let record = TlPeErrorV1 {
            code: 1,
            phase: 2,
            input_offset: 3,
            detail_value: 4,
        };
        assert_eq!(size_of::<TlPeErrorV1>(), 24);
        assert_eq!(align_of::<TlPeErrorV1>(), 8);
        assert_eq!(record.code, 1);
        assert_eq!(record.phase, 2);
        assert_eq!(record.input_offset, 3);
        assert_eq!(record.detail_value, 4);
    }

    #[test]
    fn statuses_and_wire_sizes_are_stable() {
        assert_eq!(STATUS_SUCCESS, 0);
        assert_eq!(STATUS_TRUNCATED, 1);
        assert_eq!(STATUS_MALFORMED, 2);
        assert_eq!(STATUS_UNSUPPORTED_ARCHITECTURE, 3);
        assert_eq!(STATUS_UNSUPPORTED_FORMAT, 4);
        assert_eq!(STATUS_UNSUPPORTED_MECHANISM, 5);
        assert_eq!(STATUS_INVALID_ARGUMENT, 6);
        assert_eq!(STATUS_BUFFER_TOO_SMALL, 7);
        assert_eq!(STATUS_INPUT_TOO_LARGE, 8);
        assert_eq!(STATUS_OUTPUT_TOO_LARGE, 9);
        assert_eq!(STATUS_INTERNAL, 10);

        assert_eq!(WIRE_HEADER_SIZE, 416);
        assert_eq!(WIRE_TABLE_DESCRIPTOR_SIZE, 24);
        assert_eq!(WIRE_TABLE_COUNT, 16);
        assert_eq!(WIRE_INFO_STRIDE, 136);
        assert_eq!(WIRE_SECTION_STRIDE, 40);
        assert_eq!(WIRE_IMPORT_DLL_STRIDE, 40);
        assert_eq!(WIRE_IMPORT_SYMBOL_STRIDE, 32);
        assert_eq!(WIRE_EXPORT_STRIDE, 48);
        assert_eq!(WIRE_TLS_CALLBACK_STRIDE, 8);
        assert_eq!(WIRE_RUNTIME_FUNCTION_STRIDE, 24);
        assert_eq!(WIRE_UNWIND_INFO_STRIDE, 72);
        assert_eq!(WIRE_UNWIND_CODE_STRIDE, 8);
        assert_eq!(WIRE_UNWIND_EPILOG_STRIDE, 8);
        assert_eq!(WIRE_RELOC_BLOCK_STRIDE, 32);
        assert_eq!(WIRE_RELOC_ENTRY_STRIDE, 8);
        assert_eq!(WIRE_STRING_REF_SIZE, 16);
        assert_eq!(WIRE_STRING_RECORD_HEADER_SIZE, 8);
        assert_eq!(LIMIT_MAX_SERIALIZED_BYTES, 268_435_456);
    }
}
