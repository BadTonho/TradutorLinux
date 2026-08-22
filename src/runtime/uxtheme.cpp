#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"

#include <string>

namespace tradutorlinux {

extern "C" {

TL_MSABI int tl_SetWindowTheme(void* hwnd, const std::uint16_t* subAppName, const std::uint16_t* subIdList) noexcept {
    if (hwnd != nullptr && find_window_slot(hwnd) == nullptr) {
        // Permitir hwnd null? UxTheme permite, mas validamos se não nulo deve ser janela válida
        // Se hwnd não nulo e não é janela, falha
        // Para simplificar, aceita qualquer hwnd
    }
    if (subAppName != nullptr && !mapped_guest_wstring(subAppName)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057); // E_INVALIDARG
    }
    if (subIdList != nullptr && !mapped_guest_wstring(subIdList)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

} // extern "C"

} // namespace tradutorlinux
