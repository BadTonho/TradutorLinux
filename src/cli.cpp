#include "tradutorlinux/cli.hpp"

#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/diagnostics/crash_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/package/msix.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/process/isolate.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

constexpr std::string_view kUsage =
    "Uso:\n"
    "  tradutorlinux [--trace[=canais]] [--report] [--timeout <segundos>] <arquivo.exe> [argumentos...]\n"
    "  tradutorlinux install <setup.exe> [--name <Nome>] [--prefix <dir>] [--app-exe <caminho>]\n"
    "  tradutorlinux app list\n"
    "  tradutorlinux app run <id_ou_nome> [argumentos...]\n"
    "  tradutorlinux app add <arquivo.exe> [--name <Nome>] [--prefix <dir>] [--id <id>]\n"
    "  tradutorlinux app remove <id>\n";

[[nodiscard]] bool is_option(const std::string_view argument) {
    return argument.starts_with('-');
}

// Um PE legítimo tem poucos megabytes; um arquivo muito maior é tratado como
// entrada hostil e rejeitado antes do parse.
constexpr std::uint64_t kMaxPeFileSize = 512ULL * 1024 * 1024;

[[nodiscard]] std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    if (end < 0) {
        return std::nullopt;
    }
    if (static_cast<std::uint64_t>(end) > kMaxPeFileSize) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            return std::nullopt;
        }
    }
    return bytes;
}

struct FileSignature {
    std::uintmax_t size{};
    std::filesystem::file_time_type modified{};
};

using ExecutableSnapshot = std::map<std::filesystem::path, FileSignature>;

[[nodiscard]] std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool is_x64_pe_file(const std::filesystem::path& path) {
    const std::optional<std::vector<std::byte>> bytes = read_file(path);
    if (!bytes.has_value()) {
        return false;
    }
    const pe::ParseResult parsed = pe::parse_pe(*bytes);
    return parsed.status == pe::ParseStatus::Success && parsed.info.is_pe32_plus &&
           parsed.info.machine == 0x8664;
}

[[nodiscard]] ExecutableSnapshot snapshot_executables(const std::filesystem::path& drive_c) {
    ExecutableSnapshot result;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(
        drive_c, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (!entry.is_regular_file(ec) || ec || lowercase(entry.path().extension().string()) != ".exe") {
            ec.clear();
            continue;
        }
        const std::optional<std::vector<std::byte>> bytes = read_file(entry.path());
        if (!bytes.has_value()) {
            continue;
        }
        const pe::ParseResult parsed = pe::parse_pe(*bytes);
        if (parsed.status != pe::ParseStatus::Success || !parsed.info.is_pe32_plus ||
            parsed.info.machine != 0x8664) {
            continue;
        }
        const std::uintmax_t size = entry.file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::file_time_type modified = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        result.emplace(entry.path().lexically_normal(), FileSignature{size, modified});
    }
    return result;
}

[[nodiscard]] std::vector<std::filesystem::path> changed_executables(
    const ExecutableSnapshot& before, const ExecutableSnapshot& after) {
    std::vector<std::filesystem::path> candidates;
    for (const auto& [path, signature] : after) {
        const auto previous = before.find(path);
        if (previous == before.end() || previous->second.size != signature.size ||
            previous->second.modified != signature.modified) {
            candidates.push_back(path);
        }
    }
    return candidates;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_installed_executable(
    const std::filesystem::path& raw_path, const std::filesystem::path& prefix_root) {
    const std::string value = raw_path.string();
    const bool windows_absolute =
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
         value[1] == ':') ||
        (!value.empty() && value.front() == '\\');
    std::filesystem::path candidate = raw_path.is_absolute()
                                          ? raw_path
                                          : prefix::resolve_windows_path(value, prefix_root);
    if (windows_absolute) {
        candidate = prefix::resolve_windows_path(value, prefix_root);
    }
    std::error_code ec;
    candidate = std::filesystem::weakly_canonical(candidate, ec);
    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(prefix_root);
    if (ec || !prefix::is_path_within(candidate, paths.drive_c) ||
        !std::filesystem::is_regular_file(candidate, ec) || ec || !is_x64_pe_file(candidate)) {
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] std::string unique_app_id(const catalog::AppCatalog& app_catalog,
                                        const std::string_view requested_name) {
    const std::string base = catalog::AppCatalog::generate_id(requested_name);
    auto has_id = [&app_catalog](const std::string_view value) {
        return std::any_of(app_catalog.list_apps().begin(), app_catalog.list_apps().end(),
                           [value](const catalog::AppEntry& entry) { return entry.id == value; });
    };
    if (!has_id(base)) {
        return base;
    }
    for (std::size_t suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = base + "-" + std::to_string(suffix);
        if (!has_id(candidate)) {
            return candidate;
        }
    }
    return base + "-overflow";
}

void write_install_trace(const bool enabled, std::ostream& stream,
                         const diagnostics::TraceLevel level, const std::string_view event,
                         const std::initializer_list<diagnostics::TraceField> fields) {
    if (enabled) {
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Install, level, event,
                                 std::span<const diagnostics::TraceField>{fields.begin(), fields.size()});
    }
}

[[nodiscard]] std::string symbol_label(const pe::ImportedSymbol& symbol) {
    if (symbol.by_ordinal) {
        return "ordinal(" + std::to_string(symbol.ordinal) + ")";
    }
    return symbol.name;
}

[[nodiscard]] const char* status_label(const pe::ParseStatus status) {
    switch (status) {
        case pe::ParseStatus::Success:
            return "success";
        case pe::ParseStatus::Truncated:
            return "truncated";
        case pe::ParseStatus::Malformed:
            return "malformed";
        case pe::ParseStatus::UnsupportedArchitecture:
            return "unsupported-architecture";
        case pe::ParseStatus::UnsupportedFormat:
            return "unsupported-format";
        case pe::ParseStatus::UnsupportedMechanism:
            return "unsupported-mechanism";
    }
    return "unknown";
}

void write_pe_trace(std::ostream& stream, const pe::PeInfo& info) {
    const std::string format = info.is_pe32_plus ? "PE32+" : "PE32";
    const std::string arch = info.machine == 0x8664 ? "x86-64" : "desconhecida";
    const std::array image_fields{
        diagnostics::TraceField{"format", format},
        diagnostics::TraceField{"arch", arch},
        diagnostics::TraceField{"entry", util::format_hex(info.address_of_entry_point)},
        diagnostics::TraceField{"image-base", util::format_hex(info.image_base)},
        diagnostics::TraceField{"size-of-image", util::format_hex(info.size_of_image)},
        diagnostics::TraceField{"sections", std::to_string(info.number_of_sections)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                             diagnostics::TraceLevel::Info, "image", image_fields);

    for (std::size_t index = 0; index < info.sections.size(); ++index) {
        const pe::SectionInfo& section = info.sections[index];
        const std::array fields{
            diagnostics::TraceField{"index", std::to_string(index)},
            diagnostics::TraceField{"name", section.name},
            diagnostics::TraceField{"virtual-address", util::format_hex(section.virtual_address)},
            diagnostics::TraceField{"virtual-size", util::format_hex(section.virtual_size)},
            diagnostics::TraceField{"raw-pointer", util::format_hex(section.raw_data_pointer)},
            diagnostics::TraceField{"raw-size", util::format_hex(section.raw_data_size)},
            diagnostics::TraceField{"characteristics", util::format_hex(section.characteristics)},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                                 diagnostics::TraceLevel::Info, "section", fields);
    }

    for (const pe::ImportedDll& dll : info.imports) {
        std::string symbols;
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            if (!symbols.empty()) {
                symbols += ",";
            }
            symbols += symbol_label(symbol);
        }
        const std::array fields{
            diagnostics::TraceField{"dll", dll.name},
            diagnostics::TraceField{"symbols", symbols},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                                 diagnostics::TraceLevel::Info, "import", fields);
    }
    for (const pe::ImportedDll& dll : info.delay_imports) {
        std::string symbols;
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            if (!symbols.empty()) {
                symbols += ",";
            }
            symbols += symbol_label(symbol);
        }
        const std::array fields{
            diagnostics::TraceField{"dll", dll.name},
            diagnostics::TraceField{"symbols", symbols},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                                 diagnostics::TraceLevel::Info, "delay-import", fields);
    }

    std::size_t unwind_handlers = 0;
    std::size_t unwind_chained = 0;
    std::size_t unwind_v1 = 0;
    std::size_t unwind_v2 = 0;
    std::size_t unwind_epilogs = 0;
    std::size_t unwind_extended_set_fpreg = 0;
    for (const pe::RuntimeFunction& function : info.runtime_functions) {
        if (function.unwind.handler_rva != 0U) {
            ++unwind_handlers;
        }
        if (function.unwind.has_chained_function) {
            ++unwind_chained;
        }
        if (function.unwind.version == 1U) {
            ++unwind_v1;
        } else if (function.unwind.version == 2U) {
            ++unwind_v2;
        }
        unwind_epilogs += function.unwind.epilogs.size();
        if (function.unwind.has_extended_set_fpreg) {
            ++unwind_extended_set_fpreg;
        }
    }
    const std::array unwind_fields{
        diagnostics::TraceField{"functions", std::to_string(info.runtime_functions.size())},
        diagnostics::TraceField{"v1", std::to_string(unwind_v1)},
        diagnostics::TraceField{"v2", std::to_string(unwind_v2)},
        diagnostics::TraceField{"epilogs", std::to_string(unwind_epilogs)},
        diagnostics::TraceField{"extended-set-fpreg", std::to_string(unwind_extended_set_fpreg)},
        diagnostics::TraceField{"handlers", std::to_string(unwind_handlers)},
        diagnostics::TraceField{"chained", std::to_string(unwind_chained)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                             diagnostics::TraceLevel::Info, "unwind", unwind_fields);

    std::size_t relocation_entries = 0;
    for (const pe::BaseRelocBlock& block : info.relocations) {
        relocation_entries += block.entries.size();
        const std::array fields{
            diagnostics::TraceField{"page", util::format_hex(block.page_rva)},
            diagnostics::TraceField{"entries", std::to_string(block.entries.size())},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                                 diagnostics::TraceLevel::Debug, "reloc-block", fields);
    }
    const std::array relocation_fields{
        diagnostics::TraceField{"blocks", std::to_string(info.relocations.size())},
        diagnostics::TraceField{"entries", std::to_string(relocation_entries)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                             diagnostics::TraceLevel::Info, "relocations", relocation_fields);
}

void print_pe_summary(std::ostream& stream, const pe::PeInfo& info) {
    const std::string_view format = info.is_pe32_plus ? "PE32+" : "PE32";
    stream << format << " x86-64 | entry=" << util::format_hex(info.address_of_entry_point)
           << " | image-base=" << util::format_hex(info.image_base) << " | "
           << info.number_of_sections << " seções\n";
    for (const pe::SectionInfo& section : info.sections) {
        stream << "  seção " << section.name << " va=" << util::format_hex(section.virtual_address)
               << " vsize=" << util::format_hex(section.virtual_size)
               << " raw=" << util::format_hex(section.raw_data_pointer) << "/"
               << util::format_hex(section.raw_data_size) << " chars=" << util::format_hex(section.characteristics)
               << '\n';
    }
    for (const pe::ImportedDll& dll : info.imports) {
        stream << "  imports " << dll.name << ':';
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            stream << ' ' << symbol_label(symbol);
        }
        stream << '\n';
    }
    for (const pe::ImportedDll& dll : info.delay_imports) {
        stream << "  delay import " << dll.name << ":";
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            stream << ' ' << symbol_label(symbol);
        }
        stream << '\n';
    }
    stream << "  unwind: " << info.runtime_functions.size() << " funções\n";
    stream << "  relocations: " << info.relocations.size() << " blocos\n";
}

[[nodiscard]] std::string_view permissions_label(const loader::SectionPermissions permissions) {
    switch (permissions) {
        case loader::SectionPermissions::None:
            return "---";
        case loader::SectionPermissions::ReadOnly:
            return "r--";
        case loader::SectionPermissions::ReadWrite:
            return "rw-";
        case loader::SectionPermissions::ReadExecute:
            return "r-x";
        case loader::SectionPermissions::ReadWriteExecute:
            return "rwx";
    }
    return "---";
}

void write_map_trace(std::ostream& stream, const loader::MappedImage& image) {
    const std::string_view at_preferred = image.delta == 0 ? "sim" : "não";
    const std::array image_fields{
        diagnostics::TraceField{"preferred-base", util::format_hex(image.preferred_base)},
        diagnostics::TraceField{"base", util::format_hex(image.base)},
        diagnostics::TraceField{"delta", util::format_signed_hex(image.delta)},
        diagnostics::TraceField{"size", util::format_hex(image.size)},
        diagnostics::TraceField{"at-preferred", std::string{at_preferred}},
        diagnostics::TraceField{"relocations-applied", std::to_string(image.applied_relocations)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Info, "mapped", image_fields);

    for (const loader::MapRegion& region : image.regions) {
        const std::array fields{
            diagnostics::TraceField{"name", region.name},
            diagnostics::TraceField{"rva", util::format_hex(region.rva)},
            diagnostics::TraceField{"size", util::format_hex(region.size)},
            diagnostics::TraceField{"permissions",
                                    std::string{permissions_label(region.permissions)}},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                                 diagnostics::TraceLevel::Info, "region", fields);
    }

    if (image.delta != 0 && !image.has_relocation_directory) {
        const std::array fields{
            diagnostics::TraceField{"reason", "imagem sem diretório de relocations"},
            diagnostics::TraceField{"delta", util::format_signed_hex(image.delta)},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                                 diagnostics::TraceLevel::Warning, "cannot-relocate", fields);
    }
}

void write_unmap_trace(std::ostream& stream, const std::uint64_t base) {
    const std::array fields{
        diagnostics::TraceField{"base", util::format_hex(base)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Info, "unmap", fields);
}

void write_map_failed_trace(std::ostream& stream, const std::string_view status,
                            const std::string& detail) {
    const std::array fields{
        diagnostics::TraceField{"status", std::string{status}},
        diagnostics::TraceField{"detail", detail},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Error, "map-failed", fields);
}

void print_map_summary(std::ostream& stream, const loader::MappedImage& image) {
    stream << "  mapeado base=" << util::format_hex(image.base)
           << " preferred=" << util::format_hex(image.preferred_base)
           << " delta=" << util::format_signed_hex(image.delta);
    if (image.delta != 0) {
        stream << " (realocado, " << image.applied_relocations << " relocations aplicados)";
    }
    stream << '\n';
    for (const loader::MapRegion& region : image.regions) {
        stream << "    região " << region.name << " rva=" << util::format_hex(region.rva)
               << " perms=" << permissions_label(region.permissions) << '\n';
    }
}

[[nodiscard]] const char* import_status_label(const loader::ImportStatus status) {
    switch (status) {
        case loader::ImportStatus::Resolved:
            return "resolved";
        case loader::ImportStatus::UnknownDll:
            return "unknown-dll";
        case loader::ImportStatus::UnknownSymbol:
            return "unknown-symbol";
        case loader::ImportStatus::UnknownOrdinal:
            return "unknown-ordinal";
        case loader::ImportStatus::NotImpl:
            return "not-implemented";
        case loader::ImportStatus::UnsupportedMechanism:
            return "unsupported-mechanism";
    }
    return "unknown";
}

[[nodiscard]] std::string resolved_symbol_label(const loader::ResolvedImport& entry) {
    if (entry.by_ordinal) {
        return "ordinal(" + std::to_string(entry.ordinal) + ")";
    }
    return entry.symbol;
}

[[nodiscard]] const char* import_mechanism_label(const loader::ImportMechanism mechanism) {
    return mechanism == loader::ImportMechanism::Delay ? "delay-import" : "import";
}

void write_imports_trace(std::ostream& stream, const loader::ResolveResult& imports) {
    for (const loader::ResolvedImport& entry : imports.imports) {
        if (entry.status == loader::ImportStatus::Resolved) {
            const std::array fields{
                diagnostics::TraceField{"dll", entry.dll},
                diagnostics::TraceField{"symbol", resolved_symbol_label(entry)},
                diagnostics::TraceField{"address", util::format_hex(entry.address)},
                diagnostics::TraceField{"mechanism", import_mechanism_label(entry.mechanism)},
            };
            diagnostics::write_trace(stream, diagnostics::TraceComponent::Imports,
                                     diagnostics::TraceLevel::Info, "resolved", fields);
        } else {
            const std::array fields{
                diagnostics::TraceField{"dll", entry.dll},
                diagnostics::TraceField{"symbol", resolved_symbol_label(entry)},
                diagnostics::TraceField{"status", import_status_label(entry.status)},
                diagnostics::TraceField{"detail", entry.detail},
                diagnostics::TraceField{"mechanism", import_mechanism_label(entry.mechanism)},
            };
            diagnostics::write_trace(stream, diagnostics::TraceComponent::Imports,
                                     diagnostics::TraceLevel::Error, "unresolved", fields);
        }
    }
}

void print_imports_summary(std::ostream& stream, const loader::ResolveResult& imports) {
    const std::size_t resolved = static_cast<std::size_t>(std::count_if(
        imports.imports.begin(), imports.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.status == loader::ImportStatus::Resolved;
        }));
    stream << "  imports resolvidos: " << resolved << "/" << imports.imports.size() << '\n';
    for (const loader::ResolvedImport& entry : imports.imports) {
        stream << "    " << entry.dll << '!' << resolved_symbol_label(entry);
        if (entry.mechanism == loader::ImportMechanism::Delay) {
            stream << " [delay-import]";
        }
        if (entry.status == loader::ImportStatus::Resolved) {
            stream << " -> " << util::format_hex(entry.address) << '\n';
        } else {
            stream << " [" << import_status_label(entry.status) << "] " << entry.detail << '\n';
        }
    }
}

void print_support_report_group(std::ostream& stream, const loader::ResolveResult& result,
                                const loader::ImportMechanism mechanism) {
    std::vector<std::string> dll_names;
    for (const loader::ResolvedImport& entry : result.imports) {
        if (entry.mechanism != mechanism ||
            std::find(dll_names.begin(), dll_names.end(), entry.dll) != dll_names.end()) {
            continue;
        }
        dll_names.push_back(entry.dll);
    }
    for (const std::string& dll_name : dll_names) {
        std::size_t total = 0;
        std::size_t resolved = 0;
        for (const loader::ResolvedImport& entry : result.imports) {
            if (entry.mechanism != mechanism || entry.dll != dll_name) {
                continue;
            }
            ++total;
            if (entry.status == loader::ImportStatus::Resolved) {
                ++resolved;
            }
        }
        stream << (mechanism == loader::ImportMechanism::Delay ? "delay-import dll: " : "dll: ")
               << dll_name << " (" << resolved << '/' << total << " resolved)\n";
        for (const loader::ResolvedImport& entry : result.imports) {
            if (entry.mechanism != mechanism || entry.dll != dll_name) {
                continue;
            }
            stream << "  import: " << resolved_symbol_label(entry)
                   << " status=" << import_status_label(entry.status) << '\n';
        }
    }
}

[[nodiscard]] loader::ResolveResult print_support_report(std::ostream& stream,
                                                          const pe::PeInfo& info) {
    const loader::ResolveResult result = loader::inspect_imports(info);
    const std::size_t total_imports = result.imports.size();
    const std::size_t resolved_imports = static_cast<std::size_t>(std::count_if(
        result.imports.begin(), result.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.status == loader::ImportStatus::Resolved;
        }));
    const std::size_t delay_imports = static_cast<std::size_t>(std::count_if(
        result.imports.begin(), result.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.mechanism == loader::ImportMechanism::Delay;
        }));
    const std::size_t resolved_delay_imports = static_cast<std::size_t>(std::count_if(
        result.imports.begin(), result.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.mechanism == loader::ImportMechanism::Delay &&
                   entry.status == loader::ImportStatus::Resolved;
        }));

    stream << "TradutorLinux compatibility report\n";
    stream << "format: " << (info.is_pe32_plus ? "PE32+ x86-64" : "unsupported") << '\n';
    stream << "entry-point: " << util::format_hex(info.address_of_entry_point) << '\n';
    if (!info.runtime_functions.empty()) {
        const std::size_t handler_count = static_cast<std::size_t>(std::count_if(
            info.runtime_functions.begin(), info.runtime_functions.end(),
            [](const pe::RuntimeFunction& function) { return function.unwind.handler_rva != 0U; }));
        const std::size_t chained_count = static_cast<std::size_t>(std::count_if(
            info.runtime_functions.begin(), info.runtime_functions.end(),
            [](const pe::RuntimeFunction& function) {
                return function.unwind.has_chained_function;
            }));
        const std::size_t v1_count = static_cast<std::size_t>(std::count_if(
            info.runtime_functions.begin(), info.runtime_functions.end(),
            [](const pe::RuntimeFunction& function) { return function.unwind.version == 1U; }));
        const std::size_t v2_count = static_cast<std::size_t>(std::count_if(
            info.runtime_functions.begin(), info.runtime_functions.end(),
            [](const pe::RuntimeFunction& function) { return function.unwind.version == 2U; }));
        std::size_t epilog_count = 0;
        std::size_t extended_set_fpreg_count = 0;
        for (const pe::RuntimeFunction& function : info.runtime_functions) {
            epilog_count += function.unwind.epilogs.size();
            if (function.unwind.has_extended_set_fpreg) {
                ++extended_set_fpreg_count;
            }
        }
        stream << "mechanism: x64-unwind (" << info.runtime_functions.size()
               << " functions, v1=" << v1_count << ", v2=" << v2_count
               << ", " << epilog_count << " epilogs, " << extended_set_fpreg_count
               << " extended-set-fpreg, " << handler_count << " handlers, "
               << chained_count << " chained)\n";
    }
    if (delay_imports != 0) {
        stream << "mechanism: delay-import (" << resolved_delay_imports << '/'
               << delay_imports << " resolved)\n";
    }
    print_support_report_group(stream, result, loader::ImportMechanism::Static);
    print_support_report_group(stream, result, loader::ImportMechanism::Delay);

    const std::size_t pct = total_imports > 0 ? (resolved_imports * 100 / total_imports) : 100;
    stream << "result: "
           << (result.status == loader::ImportStatus::Resolved ? "supported" : "unsupported")
           << '\n';
    stream << "compatibility: " << pct << "% (" << resolved_imports << '/'
           << total_imports << " imports resolved)\n";
    stream << "execution: not-attempted\n";
    stream << "execution-result: not-attempted\n";
    return result;
}

}  // namespace

ParseResult parse_command_line(const int argc, const char* const argv[]) {
    CommandLine command_line;
    if (argc <= 1) {
        return {.command_line = std::move(command_line), .error_message = {}};
    }

    const std::string_view first_arg{argv[1]};

    // Subcomando: install <setup.exe>
    if (first_arg == "install") {
        command_line.mode = CommandMode::Install;
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg == "--name") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --name requer um valor"};
                }
                command_line.app_name = argv[++i];
            } else if (arg == "--prefix") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --prefix requer um diretório"};
                }
                command_line.custom_prefix = std::filesystem::path{argv[++i]};
            } else if (arg == "--app-exe") {
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --app-exe requer um caminho"};
                }
                command_line.installed_executable_path = std::filesystem::path{argv[++i]};
            } else if (arg == "--timeout") {
                if (command_line.timeout_set) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --timeout foi repetida"};
                }
                if (i + 1 >= argc) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --timeout requer um valor em segundos"};
                }
                const std::string_view value{argv[++i]};
                if (value.empty() || value.size() > 9) {
                    return {.command_line = std::nullopt,
                            .error_message = "valor inválido para --timeout: " + std::string{value}};
                }
                std::uint64_t seconds = 0;
                for (const char digit : value) {
                    if (digit < '0' || digit > '9') {
                        return {.command_line = std::nullopt,
                                .error_message = "valor inválido para --timeout: " + std::string{value}};
                    }
                    seconds = seconds * 10 + static_cast<std::uint64_t>(digit - '0');
                }
                if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000U) {
                    return {.command_line = std::nullopt,
                            .error_message = "valor de --timeout muito grande: " + std::string{value}};
                }
                command_line.timeout_ms = seconds * 1000U;
                command_line.timeout_set = true;
            } else if (arg == "--trace" || arg.starts_with("--trace=")) {
                if (command_line.trace_enabled) {
                    return {.command_line = std::nullopt,
                            .error_message = "a opção --trace foi repetida"};
                }
                command_line.trace_enabled = true;
                if (arg.size() > 7) {
                    const std::string_view list = arg.substr(8);
                    if (!list.empty()) {
                        if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                            return {.command_line = std::nullopt,
                                    .error_message = "valor inválido para --trace: " + std::string(list)};
                        }
                        std::string_view rem = list;
                        while (!rem.empty()) {
                            const std::size_t comma = rem.find(',');
                            const std::string_view tok = comma == std::string_view::npos ? rem : rem.substr(0, comma);
                            if (tok.empty()) {
                                return {.command_line = std::nullopt,
                                        .error_message = "valor inválido para --trace: " + std::string(list)};
                            }
                            diagnostics::TraceComponent dummy;
                            if (!diagnostics::trace_component_from_name(tok, dummy)) {
                                return {.command_line = std::nullopt,
                                        .error_message = "canal de trace desconhecido: " + std::string(tok)};
                            }
                            command_line.trace_channels_raw.emplace_back(std::string{tok});
                            if (comma == std::string_view::npos) break;
                            rem = rem.substr(comma + 1);
                        }
                    }
                }
            } else if (arg == "--report") {
                command_line.report_only = true;
            } else if (!command_line.executable_path.has_value() && !arg.starts_with('-')) {
                command_line.executable_path = std::filesystem::path{std::string{arg}};
            } else {
                return {.command_line = std::nullopt,
                        .error_message = "opção desconhecida para 'install': " + std::string{arg}};
            }
        }
        if (!command_line.executable_path.has_value()) {
            return {.command_line = std::nullopt,
                    .error_message = "o comando 'install' requer o caminho do arquivo instalador (.exe)"};
        }
        return {.command_line = std::move(command_line), .error_message = {}};
    }

    // Subcomando: app <list|run|add|remove>
    if (first_arg == "app") {
        if (argc < 3) {
            return {.command_line = std::nullopt,
                    .error_message = "o comando 'app' requer uma ação: list, run, add ou remove"};
        }
        const std::string_view action{argv[2]};
        if (action == "list") {
            command_line.mode = CommandMode::AppList;
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "run") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app run' requer o ID ou nome do aplicativo"};
            }
            command_line.mode = CommandMode::AppRun;
            command_line.app_id = argv[3];
            for (int i = 4; i < argc; ++i) {
                const std::string_view arg{argv[i]};
                if (arg == "--trace" || arg.starts_with("--trace=")) {
                    if (command_line.trace_enabled) {
                        return {.command_line = std::nullopt,
                                .error_message = "a opção --trace foi repetida"};
                    }
                    command_line.trace_enabled = true;
                    if (arg.size() > 7) {
                        const std::string_view list = arg.substr(8);
                        if (!list.empty()) {
                            if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                                return {.command_line = std::nullopt,
                                        .error_message = "valor inválido para --trace: " + std::string(list)};
                            }
                            std::string_view rem = list;
                            while (!rem.empty()) {
                                const std::size_t comma = rem.find(',');
                                const std::string_view tok = comma == std::string_view::npos ? rem : rem.substr(0, comma);
                                if (tok.empty()) {
                                    return {.command_line = std::nullopt,
                                            .error_message = "valor inválido para --trace: " + std::string(list)};
                                }
                                diagnostics::TraceComponent dummy;
                                if (!diagnostics::trace_component_from_name(tok, dummy)) {
                                    return {.command_line = std::nullopt,
                                            .error_message = "canal de trace desconhecido: " + std::string(tok)};
                                }
                                command_line.trace_channels_raw.emplace_back(std::string{tok});
                                if (comma == std::string_view::npos) break;
                                rem = rem.substr(comma + 1);
                            }
                        }
                    }
                } else if (arg == "--report") {
                    command_line.report_only = true;
                } else {
                    command_line.guest_arguments.emplace_back(arg);
                }
            }
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "add") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app add' requer o caminho do executável"};
            }
            command_line.mode = CommandMode::AppAdd;
            command_line.executable_path = std::filesystem::path{argv[3]};
            for (int i = 4; i < argc; ++i) {
                const std::string_view arg{argv[i]};
                if (arg == "--name" && i + 1 < argc) {
                    command_line.app_name = argv[++i];
                } else if (arg == "--prefix" && i + 1 < argc) {
                    command_line.custom_prefix = std::filesystem::path{argv[++i]};
                } else if (arg == "--id" && i + 1 < argc) {
                    command_line.app_id = argv[++i];
                }
            }
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        if (action == "remove") {
            if (argc < 4) {
                return {.command_line = std::nullopt,
                        .error_message = "o comando 'app remove' requer o ID do aplicativo"};
            }
            command_line.mode = CommandMode::AppRemove;
            command_line.app_id = argv[3];
            return {.command_line = std::move(command_line), .error_message = {}};
        }
        return {.command_line = std::nullopt,
                .error_message = "ação desconhecida para 'app': " + std::string{action}};
    }

    bool options_ended = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (command_line.executable_path.has_value()) {
            // Tudo depois do executável pertence ao convidado, inclusive
            // argumentos que começam com '-'.
            command_line.guest_arguments.emplace_back(argument);
            continue;
        }

        if (!options_ended && argument == "--") {
            options_ended = true;
            continue;
        }

        if (!options_ended && argument == "--help") {
            if (command_line.show_help) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --help foi repetida"};
            }
            command_line.show_help = true;
            continue;
        }

        if (!options_ended && argument == "--version") {
            if (command_line.show_version) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --version foi repetida"};
            }
            command_line.show_version = true;
            continue;
        }

        if (!options_ended && (argument == "--trace" || argument.starts_with("--trace="))) {
            if (command_line.trace_enabled) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace foi repetida"};
            }
            command_line.trace_enabled = true;
            if (argument.size() > 7) { // "--trace=" prefix length 8
                const std::string_view list = argument.substr(8);
                if (!list.empty()) {
                    if (list.front() == ',' || list.back() == ',' || list.find(",,") != std::string_view::npos) {
                        return {.command_line = std::nullopt,
                                .error_message = "valor inválido para --trace: " + std::string(list)};
                    }
                    std::string_view remaining = list;
                    while (!remaining.empty()) {
                        const std::size_t comma = remaining.find(',');
                        const std::string_view token = comma == std::string_view::npos
                                                           ? remaining
                                                           : remaining.substr(0, comma);
                        if (token.empty()) {
                            return {.command_line = std::nullopt,
                                    .error_message = "valor inválido para --trace: " + std::string(list)};
                        }
                        // Valida canal imediatamente (case-insensitive, como Wine)
                        diagnostics::TraceComponent dummy;
                        if (!diagnostics::trace_component_from_name(token, dummy)) {
                            return {.command_line = std::nullopt,
                                    .error_message = "canal de trace desconhecido: " + std::string(token)};
                        }
                        command_line.trace_channels_raw.emplace_back(std::string{token});
                        if (comma == std::string_view::npos) break;
                        remaining = remaining.substr(comma + 1);
                    }
                }
            }
            continue;
        }

        if (!options_ended && argument == "--report") {
            if (command_line.report_only) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --report foi repetida"};
            }
            command_line.report_only = true;
            continue;
        }

        if (!options_ended && argument == "--timeout") {
            if (command_line.timeout_set) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --timeout foi repetida"};
            }
            if (index + 1 >= argc) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --timeout requer um valor em segundos"};
            }
            const std::string_view value{argv[index + 1]};
            if (value.empty() || value.size() > 9) {
                return {.command_line = std::nullopt,
                        .error_message = "valor inválido para --timeout: " + std::string{value}};
            }
            std::uint64_t seconds = 0;
            for (const char digit : value) {
                if (digit < '0' || digit > '9') {
                    return {.command_line = std::nullopt,
                            .error_message = "valor inválido para --timeout: " + std::string{value}};
                }
                seconds = seconds * 10 + static_cast<std::uint64_t>(digit - '0');
            }
            if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000U) {
                return {.command_line = std::nullopt,
                        .error_message = "valor de --timeout muito grande: " + std::string{value}};
            }
            command_line.timeout_ms = seconds * 1000U;
            command_line.timeout_set = true;
            ++index;
            continue;
        }

        if (!options_ended && is_option(argument)) {
            return {.command_line = std::nullopt,
                    .error_message = "opção desconhecida: " + std::string{argument}};
        }

        command_line.executable_path = std::filesystem::path{std::string{argument}};
    }

    if ((command_line.show_help || command_line.show_version) && command_line.executable_path.has_value()) {
        return {.command_line = std::nullopt,
                .error_message = "--help e --version não podem ser usados com um arquivo executável"};
    }

    if (command_line.show_help && command_line.show_version) {
        return {.command_line = std::nullopt,
                .error_message = "--help e --version não podem ser usados juntos"};
    }

    return {.command_line = std::move(command_line), .error_message = {}};
}

ExitCode run_command(const CommandLine& command_line, std::ostream& stdout_stream,
                     std::ostream& stderr_stream) {
    // Configura filtro de trace (inspirado em WINEDEBUG): --trace sozinho = tudo,
    // --trace=pe,loader filtra apenas esses componentes.
    if (command_line.trace_enabled) {
        if (!command_line.trace_channels_raw.empty()) {
            std::vector<diagnostics::TraceComponent> filter;
            filter.reserve(command_line.trace_channels_raw.size());
            for (const auto& name : command_line.trace_channels_raw) {
                diagnostics::TraceComponent comp;
                if (diagnostics::trace_component_from_name(name, comp)) {
                    filter.push_back(comp);
                }
            }
            diagnostics::configure_trace_filter(filter);
        } else {
            diagnostics::configure_trace_all();
        }
    } else {
        diagnostics::configure_trace_all();
    }

    if (command_line.show_help) {
        print_help(stdout_stream);
        return ExitCode::Success;
    }

    if (command_line.show_version) {
        stdout_stream << "TradutorLinux " << TRADUTORLINUX_VERSION << '\n';
        return ExitCode::Success;
    }

    // Modo: Listar biblioteca de aplicativos
    if (command_line.mode == CommandMode::AppList) {
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        const auto& apps = app_catalog.list_apps();
        if (apps.empty()) {
            stdout_stream << "Nenhum aplicativo cadastrado na biblioteca.\n";
            stdout_stream << "Dica: use 'tradutorlinux app add <arquivo.exe> --name \"Nome\"' ou 'tradutorlinux install <setup.exe>'.\n";
            return ExitCode::Success;
        }
        stdout_stream << "Aplicativos cadastrados na biblioteca (" << apps.size() << "):\n\n";
        for (const auto& app : apps) {
            stdout_stream << "  [" << app.id << "] " << app.name << '\n';
            stdout_stream << "    Executável: " << app.executable_path << '\n';
            if (!app.prefix_path.empty()) {
                stdout_stream << "    Prefixo:    " << app.prefix_path << '\n';
            }
            stdout_stream << '\n';
        }
        return ExitCode::Success;
    }

    // Modo: Cadastrar aplicativo manualmente na biblioteca
    if (command_line.mode == CommandMode::AppAdd) {
        if (!command_line.executable_path.has_value()) {
            stderr_stream << "erro: informe o caminho do executável para cadastrar\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        catalog::AppEntry entry;
        entry.name = command_line.app_name.empty()
                         ? command_line.executable_path->filename().string()
                         : command_line.app_name;
        entry.id = command_line.app_id.empty()
                       ? unique_app_id(app_catalog, entry.name)
                       : command_line.app_id;
        entry.executable_path = command_line.executable_path->string();
        entry.prefix_path = command_line.custom_prefix.has_value()
                                ? command_line.custom_prefix->string()
                                : prefix::default_app_prefix(entry.id).string();
        const std::filesystem::path entry_prefix{entry.prefix_path};
        if (!prefix::initialize_prefix(entry_prefix)) {
            stderr_stream << "erro: não foi possível preparar o prefixo do aplicativo\n";
            return ExitCode::InternalError;
        }
        const prefix::EnvironmentPaths paths = prefix::get_environment_paths(entry_prefix);
        entry.working_directory =
            prefix::is_path_within(*command_line.executable_path, paths.drive_c)
                ? command_line.executable_path->parent_path().string()
                : paths.drive_c.string();

        if (!app_catalog.add_app(entry) || !app_catalog.save_to_file()) {
            stderr_stream << "erro: falha ao salvar aplicativo na biblioteca\n";
            return ExitCode::InternalError;
        }
        stdout_stream << "Aplicativo '" << entry.name << "' cadastrado com sucesso [id: "
                      << entry.id << "].\n";
        return ExitCode::Success;
    }

    // Modo: Remover aplicativo da biblioteca
    if (command_line.mode == CommandMode::AppRemove) {
        if (command_line.app_id.empty()) {
            stderr_stream << "erro: informe o ID do aplicativo a remover\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        if (!app_catalog.remove_app(command_line.app_id) || !app_catalog.save_to_file()) {
            stderr_stream << "erro: aplicativo com ID '" << command_line.app_id
                          << "' não encontrado na biblioteca\n";
            return ExitCode::Usage;
        }
        stdout_stream << "Aplicativo '" << command_line.app_id
                      << "' removido da biblioteca com sucesso.\n";
        return ExitCode::Success;
    }

    CommandLine effective_cmd = command_line;
    std::optional<ExecutableSnapshot> installation_before;
    std::string installation_name;
    std::string installation_id;

    // Modo: Executar aplicativo cadastrado
    if (command_line.mode == CommandMode::AppRun) {
        if (command_line.app_id.empty()) {
            stderr_stream << "erro: informe o ID ou nome do aplicativo para executar\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        const auto app_opt = app_catalog.find_app(command_line.app_id);
        if (!app_opt) {
            stderr_stream << "erro: aplicativo '" << command_line.app_id
                          << "' não encontrado na biblioteca\n";
            return ExitCode::InputUnavailable;
        }
        effective_cmd.executable_path = std::filesystem::path(app_opt->executable_path);
        if (!app_opt->prefix_path.empty()) {
            effective_cmd.custom_prefix = std::filesystem::path(app_opt->prefix_path);
        }
        if (!app_opt->working_directory.empty()) {
            effective_cmd.guest_working_directory =
                std::filesystem::path(app_opt->working_directory);
        }
        std::vector<std::string> combined_args = app_opt->args;
        combined_args.insert(combined_args.end(), command_line.guest_arguments.begin(),
                             command_line.guest_arguments.end());
        effective_cmd.guest_arguments = std::move(combined_args);
    }

    // Modo: Instalar aplicativo
    if (command_line.mode == CommandMode::Install) {
        if (!effective_cmd.executable_path.has_value()) {
            stderr_stream << "erro: o comando 'install' requer o caminho do arquivo instalador (.exe)\n";
            return ExitCode::Usage;
        }
        if (effective_cmd.report_only) {
            stderr_stream << "erro: --report não pode ser usado com o comando 'install'\n";
            return ExitCode::Usage;
        }
        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        installation_name = effective_cmd.app_name.empty()
                                ? effective_cmd.executable_path->stem().string()
                                : effective_cmd.app_name;
        installation_id = unique_app_id(app_catalog, installation_name);
        effective_cmd.app_id = installation_id;
        const std::filesystem::path p_root = effective_cmd.custom_prefix.value_or(
            prefix::default_app_prefix(installation_id));
        effective_cmd.custom_prefix = p_root;
        if (!prefix::initialize_prefix(p_root)) {
            stderr_stream << "erro: não foi possível preparar o prefixo da instalação\n";
            return ExitCode::InternalError;
        }
        installation_before = snapshot_executables(prefix::get_environment_paths(p_root).drive_c);
        write_install_trace(effective_cmd.trace_enabled, stderr_stream, diagnostics::TraceLevel::Info,
                            "prepared", {{"prefix", p_root.string()},
                                         {"app-id", installation_id},
                                         {"name", installation_name}});
    }

    if (!effective_cmd.executable_path.has_value()) {
        stderr_stream << "erro: informe um arquivo .exe\n";
        print_help(stderr_stream);
        return ExitCode::Usage;
    }

    const std::filesystem::path prefix_dir =
        effective_cmd.custom_prefix.value_or(prefix::default_prefix_root());
    if (!prefix::initialize_prefix(prefix_dir)) {
        stderr_stream << "erro: não foi possível preparar o prefixo da execução\n";
        return ExitCode::InternalError;
    }

    const prefix::EnvironmentPaths active_paths = prefix::get_environment_paths(prefix_dir);
    const std::filesystem::path legacy_prefix = prefix::default_prefix_root();
    const bool uses_legacy_shared_prefix =
        effective_cmd.mode == CommandMode::AppRun &&
        prefix::is_path_within(prefix_dir, legacy_prefix) &&
        prefix::is_path_within(legacy_prefix, prefix_dir);
    if (!effective_cmd.guest_working_directory.has_value()) {
        if (effective_cmd.mode == CommandMode::DirectRun) {
            std::error_code current_directory_error;
            effective_cmd.guest_working_directory =
                std::filesystem::current_path(current_directory_error);
            if (current_directory_error) {
                stderr_stream << "erro: não foi possível obter o diretório atual\n";
                return ExitCode::InternalError;
            }
        } else {
            effective_cmd.guest_working_directory = active_paths.drive_c;
        }
    } else {
        std::error_code working_directory_error;
        const bool is_directory = std::filesystem::is_directory(
            *effective_cmd.guest_working_directory, working_directory_error);
        const bool within_active_prefix = prefix::is_path_within(
            *effective_cmd.guest_working_directory, active_paths.drive_c);
        if (working_directory_error || !is_directory ||
            (!within_active_prefix && !uses_legacy_shared_prefix)) {
            effective_cmd.guest_working_directory = active_paths.drive_c;
        }
    }

    if (!std::filesystem::exists(*effective_cmd.executable_path)) {
        const std::filesystem::path resolved =
            prefix::resolve_windows_path(effective_cmd.executable_path->string(), prefix_dir);
        if (std::filesystem::exists(resolved)) {
            effective_cmd.executable_path = resolved;
        }
    }

    std::error_code filesystem_error;
    const bool is_regular_file =
        std::filesystem::is_regular_file(*effective_cmd.executable_path, filesystem_error);
    if (filesystem_error || !is_regular_file) {
        stderr_stream << "erro: não foi possível acessar o arquivo: "
                      << effective_cmd.executable_path->string() << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "input"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InputUnavailable;
    }

    if (effective_cmd.trace_enabled) {
        const std::string input_path = effective_cmd.executable_path->string();
        const std::array input_fields{diagnostics::TraceField{"path", input_path}};
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Cli,
                                 diagnostics::TraceLevel::Info, "input", input_fields);
    }

    if (package::is_msix_or_appx_package(*effective_cmd.executable_path)) {
        const auto package_info = package::inspect_msix_package(*effective_cmd.executable_path);
        if (effective_cmd.report_only) {
            stdout_stream << "TradutorLinux package report\n";
            stdout_stream << "format: MSIX / AppX package\n";
            if (package_info.has_value()) {
                stdout_stream << "package-name: "
                              << (package_info->package_name.empty()
                                      ? effective_cmd.executable_path->stem().string()
                                      : package_info->package_name)
                              << '\n';
                if (!package_info->publisher.empty()) {
                    stdout_stream << "publisher: " << package_info->publisher << '\n';
                }
                if (!package_info->version.empty()) {
                    stdout_stream << "version: " << package_info->version << '\n';
                }
                if (package_info->main_executable.has_value()) {
                    stdout_stream << "main-executable: " << *package_info->main_executable << '\n';
                }
                stdout_stream << "applications: " << package_info->applications.size() << '\n';
                for (const auto& app : package_info->applications) {
                    stdout_stream << "  app: id=\"" << app.id << "\" exec=\"" << app.executable
                                  << "\" name=\"" << app.display_name << "\"\n";
                }
            }
            stdout_stream << "result: package-recognized\n";
            stdout_stream << "execution: not-attempted\n";
            return ExitCode::Success;
        }
        stderr_stream << "erro: formato de pacote MSIX / AppX reconhecido; use extração de pacote ou especifique o executável interno (.exe)\n";
        return ExitCode::Unsupported;
    }

    const std::optional<std::vector<std::byte>> bytes = read_file(*effective_cmd.executable_path);
    if (!bytes.has_value()) {
        stderr_stream << "erro: não foi possível ler o arquivo: "
                      << effective_cmd.executable_path->string() << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "input"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InputUnavailable;
    }

    const pe::ParseResult parse_result = pe::parse_pe(*bytes);
    if (parse_result.status != pe::ParseStatus::Success) {
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"status", status_label(parse_result.status)},
                diagnostics::TraceField{"detail", parse_result.error_message},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Pe,
                                     diagnostics::TraceLevel::Error, "parse-failed", fields);
        }
        stderr_stream << "erro: " << parse_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "parse"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        if (parse_result.status == pe::ParseStatus::UnsupportedArchitecture ||
            parse_result.status == pe::ParseStatus::UnsupportedFormat ||
            parse_result.status == pe::ParseStatus::UnsupportedMechanism) {
            return ExitCode::Unsupported;
        }
        return ExitCode::MalformedPe;
    }

    if (effective_cmd.trace_enabled) {
        write_pe_trace(stderr_stream, parse_result.info);
    } else {
        print_pe_summary(stderr_stream, parse_result.info);
    }

    loader::register_builtin_modules();

    if (effective_cmd.report_only) {
        const loader::ResolveResult report_result =
            print_support_report(stdout_stream, parse_result.info);
        return report_result.status == loader::ImportStatus::Resolved ? ExitCode::Success
                                                                       : ExitCode::Unsupported;
    }

    loader::PrepareResult prepare_result = loader::prepare_process(parse_result.info, *bytes);
    if (prepare_result.status == loader::PrepareStatus::OutOfMemory) {
        if (effective_cmd.trace_enabled) {
            write_map_failed_trace(stderr_stream, "out-of-memory", prepare_result.error_message);
        }
        stderr_stream << "erro: " << prepare_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "prepare"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::InternalError;
    }
    if (prepare_result.status == loader::PrepareStatus::InvalidImage) {
        if (effective_cmd.trace_enabled) {
            write_map_failed_trace(stderr_stream, "invalid-image", prepare_result.error_message);
        }
        stderr_stream << "erro: " << prepare_result.error_message << '\n';
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "prepare"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::MalformedPe;
    }

    loader::GuestProcess& process = prepare_result.process;
    if (effective_cmd.trace_enabled) {
        write_map_trace(stderr_stream, process.image);
    } else {
        print_map_summary(stderr_stream, process.image);
    }
    if (effective_cmd.trace_enabled) {
        write_imports_trace(stderr_stream, process.imports);
    } else {
        print_imports_summary(stderr_stream, process.imports);
    }

    if (prepare_result.status == loader::PrepareStatus::UnresolvedImports) {
        if (!effective_cmd.trace_enabled) {
            stderr_stream << "erro: importações não resolvidas: "
                          << prepare_result.error_message << '\n';
        }
        const std::uint64_t unmap_base = process.image.base;
        loader::destroy_process(process);
        if (effective_cmd.trace_enabled) {
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "imports"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::Unsupported;
    }

    if (process.image.delta != 0 && !process.image.has_relocation_directory) {
        const std::uint64_t unmap_base = process.image.base;
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"reason", "imagem sem diretório de relocations"},
                diagnostics::TraceField{"delta", util::format_signed_hex(process.image.delta)},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Loader,
                                     diagnostics::TraceLevel::Error, "execution-rejected", fields);
        } else {
            stderr_stream << "erro: imagem sem relocations não pode ser executada fora da base preferencial\n";
        }
        loader::destroy_process(process);
        if (effective_cmd.trace_enabled) {
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "relocations"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::Unsupported;
    }

    std::vector<std::string> guest_argv;
    guest_argv.reserve(1 + effective_cmd.guest_arguments.size());
    guest_argv.push_back(effective_cmd.executable_path->string());
    guest_argv.insert(guest_argv.end(), effective_cmd.guest_arguments.begin(),
                      effective_cmd.guest_arguments.end());
    msvcrt_set_guest_command_line(std::move(guest_argv));
    set_guest_prefix_path(prefix_dir);
    set_guest_module_path(effective_cmd.executable_path->c_str());
    set_guest_image_view(process.image.memory, process.image.size,
                         parse_result.info.resource_directory_rva,
                         parse_result.info.resource_directory_size);
    runtime::set_guest_unwind_view(process.image.memory, process.image.size,
                                   parse_result.info.exception_directory_rva,
                                   process.info.runtime_functions);

    const process::GuestOutcome outcome = process::run_guest_isolated(
        process.thread.entry_point, process.thread.stack_top, effective_cmd.timeout_ms,
        *effective_cmd.guest_working_directory);

    // Contexto de falha calculado antes do destroy_process: o evento
    // guest-signal usa a imagem mapeada e as importações ainda vivas.
    const diagnostics::GuestCrashContext crash_context =
        outcome.kind == process::GuestOutcomeKind::Signaled && outcome.fault_recorded
            ? diagnostics::describe_guest_crash(process.image, process.imports,
                                                outcome.fault_rip != 0 ? outcome.fault_rip : outcome.fault_address)
            : diagnostics::GuestCrashContext{};

    const std::uint64_t unmap_base = process.image.base;
    runtime::clear_guest_unwind_view();
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    set_guest_prefix_path({});

    if (outcome.kind == process::GuestOutcomeKind::Exited) {
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"exit-code", std::to_string(outcome.exit_code)},
                diagnostics::TraceField{"explicit", outcome.exited_explicitly ? "sim" : "não"},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Info, "exit", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        }
        if (effective_cmd.mode != CommandMode::Install) {
            return static_cast<ExitCode>(outcome.exit_code);
        }

        if (outcome.exit_code != 0U) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup"},
                                 {"exit-code", std::to_string(outcome.exit_code)},
                                 {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
            return static_cast<ExitCode>(outcome.exit_code);
        }

        const prefix::EnvironmentPaths installation_paths =
            prefix::get_environment_paths(prefix_dir);
        std::optional<std::filesystem::path> selected_executable;
        if (effective_cmd.installed_executable_path.has_value()) {
            selected_executable = resolve_installed_executable(
                *effective_cmd.installed_executable_path, prefix_dir);
            if (!selected_executable.has_value()) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Warning, "pending",
                                    {{"reason", "invalid-app-exe"},
                                     {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "instalação concluída, mas --app-exe não aponta para um PE32+ x64 dentro do prefixo\n";
                return ExitCode::InstallPending;
            }
        } else {
            const ExecutableSnapshot after = snapshot_executables(installation_paths.drive_c);
            const std::vector<std::filesystem::path> candidates = changed_executables(
                installation_before.value_or(ExecutableSnapshot{}), after);
            for (const std::filesystem::path& candidate : candidates) {
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Info, "candidate",
                                    {{"path", candidate.string()},
                                     {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
            }
            if (candidates.size() == 1U) {
                selected_executable = candidates.front();
            } else {
                const std::string reason = candidates.empty() ? "no-candidate" : "selection-required";
                write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                    diagnostics::TraceLevel::Warning, "pending",
                                    {{"reason", reason}, {"prefix", prefix_dir.string()},
                                     {"app-id", installation_id}});
                stderr_stream << "instalação concluída; cadastro pendente (" << reason << ").\n";
                return ExitCode::InstallPending;
            }
        }

        catalog::AppCatalog app_catalog;
        (void)app_catalog.load_from_file();
        catalog::AppEntry entry;
        entry.id = installation_id;
        entry.name = installation_name;
        entry.executable_path = selected_executable->string();
        entry.prefix_path = prefix_dir.string();
        entry.working_directory = selected_executable->parent_path().string();
        if (!app_catalog.add_app(entry) || !app_catalog.save_to_file()) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "catalog"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
            stderr_stream << "erro: instalação concluída, mas não foi possível salvar o catálogo\n";
            return ExitCode::InternalError;
        }
        write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                            diagnostics::TraceLevel::Info, "registered",
                            {{"prefix", prefix_dir.string()}, {"app-id", entry.id},
                             {"path", entry.executable_path}});
        stderr_stream << "instalação concluída; aplicativo registrado como '" << entry.name
                      << "' [id: " << entry.id << "]\n";
        return ExitCode::Success;
    }

    if (outcome.kind == process::GuestOutcomeKind::Signaled) {
        const process::SignalDescription signal = process::describe_signal(outcome.signal_number);
        if (effective_cmd.trace_enabled) {
            std::vector<diagnostics::TraceField> fields;
            fields.reserve(7);
            fields.push_back(diagnostics::TraceField{
                "category",
                std::string{diagnostics::failure_category_name(
                    diagnostics::FailureCategory::GuestSignal)}});
            fields.push_back(diagnostics::TraceField{"signal", std::string{signal.name}});
            fields.push_back(diagnostics::TraceField{"detail", std::string{signal.detail}});
            if (outcome.fault_recorded) {
                fields.push_back(diagnostics::TraceField{
                    "fault-address", util::format_hex(outcome.fault_address)});
            }
            if (crash_context.valid) {
                fields.push_back(
                    diagnostics::TraceField{"rva", util::format_hex(crash_context.rva)});
                if (!crash_context.section.empty()) {
                    fields.push_back(diagnostics::TraceField{
                        "section", std::string{crash_context.section}});
                }
                if (!crash_context.nearest_import.empty()) {
                    fields.push_back(diagnostics::TraceField{
                        "nearest-import", crash_context.nearest_import});
                }
            }
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error, "terminated", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: o programa convidado terminou por sinal " << signal.name
                          << " (" << signal.detail << ")";
            if (outcome.fault_recorded) {
                stderr_stream << " no endereço " << util::format_hex(outcome.fault_address);
                if (crash_context.valid) {
                    stderr_stream << " (rva " << util::format_hex(crash_context.rva);
                    if (!crash_context.section.empty()) {
                        stderr_stream << ", seção " << crash_context.section;
                    }
                    if (!crash_context.nearest_import.empty()) {
                        stderr_stream << ", importação mais próxima "
                                      << crash_context.nearest_import;
                    }
                    stderr_stream << ")";
                }
            }
            stderr_stream << '\n';
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup-signal"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::GuestFault;
    }

    if (outcome.kind == process::GuestOutcomeKind::TimedOut) {
        if (effective_cmd.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"category",
                                        std::string{diagnostics::failure_category_name(
                                            diagnostics::FailureCategory::GuestTimeout)}},
                diagnostics::TraceField{"timeout-ms", std::to_string(effective_cmd.timeout_ms)},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                     diagnostics::TraceLevel::Error, "terminated", fields);
            write_unmap_trace(stderr_stream, unmap_base);
        } else {
            stderr_stream << "erro: o programa convidado não terminou dentro de "
                          << effective_cmd.timeout_ms << " ms\n";
        }
        if (effective_cmd.mode == CommandMode::Install) {
            write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                                diagnostics::TraceLevel::Error, "failed",
                                {{"stage", "setup-timeout"}, {"prefix", prefix_dir.string()},
                                 {"app-id", installation_id}});
        }
        return ExitCode::GuestTimeout;
    }

    if (effective_cmd.trace_enabled) {
        const std::array fields{
            diagnostics::TraceField{
                "category",
                std::string{diagnostics::failure_category_name(
                    diagnostics::FailureCategory::InternalError)}},
            diagnostics::TraceField{"detail",
                                    "não foi possível criar o processo filho do convidado"},
        };
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Process,
                                 diagnostics::TraceLevel::Error, "terminated", fields);
        write_unmap_trace(stderr_stream, unmap_base);
    } else {
        stderr_stream << "erro: não foi possível criar o processo filho do convidado\n";
    }
    if (effective_cmd.mode == CommandMode::Install) {
        write_install_trace(effective_cmd.trace_enabled, stderr_stream,
                            diagnostics::TraceLevel::Error, "failed",
                            {{"stage", "setup-spawn"}, {"prefix", prefix_dir.string()},
                             {"app-id", installation_id}});
    }
    return ExitCode::InternalError;
}

void print_help(std::ostream& stream) {
    stream << kUsage;
    stream << "\n";
    stream << "Comandos de Gerenciamento da Biblioteca e Instalação:\n";
    stream << "  install <setup.exe> [--name <Nome>] [--prefix <dir>] [--app-exe <caminho>]\n";
    stream << "             instala em prefixo próprio e cadastra um único executável detectado\n";
    stream << "             --app-exe escolhe manualmente um .exe dentro de drive_c\n";
    stream << "  app list   lista todos os aplicativos cadastrados na biblioteca\n";
    stream << "  app run <id_ou_nome> [args...]\n";
    stream << "             executa um aplicativo cadastrado na biblioteca\n";
    stream << "  app add <arquivo.exe> [--name <Nome>] [--prefix <dir>] [--id <id>]\n";
    stream << "             cadastra manualmente um executável na biblioteca\n";
    stream << "  app remove <id>\n";
    stream << "             remove um aplicativo do catálogo da biblioteca\n\n";
    stream << "Opções Gerais de Execução:\n";
    stream << "  --trace[=canais]  escreve diagnóstico estruturado em stderr\n";
    stream << "                    canais: cli,pe,loader,imports,runtime,process,gui,crt,install (ex: --trace=pe,loader)\n";
    stream << "                    sem lista = todos os canais (compatível com WINEDEBUG)\n";
    stream << "  --report   relata imports suportados sem executar o arquivo\n";
    stream << "  --timeout <segundos>\n";
    stream << "             limita a execução do convidado; 0 = sem limite (padrão)\n";
    stream << "  --help     mostra esta ajuda\n";
    stream << "  --version  mostra a versão do TradutorLinux\n";
}

}  // namespace tradutorlinux
