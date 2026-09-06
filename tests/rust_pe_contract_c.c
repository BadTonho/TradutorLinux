#include "tradutorlinux/ffi/rust_pe_parser.h"

int main(void) {
    tl_pe_error_v1 error = {0, 0, 0, 0};
    return sizeof(error) == 24U && TL_PE_STATUS_SUCCESS == 0U &&
                   TL_PE_WIRE_HEADER_SIZE == 416U
               ? 0
               : 1;
}
