#include "tradutorlinux/runtime/oleaut32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include <cstdlib>
#include <cstring>

namespace tradutorlinux {

namespace {

std::size_t guest_wcslen(const std::uint16_t* s) noexcept {
    if (s == nullptr) {
        return 0;
    }
    std::size_t len = 0;
    while (s[len] != 0) {
        ++len;
    }
    return len;
}

}  // namespace

extern "C" {

TL_OLEAUT_MSABI GuestBstr tl_SysAllocString(const std::uint16_t* sz) noexcept {
    if (sz == nullptr) {
        return nullptr;
    }
    const std::size_t char_len = guest_wcslen(sz);
    const std::uint32_t byte_len = static_cast<std::uint32_t>(char_len * sizeof(std::uint16_t));
    const std::size_t total_size = sizeof(std::uint32_t) + byte_len + sizeof(std::uint16_t);

    auto* mem = static_cast<std::byte*>(std::malloc(total_size));
    if (mem == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<std::uint32_t*>(mem) = byte_len;
    auto* bstr = reinterpret_cast<std::uint16_t*>(mem + sizeof(std::uint32_t));
    std::memcpy(bstr, sz, byte_len);
    bstr[char_len] = 0;
    return bstr;
}

TL_OLEAUT_MSABI GuestBstr tl_SysAllocStringLen(const std::uint16_t* str, const std::uint32_t len) noexcept {
    const std::uint32_t byte_len = len * static_cast<std::uint32_t>(sizeof(std::uint16_t));
    const std::size_t total_size = sizeof(std::uint32_t) + byte_len + sizeof(std::uint16_t);

    auto* mem = static_cast<std::byte*>(std::malloc(total_size));
    if (mem == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<std::uint32_t*>(mem) = byte_len;
    auto* bstr = reinterpret_cast<std::uint16_t*>(mem + sizeof(std::uint32_t));
    if (str != nullptr) {
        std::memcpy(bstr, str, byte_len);
    } else {
        std::memset(bstr, 0, byte_len);
    }
    bstr[len] = 0;
    return bstr;
}

TL_OLEAUT_MSABI void tl_SysFreeString(GuestBstr bstr) noexcept {
    if (bstr == nullptr) {
        return;
    }
    auto* mem = reinterpret_cast<std::byte*>(bstr) - sizeof(std::uint32_t);
    std::free(mem);
}

TL_OLEAUT_MSABI std::uint32_t tl_SysStringLen(const std::uint16_t* bstr) noexcept {
    if (bstr == nullptr) {
        return 0;
    }
    const auto* mem = reinterpret_cast<const std::byte*>(bstr) - sizeof(std::uint32_t);
    const std::uint32_t byte_len = *reinterpret_cast<const std::uint32_t*>(mem);
    return byte_len / static_cast<std::uint32_t>(sizeof(std::uint16_t));
}

TL_OLEAUT_MSABI std::uint32_t tl_SysStringByteLen(const std::uint16_t* bstr) noexcept {
    if (bstr == nullptr) {
        return 0;
    }
    const auto* mem = reinterpret_cast<const std::byte*>(bstr) - sizeof(std::uint32_t);
    return *reinterpret_cast<const std::uint32_t*>(mem);
}

TL_OLEAUT_MSABI GuestBstr tl_SysAllocStringByteLen(const char* str, const std::uint32_t len) noexcept {
    const std::size_t total_size = sizeof(std::uint32_t) + len + sizeof(std::uint16_t);
    auto* mem = static_cast<std::byte*>(std::malloc(total_size));
    if (mem == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<std::uint32_t*>(mem) = len;
    auto* bstr = reinterpret_cast<std::uint16_t*>(mem + sizeof(std::uint32_t));
    if (str != nullptr) {
        std::memcpy(bstr, str, len);
    } else {
        std::memset(bstr, 0, len);
    }
    *reinterpret_cast<std::uint16_t*>(mem + sizeof(std::uint32_t) + len) = 0;
    return bstr;
}

TL_OLEAUT_MSABI std::int32_t tl_SysReAllocString(GuestBstr* pbstr, const std::uint16_t* sz) noexcept {
    if (pbstr == nullptr) {
        return 0;
    }
    GuestBstr new_str = tl_SysAllocString(sz);
    tl_SysFreeString(*pbstr);
    *pbstr = new_str;
    return 1;
}

TL_OLEAUT_MSABI std::int32_t tl_SysReAllocStringLen(GuestBstr* pbstr, const std::uint16_t* str, const std::uint32_t len) noexcept {
    if (pbstr == nullptr) {
        return 0;
    }
    GuestBstr new_str = tl_SysAllocStringLen(str, len);
    tl_SysFreeString(*pbstr);
    *pbstr = new_str;
    return 1;
}

TL_OLEAUT_MSABI void tl_VariantInit(GuestVariant* pvarg) noexcept {
    if (pvarg == nullptr) {
        return;
    }
    pvarg->vt = kGuestVtEmpty;
    pvarg->wReserved1 = 0;
    pvarg->wReserved2 = 0;
    pvarg->wReserved3 = 0;
    std::memset(pvarg->data.raw, 0, sizeof(pvarg->data.raw));
}

TL_OLEAUT_MSABI std::int32_t tl_VariantClear(GuestVariant* pvarg) noexcept {
    if (pvarg == nullptr) {
        return 0;
    }
    if (pvarg->vt == kGuestVtBstr) {
        tl_SysFreeString(pvarg->data.bstrVal);
    } else if (pvarg->vt == (kGuestVtArray | kGuestVtI1) ||
               (pvarg->vt & kGuestVtArray) != 0) {
        if (pvarg->data.parray != nullptr) {
            tl_SafeArrayDestroy(pvarg->data.parray);
        }
    }
    tl_VariantInit(pvarg);
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_VariantCopy(GuestVariant* pvargDest, const GuestVariant* pvargSrc) noexcept {
    if (pvargDest == nullptr) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    if (pvargSrc == nullptr) {
        tl_VariantClear(pvargDest);
        return 0;
    }
    if (pvargDest == pvargSrc) {
        return 0;
    }
    tl_VariantClear(pvargDest);
    pvargDest->vt = pvargSrc->vt;
    pvargDest->wReserved1 = pvargSrc->wReserved1;
    pvargDest->wReserved2 = pvargSrc->wReserved2;
    pvargDest->wReserved3 = pvargSrc->wReserved3;
    if (pvargSrc->vt == kGuestVtBstr) {
        pvargDest->data.bstrVal = tl_SysAllocStringByteLen(
            reinterpret_cast<const char*>(pvargSrc->data.bstrVal),
            tl_SysStringByteLen(pvargSrc->data.bstrVal)
        );
    } else {
        std::memcpy(pvargDest->data.raw, pvargSrc->data.raw, sizeof(pvargDest->data.raw));
    }
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_VariantCopyInd(GuestVariant* pvargDest, const GuestVariant* pvargSrc) noexcept {
    return tl_VariantCopy(pvargDest, pvargSrc);
}

TL_OLEAUT_MSABI std::int32_t tl_VariantChangeType(GuestVariant* pvargDest, const GuestVariant* pvarSrc,
                                                 const std::uint16_t wFlags, const std::uint16_t vt) noexcept {
    (void)wFlags;
    (void)vt;
    if (pvargDest == nullptr || pvarSrc == nullptr) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    return tl_VariantCopy(pvargDest, pvarSrc);
}

TL_OLEAUT_MSABI std::int32_t tl_VariantChangeTypeEx(GuestVariant* pvargDest, const GuestVariant* pvarSrc,
                                                   const std::uint32_t lcid, const std::uint16_t wFlags,
                                                   const std::uint16_t vt) noexcept {
    (void)lcid;
    return tl_VariantChangeType(pvargDest, pvarSrc, wFlags, vt);
}

TL_OLEAUT_MSABI GuestSafeArray* tl_SafeArrayCreate(const std::uint16_t vt, const std::uint32_t cDims,
                                                   const GuestSafeArrayBound* rgsabound) noexcept {
    (void)vt;
    if (cDims == 0 || rgsabound == nullptr) {
        return nullptr;
    }
    std::size_t total_elements = 1;
    for (std::uint32_t i = 0; i < cDims; ++i) {
        total_elements *= rgsabound[i].cElements;
    }
    const std::uint32_t elem_size = 8;
    const std::size_t data_size = total_elements * elem_size;
    const std::size_t struct_size = sizeof(GuestSafeArray) + (cDims > 1 ? (cDims - 1) * sizeof(GuestSafeArrayBound) : 0);

    auto* psa = static_cast<GuestSafeArray*>(std::calloc(1, struct_size));
    if (psa == nullptr) {
        return nullptr;
    }
    psa->cDims = static_cast<std::uint16_t>(cDims);
    psa->cbElements = elem_size;
    psa->pvData = std::calloc(1, data_size > 0 ? data_size : 8);
    for (std::uint32_t i = 0; i < cDims; ++i) {
        psa->rgsabound[i] = rgsabound[i];
    }
    return psa;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroy(GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return 0;
    }
    if (psa->pvData != nullptr) {
        std::free(psa->pvData);
    }
    std::free(psa);
    return 0;
}

TL_OLEAUT_MSABI GuestSafeArray* tl_SafeArrayCreateVector(const std::uint16_t vt, const std::int32_t lLbound,
                                                         const std::uint32_t cElements) noexcept {
    GuestSafeArrayBound bound{cElements, lLbound};
    return tl_SafeArrayCreate(vt, 1, &bound);
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroyData(GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return 0;
    }
    if (psa->pvData != nullptr) {
        std::free(psa->pvData);
        psa->pvData = nullptr;
    }
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayDestroyDescriptor(GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return 0;
    }
    std::free(psa);
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetDim(const GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return 0;
    }
    return psa->cDims;
}

TL_OLEAUT_MSABI std::uint32_t tl_SafeArrayGetElemsize(const GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return 0;
    }
    return psa->cbElements;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetUBound(const GuestSafeArray* psa, const std::uint32_t nDim,
                                                  std::int32_t* plUbound) noexcept {
    if (psa == nullptr || plUbound == nullptr || nDim == 0 || nDim > psa->cDims) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    const auto& bound = psa->rgsabound[nDim - 1];
    *plUbound = bound.lLbound + static_cast<std::int32_t>(bound.cElements) - 1;
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayGetLBound(const GuestSafeArray* psa, const std::uint32_t nDim,
                                                  std::int32_t* plLbound) noexcept {
    if (psa == nullptr || plLbound == nullptr || nDim == 0 || nDim > psa->cDims) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    *plLbound = psa->rgsabound[nDim - 1].lLbound;
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayLock(GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    ++psa->cLocks;
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayUnlock(GuestSafeArray* psa) noexcept {
    if (psa == nullptr) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    if (psa->cLocks > 0) {
        --psa->cLocks;
    }
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayAccessData(GuestSafeArray* psa, void** ppvData) noexcept {
    if (psa == nullptr || ppvData == nullptr) {
        return static_cast<std::int32_t>(0x80070057U);
    }
    *ppvData = psa->pvData;
    ++psa->cLocks;
    return 0;
}

TL_OLEAUT_MSABI std::int32_t tl_SafeArrayUnaccessData(GuestSafeArray* psa) noexcept {
    return tl_SafeArrayUnlock(psa);
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_oleaut32_module() {
    static const ExportedFunction kOleaut32Exports[] = {
        {"SysAllocString", 2, reinterpret_cast<std::uintptr_t>(&tl_SysAllocString)},
        {"SysReAllocString", 3, reinterpret_cast<std::uintptr_t>(&tl_SysReAllocString)},
        {"SysAllocStringLen", 4, reinterpret_cast<std::uintptr_t>(&tl_SysAllocStringLen)},
        {"SysReAllocStringLen", 5, reinterpret_cast<std::uintptr_t>(&tl_SysReAllocStringLen)},
        {"SysFreeString", 6, reinterpret_cast<std::uintptr_t>(&tl_SysFreeString)},
        {"SysStringLen", 7, reinterpret_cast<std::uintptr_t>(&tl_SysStringLen)},
        {"VariantInit", 8, reinterpret_cast<std::uintptr_t>(&tl_VariantInit)},
        {"VariantClear", 9, reinterpret_cast<std::uintptr_t>(&tl_VariantClear)},
        {"VariantCopy", 10, reinterpret_cast<std::uintptr_t>(&tl_VariantCopy)},
        {"VariantCopyInd", 11, reinterpret_cast<std::uintptr_t>(&tl_VariantCopyInd)},
        {"VariantChangeType", 12, reinterpret_cast<std::uintptr_t>(&tl_VariantChangeType)},
        {"SafeArrayCreate", 15, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayCreate)},
        {"SafeArrayDestroy", 16, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroy)},
        {"SafeArrayGetDim", 17, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetDim)},
        {"SafeArrayGetElemsize", 18, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetElemsize)},
        {"SafeArrayGetUBound", 19, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetUBound)},
        {"SafeArrayGetLBound", 20, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetLBound)},
        {"SafeArrayLock", 21, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayLock)},
        {"SafeArrayUnlock", 22, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayUnlock)},
        {"SafeArrayAccessData", 23, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayAccessData)},
        {"SafeArrayUnaccessData", 24, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayUnaccessData)},
        {"VariantChangeTypeEx", 147, reinterpret_cast<std::uintptr_t>(&tl_VariantChangeTypeEx)},
        {"SysStringByteLen", 149, reinterpret_cast<std::uintptr_t>(&tl_SysStringByteLen)},
        {"SysAllocStringByteLen", 150, reinterpret_cast<std::uintptr_t>(&tl_SysAllocStringByteLen)},
        {"SafeArrayDestroyData", 200, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroyData)},
        {"SafeArrayDestroyDescriptor", 201, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroyDescriptor)},
        {"SafeArrayCreateVector", 411, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayCreateVector)},
    };
    static const InternalModule kOleaut32Module{"OLEAUT32.dll", kOleaut32Exports};
    register_module(kOleaut32Module);
}

}  // namespace tradutorlinux::loader

