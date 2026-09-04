#include "cli_internal.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::cli_detail {

[[nodiscard]] std::string symbol_label(const pe::ImportedSymbol& symbol) {
    TL_TRACE_FUNCTION();
    if (symbol.by_ordinal) {
        return "ordinal(" + std::to_string(symbol.ordinal) + ")";
    }
    return symbol.name;
}

[[nodiscard]] const char* status_label(const pe::ParseStatus status) {
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
    const std::array fields{
        diagnostics::TraceField{"base", util::format_hex(base)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Info, "unmap", fields);
}

void write_map_failed_trace(std::ostream& stream, const std::string_view status,
                            const std::string& detail) {
    TL_TRACE_FUNCTION();
    const std::array fields{
        diagnostics::TraceField{"status", std::string{status}},
        diagnostics::TraceField{"detail", detail},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Error, "map-failed", fields);
}

void print_map_summary(std::ostream& stream, const loader::MappedImage& image) {
    TL_TRACE_FUNCTION();
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
    TL_TRACE_FUNCTION();
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

[[nodiscard]] const char* export_support_label(const loader::ExportSupport support) {
    TL_TRACE_FUNCTION();
    switch (support) {
        case loader::ExportSupport::Full:
            return "full";
        case loader::ExportSupport::Limited:
            return "limited";
        case loader::ExportSupport::Stub:
            return "stub";
    }
    return "unknown";
}

[[nodiscard]] std::string resolved_symbol_label(const loader::ResolvedImport& entry) {
    TL_TRACE_FUNCTION();
    if (entry.by_ordinal) {
        return "ordinal(" + std::to_string(entry.ordinal) + ")";
    }
    return entry.symbol;
}

[[nodiscard]] const char* import_mechanism_label(const loader::ImportMechanism mechanism) {
    TL_TRACE_FUNCTION();
    return mechanism == loader::ImportMechanism::Delay ? "delay-import" : "import";
}

void write_imports_trace(std::ostream& stream, const loader::ResolveResult& imports) {
    TL_TRACE_FUNCTION();
    for (const loader::ResolvedImport& entry : imports.imports) {
        if (entry.status == loader::ImportStatus::Resolved) {
            const std::array fields{
                diagnostics::TraceField{"dll", entry.dll},
                diagnostics::TraceField{"symbol", resolved_symbol_label(entry)},
                diagnostics::TraceField{"address", util::format_hex(entry.address)},
                diagnostics::TraceField{"mechanism", import_mechanism_label(entry.mechanism)},
                diagnostics::TraceField{"support", export_support_label(entry.support)},
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
    TL_TRACE_FUNCTION();
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
            stream << " -> " << util::format_hex(entry.address)
                   << " support=" << export_support_label(entry.support) << '\n';
        } else {
            stream << " [" << import_status_label(entry.status) << "] " << entry.detail << '\n';
        }
    }
}

void print_support_report_group(std::ostream& stream, const loader::ResolveResult& result,
                                const loader::ImportMechanism mechanism) {
    TL_TRACE_FUNCTION();
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
                   << " status=" << import_status_label(entry.status);
            if (entry.status == loader::ImportStatus::Resolved) {
                stream << " support=" << export_support_label(entry.support);
            }
            stream << '\n';
        }
    }
}

[[nodiscard]] loader::ResolveResult print_support_report(std::ostream& stream,
                                                          const pe::PeInfo& info) {
    TL_TRACE_FUNCTION();
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
    const std::size_t limited_exports = static_cast<std::size_t>(std::count_if(
        result.imports.begin(), result.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.status == loader::ImportStatus::Resolved &&
                   entry.support == loader::ExportSupport::Limited;
        }));
    const std::size_t stub_exports = static_cast<std::size_t>(std::count_if(
        result.imports.begin(), result.imports.end(), [](const loader::ResolvedImport& entry) {
            return entry.status == loader::ImportStatus::Resolved &&
                   entry.support == loader::ExportSupport::Stub;
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
    const char* runtime_support = result.status != loader::ImportStatus::Resolved
                                      ? "unresolved"
                                      : (stub_exports != 0 ? "stub"
                                                           : (limited_exports != 0 ? "limited" : "full"));
    stream << "runtime-support: " << runtime_support
           << " (" << limited_exports << " limited, " << stub_exports << " stub)\n";
    stream << "execution: not-attempted\n";
    stream << "execution-result: not-attempted\n";
    return result;
}

}  // namespace tradutorlinux::cli_detail
