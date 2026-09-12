#include "tradutorlinux/runtime/imm32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include <cstdint>
#include "core/runtime_state_common.hpp"

namespace tradutorlinux {

extern "C" {

TL_IMM_MSABI void* tl_ImmGetContext(const void* window) noexcept {
    if (window == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_IMM_MSABI int tl_ImmReleaseContext(const void* window, void* context) noexcept {
    if (window == nullptr || context == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI int tl_ImmSetCompositionWindow(void* context, const void* comp_form) noexcept {
    if (context == nullptr || comp_form == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringA(void* context, const std::uint32_t index,
                                                      void* buf, const std::uint32_t buflen) noexcept {
    (void)index;
    (void)buf;
    (void)buflen;
    if (context == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    set_last_error(abi::kErrorNotSupported);
    return -1;
}

TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringW(void* context, const std::uint32_t index,
                                                      void* buf, const std::uint32_t buflen) noexcept {
    (void)index;
    (void)buf;
    (void)buflen;
    if (context == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    set_last_error(abi::kErrorNotSupported);
    return -1;
}

TL_IMM_MSABI void* tl_ImmAssociateContext(const void* window, void* context) noexcept {
    (void)context;
    if (window == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_IMM_MSABI std::uint32_t tl_ImmGetVirtualKey(void* const hwnd) noexcept {
    (void)hwnd;
    set_last_error(abi::kErrorNotSupported);
    return 0; // VK_PROCESSKEY none
}

TL_IMM_MSABI int tl_ImmSetCompositionFontA(void* const himc, void* const logfont) noexcept {
    if (himc == nullptr || logfont == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI int tl_ImmSetCompositionFontW(void* const himc, void* const logfont) noexcept {
    if (himc == nullptr || logfont == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI int tl_ImmSetCandidateWindow(void* const himc, const void* const candidate_form) noexcept {
    if (himc == nullptr || candidate_form == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI int tl_ImmSetCompositionStringW(void* const himc, const std::uint32_t index, const void* const comp, const std::uint32_t comp_len, const void* const read, const std::uint32_t read_len) noexcept {
    (void)index;
    (void)comp;
    (void)comp_len;
    (void)read;
    (void)read_len;
    if (himc == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI std::intptr_t tl_ImmEscapeW(void* const hkl, void* const himc, const std::uint32_t escape, void* const data) noexcept {
    (void)hkl;
    (void)himc;
    (void)escape;
    (void)data;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_IMM_MSABI int tl_ImmNotifyIME(void* const himc, const std::uint32_t action, const std::uint32_t index, const std::uint32_t value) noexcept {
    (void)action;
    (void)index;
    (void)value;
    if (himc == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_imm32_module() {
    static const ExportedFunction kImm32Exports[] = {
        {"ImmGetContext", 1, reinterpret_cast<std::uintptr_t>(&tl_ImmGetContext), ExportSupport::Stub},
        {"ImmReleaseContext", 2, reinterpret_cast<std::uintptr_t>(&tl_ImmReleaseContext), ExportSupport::Stub},
        {"ImmSetCompositionWindow", 3, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionWindow), ExportSupport::Stub},
        {"ImmGetCompositionStringA", 4, reinterpret_cast<std::uintptr_t>(&tl_ImmGetCompositionStringA), ExportSupport::Stub},
        {"ImmGetCompositionStringW", 5, reinterpret_cast<std::uintptr_t>(&tl_ImmGetCompositionStringW), ExportSupport::Stub},
        {"ImmAssociateContext", 6, reinterpret_cast<std::uintptr_t>(&tl_ImmAssociateContext), ExportSupport::Stub},
        {"ImmGetVirtualKey", 7, reinterpret_cast<std::uintptr_t>(&tl_ImmGetVirtualKey), ExportSupport::Stub},
        {"ImmSetCompositionFontA", 8, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionFontA), ExportSupport::Stub},
        {"ImmSetCompositionFontW", 9, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionFontW), ExportSupport::Stub},
        {"ImmSetCandidateWindow", 10, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCandidateWindow), ExportSupport::Stub},
        {"ImmSetCompositionStringW", 11, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionStringW), ExportSupport::Stub},
        {"ImmEscapeW", 12, reinterpret_cast<std::uintptr_t>(&tl_ImmEscapeW), ExportSupport::Stub},
        {"ImmNotifyIME", 13, reinterpret_cast<std::uintptr_t>(&tl_ImmNotifyIME), ExportSupport::Stub},
    };
    static const InternalModule kImm32Module{"IMM32.dll", kImm32Exports};
    register_module(kImm32Module);
}

}  // namespace tradutorlinux::loader
