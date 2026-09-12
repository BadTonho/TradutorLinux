#include "tradutorlinux/ffi/rust_profile_parser.h"

#include <cstdint>
#include <type_traits>

static_assert(sizeof(tl_profile_identity_v1) == 48);
static_assert(alignof(tl_profile_identity_v1) == 8);
static_assert(sizeof(tl_profile_error_v1) == 24);
static_assert(alignof(tl_profile_error_v1) == 8);
static_assert(std::is_same_v<decltype(tl_profile_identity_v1::app_id_length), std::uint64_t>);
static_assert(std::is_same_v<decltype(tl_profile_error_v1::detail_value), std::uint64_t>);
static_assert(TL_PROFILE_WIRE_HEADER_SIZE == 128U);
static_assert(TL_PROFILE_WIRE_TABLE_DESCRIPTOR_SIZE == 24U);
static_assert(TL_PROFILE_WIRE_INFO_STRIDE == 96U);
static_assert(TL_PROFILE_WIRE_FILE_STRIDE == 32U);
static_assert(TL_PROFILE_WIRE_DLL_STRIDE == 32U);
static_assert(TL_PROFILE_WIRE_INFO_EXTENSION_OFFSET == 80U);
static_assert(TL_PROFILE_WIRE_INFO_FLAG_EXTENSION_DECLARED == 2U);

int main() {
    return TL_PROFILE_WIRE_MAGIC_0 == static_cast<std::uint8_t>('T') &&
                   TL_PROFILE_WIRE_MAGIC_1 == static_cast<std::uint8_t>('L') &&
                   TL_PROFILE_WIRE_MAGIC_2 == static_cast<std::uint8_t>('P') &&
                   TL_PROFILE_WIRE_MAGIC_3 == static_cast<std::uint8_t>('R')
               ? 0
               : 1;
}
