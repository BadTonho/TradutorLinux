#include "tradutorlinux/runtime/ole32.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>

#include "tradutorlinux/runtime/memory_validator.hpp"

namespace tradutorlinux {

namespace {

constexpr std::int32_t kSOk = 0;
constexpr std::int32_t kEInvalidArg = static_cast<std::int32_t>(0x80070057U);
constexpr std::int32_t kEUnexpected = static_cast<std::int32_t>(0x8000FFFFU);

struct Win32Guid {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint8_t data4[8];
};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_OLE_MSABI std::int32_t tl_CoInitialize(void* reserved) noexcept {
    (void)reserved;
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_CoInitializeEx(void* reserved, const std::uint32_t co_init) noexcept {
    (void)reserved;
    (void)co_init;
    return kSOk;
}

TL_OLE_MSABI void tl_CoUninitialize() noexcept {
}

TL_OLE_MSABI std::int32_t tl_CoCreateGuid(void* guid) noexcept {
    if (guid == nullptr || !mapped_range(guid, sizeof(Win32Guid), true)) {
        return kEInvalidArg;
    }
    auto* out = static_cast<Win32Guid*>(guid);
    std::ifstream urandom{"/dev/urandom", std::ios::binary};
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(out), sizeof(Win32Guid));
    } else {
        std::uint8_t* raw = reinterpret_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < sizeof(Win32Guid); ++i) {
            raw[i] = static_cast<std::uint8_t>(std::rand() & 0xFF);
        }
    }
    // UUID v4 format
    out->data3 = (out->data3 & 0x0FFFU) | 0x4000U;
    out->data4[0] = (out->data4[0] & 0x3FU) | 0x80U;
    return kSOk;
}

TL_OLE_MSABI void* tl_CoTaskMemAlloc(const std::size_t size) noexcept {
    if (size == 0) {
        return nullptr;
    }
    return std::malloc(size);
}

TL_OLE_MSABI void tl_CoTaskMemFree(void* ptr) noexcept {
    if (ptr != nullptr) {
        std::free(ptr);
    }
}

TL_OLE_MSABI void* tl_CoTaskMemRealloc(void* ptr, const std::size_t size) noexcept {
    if (ptr == nullptr) {
        return tl_CoTaskMemAlloc(size);
    }
    if (size == 0) {
        tl_CoTaskMemFree(ptr);
        return nullptr;
    }
    return std::realloc(ptr, size);
}

}  // extern "C"

}  // namespace tradutorlinux
