#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_OLE_MSABI __attribute__((ms_abi))
#else
#error "TL_OLE_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

using OleHGlobal = void*;

struct GuestStatStg {
    std::uint16_t* pwcs_name{};
    std::uint32_t type{};
    std::uint64_t cb_size{};
    std::uint32_t mtime_low{};
    std::uint32_t mtime_high{};
    std::uint32_t ctime_low{};
    std::uint32_t ctime_high{};
    std::uint32_t atime_low{};
    std::uint32_t atime_high{};
    std::uint32_t grf_mode{};
    std::uint32_t grf_locks_supported{};
    std::uint8_t clsid[16]{};
    std::uint32_t grf_state_bits{};
    std::uint32_t reserved{};
};
static_assert(sizeof(GuestStatStg) == 80);

struct GuestIStream;

using GuestIStreamQueryInterface = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self,
                                                                  const void* riid,
                                                                  GuestIStream** object);
using GuestIStreamAddRef = TL_OLE_MSABI std::uint32_t (*)(GuestIStream* self);
using GuestIStreamRelease = TL_OLE_MSABI std::uint32_t (*)(GuestIStream* self);
using GuestIStreamRead = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, void* buffer,
                                                        std::uint32_t bytes, std::uint32_t* read);
using GuestIStreamWrite = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, const void* buffer,
                                                         std::uint32_t bytes, std::uint32_t* written);
using GuestIStreamSeek = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, std::int64_t move,
                                                        std::uint32_t origin, std::uint64_t* position);
using GuestIStreamSetSize = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, std::uint64_t size);
using GuestIStreamCopyTo = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, GuestIStream* destination,
                                                          std::uint64_t bytes, std::uint64_t* read,
                                                          std::uint64_t* written);
using GuestIStreamCommit = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, std::uint32_t flags);
using GuestIStreamRevert = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self);
using GuestIStreamLockRegion = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, std::uint64_t offset,
                                                              std::uint64_t bytes, std::uint32_t type);
using GuestIStreamUnlockRegion = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, std::uint64_t offset,
                                                                std::uint64_t bytes, std::uint32_t type);
using GuestIStreamStat = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, GuestStatStg* stat,
                                                        std::uint32_t flags);
using GuestIStreamClone = TL_OLE_MSABI std::int32_t (*)(GuestIStream* self, GuestIStream** clone);

struct GuestIStreamVtable {
    GuestIStreamQueryInterface query_interface{};
    GuestIStreamAddRef add_ref{};
    GuestIStreamRelease release{};
    GuestIStreamRead read{};
    GuestIStreamWrite write{};
    GuestIStreamSeek seek{};
    GuestIStreamSetSize set_size{};
    GuestIStreamCopyTo copy_to{};
    GuestIStreamCommit commit{};
    GuestIStreamRevert revert{};
    GuestIStreamLockRegion lock_region{};
    GuestIStreamUnlockRegion unlock_region{};
    GuestIStreamStat stat{};
    GuestIStreamClone clone{};
};

struct GuestIStream {
    GuestIStreamVtable* vtable{};
};

static_assert(sizeof(GuestIStream) == 8);
static_assert(sizeof(GuestIStreamVtable) == 14U * sizeof(void*));

constexpr std::int32_t kSOk = 0;
constexpr std::int32_t kSFalse = 1;
constexpr std::int32_t kEInvalidArg = static_cast<std::int32_t>(0x80070057U);
constexpr std::int32_t kENoInterface = static_cast<std::int32_t>(0x80004002U);
constexpr std::int32_t kStgEInvalidFunction = static_cast<std::int32_t>(0x80030001U);
constexpr std::int32_t kStgESeekError = static_cast<std::int32_t>(0x80030019U);

extern "C" {

TL_OLE_MSABI std::int32_t tl_CoInitialize(void* reserved) noexcept;
TL_OLE_MSABI std::int32_t tl_CoInitializeEx(void* reserved, std::uint32_t co_init) noexcept;
TL_OLE_MSABI void tl_CoUninitialize() noexcept;
TL_OLE_MSABI std::int32_t tl_CoCreateGuid(void* guid) noexcept;
TL_OLE_MSABI void* tl_CoTaskMemAlloc(std::size_t size) noexcept;
TL_OLE_MSABI void tl_CoTaskMemFree(void* ptr) noexcept;
TL_OLE_MSABI void* tl_CoTaskMemRealloc(void* ptr, std::size_t size) noexcept;
TL_OLE_MSABI std::int32_t tl_CreateStreamOnHGlobal(OleHGlobal hglobal,
                                                   std::int32_t delete_on_release,
                                                   GuestIStream** stream) noexcept;
TL_OLE_MSABI std::int32_t tl_CoCreateInstance(const void* rclsid, void* unkOuter, std::uint32_t clsContext,
                                              const void* riid, void** ppv) noexcept;
TL_OLE_MSABI std::int32_t tl_CoGetClassObject(const void* rclsid, std::uint32_t clsContext, void* serverInfo,
                                              const void* riid, void** ppv) noexcept;
TL_OLE_MSABI std::int32_t tl_OleInitialize(void* reserved) noexcept;
TL_OLE_MSABI void tl_OleUninitialize() noexcept;

}  // extern "C"

}  // namespace tradutorlinux
