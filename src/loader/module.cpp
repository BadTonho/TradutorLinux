#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace tradutorlinux::loader {
namespace {

using OwnedExport = runtime::GuestContext::ContextExport;
using OwnedModule = runtime::GuestContext::ContextModule;

std::vector<OwnedModule>& modules() { return runtime::guest_context().modules; }

std::mutex& modules_mutex() { return runtime::guest_context().modules_mutex; }

// Requer que modules_mutex() esteja bloqueado pelo chamador.
const OwnedModule* find_module_locked(const std::string_view dll) {
    const std::vector<OwnedModule>& registry = modules();
    const auto found = std::find_if(registry.begin(), registry.end(), [&](const OwnedModule& module) {
        return util::ascii_iequals(module.name, dll);
    });
    if (found == registry.end()) {
        return nullptr;
    }
    return &*found;
}

}  // namespace

bool is_api_set_dll(const std::string_view dll) noexcept {
    if (dll.size() < 11) {
        return false;
    }
    // api-ms-win-* (11 chars) e ext-ms-win-* (11 chars), case-insensitive.
    auto starts_with_ci = [](std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
        }
        return true;
    };
    return starts_with_ci(dll, "api-ms-win-") || starts_with_ci(dll, "ext-ms-win-");
}

bool is_kernelbase_dll(const std::string_view dll) noexcept {
    return util::ascii_iequals(dll, "kernelbase.dll") ||
           util::ascii_iequals(dll, "kernelbase");
}

[[nodiscard]] std::string_view preferred_api_set_module(const std::string_view dll) noexcept {
    auto starts_with_ci = [](std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
        }
        return true;
    };
    if (starts_with_ci(dll, "api-ms-win-core-") || starts_with_ci(dll, "ext-ms-win-kernel32-")) {
        return "KERNEL32.dll";
    }
    if (starts_with_ci(dll, "api-ms-win-crt-") || starts_with_ci(dll, "api-ms-win-core-crt-")) {
        return "msvcrt.dll";
    }
    if (starts_with_ci(dll, "api-ms-win-security-") || starts_with_ci(dll, "api-ms-win-eventing-") ||
        starts_with_ci(dll, "api-ms-win-service-")) {
        return "ADVAPI32.dll";
    }
    if (starts_with_ci(dll, "ext-ms-win-ntuser-") || starts_with_ci(dll, "ext-ms-win-gui-")) {
        return "USER32.dll";
    }
    if (starts_with_ci(dll, "ext-ms-win-gdi-")) {
        return "GDI32.dll";
    }
    if (starts_with_ci(dll, "api-ms-win-shcore-") || starts_with_ci(dll, "api-ms-win-shell-")) {
        return "SHELL32.dll";
    }
    return {};
}

ExportLookup find_export_forwarded(const ExportQuery& query) {
    ExportLookup direct = find_export(query);
    if (direct.found) {
        return direct;
    }
    // KERNELBASE é o host real de grande parte do KERNEL32 em Windows 7+.
    if (is_kernelbase_dll(query.dll)) {
        ExportLookup k32 = find_export(ExportQuery{"KERNEL32.dll", query.symbol});
        if (k32.found) return k32;
    }
    if (!is_api_set_dll(query.dll)) {
        return direct;
    }
    const std::string_view preferred = preferred_api_set_module(query.dll);
    if (!preferred.empty()) {
        ExportLookup pref_lookup = find_export(ExportQuery{preferred, query.symbol});
        if (pref_lookup.found) return pref_lookup;
    }
    // Ordem de tentativa espelha Wine: esgotar os provedores reais mais comuns.
    static constexpr std::string_view kCandidates[] = {
        "KERNEL32.dll", "USER32.dll",  "GDI32.dll",   "ADVAPI32.dll", "WS2_32.dll",
        "SHELL32.dll",  "ole32.dll",   "SHLWAPI.dll", "version.dll",  "WINMM.dll",
        "COMCTL32.dll", "COMDLG32.dll","IMM32.dll",   "PSAPI.dll",    "msvcrt.dll",
    };
    for (const auto& cand : kCandidates) {
        if (cand == preferred) continue;
        ExportLookup cand_lookup = find_export(ExportQuery{cand, query.symbol});
        if (cand_lookup.found) {
            return cand_lookup;
        }
    }
    return direct;
}

ExportLookup find_export_by_ordinal_forwarded(const std::string_view dll,
                                              const std::uint16_t ordinal) {
    ExportLookup direct = find_export_by_ordinal(dll, ordinal);
    if (direct.found) return direct;
    if (is_kernelbase_dll(dll)) {
        ExportLookup k32 = find_export_by_ordinal("KERNEL32.dll", ordinal);
        if (k32.found) return k32;
    }
    if (!is_api_set_dll(dll)) return direct;
    const std::string_view preferred = preferred_api_set_module(dll);
    if (!preferred.empty()) {
        ExportLookup pref_lookup = find_export_by_ordinal(preferred, ordinal);
        if (pref_lookup.found) return pref_lookup;
    }
    static constexpr std::string_view kCandidates[] = {
        "KERNEL32.dll", "USER32.dll",  "GDI32.dll",   "ADVAPI32.dll", "WS2_32.dll",
        "SHELL32.dll",  "ole32.dll",   "SHLWAPI.dll", "version.dll",  "WINMM.dll",
        "COMCTL32.dll", "COMDLG32.dll","IMM32.dll",   "PSAPI.dll",    "msvcrt.dll",
    };
    for (const auto& cand : kCandidates) {
        if (cand == preferred) continue;
        ExportLookup cand_lookup = find_export_by_ordinal(cand, ordinal);
        if (cand_lookup.found) return cand_lookup;
    }
    return direct;
}

bool is_module_registered_forwarded(const std::string_view dll) noexcept {
    if (is_module_registered(dll)) return true;
    if (is_kernelbase_dll(dll)) return is_module_registered("KERNEL32.dll");
    if (!is_api_set_dll(dll)) return false;
    // API Set só é considerado registrado se ao menos um provedor real existir.
    // Evita classificar como unknown-symbol quando registry está vazio (ex: testes com clear_modules).
    return is_module_registered("KERNEL32.dll") || is_module_registered("USER32.dll") ||
           is_module_registered("GDI32.dll") || is_module_registered("ADVAPI32.dll") ||
           is_module_registered("WS2_32.dll") || is_module_registered("SHELL32.dll") ||
           is_module_registered("ole32.dll") || is_module_registered("SHLWAPI.dll") ||
           is_module_registered("version.dll") || is_module_registered("WINMM.dll") ||
           is_module_registered("COMCTL32.dll") || is_module_registered("COMDLG32.dll") ||
           is_module_registered("IMM32.dll") || is_module_registered("PSAPI.dll") ||
           is_module_registered("msvcrt.dll");
}

bool register_module(const InternalModule& module) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    if (find_module_locked(module.name) != nullptr) {
        return false;
    }
    OwnedModule owned;
    owned.name = std::string{module.name};
    owned.exports.reserve(module.exports.size());
    for (const ExportedFunction& export_ : module.exports) {
        owned.exports.push_back(OwnedExport{
            .name = std::string{export_.name},
            .ordinal = export_.ordinal,
            .address = export_.address,
        });
    }
    modules().push_back(std::move(owned));
    return true;
}

void clear_modules() {
    std::lock_guard<std::mutex> lock(modules_mutex());
    modules().clear();
}

void register_builtin_modules() {
    register_kernel32_module();
    register_user32_module();
    register_gdi32_module();
    register_ws2_32_module();
    register_wininet_module();
    register_msvcrt_module();
    register_shell32_module();
    register_advapi32_module();
    register_ole32_module();
    register_oleaut32_module();
    register_wintrust_module();
    register_crypt32_module();
    register_shlwapi_module();
    register_version_module();
    register_winmm_module();
    register_gdiplus_module();
    register_uxtheme_module();
    register_dbghelp_module();
    register_powrprof_module();
    register_iphlpapi_module();
    register_comctl32_module();
    register_comdlg32_module();
    register_imm32_module();
    register_psapi_module();
    register_mpr_module();
    register_dwmapi_module();
    register_winapi_stubs_module();
}

bool is_module_registered(const std::string_view dll) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    return find_module_locked(dll) != nullptr;
}

ExportLookup find_export(const ExportQuery& query) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    const OwnedModule* module = find_module_locked(query.dll);
    if (module == nullptr) {
        return lookup;
    }
    const auto found = std::find_if(module->exports.begin(), module->exports.end(),
                                    [&](const OwnedExport& export_) {
                                        return export_.name == query.symbol;
                                    });
    if (found != module->exports.end()) {
        lookup.found = true;
        lookup.ordinal = found->ordinal;
        lookup.address = found->address;
    }
    return lookup;
}

ExportLookup find_export_by_ordinal(const std::string_view dll, const std::uint16_t ordinal) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    const OwnedModule* module = find_module_locked(dll);
    if (module == nullptr) {
        return lookup;
    }
    const auto found = std::find_if(module->exports.begin(), module->exports.end(),
                                    [&](const OwnedExport& export_) {
                                        return export_.ordinal == ordinal;
                                    });
    if (found != module->exports.end()) {
        lookup.found = true;
        lookup.ordinal = found->ordinal;
        lookup.address = found->address;
    }
    return lookup;
}

ExportLookup find_export_global(const std::string_view symbol) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    for (const auto& module : modules()) {
        const auto found = std::find_if(module.exports.begin(), module.exports.end(),
                                        [&](const OwnedExport& export_) {
                                            return export_.name == symbol;
                                        });
        if (found != module.exports.end()) {
            lookup.found = true;
            lookup.ordinal = found->ordinal;
            lookup.address = found->address;
            return lookup;
        }
    }
    return lookup;
}

ExportLookup find_export_by_ordinal_global(const std::uint16_t ordinal) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    for (const auto& module : modules()) {
        const auto found = std::find_if(module.exports.begin(), module.exports.end(),
                                        [&](const OwnedExport& export_) {
                                            return export_.ordinal == ordinal;
                                        });
        if (found != module.exports.end()) {
            lookup.found = true;
            lookup.ordinal = found->ordinal;
            lookup.address = found->address;
            return lookup;
        }
    }
    return lookup;
}

bool is_valid_module_handle(void* handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    return value == 0x1000U;
}

std::size_t registered_module_count() {
    std::lock_guard<std::mutex> lock(modules_mutex());
    return modules().size();
}

}  // namespace tradutorlinux::loader
