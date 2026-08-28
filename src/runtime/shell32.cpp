#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace tradutorlinux {

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
    (void)message;
    (void)data;
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

std::string get_home_dir() {
    const char* home = ::getenv("HOME");
    if (home != nullptr && home[0] != '\0') return home;
    home = ::getenv("USERPROFILE");
    if (home != nullptr && home[0] != '\0') return home;
    return "/tmp";
}

std::string known_folder_path_for_guid(const Guid* rfid) {
    // GUIDs conhecidos
    static const Guid kRoamingAppData = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}};
    static const Guid kLocalAppData = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}};
    static const Guid kProgramData = {0x62AB5D82, 0xFDC1, 0x4DC3, {0xA9,0xDD,0x07,0x0D,0x1D,0x49,0x5D,0x97}};
    static const Guid kDesktop = {0xB4BFCC3A, 0xDB2C, 0x424C, {0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41}};
    static const Guid kDocuments = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}};
    static const Guid kDownloads = {0x374DE290, 0x123F, 0x4565, {0x91,0x64,0x39,0xC4,0x92,0x5E,0x46,0x7B}};
    static const Guid kProfile = {0x5E6C858F, 0x0E22, 0x4760, {0x9A,0xFE,0xEA,0x33,0x17,0xB6,0x71,0x73}};
    const std::string home = get_home_dir();
    if (guid_equal(rfid, &kRoamingAppData)) {
        const char* xdg = ::getenv("XDG_CONFIG_HOME");
        if (xdg != nullptr && xdg[0] != '\0') return xdg;
        return home + "/.config";
    }
    if (guid_equal(rfid, &kLocalAppData)) {
        const char* xdg = ::getenv("XDG_DATA_HOME");
        if (xdg != nullptr && xdg[0] != '\0') return xdg;
        return home + "/.local/share";
    }
    if (guid_equal(rfid, &kProgramData)) {
        return "/tmp/ProgramData";
    }
    if (guid_equal(rfid, &kDesktop)) {
        return home + "/Desktop";
    }
    if (guid_equal(rfid, &kDocuments)) {
        const char* xdg = ::getenv("XDG_DOCUMENTS_DIR");
        if (xdg != nullptr && xdg[0] != '\0') return xdg;
        return home + "/Documents";
    }
    if (guid_equal(rfid, &kDownloads)) {
        return home + "/Downloads";
    }
    if (guid_equal(rfid, &kProfile)) {
        return home;
    }
    return "";
}

std::string csidl_to_path(int csidl) {
    const std::string home = get_home_dir();
    switch (csidl & 0xFF) {
        case 0x00: return home + "/Desktop"; // CSIDL_DESKTOP
        case 0x05: return home + "/Documents"; // PERSONAL
        case 0x1A: { // APPDATA
            const char* xdg = ::getenv("XDG_CONFIG_HOME");
            if (xdg && xdg[0]) return xdg;
            return home + "/.config";
        }
        case 0x1C: { // LOCAL_APPDATA
            const char* xdg = ::getenv("XDG_DATA_HOME");
            if (xdg && xdg[0]) return xdg;
            return home + "/.local/share";
        }
        case 0x23: return "/tmp/ProgramData"; // COMMON_APPDATA
        case 0x28: return home; // PROFILE
        default: return home;
    }
}

void ensure_directory_exists(const std::string& path) {
    // Cria diretório de forma best-effort, ignora erro se já existe
    ::mkdir(path.c_str(), 0755);
    // Tenta criar pais recursivamente via sistema simples
    // Se falhar por ENOENT, tenta criar pai
    if (::mkdir(path.c_str(), 0755) != 0 && errno == ENOENT) {
        std::size_t pos = path.find_last_of('/');
        if (pos != std::string::npos && pos > 0) {
            ensure_directory_exists(path.substr(0, pos));
            ::mkdir(path.c_str(), 0755);
        }
    }
}
} // namespace

TL_MSABI int tl_SHGetKnownFolderPath(const void* rfid, const std::uint32_t flags, void* token,
                                     std::uint16_t** path) noexcept {
    (void)flags;
    (void)token;
    if (rfid == nullptr || path == nullptr || !mapped_guest_range(path, sizeof(*path), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057); // E_INVALIDARG
    }
    if (!mapped_guest_range(rfid, sizeof(Guid), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    const Guid* guid = static_cast<const Guid*>(rfid);
    std::string utf8_path = known_folder_path_for_guid(guid);
    if (utf8_path.empty()) {
        utf8_path = get_home_dir();
    }
    ensure_directory_exists(utf8_path);
    const std::u16string wide = util::utf8_to_wide(utf8_path);
    const std::size_t bytes = (wide.size() + 1) * sizeof(std::uint16_t);
    // Usa CoTaskMemAlloc (ole32) para alocar; aqui malloc é suficiente pois CoTaskMemFree é free
    std::uint16_t* allocated = static_cast<std::uint16_t*>(::malloc(bytes));
    if (allocated == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return static_cast<int>(0x8007000E); // E_OUTOFMEMORY
    }
    std::copy(wide.begin(), wide.end(), allocated);
    allocated[wide.size()] = 0;
    *path = allocated;
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

TL_MSABI int tl_SHGetFolderPathW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                 std::uint16_t* path) noexcept {
    (void)hwnd;
    (void)token;
    (void)flags;
    if (path == nullptr || !mapped_guest_range(path, 260 * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    std::string utf8_path = csidl_to_path(csidl);
    ensure_directory_exists(utf8_path);
    const std::u16string wide = util::utf8_to_wide(utf8_path);
    const std::size_t to_copy = std::min<std::size_t>(wide.size(), 259);
    for (std::size_t i = 0; i < to_copy; ++i) path[i] = wide[i];
    path[to_copy] = 0;
    set_last_error(abi::kErrorSuccess);
    return 0; // S_OK
}

TL_MSABI int tl_SHGetFolderPathAndSubDirW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                          const std::uint16_t* sub_dir, std::uint16_t* path) noexcept {
    (void)hwnd;
    (void)token;
    (void)flags;
    if (path == nullptr || !mapped_guest_range(path, 260 * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<int>(0x80070057);
    }
    std::string base = csidl_to_path(csidl);
    if (sub_dir != nullptr) {
        if (!mapped_guest_wstring(sub_dir)) {
            set_last_error(abi::kErrorInvalidParameter);
            return static_cast<int>(0x80070057);
        }
        std::string sub = util::wide_to_utf8(sub_dir);
        // Normaliza separadores
        for (char& c : sub) if (c == '\\') c = '/';
        if (!sub.empty() && sub.front() == '/') sub.erase(0,1);
        if (!base.empty() && base.back() != '/') base += "/";
        base += sub;
        // Converte de volta separador Windows para path nativo? Mantém '/'
    }
    ensure_directory_exists(base);
    const std::u16string wide = util::utf8_to_wide(base);
    const std::size_t to_copy = std::min<std::size_t>(wide.size(), 259);
    for (std::size_t i = 0; i < to_copy; ++i) path[i] = wide[i];
    path[to_copy] = 0;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI void* tl_ShellExecuteW(void* hwnd, const std::uint16_t* operation,
                                const std::uint16_t* file, const std::uint16_t* parameters,
                                const std::uint16_t* directory, int show) noexcept {
    (void)hwnd;
    (void)operation;
    (void)parameters;
    (void)directory;
    (void)show;
    if (file != nullptr && !mapped_guest_wstring(file)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0)); // failure <32
    }
    if (file == nullptr || file[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(2)); // SE_ERR_FNF
    }
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(42)); // >32 success
}

TL_MSABI int tl_ShellExecuteExW(void* exec_info) noexcept {
    if (exec_info == nullptr || !mapped_guest_range(exec_info, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t cbSize = 0;
    std::memcpy(&cbSize, exec_info, sizeof(cbSize));
    if (cbSize < 60 || !mapped_guest_range(exec_info, cbSize, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // SHELLEXECUTEINFOW layout: cbSize(4), fMask(4), hwnd(8), lpVerb(8), lpFile(8), lpParameters(8), lpDirectory(8), nShow(4), hInstApp(8), ... hProcess(8)
    // Para stub, apenas valida lpFile se presente e preenche hProcess com dummy
    auto* base = static_cast<std::uint8_t*>(exec_info);
    // Offsets: lpFile at 24 (4+4+8+8), mas depende de packing; vamos apenas validar que se lpFile não nulo, é wstring válida
    // Simplificamos: não valida profundamente, apenas retorna sucesso e seta hProcess se houver espaço
    if (cbSize >= 60) {
        // Tenta setar hProcess em offset 60? Na estrutura real, hProcess está em offset 56 (após hInstApp). Vamos tentar escrever um handle dummy se houver espaço
        // Se cbSize >= 64, tenta escrever em 56
        if (cbSize >= 64) {
            void* dummy = reinterpret_cast<void*>(0x1);
            std::memcpy(base + 56, &dummy, sizeof(void*));
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
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
    set_last_error(abi::kErrorSuccess);
    return 0; // 0 = S_OK / success in SHFileOperation
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
    (void)path;
    (void)file_attributes;
    (void)flags;
    if (sfi != nullptr && cb_file_info >= sizeof(GuestShFileInfoW) &&
        mapped_guest_range(sfi, sizeof(GuestShFileInfoW), true)) {
        std::memset(sfi, 0, sizeof(GuestShFileInfoW));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SHGetPathFromIDListW(const void* const pidl, std::uint16_t* const path) noexcept {
    (void)pidl;
    if (path == nullptr || !mapped_guest_range(path, 260 * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string home = get_home_dir();
    const std::u16string wide_home(home.begin(), home.end());
    const std::size_t len = std::min(wide_home.size(), static_cast<std::size_t>(259));
    std::memcpy(path, wide_home.data(), len * sizeof(std::uint16_t));
    path[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_SHBrowseForFolderW(void* const bi) noexcept {
    (void)bi;
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_MSABI int tl_SHGetMalloc(void** const pp_malloc) noexcept {
    if (pp_malloc == nullptr || !mapped_guest_range(pp_malloc, sizeof(void*), true)) {
        return static_cast<int>(0x80070057U); // E_INVALIDARG
    }
    *pp_malloc = nullptr;
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
    if (icon_large != nullptr && mapped_guest_range(icon_large, sizeof(void*), true)) {
        *icon_large = reinterpret_cast<void*>(0x1000);
    }
    if (icon_small != nullptr && mapped_guest_range(icon_small, sizeof(void*), true)) {
        *icon_small = reinterpret_cast<void*>(0x1000);
    }
    return 1;
}

TL_MSABI int tl_SHGetDesktopFolder(void** const ppshf) noexcept {
    if (ppshf != nullptr && mapped_guest_range(ppshf, sizeof(void*), true)) {
        *ppshf = reinterpret_cast<void*>(0x4445534BULL); // 'DESK'
    }
    return 0; // S_OK
}

TL_MSABI int tl_SHGetSpecialFolderLocation(void* const hwnd, const int folder, void** const ppidl) noexcept {
    (void)hwnd;
    (void)folder;
    if (ppidl != nullptr && mapped_guest_range(ppidl, sizeof(void*), true)) {
        *ppidl = reinterpret_cast<void*>(0x5049444CULL); // 'PIDL'
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
    (void)message;
    (void)data;
    return 1;
}

TL_MSABI void* tl_ShellExecuteA(void* const hwnd, const char* const operation, const char* const file, const char* const parameters, const char* const directory, const int show_cmd) noexcept {
    (void)hwnd;
    (void)operation;
    (void)file;
    (void)parameters;
    (void)directory;
    (void)show_cmd;
    return reinterpret_cast<void*>(42); // HINSTANCE > 32 indicates success
}

}  // extern "C"

}  // namespace tradutorlinux
