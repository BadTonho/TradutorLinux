#include "tradutorlinux/runtime/imm32.hpp"

#include <cstdint>

#include "tradutorlinux/runtime/memory_validator.hpp"

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_IMM_MSABI void* tl_ImmGetContext(const void* window) noexcept {
    if (window == nullptr) {
        return nullptr;
    }
    static char g_himc_token = 0;
    return &g_himc_token;
}

TL_IMM_MSABI int tl_ImmReleaseContext(const void* window, void* context) noexcept {
    (void)window;
    (void)context;
    return 1;
}

TL_IMM_MSABI int tl_ImmSetCompositionWindow(void* context, const void* comp_form) noexcept {
    (void)context;
    (void)comp_form;
    return 1;
}

TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringA(void* context, const std::uint32_t index,
                                                      void* buf, const std::uint32_t buflen) noexcept {
    (void)context;
    (void)index;
    (void)buf;
    (void)buflen;
    return 0;
}

TL_IMM_MSABI std::int32_t tl_ImmGetCompositionStringW(void* context, const std::uint32_t index,
                                                      void* buf, const std::uint32_t buflen) noexcept {
    (void)context;
    (void)index;
    (void)buf;
    (void)buflen;
    return 0;
}

TL_IMM_MSABI void* tl_ImmAssociateContext(const void* window, void* context) noexcept {
    (void)window;
    return context;
}

TL_IMM_MSABI std::uint32_t tl_ImmGetVirtualKey(void* const hwnd) noexcept {
    (void)hwnd;
    return 0; // VK_PROCESSKEY none
}

TL_IMM_MSABI int tl_ImmSetCompositionFontA(void* const himc, void* const logfont) noexcept {
    (void)himc;
    (void)logfont;
    return 1;
}

TL_IMM_MSABI int tl_ImmSetCompositionFontW(void* const himc, void* const logfont) noexcept {
    (void)himc;
    (void)logfont;
    return 1;
}

TL_IMM_MSABI int tl_ImmSetCandidateWindow(void* const himc, const void* const candidate_form) noexcept {
    (void)himc;
    (void)candidate_form;
    return 1;
}

TL_IMM_MSABI int tl_ImmSetCompositionStringW(void* const himc, const std::uint32_t index, const void* const comp, const std::uint32_t comp_len, const void* const read, const std::uint32_t read_len) noexcept {
    (void)himc;
    (void)index;
    (void)comp;
    (void)comp_len;
    (void)read;
    (void)read_len;
    return 1;
}

TL_IMM_MSABI std::intptr_t tl_ImmEscapeW(void* const hkl, void* const himc, const std::uint32_t escape, void* const data) noexcept {
    (void)hkl;
    (void)himc;
    (void)escape;
    (void)data;
    return 0;
}

TL_IMM_MSABI int tl_ImmNotifyIME(void* const himc, const std::uint32_t action, const std::uint32_t index, const std::uint32_t value) noexcept {
    (void)himc;
    (void)action;
    (void)index;
    (void)value;
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
