#include "tradutorlinux/loader/module_graph.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/mman.h>

namespace tradutorlinux::loader {
namespace {

constexpr std::size_t kNoModule = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMaxPeFileSize = 512U * 1024U * 1024U;
constexpr std::size_t kMaxForwarderDepth = 32;
constexpr std::uintptr_t kBuiltinHandleBase = 0x0000200000000000ULL;

enum class LoadedState {
    Resolving,
    Ready,
    Attaching,
    Attached,
    Rejected,
    Unloaded,
};

[[nodiscard]] std::string provider_name(const ModuleProvider provider) {
    switch (provider) {
        case ModuleProvider::Profile: return "profile";
        case ModuleProvider::DriveC: return "drive_c";
        case ModuleProvider::Builtin: return "builtin";
    }
    return "unknown";
}

[[nodiscard]] std::string normalize_module_name(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    value = value.substr(start, end - start);
    const std::size_t separator = value.find_last_of("/\\:");
    if (separator != std::string_view::npos) {
        if (separator + 1U >= value.size()) return {};
        value = value.substr(separator + 1U);
    }
    if (value.empty()) return {};
    std::string result;
    result.reserve(value.size() + 4U);
    for (const char character : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (!result.ends_with(".dll")) result += ".dll";
    return result;
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return std::nullopt;
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxPeFileSize) return std::nullopt;
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::uint64_t relocate_va(const std::uint64_t value,
                                         const pe::PeInfo& info,
                                         const MappedImage& image) noexcept {
    if (value == 0 || value < info.image_base ||
        value - info.image_base >= static_cast<std::uint64_t>(info.size_of_image)) {
        return value;
    }
    const std::uint64_t rva = value - info.image_base;
    if (rva >= image.size || image.base > std::numeric_limits<std::uint64_t>::max() - rva) {
        return value;
    }
    return image.base + rva;
}

[[nodiscard]] bool same_visit(const std::vector<GuestModuleGraph::LookupVisit>&,
                              std::string_view, bool, std::string_view,
                              std::uint16_t) noexcept;

}  // namespace

struct GuestModuleGraph::LoadedModule {
    std::string name;
    ModuleProvider provider{ModuleProvider::Builtin};
    std::filesystem::path source;
    std::vector<std::byte> file_bytes;
    pe::PeInfo info;
    MappedImage image;
    LoadedState state{LoadedState::Resolving};
    std::uint32_t static_refs{0};
    std::uint32_t dynamic_refs{0};
    bool process_attached{false};
    std::vector<std::size_t> dependencies;
};

struct GuestModuleGraph::LookupVisit {
    std::string module;
    bool by_ordinal{false};
    std::string symbol;
    std::uint16_t ordinal{0};
};

namespace {

[[nodiscard]] bool same_visit(const std::vector<GuestModuleGraph::LookupVisit>& visits,
                              const std::string_view module, const bool by_ordinal,
                              const std::string_view symbol,
                              const std::uint16_t ordinal) noexcept {
    return std::any_of(visits.begin(), visits.end(), [&](const auto& visit) {
        if (visit.by_ordinal != by_ordinal || !util::ascii_iequals(visit.module, module)) {
            return false;
        }
        return by_ordinal ? visit.ordinal == ordinal : visit.symbol == symbol;
    });
}

struct ForwarderTarget {
    std::string module;
    bool by_ordinal{false};
    std::string symbol;
    std::uint16_t ordinal{0};
};

[[nodiscard]] std::optional<ForwarderTarget> parse_forwarder(const std::string_view value) {
    const std::size_t separator = value.rfind('.');
    if (separator == std::string_view::npos || separator == 0 || separator + 1U >= value.size()) {
        return std::nullopt;
    }
    std::string module = normalize_module_name(value.substr(0, separator));
    if (module.empty()) return std::nullopt;
    const std::string_view target = value.substr(separator + 1U);
    if (!target.starts_with('#')) {
        return ForwarderTarget{.module = std::move(module), .by_ordinal = false,
                               .symbol = std::string{target}, .ordinal = 0};
    }
    if (target.size() == 1U) return std::nullopt;
    std::uint32_t ordinal = 0;
    for (std::size_t index = 1; index < target.size(); ++index) {
        const unsigned char digit = static_cast<unsigned char>(target[index]);
        if (digit < '0' || digit > '9') return std::nullopt;
        const std::uint32_t value_digit = digit - '0';
        if (ordinal > (0xFFFFU - value_digit) / 10U) return std::nullopt;
        ordinal = ordinal * 10U + value_digit;
    }
    if (ordinal == 0) return std::nullopt;
    return ForwarderTarget{.module = std::move(module), .by_ordinal = true,
                           .symbol = {}, .ordinal = static_cast<std::uint16_t>(ordinal)};
}

[[nodiscard]] ExportLookup direct_export(const GuestModuleGraph::LoadedModule& module,
                                          const std::string_view symbol) {
    const auto found = std::find_if(module.info.exports.begin(), module.info.exports.end(),
                                    [&](const pe::ExportedSymbol& export_) {
                                        return export_.by_name && export_.name == symbol;
                                    });
    if (found == module.info.exports.end()) return {};
    ExportLookup result;
    result.found = true;
    result.ordinal = found->ordinal;
    result.address = found->forwarder.empty() && found->rva < module.image.size
                         ? module.image.base + found->rva
                         : 0;
    result.support = ExportSupport::Full;
    result.forwarder = found->forwarder;
    return result;
}

[[nodiscard]] ExportLookup direct_export(const GuestModuleGraph::LoadedModule& module,
                                          const std::uint16_t ordinal) {
    const auto found = std::find_if(module.info.exports.begin(), module.info.exports.end(),
                                    [&](const pe::ExportedSymbol& export_) {
                                        return export_.ordinal == ordinal;
                                    });
    if (found == module.info.exports.end()) return {};
    ExportLookup result;
    result.found = true;
    result.ordinal = found->ordinal;
    result.address = found->forwarder.empty() && found->rva < module.image.size
                         ? module.image.base + found->rva
                         : 0;
    result.support = ExportSupport::Full;
    result.forwarder = found->forwarder;
    return result;
}

[[nodiscard]] ExportLookup direct_export(const pe::PeInfo& info,
                                          const MappedImage& image,
                                          const std::string_view symbol) {
    const auto found = std::find_if(info.exports.begin(), info.exports.end(),
                                    [&](const pe::ExportedSymbol& export_) {
                                        return export_.by_name && export_.name == symbol;
                                    });
    if (found == info.exports.end()) return {};
    ExportLookup result;
    result.found = true;
    result.ordinal = found->ordinal;
    result.address = found->forwarder.empty() && found->rva < image.size
                         ? image.base + found->rva
                         : 0;
    result.support = ExportSupport::Full;
    result.forwarder = found->forwarder;
    return result;
}

[[nodiscard]] ExportLookup direct_export(const pe::PeInfo& info,
                                          const MappedImage& image,
                                          const std::uint16_t ordinal) {
    const auto found = std::find_if(info.exports.begin(), info.exports.end(),
                                    [&](const pe::ExportedSymbol& export_) {
                                        return export_.ordinal == ordinal;
                                    });
    if (found == info.exports.end()) return {};
    ExportLookup result;
    result.found = true;
    result.ordinal = found->ordinal;
    result.address = found->forwarder.empty() && found->rva < image.size
                         ? image.base + found->rva
                         : 0;
    result.support = ExportSupport::Full;
    result.forwarder = found->forwarder;
    return result;
}

[[nodiscard]] bool patch_import(MappedImage& image, ResolvedImport& entry) {
    if (entry.iat_rva == 0) {
        entry.status = ImportStatus::UnsupportedMechanism;
        entry.detail = "RVA do slot na IAT nulo";
        return false;
    }
    std::array<std::byte, 8> address{};
    for (std::size_t index = 0; index < address.size(); ++index) {
        address[index] = static_cast<std::byte>((entry.address >> (index * 8U)) & 0xFFU);
    }
    if (write_image_bytes(image, entry.iat_rva, address.data(), address.size()) !=
        PatchStatus::Success) {
        entry.status = ImportStatus::UnsupportedMechanism;
        entry.detail = "não foi possível preencher o slot da IAT";
        return false;
    }
    return true;
}

}  // namespace

GuestModuleGraph::GuestModuleGraph(std::filesystem::path prefix_root,
                                   std::optional<compat::Profile> profile,
                                   std::filesystem::path main_path,
                                   const bool trace_enabled) noexcept
    : prefix_root_(std::move(prefix_root)),
      main_path_(std::move(main_path)),
      profile_(std::move(profile)),
      trace_enabled_(trace_enabled) {}

GuestModuleGraph::~GuestModuleGraph() {
    reset();
}

std::string GuestModuleGraph::normalize_module(const std::string_view module_name) const {
    return normalize_module_name(module_name);
}

void GuestModuleGraph::trace_event(const std::string_view event,
                                   const std::string_view module,
                                   const std::string_view detail) const noexcept {
    if (!trace_enabled_) return;
    try {
        const bool is_provider = detail == "profile" || detail == "drive_c" ||
                                 detail == "builtin";
        const std::array fields{
            diagnostics::TraceField{"module", std::string{module}},
            diagnostics::TraceField{"provider", is_provider ? std::string{detail} : std::string{}},
            diagnostics::TraceField{"detail", is_provider ? std::string{} : std::string{detail}},
        };
        diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Loader,
                                 diagnostics::TraceLevel::Info, event, fields);
    } catch (...) {
    }
}

std::optional<std::size_t> GuestModuleGraph::find_loaded(
    const std::string_view module_name, const ModuleProvider provider) const noexcept {
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        const LoadedModule& module = *modules_[index];
        if (module.provider == provider && module.state != LoadedState::Rejected &&
            module.state != LoadedState::Unloaded && util::ascii_iequals(module.name, module_name)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> GuestModuleGraph::find_module_for_handle(
    void* const module_handle) const noexcept {
    if (module_handle == nullptr) return std::nullopt;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module_handle);
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        const LoadedModule& module = *modules_[index];
        if (module.image.base == value && module.image.memory != nullptr &&
            module.state != LoadedState::Rejected && module.state != LoadedState::Unloaded) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> GuestModuleGraph::ensure_profile_module(
    const std::string_view module_name, const std::size_t owner_index) {
    if (!profile_.has_value()) return std::nullopt;
    const auto rejected = std::find_if(rejected_profile_modules_.begin(),
                                      rejected_profile_modules_.end(),
                                      [&](const std::string& item) {
                                          return util::ascii_iequals(item, module_name);
                                      });
    if (rejected != rejected_profile_modules_.end()) return std::nullopt;
    const auto mapping = std::find_if(profile_->dlls.begin(), profile_->dlls.end(),
                                      [&](const compat::DllMapping& item) {
                                          return util::ascii_iequals(item.module, module_name);
                                      });
    if (mapping == profile_->dlls.end()) return std::nullopt;
    const auto existing = find_loaded(module_name, ModuleProvider::Profile);
    if (existing.has_value()) {
        if (modules_[*existing]->state == LoadedState::Resolving) {
            trace_event("module-cycle", module_name, "profile");
            return std::nullopt;
        }
        return existing;
    }
    const auto paths = prefix::get_environment_paths(prefix_root_);
    return load_pe_module(std::string{module_name}, ModuleProvider::Profile,
                          paths.compat_dlls_dir / mapping->source, owner_index);
}

std::optional<std::size_t> GuestModuleGraph::ensure_drive_module(
    const std::string_view module_name, const std::filesystem::path& requester,
    const std::size_t owner_index) {
    const auto paths = prefix::get_environment_paths(prefix_root_);
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path module_path{std::string{module_name}};
    if (!requester.empty()) candidates.push_back(requester.parent_path() / module_path);
    candidates.push_back(paths.system32_dir / module_path);
    candidates.push_back(paths.windows_dir / module_path);
    candidates.push_back(paths.drive_c / module_path);

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec) ||
            std::filesystem::is_symlink(candidate, ec)) {
            continue;
        }
        const auto existing = find_loaded(module_name, ModuleProvider::DriveC);
        if (existing.has_value()) {
            if (modules_[*existing]->state == LoadedState::Resolving) {
                trace_event("module-cycle", module_name, "drive_c");
                return std::nullopt;
            }
            return existing;
        }
        return load_pe_module(std::string{module_name}, ModuleProvider::DriveC,
                              candidate, owner_index);
    }
    return std::nullopt;
}

std::optional<std::size_t> GuestModuleGraph::load_pe_module(
    std::string module_name, const ModuleProvider provider,
    std::filesystem::path source, const std::size_t owner_index) {
    static_cast<void>(owner_index);
    const auto existing = find_loaded(module_name, provider);
    if (existing.has_value()) return existing;
    const std::optional<std::vector<std::byte>> bytes = read_file(source);
    if (!bytes.has_value()) {
        trace_event("provider-rejected", module_name, provider_name(provider));
        return std::nullopt;
    }
    const pe::ParseResult parsed = pe::parse_pe(*bytes);
    if (parsed.status != pe::ParseStatus::Success || !parsed.info.is_dll ||
        !parsed.info.is_pe32_plus || parsed.info.machine != 0x8664) {
        trace_event("provider-rejected", module_name, provider_name(provider));
        return std::nullopt;
    }
    MapResult mapped = map_image(parsed.info, *bytes);
    if (mapped.status != MapStatus::Success) {
        trace_event("provider-rejected", module_name, provider_name(provider));
        return std::nullopt;
    }
    runtime::invalidate_memory_map_cache();

    auto loaded = std::make_unique<LoadedModule>();
    loaded->name = std::move(module_name);
    loaded->provider = provider;
    loaded->source = std::move(source);
    loaded->file_bytes = *bytes;
    loaded->info = parsed.info;
    loaded->image = std::move(mapped.image);
    loaded->state = LoadedState::Resolving;
    modules_.push_back(std::move(loaded));
    const std::size_t index = modules_.size() - 1U;
    trace_event("dll-mapped", modules_[index]->name, provider_name(provider));

    const ResolveResult imports = resolve_imports_for_module(
        modules_[index]->image, modules_[index]->info, modules_[index]->source, index);
    if (imports.status != ImportStatus::Resolved) {
        const std::string rejected_name = modules_[index]->name;
        for (const std::size_t dependency : modules_[index]->dependencies) {
            if (dependency < index) release_dependency(dependency);
        }
        for (std::size_t cursor = modules_.size(); cursor > index; --cursor) {
            LoadedModule& candidate = *modules_[cursor - 1U];
            if (candidate.image.memory != nullptr) {
                unmap_image(candidate.image);
                runtime::invalidate_memory_map_cache();
            }
            candidate.state = LoadedState::Unloaded;
        }
        modules_.resize(index);
        if (provider == ModuleProvider::Profile) reject_profile_module(rejected_name);
        trace_event("provider-rejected", rejected_name, imports.error_message);
        return std::nullopt;
    }
    modules_[index]->state = LoadedState::Ready;
    trace_event("provider-selected", modules_[index]->name, provider_name(provider));
    return index;
}

GraphExportLookup GuestModuleGraph::resolve_export(
    const std::string_view module_name, const std::string_view symbol,
    const std::filesystem::path& requester, const std::size_t owner_index) {
    std::vector<LookupVisit> visits;
    return resolve_export_named_internal(module_name, symbol, requester, owner_index, visits, 0);
}

GraphExportLookup GuestModuleGraph::resolve_export(
    const std::string_view module_name, const std::uint16_t ordinal,
    const std::filesystem::path& requester, const std::size_t owner_index) {
    std::vector<LookupVisit> visits;
    return resolve_export_ordinal_internal(module_name, ordinal, requester, owner_index, visits, 0);
}

GraphExportLookup GuestModuleGraph::resolve_export_named_internal(
    const std::string_view raw_module, const std::string_view symbol,
    const std::filesystem::path& requester, const std::size_t owner_index,
    std::vector<LookupVisit>& visits, const std::size_t depth) {
    const std::string module_name = normalize_module(raw_module);
    GraphExportLookup missing;
    if (module_name.empty()) {
        missing.lookup.detail = "nome de módulo inválido";
        return missing;
    }
    if (depth >= kMaxForwarderDepth || same_visit(visits, module_name, false, symbol, 0)) {
        missing.lookup.detail = "ciclo ou profundidade excessiva de forwarder";
        trace_event("module-cycle", module_name, missing.lookup.detail);
        return missing;
    }
    visits.push_back(LookupVisit{module_name, false, std::string{symbol}, 0});
    const auto finish = [&](GraphExportLookup value) {
        visits.pop_back();
        return value;
    };

    const auto try_guest = [&](const std::optional<std::size_t> index,
                               const ModuleProvider provider) -> std::optional<GraphExportLookup> {
        if (!index.has_value()) return std::nullopt;
        ExportLookup direct = direct_export(*modules_[*index], symbol);
        if (!direct.found) return std::nullopt;
        GraphExportLookup result{.lookup = std::move(direct), .provider = provider,
                                 .module_index = *index,
                                 .provider_name = provider_name(provider)};
        if (!result.lookup.forwarder.empty()) {
            const auto target = parse_forwarder(result.lookup.forwarder);
            if (!target.has_value()) {
                result.lookup = {};
                result.lookup.detail = "forwarder de export inválido";
                return result;
            }
            GraphExportLookup forwarded = target->by_ordinal
                                              ? resolve_export_ordinal_internal(
                                                    target->module, target->ordinal, requester,
                                                    owner_index, visits, depth + 1U)
                                              : resolve_export_named_internal(
                                                    target->module, target->symbol, requester,
                                                    owner_index, visits, depth + 1U);
            if (!forwarded.lookup.found) return forwarded;
            forwarded.provider = provider;
            forwarded.module_index = *index;
            forwarded.provider_name = provider_name(provider);
            return forwarded;
        }
        return result;
    };

    if (const auto profile = ensure_profile_module(module_name, owner_index)) {
        if (const auto result = try_guest(profile, ModuleProvider::Profile)) {
            return finish(*result);
        }
    }
    if (const auto drive = ensure_drive_module(module_name, requester, owner_index)) {
        if (const auto result = try_guest(drive, ModuleProvider::DriveC)) {
            return finish(*result);
        }
    }
    ExportLookup builtin = find_export_forwarded(ExportQuery{module_name, symbol});
    if (builtin.found) {
        return finish(GraphExportLookup{.lookup = std::move(builtin),
                                        .provider = ModuleProvider::Builtin,
                                        .module_index = kNoModule,
                                        .provider_name = "builtin"});
    }
    missing.lookup.detail = builtin.detail;
    return finish(missing);
}

GraphExportLookup GuestModuleGraph::resolve_export_ordinal_internal(
    const std::string_view raw_module, const std::uint16_t ordinal,
    const std::filesystem::path& requester, const std::size_t owner_index,
    std::vector<LookupVisit>& visits, const std::size_t depth) {
    const std::string module_name = normalize_module(raw_module);
    GraphExportLookup missing;
    if (module_name.empty()) {
        missing.lookup.detail = "nome de módulo inválido";
        return missing;
    }
    if (depth >= kMaxForwarderDepth || same_visit(visits, module_name, true, {}, ordinal)) {
        missing.lookup.detail = "ciclo ou profundidade excessiva de forwarder";
        trace_event("module-cycle", module_name, missing.lookup.detail);
        return missing;
    }
    visits.push_back(LookupVisit{module_name, true, {}, ordinal});
    const auto finish = [&](GraphExportLookup value) {
        visits.pop_back();
        return value;
    };

    const auto try_guest = [&](const std::optional<std::size_t> index,
                               const ModuleProvider provider) -> std::optional<GraphExportLookup> {
        if (!index.has_value()) return std::nullopt;
        ExportLookup direct = direct_export(*modules_[*index], ordinal);
        if (!direct.found) return std::nullopt;
        GraphExportLookup result{.lookup = std::move(direct), .provider = provider,
                                 .module_index = *index,
                                 .provider_name = provider_name(provider)};
        if (!result.lookup.forwarder.empty()) {
            const auto target = parse_forwarder(result.lookup.forwarder);
            if (!target.has_value()) {
                result.lookup = {};
                result.lookup.detail = "forwarder de export inválido";
                return result;
            }
            GraphExportLookup forwarded = target->by_ordinal
                                              ? resolve_export_ordinal_internal(
                                                    target->module, target->ordinal, requester,
                                                    owner_index, visits, depth + 1U)
                                              : resolve_export_named_internal(
                                                    target->module, target->symbol, requester,
                                                    owner_index, visits, depth + 1U);
            if (!forwarded.lookup.found) return forwarded;
            forwarded.provider = provider;
            forwarded.module_index = *index;
            forwarded.provider_name = provider_name(provider);
            return forwarded;
        }
        return result;
    };

    if (const auto profile = ensure_profile_module(module_name, owner_index)) {
        if (const auto result = try_guest(profile, ModuleProvider::Profile)) {
            return finish(*result);
        }
    }
    if (const auto drive = ensure_drive_module(module_name, requester, owner_index)) {
        if (const auto result = try_guest(drive, ModuleProvider::DriveC)) {
            return finish(*result);
        }
    }
    ExportLookup builtin = find_export_by_ordinal_forwarded(module_name, ordinal);
    if (builtin.found) {
        return finish(GraphExportLookup{.lookup = std::move(builtin),
                                        .provider = ModuleProvider::Builtin,
                                        .module_index = kNoModule,
                                        .provider_name = "builtin"});
    }
    missing.lookup.detail = builtin.detail;
    return finish(missing);
}

void GuestModuleGraph::release_dependency(const std::size_t index) noexcept {
    if (index >= modules_.size()) return;
    LoadedModule& module = *modules_[index];
    if (module.static_refs > 0) --module.static_refs;
    if (module.static_refs == 0 && module.dynamic_refs == 0 &&
        module.state == LoadedState::Attached) {
        detach_module(index);
    }
}

void GuestModuleGraph::reject_profile_module(const std::string_view module_name) noexcept {
    try {
        if (std::none_of(rejected_profile_modules_.begin(), rejected_profile_modules_.end(),
                         [&](const std::string& item) {
                             return util::ascii_iequals(item, module_name);
                         })) {
            rejected_profile_modules_.emplace_back(module_name);
        }
    } catch (...) {
    }
}

ResolveResult GuestModuleGraph::resolve_imports_for_module(
    MappedImage& image, const pe::PeInfo& info, const std::filesystem::path& requester,
    const std::size_t owner_index) {
    ResolveResult result;
    const auto fail_entry = [&](ResolvedImport& entry, const ImportStatus status,
                                std::string detail) {
        entry.status = status;
        entry.detail = std::move(detail);
        if (result.status == ImportStatus::Resolved) {
            const std::string symbol = entry.by_ordinal
                                           ? "ordinal(" + std::to_string(entry.ordinal) + ")"
                                           : entry.symbol;
            result.status = status;
            result.error_message = entry.dll + "!" + symbol + ": " + entry.detail;
        }
    };
    const auto resolve_group = [&](const std::vector<pe::ImportedDll>& dlls,
                                   const ImportMechanism mechanism) {
        for (const pe::ImportedDll& dll : dlls) {
            for (const pe::ImportedSymbol& symbol : dll.symbols) {
                ResolvedImport entry;
                entry.dll = dll.name;
                entry.mechanism = mechanism;
                entry.by_ordinal = symbol.by_ordinal;
                entry.symbol = symbol.by_ordinal ? std::string{} : symbol.name;
                entry.ordinal = symbol.by_ordinal ? symbol.ordinal : 0;
                entry.iat_rva = symbol.iat_rva;
                const GraphExportLookup lookup = symbol.by_ordinal
                                                      ? resolve_export(dll.name, symbol.ordinal,
                                                                       requester, owner_index)
                                                      : resolve_export(dll.name, symbol.name,
                                                                       requester, owner_index);
                if (!lookup.lookup.found) {
                    fail_entry(entry,
                               lookup.lookup.detail.empty()
                                   ? (lookup.provider_name.empty() ? ImportStatus::UnknownDll
                                                                   : (symbol.by_ordinal
                                                                          ? ImportStatus::UnknownOrdinal
                                                                          : ImportStatus::UnknownSymbol))
                                   : (symbol.by_ordinal ? ImportStatus::UnknownOrdinal
                                                         : ImportStatus::UnknownSymbol),
                               lookup.lookup.detail.empty()
                                   ? (lookup.provider_name.empty() ? "módulo não registrado"
                                                                   : "símbolo não exportado pelo módulo")
                                   : lookup.lookup.detail);
                    result.imports.push_back(std::move(entry));
                    continue;
                }
                entry.ordinal = lookup.lookup.ordinal;
                entry.address = lookup.lookup.address;
                entry.support = lookup.lookup.support;
                entry.provider = lookup.provider_name;
                if (entry.address == 0) {
                    fail_entry(entry, ImportStatus::NotImpl,
                               "símbolo conhecido sem implementação");
                } else if (!patch_import(image, entry)) {
                    fail_entry(entry, entry.status, entry.detail);
                } else {
                    if (lookup.module_index != kNoModule && lookup.module_index != owner_index) {
                        LoadedModule& dependency = *modules_[lookup.module_index];
                        if (owner_index == kNoModule) {
                            ++dependency.static_refs;
                        } else {
                            LoadedModule& owner = *modules_[owner_index];
                            if (std::find(owner.dependencies.begin(), owner.dependencies.end(),
                                          lookup.module_index) == owner.dependencies.end()) {
                                owner.dependencies.push_back(lookup.module_index);
                                ++dependency.static_refs;
                            }
                        }
                    }
                    trace_event("import-resolved", entry.dll, entry.provider);
                }
                result.imports.push_back(std::move(entry));
            }
        }
    };
    resolve_group(info.imports, ImportMechanism::Static);
    resolve_group(info.delay_imports, ImportMechanism::Delay);
    return result;
}

ResolveResult GuestModuleGraph::resolve_imports(MappedImage& image,
                                                const pe::PeInfo& info,
                                                const std::filesystem::path& requester) {
    main_image_ = &image;
    main_info_ = &info;
    main_path_ = requester;
    return resolve_imports_for_module(image, info, requester, kNoModule);
}

bool GuestModuleGraph::is_guest_executable(const std::uintptr_t address) const noexcept {
    if (address == 0) return false;
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) == 3 &&
            address >= start && address < end) {
            return permissions[2] == 'x';
        }
    }
    return false;
}

bool GuestModuleGraph::attach_module(const std::size_t index) noexcept {
    if (index >= modules_.size()) return false;
    LoadedModule& module = *modules_[index];
    if (module.state == LoadedState::Attached) return true;
    if (module.state == LoadedState::Attaching || module.state != LoadedState::Ready) {
        return false;
    }
    module.state = LoadedState::Attaching;
    for (const std::size_t dependency : module.dependencies) {
        if (!attach_module(dependency)) {
            module.state = LoadedState::Rejected;
            return false;
        }
    }
    using TlsCallback = TL_MSABI void (*)(void*, std::uint32_t, void*);
    using DllMain = TL_MSABI int (*)(void*, std::uint32_t, void*);
    for (const std::uint64_t callback : module.info.tls_info.callback_vas) {
        const std::uintptr_t address = static_cast<std::uintptr_t>(
            relocate_va(callback, module.info, module.image));
        if (!is_guest_executable(address)) {
            module.state = LoadedState::Rejected;
            return false;
        }
        trace_event("tls-callback", module.name, "process-attach");
        reinterpret_cast<TlsCallback>(address)(module.image.memory, 1U, nullptr);
    }
    if (module.info.address_of_entry_point != 0) {
        const std::uintptr_t address = static_cast<std::uintptr_t>(
            module.image.base + module.info.address_of_entry_point);
        if (!is_guest_executable(address) || reinterpret_cast<DllMain>(address)(
                                                  module.image.memory, 1U, nullptr) == 0) {
            trace_event("provider-rejected", module.name, "DllMain(PROCESS_ATTACH)");
            module.state = LoadedState::Rejected;
            return false;
        }
    }
    module.process_attached = true;
    module.state = LoadedState::Attached;
    trace_event("dll-attach", module.name, provider_name(module.provider));
    return true;
}

void GuestModuleGraph::detach_module(const std::size_t index) noexcept {
    if (index >= modules_.size()) return;
    LoadedModule& module = *modules_[index];
    if (module.state != LoadedState::Attached) return;
    using TlsCallback = TL_MSABI void (*)(void*, std::uint32_t, void*);
    using DllMain = TL_MSABI int (*)(void*, std::uint32_t, void*);
    if (module.info.address_of_entry_point != 0) {
        const std::uintptr_t address = static_cast<std::uintptr_t>(
            module.image.base + module.info.address_of_entry_point);
        if (is_guest_executable(address)) {
            reinterpret_cast<DllMain>(address)(module.image.memory, 0U, nullptr);
        }
    }
    for (auto callback = module.info.tls_info.callback_vas.rbegin();
         callback != module.info.tls_info.callback_vas.rend(); ++callback) {
        const std::uintptr_t address = static_cast<std::uintptr_t>(
            relocate_va(*callback, module.info, module.image));
        if (is_guest_executable(address)) {
            trace_event("tls-callback", module.name, "process-detach");
            reinterpret_cast<TlsCallback>(address)(module.image.memory, 0U, nullptr);
        }
    }
    module.process_attached = false;
    module.state = LoadedState::Unloaded;
    trace_event("dll-detach", module.name, provider_name(module.provider));
    unmap_image(module.image);
    runtime::invalidate_memory_map_cache();
    for (const std::size_t dependency : module.dependencies) release_dependency(dependency);
}

bool GuestModuleGraph::process_attach() noexcept {
    try {
        for (;;) {
            bool retry_with_fallback = false;
            for (std::size_t index = 0; index < modules_.size(); ++index) {
                LoadedModule& module = *modules_[index];
                if (module.state == LoadedState::Rejected ||
                    module.state == LoadedState::Unloaded ||
                    (module.static_refs == 0 && module.dynamic_refs == 0)) {
                    continue;
                }
                if (!attach_module(index)) {
                    if (module.provider != ModuleProvider::Profile) return false;
                    // Um provider de perfil que falha no attach não pode
                    // continuar parcialmente no processo. Todas as DLLs do
                    // perfil carregadas nesta tentativa são descartadas e a
                    // resolução do executável recomeça usando drive_c/built-in.
                    for (const auto& candidate : modules_) {
                        if (candidate->provider == ModuleProvider::Profile) {
                            reject_profile_module(candidate->name);
                        }
                    }
                    MappedImage* const main_image = main_image_;
                    const pe::PeInfo* const main_info = main_info_;
                    const std::filesystem::path main_path = main_path_;
                    discard_loaded_modules();
                    if (main_image == nullptr || main_info == nullptr) return false;
                    main_image_ = main_image;
                    main_info_ = main_info;
                    main_path_ = main_path;
                    const ResolveResult fallback = resolve_imports_for_module(
                        *main_image_, *main_info_, main_path_, kNoModule);
                    if (fallback.status != ImportStatus::Resolved) return false;
                    retry_with_fallback = true;
                    break;
                }
            }
            if (!retry_with_fallback) return true;
        }
    } catch (...) {
        return false;
    }
}

void GuestModuleGraph::process_detach() noexcept {
    // O proprietário é inserido no grafo antes de suas dependências. Ao
    // desmontá-lo primeiro, release_dependency pode desmontar a dependência
    // somente depois do DllMain(DLL_PROCESS_DETACH) do proprietário.
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        detach_module(index);
    }
}

void GuestModuleGraph::thread_attach() noexcept {
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        LoadedModule& module = *modules_[index];
        if (!module.process_attached) continue;
        using TlsCallback = TL_MSABI void (*)(void*, std::uint32_t, void*);
        for (const std::uint64_t callback : module.info.tls_info.callback_vas) {
            const std::uintptr_t address = static_cast<std::uintptr_t>(
                relocate_va(callback, module.info, module.image));
            if (is_guest_executable(address)) {
                trace_event("tls-callback", module.name, "thread-attach");
                reinterpret_cast<TlsCallback>(address)(module.image.memory, 2U, nullptr);
            }
        }
    }
}

void GuestModuleGraph::thread_detach() noexcept {
    for (std::size_t index = modules_.size(); index > 0; --index) {
        LoadedModule& module = *modules_[index - 1U];
        if (!module.process_attached) continue;
        using TlsCallback = TL_MSABI void (*)(void*, std::uint32_t, void*);
        for (auto callback = module.info.tls_info.callback_vas.rbegin();
             callback != module.info.tls_info.callback_vas.rend(); ++callback) {
            const std::uintptr_t address = static_cast<std::uintptr_t>(
                relocate_va(*callback, module.info, module.image));
            if (is_guest_executable(address)) {
                trace_event("tls-callback", module.name, "thread-detach");
                reinterpret_cast<TlsCallback>(address)(module.image.memory, 3U, nullptr);
            }
        }
    }
}

void* GuestModuleGraph::load_library(const std::string_view raw_module) noexcept {
    try {
        const std::string module_name = normalize_module(raw_module);
        if (module_name.empty()) return nullptr;
        if (const auto profile = ensure_profile_module(module_name, kNoModule)) {
            LoadedModule& module = *modules_[*profile];
            ++module.dynamic_refs;
            if (!attach_module(*profile)) {
                --module.dynamic_refs;
                if (module.provider == ModuleProvider::Profile) reject_profile_module(module.name);
                return load_library(module_name);
            }
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(module.image.base));
        }
        if (const auto drive = ensure_drive_module(module_name, main_path_, kNoModule)) {
            LoadedModule& module = *modules_[*drive];
            ++module.dynamic_refs;
            if (!attach_module(*drive)) {
                --module.dynamic_refs;
                return nullptr;
            }
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(module.image.base));
        }
        if (!is_module_registered_forwarded(module_name)) return nullptr;
        const auto builtin = std::find_if(builtin_modules_.begin(), builtin_modules_.end(),
                                          [&](const std::string& item) {
                                              return util::ascii_iequals(item, module_name);
                                          });
        if (builtin != builtin_modules_.end()) {
            const std::size_t index = static_cast<std::size_t>(builtin - builtin_modules_.begin());
            ++builtin_refcounts_[index];
            return reinterpret_cast<void*>(builtin_handles_[index]);
        }
        builtin_modules_.push_back(module_name);
        builtin_handles_.push_back(kBuiltinHandleBase + builtin_handles_.size() * 0x1000U);
        builtin_refcounts_.push_back(1);
        return reinterpret_cast<void*>(builtin_handles_.back());
    } catch (...) {
        return nullptr;
    }
}

void* GuestModuleGraph::get_module_handle(const std::string_view raw_module) noexcept {
    try {
        const std::string module_name = normalize_module(raw_module);
        if (module_name.empty()) return nullptr;
        for (const auto& module : modules_) {
            if (module->state != LoadedState::Rejected && module->state != LoadedState::Unloaded &&
                util::ascii_iequals(module->name, module_name) && module->image.base != 0) {
                return reinterpret_cast<void*>(static_cast<std::uintptr_t>(module->image.base));
            }
        }
        if (!is_module_registered_forwarded(module_name)) return nullptr;
        const auto found = std::find_if(builtin_modules_.begin(), builtin_modules_.end(),
                                        [&](const std::string& item) {
                                            return util::ascii_iequals(item, module_name);
                                        });
        if (found != builtin_modules_.end()) {
            return reinterpret_cast<void*>(builtin_handles_[static_cast<std::size_t>(
                found - builtin_modules_.begin())]);
        }
        builtin_modules_.push_back(module_name);
        builtin_handles_.push_back(kBuiltinHandleBase + builtin_handles_.size() * 0x1000U);
        builtin_refcounts_.push_back(0);
        return reinterpret_cast<void*>(builtin_handles_.back());
    } catch (...) {
        return nullptr;
    }
}

void* GuestModuleGraph::module_handle_from_address(const std::uintptr_t address) const noexcept {
    for (const auto& module : modules_) {
        if (module->state == LoadedState::Rejected || module->state == LoadedState::Unloaded ||
            module->image.memory == nullptr || address < module->image.base ||
            address - module->image.base >= module->image.size) {
            continue;
        }
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(module->image.base));
    }
    return nullptr;
}

bool GuestModuleGraph::free_library(void* const module_handle) noexcept {
    try {
        if (const auto index = find_module_for_handle(module_handle)) {
            LoadedModule& module = *modules_[*index];
            if (module.dynamic_refs == 0) return false;
            --module.dynamic_refs;
            trace_event("module-refcount", module.name,
                        std::to_string(module.static_refs + module.dynamic_refs));
            if (module.dynamic_refs == 0 && module.static_refs == 0) detach_module(*index);
            return true;
        }
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module_handle);
        const auto builtin = std::find(builtin_handles_.begin(), builtin_handles_.end(), value);
        if (builtin == builtin_handles_.end()) return false;
        const std::size_t index = static_cast<std::size_t>(builtin - builtin_handles_.begin());
        if (builtin_refcounts_[index] == 0) return false;
        --builtin_refcounts_[index];
        return true;
    } catch (...) {
        return false;
    }
}

GraphExportLookup GuestModuleGraph::get_proc_address(void* const module_handle,
                                                     const std::string_view symbol) noexcept {
    try {
        if (module_handle == nullptr) {
            const ExportLookup builtin = find_export_global(symbol);
            return {.lookup = builtin, .provider = ModuleProvider::Builtin,
                    .module_index = kNoModule, .provider_name = "builtin"};
        }
        if (main_image_ != nullptr && main_image_->memory != nullptr && main_info_ != nullptr &&
            reinterpret_cast<std::uintptr_t>(module_handle) == main_image_->base) {
            ExportLookup main_export = direct_export(*main_info_, *main_image_, symbol);
            if (!main_export.found) return {};
            if (!main_export.forwarder.empty()) {
                const auto target = parse_forwarder(main_export.forwarder);
                if (!target.has_value()) return {};
                return target->by_ordinal
                           ? resolve_export(target->module, target->ordinal, main_path_, kNoModule)
                           : resolve_export(target->module, target->symbol, main_path_, kNoModule);
            }
            return {.lookup = std::move(main_export), .provider = ModuleProvider::Builtin,
                    .module_index = kNoModule, .provider_name = "main"};
        }
        if (const auto index = find_module_for_handle(module_handle)) {
            return resolve_export(modules_[*index]->name, symbol, modules_[*index]->source,
                                  kNoModule);
        }
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module_handle);
        const auto builtin = std::find(builtin_handles_.begin(), builtin_handles_.end(), value);
        if (builtin == builtin_handles_.end()) return {};
        return resolve_export(builtin_modules_[static_cast<std::size_t>(builtin - builtin_handles_.begin())],
                              symbol, main_path_, kNoModule);
    } catch (...) {
        return {};
    }
}

GraphExportLookup GuestModuleGraph::get_proc_address(void* const module_handle,
                                                     const std::uint16_t ordinal) noexcept {
    try {
        if (module_handle == nullptr) {
            const ExportLookup builtin = find_export_by_ordinal_global(ordinal);
            return {.lookup = builtin, .provider = ModuleProvider::Builtin,
                    .module_index = kNoModule, .provider_name = "builtin"};
        }
        if (main_image_ != nullptr && main_image_->memory != nullptr && main_info_ != nullptr &&
            reinterpret_cast<std::uintptr_t>(module_handle) == main_image_->base) {
            ExportLookup main_export = direct_export(*main_info_, *main_image_, ordinal);
            if (!main_export.found) return {};
            if (!main_export.forwarder.empty()) {
                const auto target = parse_forwarder(main_export.forwarder);
                if (!target.has_value()) return {};
                return target->by_ordinal
                           ? resolve_export(target->module, target->ordinal, main_path_, kNoModule)
                           : resolve_export(target->module, target->symbol, main_path_, kNoModule);
            }
            return {.lookup = std::move(main_export), .provider = ModuleProvider::Builtin,
                    .module_index = kNoModule, .provider_name = "main"};
        }
        if (const auto index = find_module_for_handle(module_handle)) {
            return resolve_export(modules_[*index]->name, ordinal, modules_[*index]->source,
                                  kNoModule);
        }
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module_handle);
        const auto builtin = std::find(builtin_handles_.begin(), builtin_handles_.end(), value);
        if (builtin == builtin_handles_.end()) return {};
        return resolve_export(builtin_modules_[static_cast<std::size_t>(builtin - builtin_handles_.begin())],
                              ordinal, main_path_, kNoModule);
    } catch (...) {
        return {};
    }
}

bool GuestModuleGraph::is_valid_module_handle(void* const module_handle) const noexcept {
    if (find_module_for_handle(module_handle).has_value()) return true;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module_handle);
    return std::find(builtin_handles_.begin(), builtin_handles_.end(), value) != builtin_handles_.end();
}

void GuestModuleGraph::discard_loaded_modules() noexcept {
    process_detach();
    for (const auto& module : modules_) {
        if (module->image.memory != nullptr) {
            unmap_image(module->image);
            runtime::invalidate_memory_map_cache();
        }
        module->state = LoadedState::Unloaded;
    }
    modules_.clear();
    builtin_modules_.clear();
    builtin_handles_.clear();
    builtin_refcounts_.clear();
}

void GuestModuleGraph::reset() noexcept {
    discard_loaded_modules();
    main_image_ = nullptr;
    main_info_ = nullptr;
}

}  // namespace tradutorlinux::loader
