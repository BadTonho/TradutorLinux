#include "tradutorlinux/ffi/rust_msix_parser.h"

#include <stddef.h>

int main(void) {
    tl_msix_error_v1 error = {0, 0, 0, 0};
    return sizeof(error) == 24U &&
                   offsetof(tl_msix_error_v1, code) == 0U &&
                   offsetof(tl_msix_error_v1, phase) == 4U &&
                   offsetof(tl_msix_error_v1, input_offset) == 8U &&
                   offsetof(tl_msix_error_v1, detail_value) == 16U &&
                   TL_MSIX_STATUS_SUCCESS == 0U &&
                   TL_MSIX_WIRE_HEADER_SIZE == 128U &&
                   TL_MSIX_WIRE_TABLE_COUNT == 4U &&
                   TL_MSIX_WIRE_INFO_STRIDE == 64U &&
                   TL_MSIX_WIRE_APPLICATION_STRIDE == 64U
               ? 0
               : 1;
}
