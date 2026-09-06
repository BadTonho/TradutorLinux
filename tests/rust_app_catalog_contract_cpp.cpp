#include "tradutorlinux/ffi/rust_app_catalog_parser.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(sizeof(tl_app_catalog_status_t) == sizeof(std::uint32_t));
static_assert(sizeof(tl_app_catalog_error_v1) == 24U);
static_assert(alignof(tl_app_catalog_error_v1) == alignof(std::uint64_t));
static_assert(offsetof(tl_app_catalog_error_v1, input_offset) == 8U);
static_assert(offsetof(tl_app_catalog_error_v1, detail_value) == 16U);
static_assert(TL_APP_CATALOG_WIRE_HEADER_SIZE ==
              TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_OFFSET +
                  TL_APP_CATALOG_WIRE_TABLE_COUNT *
                      TL_APP_CATALOG_WIRE_TABLE_DESCRIPTOR_SIZE);
static_assert(TL_APP_CATALOG_WIRE_APP_RESERVED_OFFSET + 16U ==
              TL_APP_CATALOG_WIRE_APP_STRIDE);
static_assert(std::is_same_v<decltype(tl_app_catalog_error_v1::detail_value), std::uint64_t>);

int main() {
    return TL_APP_CATALOG_WIRE_MAGIC_0 == static_cast<std::uint8_t>('T') &&
                   TL_APP_CATALOG_WIRE_MAGIC_1 == static_cast<std::uint8_t>('L') &&
                   TL_APP_CATALOG_WIRE_MAGIC_2 == static_cast<std::uint8_t>('A') &&
                   TL_APP_CATALOG_WIRE_MAGIC_3 == static_cast<std::uint8_t>('C') &&
                   TL_APP_CATALOG_LIMIT_MAX_INPUT_BYTES == UINT64_C(4194304) &&
                   TL_APP_CATALOG_LIMIT_MAX_SERIALIZED_BYTES == UINT64_C(67108864)
               ? 0
               : 1;
}
