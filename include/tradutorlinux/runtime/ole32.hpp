#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_OLE_MSABI __attribute__((ms_abi))
#else
#error "TL_OLE_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_OLE_MSABI std::int32_t tl_CoInitialize(void* reserved) noexcept;
TL_OLE_MSABI std::int32_t tl_CoInitializeEx(void* reserved, std::uint32_t co_init) noexcept;
TL_OLE_MSABI void tl_CoUninitialize() noexcept;
TL_OLE_MSABI std::int32_t tl_CoCreateGuid(void* guid) noexcept;
TL_OLE_MSABI void* tl_CoTaskMemAlloc(std::size_t size) noexcept;
TL_OLE_MSABI void tl_CoTaskMemFree(void* ptr) noexcept;
TL_OLE_MSABI void* tl_CoTaskMemRealloc(void* ptr, std::size_t size) noexcept;
TL_OLE_MSABI std::int32_t tl_CoCreateInstance(const void* rclsid, void* unkOuter, std::uint32_t clsContext,
                                              const void* riid, void** ppv) noexcept;
TL_OLE_MSABI std::int32_t tl_CoGetClassObject(const void* rclsid, std::uint32_t clsContext, void* serverInfo,
                                              const void* riid, void** ppv) noexcept;
TL_OLE_MSABI std::int32_t tl_OleInitialize(void* reserved) noexcept;
TL_OLE_MSABI void tl_OleUninitialize() noexcept;

}  // extern "C"

}  // namespace tradutorlinux
