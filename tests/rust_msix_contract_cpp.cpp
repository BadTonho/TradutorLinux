#include "tradutorlinux/ffi/rust_msix_parser.h"

#include <cstddef>
#include <cstdint>

static_assert(sizeof(tl_msix_status_t) == sizeof(std::uint32_t));
static_assert(sizeof(tl_msix_error_v1) == 24U);
static_assert(alignof(tl_msix_error_v1) == alignof(std::uint64_t));
static_assert(offsetof(tl_msix_error_v1, input_offset) == 8U);
static_assert(TL_MSIX_WIRE_HEADER_SIZE ==
              TL_MSIX_WIRE_TABLE_DESCRIPTOR_OFFSET +
                  TL_MSIX_WIRE_TABLE_COUNT * TL_MSIX_WIRE_TABLE_DESCRIPTOR_SIZE);

int main() {
    return TL_MSIX_STATUS_INTERNAL == 9U && TL_MSIX_WIRE_MAGIC_0 == 'T' ? 0 : 1;
}
