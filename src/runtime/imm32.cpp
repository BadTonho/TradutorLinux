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

}  // extern "C"

}  // namespace tradutorlinux
