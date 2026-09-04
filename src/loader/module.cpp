#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include "tradutorlinux/runtime/guest_context.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
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

constexpr std::size_t kMaxForwarderDepth = 32;

struct ForwarderQuery {
    std::string_view dll;
    bool by_ordinal = false;
    std::string_view symbol;
    std::uint16_t ordinal = 0;
};

struct ForwarderVisit {
    std::string dll;
    std::string symbol;
    bool by_ordinal = false;
    std::uint16_t ordinal = 0;
};

struct ForwarderTarget {
    std::string dll;
    bool by_ordinal = false;
    std::string symbol;
    std::uint16_t ordinal = 0;
};

[[nodiscard]] ExportLookup forwarder_error(const std::string_view detail) {
    ExportLookup lookup;
    lookup.detail = detail;
    return lookup;
}

[[nodiscard]] bool has_visit(const std::vector<ForwarderVisit>& visits,
                             const ForwarderQuery& query) {
    return std::any_of(visits.begin(), visits.end(), [&](const ForwarderVisit& visit) {
        if (visit.by_ordinal != query.by_ordinal || !util::ascii_iequals(visit.dll, query.dll)) {
            return false;
        }
        return visit.by_ordinal ? visit.ordinal == query.ordinal : visit.symbol == query.symbol;
    });
}

[[nodiscard]] std::optional<ForwarderTarget> parse_forwarder(
    const std::string_view forwarder) {
    const std::size_t separator = forwarder.rfind('.');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1U >= forwarder.size()) {
        return std::nullopt;
    }
    ForwarderTarget target;
    target.dll = std::string(forwarder.substr(0, separator));
    if (target.dll.find('.') == std::string::npos) target.dll += ".dll";
    const std::string_view symbol = forwarder.substr(separator + 1U);
    if (symbol.front() != '#') {
        target.symbol = std::string(symbol);
        return target;
    }
    if (symbol.size() == 1U) return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t index = 1; index < symbol.size(); ++index) {
        const unsigned char digit = static_cast<unsigned char>(symbol[index]);
        if (digit < static_cast<unsigned char>('0') ||
            digit > static_cast<unsigned char>('9')) {
            return std::nullopt;
        }
        const std::uint32_t value_digit = digit - static_cast<unsigned char>('0');
        if (value > (0xFFFFU - value_digit) / 10U) return std::nullopt;
        value = value * 10U + value_digit;
    }
    if (value == 0) return std::nullopt;
    target.by_ordinal = true;
    target.ordinal = static_cast<std::uint16_t>(value);
    return target;
}

ExportLookup resolve_forwarder_named(const ForwarderQuery& query,
                                     std::vector<ForwarderVisit>& visits,
                                     const std::size_t depth);

ExportLookup resolve_forwarder_ordinal(const ForwarderQuery& query,
                                       std::vector<ForwarderVisit>& visits,
                                       const std::size_t depth) {
    if (depth >= kMaxForwarderDepth) return forwarder_error("forwarder depth exceeded");
    if (has_visit(visits, query)) return forwarder_error("forwarder cycle detected");
    visits.push_back(ForwarderVisit{std::string(query.dll), {}, true, query.ordinal});
    const auto finish = [&](ExportLookup lookup) {
        visits.pop_back();
        return lookup;
    };

    ExportLookup direct = find_export_by_ordinal(query.dll, query.ordinal);
    if (direct.found) {
        if (direct.forwarder.empty()) return finish(std::move(direct));
        const auto target = parse_forwarder(direct.forwarder);
        if (!target.has_value()) return finish(forwarder_error("invalid export forwarder"));
        const ForwarderQuery next{target->dll, target->by_ordinal, target->symbol,
                                  target->ordinal};
        ExportLookup resolved = target->by_ordinal
                                    ? resolve_forwarder_ordinal(next, visits, depth + 1U)
                                    : resolve_forwarder_named(next, visits, depth + 1U);
        if (!resolved.found && resolved.detail.empty()) {
            resolved.detail = "forwarder target not found";
        }
        return finish(std::move(resolved));
    }
    if (is_kernelbase_dll(query.dll)) {
        ExportLookup kernel32 = resolve_forwarder_ordinal(
            ForwarderQuery{"KERNEL32.dll", true, {}, query.ordinal}, visits, depth + 1U);
        if (kernel32.found || !kernel32.detail.empty()) return finish(std::move(kernel32));
    }
    return finish(std::move(direct));
}

ExportLookup resolve_forwarder_named(const ForwarderQuery& query,
                                     std::vector<ForwarderVisit>& visits,
                                     const std::size_t depth) {
    if (depth >= kMaxForwarderDepth) return forwarder_error("forwarder depth exceeded");
    if (has_visit(visits, query)) return forwarder_error("forwarder cycle detected");
    visits.push_back(ForwarderVisit{std::string(query.dll), std::string(query.symbol), false, 0});
    const auto finish = [&](ExportLookup lookup) {
        visits.pop_back();
        return lookup;
    };

    ExportLookup direct = find_export(ExportQuery{query.dll, query.symbol});
    if (direct.found) {
        if (direct.forwarder.empty()) return finish(std::move(direct));
        const auto target = parse_forwarder(direct.forwarder);
        if (!target.has_value()) return finish(forwarder_error("invalid export forwarder"));
        const ForwarderQuery next{target->dll, target->by_ordinal, target->symbol,
                                  target->ordinal};
        ExportLookup resolved = target->by_ordinal
                                    ? resolve_forwarder_ordinal(next, visits, depth + 1U)
                                    : resolve_forwarder_named(next, visits, depth + 1U);
        if (!resolved.found && resolved.detail.empty()) {
            resolved.detail = "forwarder target not found";
        }
        return finish(std::move(resolved));
    }
    if (is_kernelbase_dll(query.dll)) {
        ExportLookup kernel32 = resolve_forwarder_named(
            ForwarderQuery{"KERNEL32.dll", false, query.symbol, 0}, visits, depth + 1U);
        if (kernel32.found || !kernel32.detail.empty()) return finish(std::move(kernel32));
    }
    if (!is_api_set_dll(query.dll)) return finish(std::move(direct));

    static constexpr std::string_view kCandidates[] = {
        "KERNEL32.dll", "USER32.dll",  "GDI32.dll",   "ADVAPI32.dll", "WS2_32.dll",
        "SHELL32.dll",  "ole32.dll",   "SHLWAPI.dll", "version.dll",  "WINMM.dll",
        "COMCTL32.dll", "COMDLG32.dll", "IMM32.dll",  "PSAPI.dll",    "msvcrt.dll",
    };
    const std::string_view preferred = preferred_api_set_module(query.dll);
    const auto try_module = [&](const std::string_view module) -> std::optional<ExportLookup> {
        ExportLookup candidate = resolve_forwarder_named(
            ForwarderQuery{module, false, query.symbol, 0}, visits, depth + 1U);
        if (candidate.found || !candidate.detail.empty()) return candidate;
        return std::nullopt;
    };
    if (!preferred.empty()) {
        if (const auto candidate = try_module(preferred)) return finish(*candidate);
    }
    for (const std::string_view candidate_module : kCandidates) {
        if (candidate_module == preferred) continue;
        if (const auto candidate = try_module(candidate_module)) {
            return finish(*candidate);
        }
    }
    return finish(std::move(direct));
}

ExportLookup find_export_forwarded(const ExportQuery& query) {
    std::vector<ForwarderVisit> visits;
    return resolve_forwarder_named(ForwarderQuery{query.dll, false, query.symbol, 0}, visits, 0);
}

ExportLookup find_export_by_ordinal_forwarded(const std::string_view dll,
                                              const std::uint16_t ordinal) {
    std::vector<ForwarderVisit> visits;
    return resolve_forwarder_ordinal(ForwarderQuery{dll, true, {}, ordinal}, visits, 0);
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
        if (!export_.forwarder.empty() && export_.address != 0) {
            return false;
        }
        owned.exports.push_back(OwnedExport{
            .name = std::string{export_.name},
            .ordinal = export_.ordinal,
            .address = export_.address,
            .support = static_cast<std::uint8_t>(export_.support),
            .forwarder = std::string{export_.forwarder},
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
        lookup.support = static_cast<ExportSupport>(found->support);
        lookup.forwarder = found->forwarder;
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
        lookup.support = static_cast<ExportSupport>(found->support);
        lookup.forwarder = found->forwarder;
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
            lookup.support = static_cast<ExportSupport>(found->support);
            lookup.forwarder = found->forwarder;
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
            lookup.support = static_cast<ExportSupport>(found->support);
            lookup.forwarder = found->forwarder;
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
