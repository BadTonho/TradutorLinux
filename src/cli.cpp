#include "tradutorlinux/cli.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/loader/image_mapper.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

constexpr std::string_view kUsage = "Uso: tradutorlinux [--trace] <arquivo.exe>\n";

[[nodiscard]] bool is_option(const std::string_view argument) {
    return argument.starts_with('-');
}

[[nodiscard]] std::string format_hex(const std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result = "0x";
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned int digit = static_cast<unsigned int>((value >> shift) & 0xFULL);
        if (digit != 0 || started) {
            result.push_back(kDigits[digit]);
            started = true;
        }
    }
    if (!started) {
        result.push_back('0');
    }
    return result;
}

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
    }
    return "unknown";
}

void write_pe_trace(std::ostream& stream, const pe::PeInfo& info) {
    const std::string format = info.is_pe32_plus ? "PE32+" : "PE32";
    const std::string arch = info.machine == 0x8664 ? "x86-64" : "desconhecida";
    const std::array image_fields{
        diagnostics::TraceField{"format", format},
        diagnostics::TraceField{"arch", arch},
        diagnostics::TraceField{"entry", format_hex(info.address_of_entry_point)},
        diagnostics::TraceField{"image-base", format_hex(info.image_base)},
        diagnostics::TraceField{"size-of-image", format_hex(info.size_of_image)},
        diagnostics::TraceField{"sections", std::to_string(info.number_of_sections)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Pe,
                             diagnostics::TraceLevel::Info, "image", image_fields);

    for (std::size_t index = 0; index < info.sections.size(); ++index) {
        const pe::SectionInfo& section = info.sections[index];
        const std::array fields{
            diagnostics::TraceField{"index", std::to_string(index)},
            diagnostics::TraceField{"name", section.name},
            diagnostics::TraceField{"virtual-address", format_hex(section.virtual_address)},
            diagnostics::TraceField{"virtual-size", format_hex(section.virtual_size)},
            diagnostics::TraceField{"raw-pointer", format_hex(section.raw_data_pointer)},
            diagnostics::TraceField{"raw-size", format_hex(section.raw_data_size)},
            diagnostics::TraceField{"characteristics", format_hex(section.characteristics)},
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

    std::size_t relocation_entries = 0;
    for (const pe::BaseRelocBlock& block : info.relocations) {
        relocation_entries += block.entries.size();
        const std::array fields{
            diagnostics::TraceField{"page", format_hex(block.page_rva)},
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
    stream << format << " x86-64 | entry=" << format_hex(info.address_of_entry_point)
           << " | image-base=" << format_hex(info.image_base) << " | "
           << info.number_of_sections << " seções\n";
    for (const pe::SectionInfo& section : info.sections) {
        stream << "  seção " << section.name << " va=" << format_hex(section.virtual_address)
               << " vsize=" << format_hex(section.virtual_size)
               << " raw=" << format_hex(section.raw_data_pointer) << "/"
               << format_hex(section.raw_data_size) << " chars=" << format_hex(section.characteristics)
               << '\n';
    }
    for (const pe::ImportedDll& dll : info.imports) {
        stream << "  imports " << dll.name << ':';
        for (const pe::ImportedSymbol& symbol : dll.symbols) {
            stream << ' ' << symbol_label(symbol);
        }
        stream << '\n';
    }
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
    }
    return "---";
}

void write_map_trace(std::ostream& stream, const loader::MappedImage& image) {
    const std::string_view at_preferred = image.delta == 0 ? "sim" : "não";
    const std::array image_fields{
        diagnostics::TraceField{"preferred-base", format_hex(image.preferred_base)},
        diagnostics::TraceField{"base", format_hex(image.base)},
        diagnostics::TraceField{"delta", format_hex(static_cast<std::uint64_t>(image.delta))},
        diagnostics::TraceField{"size", format_hex(image.size)},
        diagnostics::TraceField{"at-preferred", std::string{at_preferred}},
        diagnostics::TraceField{"relocations-applied", std::to_string(image.applied_relocations)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Info, "mapped", image_fields);

    for (const loader::MapRegion& region : image.regions) {
        const std::array fields{
            diagnostics::TraceField{"name", region.name},
            diagnostics::TraceField{"rva", format_hex(region.rva)},
            diagnostics::TraceField{"size", format_hex(region.size)},
            diagnostics::TraceField{"permissions",
                                    std::string{permissions_label(region.permissions)}},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                                 diagnostics::TraceLevel::Info, "region", fields);
    }

    if (image.delta != 0 && !image.has_relocation_directory) {
        const std::array fields{
            diagnostics::TraceField{"reason", "imagem sem diretório de relocations"},
            diagnostics::TraceField{"delta", format_hex(static_cast<std::uint64_t>(image.delta))},
        };
        diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                                 diagnostics::TraceLevel::Warning, "cannot-relocate", fields);
    }

    const std::array unmap_fields{
        diagnostics::TraceField{"base", format_hex(image.base)},
    };
    diagnostics::write_trace(stream, diagnostics::TraceComponent::Loader,
                             diagnostics::TraceLevel::Info, "unmap", unmap_fields);
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
    stream << "  mapeado base=" << format_hex(image.base)
           << " preferred=" << format_hex(image.preferred_base)
           << " delta=" << format_hex(static_cast<std::uint64_t>(image.delta));
    if (image.delta != 0) {
        stream << " (realocado, " << image.applied_relocations << " relocations aplicados)";
    }
    stream << '\n';
    for (const loader::MapRegion& region : image.regions) {
        stream << "    região " << region.name << " rva=" << format_hex(region.rva)
               << " perms=" << permissions_label(region.permissions) << '\n';
    }
}

}  // namespace

ParseResult parse_command_line(const int argc, const char* const argv[]) {
    CommandLine command_line;
    bool options_ended = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

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

        if (!options_ended && argument == "--trace") {
            if (command_line.trace_enabled) {
                return {.command_line = std::nullopt,
                        .error_message = "a opção --trace foi repetida"};
            }
            command_line.trace_enabled = true;
            continue;
        }

        if (!options_ended && is_option(argument)) {
            return {.command_line = std::nullopt,
                    .error_message = "opção desconhecida: " + std::string{argument}};
        }

        if (command_line.executable_path.has_value()) {
            return {.command_line = std::nullopt,
                    .error_message = "apenas um arquivo executável pode ser informado"};
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
    if (command_line.show_help) {
        print_help(stdout_stream);
        return ExitCode::Success;
    }

    if (command_line.show_version) {
        stdout_stream << "TradutorLinux " << TRADUTORLINUX_VERSION << '\n';
        return ExitCode::Success;
    }

    if (!command_line.executable_path.has_value()) {
        stderr_stream << "erro: informe um arquivo .exe\n";
        print_help(stderr_stream);
        return ExitCode::Usage;
    }

    std::error_code filesystem_error;
    const bool is_regular_file =
        std::filesystem::is_regular_file(*command_line.executable_path, filesystem_error);
    if (filesystem_error || !is_regular_file) {
        stderr_stream << "erro: não foi possível acessar o arquivo: "
                      << command_line.executable_path->string() << '\n';
        return ExitCode::InputUnavailable;
    }

    if (command_line.trace_enabled) {
        const std::string input_path = command_line.executable_path->string();
        const std::array input_fields{diagnostics::TraceField{"path", input_path}};
        diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Cli,
                                 diagnostics::TraceLevel::Info, "input", input_fields);
    }

    const std::optional<std::vector<std::byte>> bytes = read_file(*command_line.executable_path);
    if (!bytes.has_value()) {
        stderr_stream << "erro: não foi possível ler o arquivo: "
                      << command_line.executable_path->string() << '\n';
        return ExitCode::InputUnavailable;
    }

    const pe::ParseResult parse_result = pe::parse_pe(*bytes);
    if (parse_result.status != pe::ParseStatus::Success) {
        if (command_line.trace_enabled) {
            const std::array fields{
                diagnostics::TraceField{"status", status_label(parse_result.status)},
                diagnostics::TraceField{"detail", parse_result.error_message},
            };
            diagnostics::write_trace(stderr_stream, diagnostics::TraceComponent::Pe,
                                     diagnostics::TraceLevel::Error, "parse-failed", fields);
        }
        stderr_stream << "erro: " << parse_result.error_message << '\n';
        if (parse_result.status == pe::ParseStatus::UnsupportedArchitecture ||
            parse_result.status == pe::ParseStatus::UnsupportedFormat) {
            return ExitCode::Unsupported;
        }
        return ExitCode::MalformedPe;
    }

    if (command_line.trace_enabled) {
        write_pe_trace(stderr_stream, parse_result.info);
    } else {
        print_pe_summary(stderr_stream, parse_result.info);
    }

    loader::MapResult map_result = loader::map_image(parse_result.info, *bytes);
    if (map_result.status == loader::MapStatus::OutOfMemory) {
        if (command_line.trace_enabled) {
            write_map_failed_trace(stderr_stream, "out-of-memory", map_result.error_message);
        }
        stderr_stream << "erro: " << map_result.error_message << '\n';
        return ExitCode::InternalError;
    }
    if (map_result.status == loader::MapStatus::InvalidImage) {
        if (command_line.trace_enabled) {
            write_map_failed_trace(stderr_stream, "invalid-image", map_result.error_message);
        }
        stderr_stream << "erro: " << map_result.error_message << '\n';
        return ExitCode::MalformedPe;
    }

    if (command_line.trace_enabled) {
        write_map_trace(stderr_stream, map_result.image);
    } else {
        print_map_summary(stderr_stream, map_result.image);
    }
    loader::unmap_image(map_result.image);
    return ExitCode::Success;
}

void print_help(std::ostream& stream) {
    stream << kUsage;
    stream << "\n";
    stream << "Opções:\n";
    stream << "  --trace    escreve diagnóstico estruturado em stderr\n";
    stream << "  --help     mostra esta ajuda\n";
    stream << "  --version  mostra a versão do TradutorLinux\n";
}

}  // namespace tradutorlinux
