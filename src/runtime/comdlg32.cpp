#include "tradutorlinux/runtime/comdlg32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "core/runtime_state_common.hpp"

namespace tradutorlinux {

namespace {

constexpr std::uint32_t kCdErrDialogFailure = 0xFFFFU;
constexpr std::uint32_t kCdErrStructSize = 0x0001U;
thread_local std::uint32_t g_commdlg_extended_error = 0;

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

int reject_common_dialog(void* dialog) noexcept {
    if (dialog == nullptr || !mapped_range(dialog, sizeof(std::uint32_t), false)) {
        g_commdlg_extended_error = kCdErrStructSize;
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_commdlg_extended_error = kCdErrDialogFailure;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

}  // namespace

extern "C" {

TL_COMDLG_MSABI int tl_GetOpenFileNameA(void* open_filename) noexcept {
    return reject_common_dialog(open_filename);
}

TL_COMDLG_MSABI int tl_GetOpenFileNameW(void* open_filename) noexcept {
    return reject_common_dialog(open_filename);
}

TL_COMDLG_MSABI int tl_GetSaveFileNameA(void* open_filename) noexcept {
    return tl_GetOpenFileNameA(open_filename);
}

TL_COMDLG_MSABI int tl_GetSaveFileNameW(void* open_filename) noexcept {
    return tl_GetOpenFileNameW(open_filename);
}

TL_COMDLG_MSABI int tl_ChooseColorA(void* choose_color) noexcept {
    return reject_common_dialog(choose_color);
}

TL_COMDLG_MSABI int tl_ChooseColorW(void* choose_color) noexcept {
    return reject_common_dialog(choose_color);
}

TL_COMDLG_MSABI int tl_ChooseFontA(void* const choose_font) noexcept {
    return reject_common_dialog(choose_font);
}

TL_COMDLG_MSABI int tl_ChooseFontW(void* const choose_font) noexcept {
    return reject_common_dialog(choose_font);
}

TL_COMDLG_MSABI int tl_PrintDlgW(void* const print_dlg) noexcept {
    return reject_common_dialog(print_dlg);
}

TL_COMDLG_MSABI std::uint32_t tl_CommDlgExtendedError() noexcept {
    return g_commdlg_extended_error;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_comdlg32_module() {
    static const ExportedFunction kComdlg32Exports[] = {
        {"GetOpenFileNameA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetOpenFileNameA), ExportSupport::Stub},
        {"GetOpenFileNameW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetOpenFileNameW), ExportSupport::Stub},
        {"GetSaveFileNameA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetSaveFileNameA), ExportSupport::Stub},
        {"GetSaveFileNameW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetSaveFileNameW), ExportSupport::Stub},
        {"ChooseColorA", 5, reinterpret_cast<std::uintptr_t>(&tl_ChooseColorA), ExportSupport::Stub},
        {"ChooseColorW", 6, reinterpret_cast<std::uintptr_t>(&tl_ChooseColorW), ExportSupport::Stub},
        {"PrintDlgW", 7, reinterpret_cast<std::uintptr_t>(&tl_PrintDlgW), ExportSupport::Stub},
        {"CommDlgExtendedError", 8, reinterpret_cast<std::uintptr_t>(&tl_CommDlgExtendedError), ExportSupport::Stub},
        {"ChooseFontA", 9, reinterpret_cast<std::uintptr_t>(&tl_ChooseFontA), ExportSupport::Stub},
        {"ChooseFontW", 10, reinterpret_cast<std::uintptr_t>(&tl_ChooseFontW), ExportSupport::Stub},
    };
    static const InternalModule kComdlg32Module{"COMDLG32.dll", kComdlg32Exports};
    register_module(kComdlg32Module);
}

}  // namespace tradutorlinux::loader
