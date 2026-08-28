#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_OLEAUT_MSABI __attribute__((ms_abi))
#else
#error "TL_OLEAUT_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

using GuestBstr = std::uint16_t*;

// VARTYPE constants
constexpr std::uint16_t kGuestVtEmpty = 0;
constexpr std::uint16_t kGuestVtNull = 1;
constexpr std::uint16_t kGuestVtI2 = 2;
constexpr std::uint16_t kGuestVtI4 = 3;
constexpr std::uint16_t kGuestVtR4 = 4;
constexpr std::uint16_t kGuestVtR8 = 5;
constexpr std::uint16_t kGuestVtCy = 6;
constexpr std::uint16_t kGuestVtDate = 7;
constexpr std::uint16_t kGuestVtBstr = 8;
constexpr std::uint16_t kGuestVtDispatch = 9;
constexpr std::uint16_t kGuestVtError = 10;
constexpr std::uint16_t kGuestVtBool = 11;
constexpr std::uint16_t kGuestVtVariant = 12;
constexpr std::uint16_t kGuestVtUnknown = 13;
constexpr std::uint16_t kGuestVtDecimal = 14;
constexpr std::uint16_t kGuestVtI1 = 16;
constexpr std::uint16_t kGuestVtUi1 = 17;
constexpr std::uint16_t kGuestVtUi2 = 18;
constexpr std::uint16_t kGuestVtUi4 = 19;
constexpr std::uint16_t kGuestVtI8 = 20;
constexpr std::uint16_t kGuestVtUi8 = 21;
constexpr std::uint16_t kGuestVtInt = 22;
constexpr std::uint16_t kGuestVtUint = 23;
constexpr std::uint16_t kGuestVtArray = 0x2000;
constexpr std::uint16_t kGuestVtByref = 0x4000;

struct GuestSafeArrayBound {
    std::uint32_t cElements{0};
    std::int32_t lLbound{0};
};

struct GuestSafeArray {
    std::uint16_t cDims{0};
    std::uint16_t fFeatures{0};
    std::uint32_t cbElements{0};
    std::uint32_t cLocks{0};
    void* pvData{nullptr};
    GuestSafeArrayBound rgsabound[1]{};
};

struct GuestVariant {
    std::uint16_t vt{kGuestVtEmpty};
    std::uint16_t wReserved1{0};
    std::uint16_t wReserved2{0};
    std::uint16_t wReserved3{0};
    union {
        std::int64_t llVal;
        std::int32_t lVal;
        std::uint8_t bVal;
        std::int16_t iVal;
        float fltVal;
        double dblVal;
        std::int16_t boolVal;
        std::int32_t scode;
        GuestBstr bstrVal;
        void* punkVal;
        void* pdispVal;
        GuestSafeArray* parray;
        void* byref;
        std::uint8_t raw[16];
    } data{};
};

extern "C" {

TL_OLEAUT_MSABI GuestBstr tl_SysAllocString(const std::uint16_t* sz) noexcept;
TL_OLEAUT_MSABI GuestBstr tl_SysAllocStringLen(const std::uint16_t* str, std::uint32_t len) noexcept;
TL_OLEAUT_MSABI void tl_SysFreeString(GuestBstr bstr) noexcept;
TL_OLEAUT_MSABI std::uint32_t tl_SysStringLen(const std::uint16_t* bstr) noexcept;
TL_OLEAUT_MSABI std::uint32_t tl_SysStringByteLen(const std::uint16_t* bstr) noexcept;
TL_OLEAUT_MSABI GuestBstr tl_SysAllocStringByteLen(const char* str, std::uint32_t len) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SysReAllocString(GuestBstr* pbstr, const std::uint16_t* sz) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SysReAllocStringLen(GuestBstr* pbstr, const std::uint16_t* str, std::uint32_t len) noexcept;

TL_OLEAUT_MSABI void tl_VariantInit(GuestVariant* pvarg) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_VariantClear(GuestVariant* pvarg) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_VariantCopy(GuestVariant* pvargDest, const GuestVariant* pvargSrc) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_VariantCopyInd(GuestVariant* pvargDest, const GuestVariant* pvargSrc) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_VariantChangeType(GuestVariant* pvargDest, const GuestVariant* pvarSrc,
                                                 std::uint16_t wFlags, std::uint16_t vt) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_VariantChangeTypeEx(GuestVariant* pvargDest, const GuestVariant* pvarSrc,
                                                   std::uint32_t lcid, std::uint16_t wFlags, std::uint16_t vt) noexcept;

TL_OLEAUT_MSABI GuestSafeArray* tl_SafeArrayCreate(std::uint16_t vt, std::uint32_t cDims,
                                                   const GuestSafeArrayBound* rgsabound) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroy(GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI GuestSafeArray* tl_SafeArrayCreateVector(std::uint16_t vt, std::int32_t lLbound,
                                                         std::uint32_t cElements) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroyData(GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroyDescriptor(GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetDim(const GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::uint32_t tl_SafeArrayGetElemsize(const GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetUBound(const GuestSafeArray* psa, std::uint32_t nDim,
                                                  std::int32_t* plUbound) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetLBound(const GuestSafeArray* psa, std::uint32_t nDim,
                                                  std::int32_t* plLbound) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayLock(GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayUnlock(GuestSafeArray* psa) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayAccessData(GuestSafeArray* psa, void** ppvData) noexcept;
TL_OLEAUT_MSABI std::int32_t tl_SafeArrayUnaccessData(GuestSafeArray* psa) noexcept;

}  // extern "C"

}  // namespace tradutorlinux

