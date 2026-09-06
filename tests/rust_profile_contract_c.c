#include "tradutorlinux/ffi/rust_profile_parser.h"

#include <stddef.h>

_Static_assert(sizeof(tl_profile_identity_v1) == 48, "identity ABI changed");
_Static_assert(_Alignof(tl_profile_identity_v1) == 8, "identity alignment changed");
_Static_assert(sizeof(tl_profile_error_v1) == 24, "error ABI changed");
_Static_assert(_Alignof(tl_profile_error_v1) == 8, "error alignment changed");

int main(void) {
    return TL_PROFILE_WIRE_HEADER_SIZE == 128U &&
                   TL_PROFILE_WIRE_TABLE_COUNT == 4U &&
                   TL_PROFILE_WIRE_INFO_STRIDE == 96U &&
                   TL_PROFILE_WIRE_FILE_STRIDE == 32U &&
                   TL_PROFILE_WIRE_DLL_STRIDE == 32U &&
                   TL_PROFILE_WIRE_STRING_RECORD_HEADER_SIZE == 8U
               ? 0
               : 1;
}
