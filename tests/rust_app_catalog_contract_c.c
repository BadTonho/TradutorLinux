#include "tradutorlinux/ffi/rust_app_catalog_parser.h"

#include <stddef.h>

int main(void) {
    tl_app_catalog_error_v1 error = {0, 0, 0, 0};
    return sizeof(error) == 24U &&
                   offsetof(tl_app_catalog_error_v1, code) == 0U &&
                   offsetof(tl_app_catalog_error_v1, phase) == 4U &&
                   offsetof(tl_app_catalog_error_v1, input_offset) == 8U &&
                   offsetof(tl_app_catalog_error_v1, detail_value) == 16U &&
                   TL_APP_CATALOG_STATUS_SUCCESS == 0U &&
                   TL_APP_CATALOG_STATUS_INTERNAL == 7U &&
                   TL_APP_CATALOG_WIRE_HEADER_SIZE == 128U &&
                   TL_APP_CATALOG_WIRE_TABLE_COUNT == 4U &&
                   TL_APP_CATALOG_WIRE_INFO_STRIDE == 32U &&
                   TL_APP_CATALOG_WIRE_APP_STRIDE == 192U &&
                   TL_APP_CATALOG_WIRE_ARG_STRIDE == 16U
               ? 0
               : 1;
}
