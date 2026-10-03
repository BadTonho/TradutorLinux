#include "kernel32_process_internal.hpp"

namespace tradutorlinux {

namespace {

static std::string g_custom_dll_directory;

[[nodiscard]] std::string extract_module_filename(const char* input) noexcept {
    if (input == nullptr) return {};
    std::string_view view{input};
    const std::size_t pos = view.find_last_of("/\\:");
    if (pos != std::string_view::npos) {
        if (pos + 1 >= view.size()) return {};
        view = view.substr(pos + 1);
    }
    view = std::string_view{view.data(), strnlen(view.data(), view.size())};
    // Trim leading/trailing spaces (comum em LoadLibrary).
    std::size_t start = 0;
    while (start < view.size() && std::isspace(static_cast<unsigned char>(view[start]))) ++start;
    std::size_t end = view.size();
    while (end > start && std::isspace(static_cast<unsigned char>(view[end - 1]))) --end;
    return std::string{view.substr(start, end - start)};
}

[[nodiscard]] std::string normalize_module_name(const char* input) noexcept {
    std::string fname = extract_module_filename(input);
    if (fname.empty()) return {};
    std::string lower;
    lower.reserve(fname.size() + 4);
    for (const char c : fname) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (!lower.ends_with(".dll")) lower += ".dll";
    return lower;
}

[[nodiscard]] bool is_module_available(const std::string& normalized) noexcept {
    if (loader::registered_module_count() == 0) {
        loader::register_builtin_modules();
    }
    if (loader::is_module_registered(normalized)) return true;
    if (loader::is_api_set_dll(normalized) || loader::is_kernelbase_dll(normalized)) {
        return loader::is_module_registered_forwarded(normalized);
    }
    return false;
}

[[nodiscard]] bool is_valid_handle_for_free(void* handle) noexcept {
    if (handle == nullptr) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    if (value == 0x1000U) return true;
    if (g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base)) return true;
    if (runtime::guest_context().module_graph != nullptr &&
        runtime::guest_context().module_graph->is_valid_module_handle(handle)) return true;
    if (loader::is_valid_module_handle(handle)) return true;
    return false;
}

[[nodiscard]] void* load_library_normalized(const std::string& normalized) noexcept {
    if (normalized.empty()) return nullptr;
    if (runtime::guest_context().module_graph != nullptr) {
        return runtime::guest_context().module_graph->load_library(normalized);
    }
    if (!is_module_available(normalized)) return nullptr;
    return reinterpret_cast<void*>(0x1000U);
}

[[nodiscard]] void* get_module_handle_normalized(const std::string& normalized) noexcept {
    if (normalized.empty()) return nullptr;
    if (runtime::guest_context().module_graph != nullptr) {
        return runtime::guest_context().module_graph->get_module_handle(normalized);
    }
    if (!is_module_available(normalized)) return nullptr;
    return reinterpret_cast<void*>(0x1000U);
}

} // namespace

extern "C" {

TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept {
    if (module_name == nullptr) {
        set_last_error(abi::kErrorSuccess);
        if (g_guest_image_base != nullptr) {
            return const_cast<std::byte*>(g_guest_image_base);
        }
        return reinterpret_cast<void*>(0x1000U);
    }
    std::string guest_module_name;
    if (!runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(guest_module_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    void* const handle = get_module_handle_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return tl_GetModuleHandleA(nullptr);
    }
    std::u16string guest_module_name;
    if (!runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_module_name.data()), guest_module_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    void* const handle = get_module_handle_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI int tl_GetModuleHandleExA(std::uint32_t flags, const char* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string guest_module_name;
    bool module_name_copied = false;
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        // Se o endereço estiver dentro da imagem do convidado ou for o token de módulo, trata como handle do exe.
        if (addr == 0x1000U) {
            if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                if (!write_guest_value(module, const_cast<std::byte*>(g_guest_image_base))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (runtime::guest_context().module_graph != nullptr) {
            if (void* const handle = runtime::guest_context().module_graph->module_handle_from_address(addr);
                handle != nullptr) {
                if (!write_guest_value(module, handle)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        // Tenta interpretar module_name como string se estiver em memória de convidado e falhar o range check acima.
        // Para manter compatibilidade, se o ponteiro for uma string válida, cai no caminho normal.
        if (!runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        module_name_copied = true;
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleA(nullptr);
        if (handle == nullptr) return 0;
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    // module_name é nome (A). Validar.
    if (module_name == nullptr ||
        (!module_name_copied &&
         !runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(guest_module_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    if (runtime::guest_context().module_graph != nullptr) {
        void* const handle = runtime::guest_context().module_graph->get_module_handle(normalized);
        if (handle == nullptr) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        if (!is_module_available(normalized)) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetModuleHandleExW(std::uint32_t flags, const std::uint16_t* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string guest_module_name;
    bool module_name_copied = false;
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        if (addr == 0x1000U) {
            if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                if (!write_guest_value(module, const_cast<std::byte*>(g_guest_image_base))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (runtime::guest_context().module_graph != nullptr) {
            if (void* const handle = runtime::guest_context().module_graph->module_handle_from_address(addr);
                handle != nullptr) {
                if (!write_guest_value(module, handle)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (!runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        module_name_copied = true;
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleW(nullptr);
        if (handle == nullptr) return 0;
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (module_name == nullptr ||
        (!module_name_copied &&
         !runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_module_name.data()), guest_module_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    if (runtime::guest_context().module_graph != nullptr) {
        void* const handle = runtime::guest_context().module_graph->get_module_handle(normalized);
        if (handle == nullptr) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        if (!is_module_available(normalized)) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadLibraryA(const char* file_name) noexcept {
    std::string guest_file_name;
    if (!runtime::copy_guest_cstring(file_name, kMaxModuleStringUnits, guest_file_name) ||
        guest_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(guest_file_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    void* const handle = load_library_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_LoadLibraryW(const std::uint16_t* file_name) noexcept {
    std::u16string guest_file_name;
    if (!runtime::copy_guest_wstring(file_name, kMaxModuleStringUnits, guest_file_name) ||
        guest_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_file_name.data()), guest_file_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    void* const handle = load_library_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_LoadLibraryExA(const char* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryA(file_name);
}

TL_MSABI void* tl_LoadLibraryExW(const std::uint16_t* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryW(file_name);
}

TL_MSABI int tl_FreeLibrary(void* module) noexcept {
    if (runtime::guest_context().module_graph != nullptr &&
        runtime::guest_context().module_graph->is_valid_module_handle(module)) {
        if (!runtime::guest_context().module_graph->free_library(module)) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (!is_valid_handle_for_free(module)) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcAddress(void* module, const char* proc_name) noexcept {
    if (proc_name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto proc_addr = reinterpret_cast<std::uintptr_t>(proc_name);
    // Ordinal via MAKEINTRESOURCE (HIWORD == 0).
    if (proc_addr <= 0xFFFFU) {
        const auto ordinal = static_cast<std::uint16_t>(proc_addr & 0xFFFFU);
        if (runtime::guest_context().module_graph != nullptr) {
            if (module != nullptr && !is_valid_handle_for_free(module) &&
                (g_guest_image_base == nullptr ||
                 reinterpret_cast<std::uintptr_t>(module) !=
                     reinterpret_cast<std::uintptr_t>(g_guest_image_base))) {
                set_last_error(abi::kErrorInvalidHandle);
                return nullptr;
            }
            const loader::GraphExportLookup found =
                runtime::guest_context().module_graph->get_proc_address(module, ordinal);
            if (found.lookup.found && found.lookup.address != 0) {
                set_last_error(abi::kErrorSuccess);
                return reinterpret_cast<void*>(found.lookup.address);
            }
            set_last_error(abi::kErrorProcNotFound);
            return nullptr;
        }
        if (loader::registered_module_count() == 0) loader::register_builtin_modules();
        loader::ExportLookup found{};
        if (module != nullptr && is_valid_handle_for_free(module)) {
            found = loader::find_export_by_ordinal_global(ordinal);
        } else if (module == nullptr) {
            found = loader::find_export_by_ordinal_global(ordinal);
        } else {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        if (found.found && found.address != 0) {
            set_last_error(abi::kErrorSuccess);
            return reinterpret_cast<void*>(found.address);
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    std::string guest_proc_name;
    if (!runtime::copy_guest_cstring(proc_name, kMaxModuleStringUnits, guest_proc_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_proc_name.empty()) {
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    if (loader::registered_module_count() == 0) loader::register_builtin_modules();
    if (runtime::guest_context().module_graph != nullptr) {
        if (module != nullptr && !is_valid_handle_for_free(module) &&
            (g_guest_image_base == nullptr ||
             reinterpret_cast<std::uintptr_t>(module) !=
                 reinterpret_cast<std::uintptr_t>(g_guest_image_base))) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        const loader::GraphExportLookup found =
            runtime::guest_context().module_graph->get_proc_address(module, guest_proc_name.c_str());
        if (found.lookup.found && found.lookup.address != 0) {
            set_last_error(abi::kErrorSuccess);
            return reinterpret_cast<void*>(found.lookup.address);
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    if (module != nullptr && !is_valid_handle_for_free(module)) {
        const auto value = reinterpret_cast<std::uintptr_t>(module);
        const bool is_image = g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base);
        if (!is_image) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    loader::ExportLookup found{};
    found = loader::find_export_global(guest_proc_name.c_str());
    if (found.found && found.address != 0) {
        set_last_error(abi::kErrorSuccess);
        return reinterpret_cast<void*>(found.address);
    }
    set_last_error(abi::kErrorProcNotFound);
    return nullptr;
}

TL_MSABI int tl_SetDllDirectoryW(const std::uint16_t* const path_name) noexcept {
    if (path_name == nullptr) {
        g_custom_dll_directory.clear();
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    std::u16string guest_path_name;
    if (!runtime::copy_guest_wstring(path_name, kMaxModuleStringUnits, guest_path_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_custom_dll_directory = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_path_name.data()), guest_path_name.size());
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_FreeLibraryAndExitThread(void* const module_handle, const std::uint32_t exit_code) noexcept {
    (void)module_handle;
    tl_ExitThread(exit_code);
}

TL_MSABI int tl_SetDefaultDllDirectories(const std::uint32_t directory_flags) noexcept {
    (void)directory_flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_FreeLibraryWhenCallbackReturns(void* const pci, void* const module) noexcept {
    (void)pci;
    (void)module;
}

}  // extern "C"
}  // namespace tradutorlinux
