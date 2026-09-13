#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/ole32.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "core/runtime_state_common.hpp"
#include "core/runtime_gui_state.hpp"
#include "core/runtime_memory_state.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace tradutorlinux {

namespace {

constexpr std::uint32_t kNimAdd = 0U;
constexpr std::uint32_t kNimModify = 1U;
constexpr std::uint32_t kNimDelete = 2U;

struct GuestNotifyIconDataPrefix {
    std::uint32_t cb_size;
    std::uint32_t padding;
    void* hwnd;
    std::uint32_t icon_id;
    std::uint32_t flags;
    std::uint32_t callback_message;
};
static_assert(offsetof(GuestNotifyIconDataPrefix, hwnd) == 8);
static_assert(offsetof(GuestNotifyIconDataPrefix, callback_message) == 24);

bool write_shell_path(std::uint16_t* const path, const std::u16string& wide) noexcept {
    const std::size_t length = std::min(wide.size(), static_cast<std::size_t>(259));
    if (length > 0 && runtime::write_guest_memory(path, wide.data(), length * sizeof(std::uint16_t)).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    auto* const terminator = reinterpret_cast<std::uint16_t*>(
        reinterpret_cast<std::byte*>(path) + length * sizeof(std::uint16_t));
    return write_guest_value(terminator, std::uint16_t{0});
}

bool update_tray_registration(const std::uint32_t message, void* const data) noexcept {
    if (data == nullptr) {
        return true;
    }
    if (!mapped_guest_range(data, sizeof(GuestNotifyIconDataPrefix), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return false;
    }
    const auto* const input = static_cast<const GuestNotifyIconDataPrefix*>(data);
    if (input->cb_size < sizeof(GuestNotifyIconDataPrefix)) {
        set_last_error(abi::kErrorBadLength);
        return false;
    }
    WindowSlot* const window = find_window_slot(input->hwnd);
    if (window == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return false;
    }
    if (message == kNimDelete) {
        window->tray_registered = false;
        window->tray_icon_id = 0;
        window->tray_callback_message = 0;
    } else if (message == kNimAdd || message == kNimModify) {
        window->tray_registered = true;
        window->tray_icon_id = input->icon_id;
        window->tray_callback_message = input->callback_message;
    }
    return true;
}

}  // namespace

extern "C" {

// CommandLineToArgvW: parseia a linha de comando no formato Windows e retorna
// um array de wchar_t* terminado em NULL. O resultado é liberado com
// LocalFree (aqui free()). O array e as strings ficam num único bloco.
TL_MSABI std::uint16_t** tl_CommandLineToArgvW(const std::uint16_t* command_line,
                                               int* argument_count) noexcept {
    if (command_line == nullptr || argument_count == nullptr) {
        return nullptr;
    }
    std::vector<std::string> arguments;
    std::string current;
    bool in_quotes = false;
    const std::uint16_t* p = command_line;
    for (;;) {
        const std::uint16_t c = *p;
        if (c == 0) {
            if (!current.empty() || in_quotes) {
                arguments.push_back(current);
            }
            break;
        }
        if (c == L'"') {
            in_quotes = !in_quotes;
            ++p;
            continue;
        }
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            if (!in_quotes) {
                if (!current.empty()) {
                    arguments.push_back(current);
                    current.clear();
                }
                ++p;
                continue;
            }
        }
        const std::uint16_t literal[2] = {c, 0};
        current += util::wide_to_utf8(literal);
        ++p;
    }
    const std::size_t count = arguments.size();
    std::size_t total_units = 0;
    for (const std::string& argument : arguments) {
        total_units += util::utf8_to_wide(argument).size() + 1;
    }
    total_units += 1;
    auto* block = static_cast<std::uint8_t*>(
        std::calloc((count + 1) * sizeof(std::uint16_t*) + total_units * sizeof(std::uint16_t), 1));
    if (block == nullptr) {
        return nullptr;
    }
    if (!register_local_free_block(block)) {
        std::free(block);
        return nullptr;
    }
    auto** argv = reinterpret_cast<std::uint16_t**>(block);
    auto* strings = reinterpret_cast<std::uint16_t*>(block + (count + 1) * sizeof(std::uint16_t*));
    for (std::size_t i = 0; i < count; ++i) {
        const std::u16string units = util::utf8_to_wide(arguments[i]);
        argv[i] = strings;
        std::copy(units.begin(), units.end(), strings);
        strings += units.size();
        *strings = 0;
        ++strings;
    }
    argv[count] = nullptr;
    *argument_count = static_cast<int>(count);
    return argv;
}

TL_MSABI int tl_ShellNotifyIconA(const std::uint32_t message, void* const data) noexcept {
    if (!update_tray_registration(message, data)) {
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

namespace {
struct Guid {
    std::uint32_t Data1;
    std::uint16_t Data2;
    std::uint16_t Data3;
    std::uint8_t Data4[8];
};

bool guid_equal(const Guid* a, const Guid* b) noexcept {
    return std::memcmp(a, b, sizeof(Guid)) == 0;
}

std::filesystem::path user_profile_path(const prefix::EnvironmentPaths& paths) {
    return paths.drive_c / "users" / "guest";
}

std::filesystem::path known_folder_path_for_guid(const Guid* rfid,
                                                 const prefix::EnvironmentPaths& paths) {
    // GUIDs conhecidos
    static const Guid kRoamingAppData = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}};
    static const Guid kLocalAppData = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}};
    static const Guid kProgramData = {0x62AB5D82, 0xFDC1, 0x4DC3, {0xA9,0xDD,0x07,0x0D,0x1D,0x49,0x5D,0x97}};
    static const Guid kDesktop = {0xB4BFCC3A, 0xDB2C, 0x424C, {0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41}};
    static const Guid kDocuments = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}};
    static const Guid kDownloads = {0x374DE290, 0x123F, 0x4565, {0x91,0x64,0x39,0xC4,0x92,0x5E,0x46,0x7B}};
    static const Guid kProfile = {0x5E6C858F, 0x0E22, 0x4760, {0x9A,0xFE,0xEA,0x33,0x17,0xB6,0x71,0x73}};
    if (guid_equal(rfid, &kRoamingAppData)) {
        return paths.app_data_roaming;
    }
    if (guid_equal(rfid, &kLocalAppData)) {
        return paths.app_data_local;
    }
    if (guid_equal(rfid, &kProgramData)) {
        return paths.drive_c / "ProgramData";
    }
    if (guid_equal(rfid, &kDesktop)) {
        return user_profile_path(paths) / "Desktop";
    }
    if (guid_equal(rfid, &kDocuments)) {
        return user_profile_path(paths) / "Documents";
    }
    if (guid_equal(rfid, &kDownloads)) {
        return user_profile_path(paths) / "Downloads";
    }
    if (guid_equal(rfid, &kProfile)) {
        return user_profile_path(paths);
    }
    return {};
}

std::filesystem::path csidl_to_path(const int csidl, const prefix::EnvironmentPaths& paths) {
    switch (csidl & 0xFF) {
        case 0x00: return user_profile_path(paths) / "Desktop"; // CSIDL_DESKTOP
        case 0x05: return user_profile_path(paths) / "Documents"; // PERSONAL
        case 0x1A: return paths.app_data_roaming; // APPDATA
        case 0x1C: return paths.app_data_local; // LOCAL_APPDATA
        case 0x23: return paths.drive_c / "ProgramData"; // COMMON_APPDATA
        case 0x28: return user_profile_path(paths); // PROFILE
        default: return user_profile_path(paths);
    }
}

bool ensure_directory_exists(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
}

bool is_safe_relative_subdirectory(const std::string& value) {
    const std::filesystem::path relative(value);
    if (relative.empty() || relative.is_absolute()) {
        return !relative.is_absolute();
    }
    for (const std::filesystem::path& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}
} // namespace

TL_MSABI int tl_SHGetKnownFolderPath(const void* rfid, const std::uint32_t flags, void* token,
                                     std::uint16_t** path) noexcept {
    (void)flags;
    (void)token;
    if (rfid == nullptr || path == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057); // E_INVALIDARG
    }
    if (!mapped_guest_range(rfid, sizeof(Guid), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(guest_prefix_root());
    const std::filesystem::path native_path =
        known_folder_path_for_guid(static_cast<const Guid*>(rfid), paths);
    if (native_path.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    if (!ensure_directory_exists(native_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80004005); // E_FAIL
    }
    const std::string win_path = prefix::to_windows_path(native_path, guest_prefix_root());
    const std::u16string wide = util::utf8_to_wide(win_path);
    const std::size_t bytes = (wide.size() + 1) * sizeof(std::uint16_t);
    std::uint16_t* allocated = static_cast<std::uint16_t*>(tl_CoTaskMemAlloc(bytes));
    if (allocated == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return static_cast<int>(0x8007000E); // E_OUTOFMEMORY
    }
    std::copy(wide.begin(), wide.end(), allocated);
    allocated[wide.size()] = 0;
    if (!write_guest_value(path, allocated)) {
        static_cast<void>(tl_CoTaskMemFree(allocated));
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057); // E_INVALIDARG
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

TL_MSABI int tl_SHGetFolderPathW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                 std::uint16_t* path) noexcept {
    (void)hwnd;
    (void)token;
    (void)flags;
    if (path == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(guest_prefix_root());
    const std::filesystem::path native_path = csidl_to_path(csidl, paths);
    if (!ensure_directory_exists(native_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80004005); // E_FAIL
    }
    const std::string win_path = prefix::to_windows_path(native_path, guest_prefix_root());
    const std::u16string wide = util::utf8_to_wide(win_path);
    if (!write_shell_path(path, wide)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

TL_MSABI int tl_SHGetFolderPathAndSubDirW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                          const std::uint16_t* sub_dir, std::uint16_t* path) noexcept {
    (void)hwnd;
    (void)token;
    (void)flags;
    if (path == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(guest_prefix_root());
    std::filesystem::path native_path = csidl_to_path(csidl, paths);
    if (sub_dir != nullptr) {
        if (!mapped_guest_wstring(sub_dir)) {
            set_last_error(abi::kErrorInvalidParameter);
            return static_cast<int>(0x80070057);
        }
        std::string sub = util::wide_to_utf8(sub_dir);
        std::replace(sub.begin(), sub.end(), '\\', '/');
        if (!is_safe_relative_subdirectory(sub)) {
            set_last_error(abi::kErrorInvalidParameter);
            return static_cast<int>(0x80070057);
        }
        native_path /= std::filesystem::path(sub);
    }
    if (!prefix::is_path_within(native_path, paths.drive_c) ||
        !ensure_directory_exists(native_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80004005); // E_FAIL
    }
    const std::string win_path = prefix::to_windows_path(native_path, guest_prefix_root());
    const std::u16string wide = util::utf8_to_wide(win_path);
    if (!write_shell_path(path, wide)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_ShellExecuteW(void* hwnd, const std::uint16_t* operation,
                                const std::uint16_t* file, const std::uint16_t* parameters,
                                const std::uint16_t* directory, int show) noexcept {
    (void)hwnd;
    (void)show;
    if ((operation != nullptr && !mapped_guest_wstring(operation)) ||
        (parameters != nullptr && !mapped_guest_wstring(parameters)) ||
        (directory != nullptr && !mapped_guest_wstring(directory)) ||
        (file != nullptr && !mapped_guest_wstring(file))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (file == nullptr || file[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    // Abrir documentos/processos pelo shell ainda não integra o ciclo de vida
    // do processo convidado. Retornar êxito aqui faria o aplicativo acreditar
    // que um processo foi iniciado quando nenhuma ação ocorreu.
    set_last_error(abi::kErrorNotSupported);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(31)); // SE_ERR_NOASSOC (< 32)
}

struct GuestShellExecuteInfoW {
    std::uint32_t cb_size;
    std::uint32_t f_mask;
    void* hwnd;
    const std::uint16_t* lp_verb;
    const std::uint16_t* lp_file;
    const std::uint16_t* lp_parameters;
    const std::uint16_t* lp_directory;
    std::int32_t n_show;
    void* h_inst_app;
    void* lp_id_list;
    const std::uint16_t* lp_class;
    void* hkey_class;
    std::uint32_t dw_hot_key;
    std::uint32_t reserved;
    void* h_monitor;
    void* h_process;
};
static_assert(sizeof(GuestShellExecuteInfoW) == 112);
static_assert(offsetof(GuestShellExecuteInfoW, h_inst_app) == 56);
static_assert(offsetof(GuestShellExecuteInfoW, h_process) == 104);

TL_MSABI int tl_ShellExecuteExW(void* exec_info) noexcept {
    if (exec_info == nullptr || !mapped_guest_range(exec_info, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t cbSize = 0;
    std::memcpy(&cbSize, exec_info, sizeof(cbSize));
    if (cbSize < sizeof(GuestShellExecuteInfoW) ||
        !mapped_guest_range(exec_info, sizeof(GuestShellExecuteInfoW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const info = static_cast<GuestShellExecuteInfoW*>(exec_info);
    if (info->lp_file == nullptr || !mapped_guest_wstring(info->lp_file) || info->lp_file[0] == 0 ||
        (info->lp_verb != nullptr && !mapped_guest_wstring(info->lp_verb)) ||
        (info->lp_parameters != nullptr && !mapped_guest_wstring(info->lp_parameters)) ||
        (info->lp_directory != nullptr && !mapped_guest_wstring(info->lp_directory)) ||
        (info->lp_class != nullptr && !mapped_guest_wstring(info->lp_class))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    info->h_inst_app = nullptr;
    info->h_process = nullptr;
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

struct GuestShFileOpStructW {
    void* hwnd;
    std::uint32_t func;
    const std::uint16_t* from;
    const std::uint16_t* to;
    std::uint16_t flags;
    int any_operations_aborted;
    void* name_mappings;
    const std::uint16_t* progress_title;
};

TL_MSABI int tl_SHFileOperationW(void* const file_op) noexcept {
    if (file_op == nullptr || !mapped_guest_range(file_op, sizeof(GuestShFileOpStructW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 1;
    }
    auto* const operation = static_cast<GuestShFileOpStructW*>(file_op);
    operation->any_operations_aborted = 1;
    operation->name_mappings = nullptr;
    // Cópia, movimentação, exclusão e renomeação do shell ainda não têm
    // implementação. Não sinalize sucesso sem ter alterado o sistema de
    // arquivos do prefixo.
    set_last_error(abi::kErrorNotSupported);
    return static_cast<int>(abi::kErrorNotSupported);
}

struct GuestShFileInfoW {
    void* hIcon{nullptr};
    int iIcon{0};
    std::uint32_t dwAttributes{0};
    std::uint16_t szDisplayName[260]{};
    std::uint16_t szTypeName[80]{};
};

TL_MSABI std::uintptr_t tl_SHGetFileInfoW(const std::uint16_t* const path, const std::uint32_t file_attributes,
                                          void* const sfi, const std::uint32_t cb_file_info,
                                          const std::uint32_t flags) noexcept {
    if (path != nullptr && !mapped_guest_wstring(path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    (void)file_attributes;
    (void)flags;
    if (sfi != nullptr && (cb_file_info < sizeof(GuestShFileInfoW) ||
                           !mapped_guest_range(sfi, sizeof(GuestShFileInfoW), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorNotSupported);
    return 0;
}

TL_MSABI int tl_SHGetPathFromIDListW(const void* const pidl, std::uint16_t* const path) noexcept {
    (void)pidl;
    if (path == nullptr || !mapped_guest_range(path, 260 * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(guest_prefix_root());
    const std::filesystem::path profile = user_profile_path(paths);
    if (!ensure_directory_exists(profile)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string win_profile = prefix::to_windows_path(profile, guest_prefix_root());
    const std::u16string wide_home = util::utf8_to_wide(win_profile);
    if (!write_shell_path(path, wide_home)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SHBrowseForFolderW(void* const bi) noexcept {
    if (bi == nullptr || !mapped_guest_range(bi, 64, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorNotSupported);
    return nullptr;
}

TL_MSABI int tl_SHGetMalloc(void** const pp_malloc) noexcept {
    if (pp_malloc == nullptr || !write_guest_value(pp_malloc, static_cast<void*>(&g_guest_imalloc))) {
        return static_cast<int>(0x80070057U); // E_INVALIDARG
    }
    return 0; // S_OK
}

TL_MSABI void tl_SHChangeNotify(const std::int32_t event_id, const std::uint32_t flags,
                                const void* const item1, const void* const item2) noexcept {
    (void)event_id;
    (void)flags;
    (void)item1;
    (void)item2;
}

TL_MSABI std::uint32_t tl_ExtractIconExW(const std::uint16_t* const file, const int index,
                                         void** const icon_large, void** const icon_small,
                                         const std::uint32_t icons) noexcept {
    (void)file;
    (void)index;
    (void)icons;
    if (icon_large != nullptr) {
        static_cast<void>(write_guest_value(icon_large, reinterpret_cast<void*>(0x1000)));
    }
    if (icon_small != nullptr) {
        static_cast<void>(write_guest_value(icon_small, reinterpret_cast<void*>(0x1000)));
    }
    return 1;
}

TL_MSABI int tl_SHGetDesktopFolder(void** const ppshf) noexcept {
    if (ppshf != nullptr) {
        static_cast<void>(write_guest_value(ppshf, reinterpret_cast<void*>(0x4445534BULL))); // 'DESK'
    }
    return 0; // S_OK
}

TL_MSABI int tl_SHGetSpecialFolderLocation(void* const hwnd, const int folder, void** const ppidl) noexcept {
    (void)hwnd;
    (void)folder;
    if (ppidl != nullptr) {
        static_cast<void>(write_guest_value(ppidl, reinterpret_cast<void*>(0x5049444CULL))); // 'PIDL'
    }
    return 0; // S_OK
}

TL_MSABI int tl_SHGetSpecialFolderPathW(void* const hwnd, std::uint16_t* const path,
                                       const int folder, const int create) noexcept {
    (void)hwnd;
    (void)create;
    return tl_SHGetFolderPathW(hwnd, folder, nullptr, 0, path) == 0 ? 1 : 0;
}

TL_MSABI int tl_ShellNotifyIconW(const std::uint32_t message, void* const data) noexcept {
    if (!update_tray_registration(message, data)) {
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_ShellExecuteA(void* const hwnd, const char* const operation, const char* const file, const char* const parameters, const char* const directory, const int show_cmd) noexcept {
    (void)hwnd;
    (void)show_cmd;
    if ((operation != nullptr && !mapped_guest_cstring(operation)) ||
        (parameters != nullptr && !mapped_guest_cstring(parameters)) ||
        (directory != nullptr && !mapped_guest_cstring(directory)) ||
        (file != nullptr && !mapped_guest_cstring(file))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (file == nullptr || file[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorNotSupported);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(31)); // SE_ERR_NOASSOC (< 32)
}

TL_MSABI int tl_SHCreateItemFromParsingName(const wchar_t* const pszPath, void* const pbc, const void* const riid, void** const ppv) noexcept {
    (void)pszPath;
    (void)pbc;
    (void)riid;
    if (ppv != nullptr) {
        static_cast<void>(write_guest_value(ppv, reinterpret_cast<void*>(0x4954454DULL))); // 'ITEM'
    }
    return 0; // S_OK
}

TL_MSABI std::uint32_t tl_DragQueryFileW(void* const hDrop, const std::uint32_t iFile, wchar_t* const lpszFile, const std::uint32_t cch) noexcept {
    (void)hDrop;
    (void)iFile;
    (void)lpszFile;
    (void)cch;
    return 0;
}

TL_MSABI int tl_DragQueryPoint(void* const hDrop, void* const lppt) noexcept {
    (void)hDrop;
    if (lppt != nullptr) {
        const std::array<std::int32_t, 2> point{};
        static_cast<void>(runtime::write_guest_memory(lppt, point.data(), sizeof(point)));
    }
    return 1;
}

TL_MSABI void tl_DragFinish(void* const hDrop) noexcept {
    (void)hDrop;
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_shell32_module() {
    static const ExportedFunction kShell32Exports[] = {
        {"CommandLineToArgvW", 1, reinterpret_cast<std::uintptr_t>(&tl_CommandLineToArgvW), ExportSupport::Full},
        {"Shell_NotifyIconA", 2, reinterpret_cast<std::uintptr_t>(&tl_ShellNotifyIconA),
         ExportSupport::Limited},
        {"SHGetKnownFolderPath", 3, reinterpret_cast<std::uintptr_t>(&tl_SHGetKnownFolderPath), ExportSupport::Full},
        {"SHGetFolderPathW", 4, reinterpret_cast<std::uintptr_t>(&tl_SHGetFolderPathW), ExportSupport::Full},
        {"SHGetFolderPathAndSubDirW", 5, reinterpret_cast<std::uintptr_t>(&tl_SHGetFolderPathAndSubDirW), ExportSupport::Full},
        {"ShellExecuteW", 6, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteW), ExportSupport::Full},
        {"ShellExecuteExW", 7, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteExW), ExportSupport::Full},
        {"SHFileOperationW", 8, reinterpret_cast<std::uintptr_t>(&tl_SHFileOperationW), ExportSupport::Full},
        {"SHGetFileInfoW", 9, reinterpret_cast<std::uintptr_t>(&tl_SHGetFileInfoW), ExportSupport::Full},
        {"SHGetPathFromIDListW", 10, reinterpret_cast<std::uintptr_t>(&tl_SHGetPathFromIDListW), ExportSupport::Full},
        {"SHBrowseForFolderW", 11, reinterpret_cast<std::uintptr_t>(&tl_SHBrowseForFolderW),
         ExportSupport::Stub},
        {"SHGetMalloc", 12, reinterpret_cast<std::uintptr_t>(&tl_SHGetMalloc), ExportSupport::Full},
        {"SHChangeNotify", 13, reinterpret_cast<std::uintptr_t>(&tl_SHChangeNotify), ExportSupport::Full},
        {"ExtractIconExW", 14, reinterpret_cast<std::uintptr_t>(&tl_ExtractIconExW), ExportSupport::Full},
        {"SHGetDesktopFolder", 15, reinterpret_cast<std::uintptr_t>(&tl_SHGetDesktopFolder), ExportSupport::Full},
        {"SHGetSpecialFolderLocation", 16, reinterpret_cast<std::uintptr_t>(&tl_SHGetSpecialFolderLocation), ExportSupport::Full},
        {"SHGetSpecialFolderPathW", 17, reinterpret_cast<std::uintptr_t>(&tl_SHGetSpecialFolderPathW), ExportSupport::Full},
        {"Shell_NotifyIconW", 18, reinterpret_cast<std::uintptr_t>(&tl_ShellNotifyIconW),
         ExportSupport::Limited},
        {"ShellExecuteA", 19, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteA), ExportSupport::Full},
        {"SHCreateItemFromParsingName", 20, reinterpret_cast<std::uintptr_t>(&tl_SHCreateItemFromParsingName), ExportSupport::Full},
        {"DragQueryFileW", 21, reinterpret_cast<std::uintptr_t>(&tl_DragQueryFileW), ExportSupport::Full},
        {"DragQueryPoint", 22, reinterpret_cast<std::uintptr_t>(&tl_DragQueryPoint), ExportSupport::Full},
        {"DragFinish", 23, reinterpret_cast<std::uintptr_t>(&tl_DragFinish), ExportSupport::Full},
        {"", 165, reinterpret_cast<std::uintptr_t>(&tl_SHCreateItemFromParsingName), ExportSupport::Full},
    };
    static const InternalModule kShell32Module{"SHELL32.dll", kShell32Exports};
    register_module(kShell32Module);
}

}  // namespace tradutorlinux::loader
