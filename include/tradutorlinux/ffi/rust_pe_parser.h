#ifndef TRADUTORLINUX_FFI_RUST_PE_PARSER_H
#define TRADUTORLINUX_FFI_RUST_PE_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t tl_pe_status_t;
typedef uint32_t tl_pe_error_code_t;
typedef uint32_t tl_pe_error_phase_t;

/* Parse results. Values 0..5 correspond to the C++ ParseStatus order. */
#define TL_PE_STATUS_SUCCESS UINT32_C(0)
#define TL_PE_STATUS_TRUNCATED UINT32_C(1)
#define TL_PE_STATUS_MALFORMED UINT32_C(2)
#define TL_PE_STATUS_UNSUPPORTED_ARCHITECTURE UINT32_C(3)
#define TL_PE_STATUS_UNSUPPORTED_FORMAT UINT32_C(4)
#define TL_PE_STATUS_UNSUPPORTED_MECHANISM UINT32_C(5)
#define TL_PE_STATUS_INVALID_ARGUMENT UINT32_C(6)
#define TL_PE_STATUS_BUFFER_TOO_SMALL UINT32_C(7)
#define TL_PE_STATUS_INPUT_TOO_LARGE UINT32_C(8)
#define TL_PE_STATUS_OUTPUT_TOO_LARGE UINT32_C(9)
#define TL_PE_STATUS_INTERNAL UINT32_C(10)

/* Stable machine-readable reason categories for tl_pe_error_v1. */
#define TL_PE_ERROR_NONE UINT32_C(0)
#define TL_PE_ERROR_INVALID_ARGUMENT UINT32_C(1)
#define TL_PE_ERROR_BUFFER_TOO_SMALL UINT32_C(2)
#define TL_PE_ERROR_INPUT_TOO_LARGE UINT32_C(3)
#define TL_PE_ERROR_OUTPUT_TOO_LARGE UINT32_C(4)
#define TL_PE_ERROR_INTERNAL UINT32_C(5)
#define TL_PE_ERROR_DOS_HEADER UINT32_C(16)
#define TL_PE_ERROR_PE_HEADER UINT32_C(17)
#define TL_PE_ERROR_COFF_HEADER UINT32_C(18)
#define TL_PE_ERROR_OPTIONAL_HEADER UINT32_C(19)
#define TL_PE_ERROR_SECTION_TABLE UINT32_C(20)
#define TL_PE_ERROR_DIRECTORY_RANGE UINT32_C(21)
#define TL_PE_ERROR_IMPORT_TABLE UINT32_C(22)
#define TL_PE_ERROR_DELAY_IMPORT_TABLE UINT32_C(23)
#define TL_PE_ERROR_EXPORT_TABLE UINT32_C(24)
#define TL_PE_ERROR_TLS_DIRECTORY UINT32_C(25)
#define TL_PE_ERROR_UNWIND_DIRECTORY UINT32_C(26)
#define TL_PE_ERROR_RELOCATION_DIRECTORY UINT32_C(27)
#define TL_PE_ERROR_SERIALIZATION_LIMIT UINT32_C(28)
#define TL_PE_ERROR_WIRE_FORMAT UINT32_C(29)

/* Stable locations used to qualify an error code. */
#define TL_PE_ERROR_PHASE_NONE UINT32_C(0)
#define TL_PE_ERROR_PHASE_INPUT UINT32_C(1)
#define TL_PE_ERROR_PHASE_DOS_HEADER UINT32_C(2)
#define TL_PE_ERROR_PHASE_COFF_HEADER UINT32_C(3)
#define TL_PE_ERROR_PHASE_OPTIONAL_HEADER UINT32_C(4)
#define TL_PE_ERROR_PHASE_SECTIONS UINT32_C(5)
#define TL_PE_ERROR_PHASE_EXPORTS UINT32_C(6)
#define TL_PE_ERROR_PHASE_IMPORTS UINT32_C(7)
#define TL_PE_ERROR_PHASE_DELAY_IMPORTS UINT32_C(8)
#define TL_PE_ERROR_PHASE_TLS UINT32_C(9)
#define TL_PE_ERROR_PHASE_UNWIND UINT32_C(10)
#define TL_PE_ERROR_PHASE_RELOCATIONS UINT32_C(11)
#define TL_PE_ERROR_PHASE_SERIALIZE UINT32_C(12)
#define TL_PE_ERROR_PHASE_WIRE UINT32_C(13)

#define TL_PE_ERROR_OFFSET_UNKNOWN UINT64_MAX

/* Fixed-width diagnostic record; it contains no Rust-owned memory. */
typedef struct tl_pe_error_v1 {
    uint32_t code;
    uint32_t phase;
    uint64_t input_offset;
    uint64_t detail_value;
} tl_pe_error_v1;

/*
 * The serialized result starts with these bytes and fields. They are wire
 * offsets, not C struct offsets; producers and consumers must encode/decode
 * each integer as little-endian.
 */
#define TL_PE_WIRE_MAGIC_0 UINT8_C(0x54) /* T */
#define TL_PE_WIRE_MAGIC_1 UINT8_C(0x4C) /* L */
#define TL_PE_WIRE_MAGIC_2 UINT8_C(0x50) /* P */
#define TL_PE_WIRE_MAGIC_3 UINT8_C(0x45) /* E */
#define TL_PE_WIRE_MAJOR UINT16_C(1)
#define TL_PE_WIRE_MINOR UINT16_C(0)

#define TL_PE_WIRE_HEADER_SIZE UINT32_C(416)
#define TL_PE_WIRE_TABLE_DESCRIPTOR_SIZE UINT32_C(24)
#define TL_PE_WIRE_TABLE_DESCRIPTOR_OFFSET UINT32_C(32)
#define TL_PE_WIRE_TABLE_COUNT UINT32_C(16)
#define TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS UINT32_C(1)

#define TL_PE_WIRE_HEADER_MAGIC_OFFSET UINT32_C(0)
#define TL_PE_WIRE_HEADER_MAJOR_OFFSET UINT32_C(4)
#define TL_PE_WIRE_HEADER_MINOR_OFFSET UINT32_C(6)
#define TL_PE_WIRE_HEADER_SIZE_OFFSET UINT32_C(8)
#define TL_PE_WIRE_HEADER_TOTAL_SIZE_OFFSET UINT32_C(12)
#define TL_PE_WIRE_HEADER_TABLE_COUNT_OFFSET UINT32_C(20)

#define TL_PE_WIRE_DESCRIPTOR_OFFSET_FIELD UINT32_C(0)
#define TL_PE_WIRE_DESCRIPTOR_COUNT_FIELD UINT32_C(8)
#define TL_PE_WIRE_DESCRIPTOR_STRIDE_FIELD UINT32_C(16)
#define TL_PE_WIRE_DESCRIPTOR_FLAGS_FIELD UINT32_C(20)

/* Table identifiers. Descriptor 15 is reserved and must be empty in v1. */
#define TL_PE_WIRE_TABLE_INFO UINT32_C(0)
#define TL_PE_WIRE_TABLE_SECTIONS UINT32_C(1)
#define TL_PE_WIRE_TABLE_STRINGS UINT32_C(2)
#define TL_PE_WIRE_TABLE_IMPORT_DLLS UINT32_C(3)
#define TL_PE_WIRE_TABLE_IMPORT_SYMBOLS UINT32_C(4)
#define TL_PE_WIRE_TABLE_DELAY_IMPORT_DLLS UINT32_C(5)
#define TL_PE_WIRE_TABLE_DELAY_IMPORT_SYMBOLS UINT32_C(6)
#define TL_PE_WIRE_TABLE_EXPORTS UINT32_C(7)
#define TL_PE_WIRE_TABLE_TLS_CALLBACKS UINT32_C(8)
#define TL_PE_WIRE_TABLE_RUNTIME_FUNCTIONS UINT32_C(9)
#define TL_PE_WIRE_TABLE_UNWIND_INFOS UINT32_C(10)
#define TL_PE_WIRE_TABLE_UNWIND_CODES UINT32_C(11)
#define TL_PE_WIRE_TABLE_UNWIND_EPILOGS UINT32_C(12)
#define TL_PE_WIRE_TABLE_RELOC_BLOCKS UINT32_C(13)
#define TL_PE_WIRE_TABLE_RELOC_ENTRIES UINT32_C(14)
#define TL_PE_WIRE_TABLE_RESERVED UINT32_C(15)

/* Fixed record strides. The strings table is the only variable-record table. */
#define TL_PE_WIRE_INFO_STRIDE UINT32_C(136)
#define TL_PE_WIRE_SECTION_STRIDE UINT32_C(40)
#define TL_PE_WIRE_IMPORT_DLL_STRIDE UINT32_C(40)
#define TL_PE_WIRE_IMPORT_SYMBOL_STRIDE UINT32_C(32)
#define TL_PE_WIRE_EXPORT_STRIDE UINT32_C(48)
#define TL_PE_WIRE_TLS_CALLBACK_STRIDE UINT32_C(8)
#define TL_PE_WIRE_RUNTIME_FUNCTION_STRIDE UINT32_C(24)
#define TL_PE_WIRE_UNWIND_INFO_STRIDE UINT32_C(72)
#define TL_PE_WIRE_UNWIND_CODE_STRIDE UINT32_C(8)
#define TL_PE_WIRE_UNWIND_EPILOG_STRIDE UINT32_C(8)
#define TL_PE_WIRE_RELOC_BLOCK_STRIDE UINT32_C(32)
#define TL_PE_WIRE_RELOC_ENTRY_STRIDE UINT32_C(8)
#define TL_PE_WIRE_STRING_REF_SIZE UINT32_C(16)
#define TL_PE_WIRE_STRING_RECORD_HEADER_SIZE UINT32_C(8)

#define TL_PE_WIRE_INFO_FLAG_PE32_PLUS UINT32_C(1)
#define TL_PE_WIRE_INFO_FLAG_DLL UINT32_C(2)
#define TL_PE_WIRE_IMPORT_SYMBOL_FLAG_ORDINAL UINT32_C(1)
#define TL_PE_WIRE_EXPORT_FLAG_BY_NAME UINT32_C(1)
#define TL_PE_WIRE_EXPORT_FLAG_FORWARDER UINT32_C(2)
#define TL_PE_WIRE_UNWIND_FLAG_EXTENDED_SET_FPREG UINT32_C(1)
#define TL_PE_WIRE_UNWIND_FLAG_CHAINED_FUNCTION UINT32_C(2)

/* Limits carried from the C++ parser plus the explicit aggregate limits. */
#define TL_PE_LIMIT_MAX_SECTIONS UINT64_C(65535)
#define TL_PE_LIMIT_MAX_IMPORT_DLLS UINT64_C(1024)
#define TL_PE_LIMIT_MAX_SYMBOLS_PER_DLL UINT64_C(4096)
#define TL_PE_LIMIT_MAX_RELOC_BLOCKS UINT64_C(4096)
#define TL_PE_LIMIT_MAX_RUNTIME_FUNCTIONS UINT64_C(65536)
#define TL_PE_LIMIT_MAX_STRING_BYTES UINT64_C(65534)
#define TL_PE_LIMIT_MAX_EXPORT_FUNCTIONS UINT64_C(65536)
#define TL_PE_LIMIT_MAX_EXPORT_NAMES UINT64_C(65536)
#define TL_PE_LIMIT_MAX_TLS_CALLBACKS UINT64_C(65536)
#define TL_PE_LIMIT_MAX_SERIALIZED_BYTES UINT64_C(268435456)

/*
 * Both calls are stateless and read input_length bytes. The input must remain
 * unchanged between the size and fill calls. output_required is mandatory.
 * Error reporting follows the caller-owned buffer contract in rust-ffi.md.
 */
tl_pe_status_t tl_pe_parse_v1_size(
    const uint8_t* input,
    uint64_t input_length,
    uint64_t* output_required,
    tl_pe_error_v1* error,
    char* error_message,
    uint64_t error_capacity,
    uint64_t* error_required);

tl_pe_status_t tl_pe_parse_v1_fill(
    const uint8_t* input,
    uint64_t input_length,
    uint8_t* output,
    uint64_t output_capacity,
    uint64_t* output_required,
    tl_pe_error_v1* error,
    char* error_message,
    uint64_t error_capacity,
    uint64_t* error_required);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* TRADUTORLINUX_FFI_RUST_PE_PARSER_H */
