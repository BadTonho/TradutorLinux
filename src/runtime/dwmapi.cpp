#include "tradutorlinux/runtime/dwmapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "core/runtime_state_common.hpp"

#include <new>
#include <vector>

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

constexpr std::int32_t kDwmENotImpl = static_cast<std::int32_t>(0x80004001U);
constexpr std::int32_t kDwmEInvalidArg = static_cast<std::int32_t>(0x80070057U);

std::int32_t reject_dwm() noexcept {
    set_last_error(abi::kErrorNotSupported);
    return kDwmENotImpl;
}

}  // namespace

extern "C" {

TL_DWM_MSABI std::int32_t tl_DwmSetWindowAttribute(void* const hwnd, const std::uint32_t attr,
                                                  const void* const attr_val, const std::uint32_t attr_sz) noexcept {
    (void)hwnd;
    (void)attr;
    (void)attr_val;
    (void)attr_sz;
    return reject_dwm();
}

TL_DWM_MSABI std::int32_t tl_DwmGetWindowAttribute(void* const hwnd, const std::uint32_t attr,
                                                  void* const attr_val, const std::uint32_t attr_sz) noexcept {
    (void)hwnd;
    (void)attr;
    if (attr_val == nullptr || attr_sz == 0 || !mapped_range(attr_val, attr_sz, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    std::vector<std::byte> zeroes;
    try {
        zeroes.resize(attr_sz);
    } catch (const std::bad_alloc&) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return kDwmENotImpl;
    }
    if (runtime::write_guest_memory(attr_val, zeroes.data(), zeroes.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    return reject_dwm();
}

TL_DWM_MSABI std::int32_t tl_DwmIsCompositionEnabled(int* const enabled) noexcept {
    if (enabled == nullptr || !mapped_range(enabled, sizeof(int), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    if (!write_guest_value(enabled, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    return reject_dwm();
}

TL_DWM_MSABI int tl_DwmDefWindowProc(void* const hwnd, const std::uint32_t msg, const std::uintptr_t wparam,
                                     const std::intptr_t lparam, std::intptr_t* const lresult) noexcept {
    (void)hwnd;
    (void)msg;
    (void)wparam;
    (void)lparam;
    if (lresult != nullptr && mapped_range(lresult, sizeof(std::intptr_t), true)) {
        if (!write_guest_value(lresult, static_cast<std::intptr_t>(0))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    return 0; // Not handled by DWM; this is the documented BOOL result.
}

TL_DWM_MSABI std::int32_t tl_DwmExtendFrameIntoClientArea(void* const hwnd, const void* const margins) noexcept {
    (void)hwnd;
    (void)margins;
    return reject_dwm();
}

TL_DWM_MSABI std::int32_t tl_DwmEnableBlurBehindWindow(void* const hwnd, const void* const blur_behind) noexcept {
    (void)hwnd;
    (void)blur_behind;
    return reject_dwm();
}

TL_DWM_MSABI std::int32_t tl_DwmFlush() noexcept {
    return reject_dwm();
}

TL_DWM_MSABI std::int32_t tl_DwmGetColorizationColor(std::uint32_t* const color, int* const opaque) noexcept {
    if (color == nullptr || opaque == nullptr ||
        !mapped_range(color, sizeof(std::uint32_t), true) ||
        !mapped_range(opaque, sizeof(int), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    if (!write_guest_value(color, std::uint32_t{0}) || !write_guest_value(opaque, 0)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kDwmEInvalidArg;
    }
    return reject_dwm();
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_dwmapi_module() {
    static const ExportedFunction kDwmApiExports[] = {
        {"DwmSetWindowAttribute", 1, reinterpret_cast<std::uintptr_t>(&tl_DwmSetWindowAttribute), ExportSupport::Stub},
        {"DwmGetWindowAttribute", 2, reinterpret_cast<std::uintptr_t>(&tl_DwmGetWindowAttribute), ExportSupport::Stub},
        {"DwmIsCompositionEnabled", 3, reinterpret_cast<std::uintptr_t>(&tl_DwmIsCompositionEnabled), ExportSupport::Stub},
        {"DwmDefWindowProc", 4, reinterpret_cast<std::uintptr_t>(&tl_DwmDefWindowProc), ExportSupport::Limited},
        {"DwmExtendFrameIntoClientArea", 5, reinterpret_cast<std::uintptr_t>(&tl_DwmExtendFrameIntoClientArea), ExportSupport::Stub},
        {"DwmEnableBlurBehindWindow", 6, reinterpret_cast<std::uintptr_t>(&tl_DwmEnableBlurBehindWindow), ExportSupport::Stub},
        {"DwmFlush", 7, reinterpret_cast<std::uintptr_t>(&tl_DwmFlush), ExportSupport::Stub},
        {"DwmGetColorizationColor", 8, reinterpret_cast<std::uintptr_t>(&tl_DwmGetColorizationColor), ExportSupport::Stub},
    };
    static const InternalModule kDwmApiModule{"DWMAPI.dll", kDwmApiExports};
    register_module(kDwmApiModule);
}

}  // namespace tradutorlinux::loader
