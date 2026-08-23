#include "tradutorlinux/pe/pe_reader.hpp"

#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace tradutorlinux::pe {
namespace {

constexpr std::size_t kDosHeaderSize = 64;
constexpr std::size_t kPeSignatureOffset = 4;
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kOptionalHeader64BaseSize = 112;
constexpr std::size_t kDataDirectoryCount = 16;
constexpr std::size_t kDataDirectoryEntrySize = 8;
constexpr std::size_t kImportDescriptorSize = 20;
constexpr std::size_t kDelayImportDescriptorSize = 32;
constexpr std::size_t kThunkEntrySize = 8;
constexpr std::size_t kBaseRelocBlockHeaderSize = 8;
constexpr std::size_t kBaseRelocEntrySize = 2;
constexpr std::size_t kImportByNameHintSize = 2;

constexpr std::uint16_t kDosMagic = 0x5A4D;
constexpr std::uint32_t kPeSignature = 0x00004550;
constexpr std::uint16_t kMachineAmd64 = 0x8664;
constexpr std::uint16_t kOptionalMagic64 = 0x20B;

struct RvaRange {
    std::uint32_t rva{};
    std::uint32_t length{};
};
constexpr std::uint16_t kOptionalMagic32 = 0x10B;
constexpr std::uint64_t kOrdinalFlag64 = 0x8000000000000000ULL;
constexpr std::uint32_t kDelayImportAttrRva = 0x1;

constexpr std::size_t kDirImport = 1;
constexpr std::size_t kDirResource = 2;
constexpr std::size_t kDirBaseReloc = 5;
constexpr std::size_t kDirDelayImport = 13;

constexpr std::size_t kMaxImportDlls = 1024;
constexpr std::size_t kMaxSymbolsPerDll = 4096;
constexpr std::size_t kMaxRelocBlocks = 4096;
constexpr std::size_t kMaxCString = 65535;

[[nodiscard]] ParseResult fail(const ParseStatus status, std::string message) {
    return {.status = status, .error_message = std::move(message), .info = {}};
}

class ByteReader {
public:
    explicit ByteReader(const std::span<const std::byte> data) : data_(data) {}

    [[nodiscard]] std::size_t size() const { return data_.size(); }

    [[nodiscard]] bool has_range(const std::size_t offset, const std::size_t length) const {
        return offset <= data_.size() && length <= data_.size() - offset;
    }

    bool read_u16(const std::size_t offset, std::uint16_t& out) const {
        if (!has_range(offset, 2)) {
            return false;
        }
        out = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_at(offset)) |
            (static_cast<std::uint16_t>(byte_at(offset + 1)) << 8));
        return true;
    }

    bool read_u32(const std::size_t offset, std::uint32_t& out) const {
        if (!has_range(offset, 4)) {
            return false;
        }
        out = static_cast<std::uint32_t>(byte_at(offset)) |
              (static_cast<std::uint32_t>(byte_at(offset + 1)) << 8) |
              (static_cast<std::uint32_t>(byte_at(offset + 2)) << 16) |
              (static_cast<std::uint32_t>(byte_at(offset + 3)) << 24);
        return true;
    }

    bool read_u64(const std::size_t offset, std::uint64_t& out) const {
        if (!has_range(offset, 8)) {
            return false;
        }
        out = static_cast<std::uint64_t>(byte_at(offset)) |
              (static_cast<std::uint64_t>(byte_at(offset + 1)) << 8) |
              (static_cast<std::uint64_t>(byte_at(offset + 2)) << 16) |
              (static_cast<std::uint64_t>(byte_at(offset + 3)) << 24) |
              (static_cast<std::uint64_t>(byte_at(offset + 4)) << 32) |
              (static_cast<std::uint64_t>(byte_at(offset + 5)) << 40) |
              (static_cast<std::uint64_t>(byte_at(offset + 6)) << 48) |
              (static_cast<std::uint64_t>(byte_at(offset + 7)) << 56);
        return true;
    }

    [[nodiscard]] std::optional<std::span<const std::byte>> read_bytes(
        const std::size_t offset, const std::size_t length) const {
        if (!has_range(offset, length)) {
            return std::nullopt;
        }
        return data_.subspan(offset, length);
    }

    [[nodiscard]] std::optional<std::string> read_cstring(const std::size_t offset) const {
        if (offset >= data_.size()) {
            return std::nullopt;
        }
        std::size_t end = offset;
        while (end < data_.size() && end - offset < kMaxCString &&
               data_[end] != std::byte{0}) {
            ++end;
        }
        if (end == data_.size() || end - offset >= kMaxCString) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(data_.data() + offset), end - offset);
    }

private:
    [[nodiscard]] unsigned int byte_at(const std::size_t offset) const {
        return std::to_integer<unsigned char>(data_[offset]);
    }

    std::span<const std::byte> data_;
};

class PeParser {
public:
    explicit PeParser(const std::span<const std::byte> data) : reader_(data) {}

    [[nodiscard]] ParseResult run() {
        if (reader_.size() < kDosHeaderSize) {
            return fail(ParseStatus::Truncated, "arquivo menor que o cabeçalho DOS (64 bytes)");
        }

        std::uint16_t dos_magic{};
        std::uint32_t e_lfanew{};
        reader_.read_u16(0, dos_magic);
        reader_.read_u32(60, e_lfanew);
        if (dos_magic != kDosMagic) {
            return fail(ParseStatus::Malformed, "assinatura DOS ausente (esperado 'MZ')");
        }

        if (static_cast<std::uint64_t>(e_lfanew) > reader_.size() - kPeSignatureOffset) {
            return fail(ParseStatus::Truncated, "e_lfanew aponta para fora do arquivo");
        }
        const std::size_t nt_offset = static_cast<std::size_t>(e_lfanew);

        if (!reader_.has_range(nt_offset, kPeSignatureOffset + kCoffHeaderSize)) {
            return fail(ParseStatus::Truncated, "cabeçalho NT/COFF truncado");
        }

        std::uint32_t pe_signature{};
        reader_.read_u32(nt_offset, pe_signature);
        if (pe_signature != kPeSignature) {
            return fail(ParseStatus::Malformed, "assinatura PE ausente (esperado 'PE\\0\\0')");
        }

        const std::size_t coff_offset = nt_offset + kPeSignatureOffset;
        std::uint16_t machine{};
        std::uint16_t section_count{};
        std::uint16_t optional_size{};
        reader_.read_u16(coff_offset, machine);
        reader_.read_u16(coff_offset + 2, section_count);
        reader_.read_u16(coff_offset + 16, optional_size);

        if (machine != kMachineAmd64) {
            return fail(ParseStatus::UnsupportedArchitecture,
                        "arquitetura de máquina " + util::format_hex(machine) +
                            " não suportada (esperado AMD64)");
        }

        if (optional_size < kOptionalHeader64BaseSize) {
            return fail(ParseStatus::Malformed,
                        "SizeOfOptionalHeader menor que o mínimo PE32+ (112 bytes)");
        }

        const std::size_t opt_offset = coff_offset + kCoffHeaderSize;
        const std::size_t section_table_offset = opt_offset + static_cast<std::size_t>(optional_size);
        if (section_table_offset > reader_.size() ||
            static_cast<std::uint64_t>(section_count) >
                (reader_.size() - section_table_offset) / kSectionHeaderSize) {
            return fail(ParseStatus::Truncated, "tabela de seções não cabe no arquivo");
        }

        std::uint16_t opt_magic{};
        reader_.read_u16(opt_offset, opt_magic);
        if (opt_magic == kOptionalMagic32) {
            return fail(ParseStatus::UnsupportedFormat,
                        "PE32 (x86) não é suportado; apenas PE32+ x86-64");
        }
        if (opt_magic != kOptionalMagic64) {
            return fail(ParseStatus::Malformed,
                        "magic do optional header " + util::format_hex(opt_magic) +
                            " não reconhecido (esperado 0x20b)");
        }

        PeInfo info;
        info.is_pe32_plus = true;
        info.machine = machine;
        info.number_of_sections = section_count;
        reader_.read_u32(opt_offset + 16, info.address_of_entry_point);
        reader_.read_u64(opt_offset + 24, info.image_base);
        reader_.read_u32(opt_offset + 32, info.section_alignment);
        reader_.read_u32(opt_offset + 56, info.size_of_image);
        reader_.read_u32(opt_offset + 60, info.size_of_headers);
        reader_.read_u16(opt_offset + 68, info.subsystem);

        if (info.size_of_image == 0 || info.size_of_headers == 0 ||
            info.size_of_headers > info.size_of_image || info.section_alignment == 0) {
            return fail(ParseStatus::Malformed,
                        "optional header com SizeOfImage, SizeOfHeaders ou SectionAlignment inválido");
        }

        std::uint32_t number_of_rva_and_sizes{};
        reader_.read_u32(opt_offset + 108, number_of_rva_and_sizes);
        const std::size_t directory_count =
            std::min<std::size_t>(static_cast<std::size_t>(number_of_rva_and_sizes),
                                  kDataDirectoryCount);
        const std::size_t directory_bytes = directory_count * kDataDirectoryEntrySize;
        if (directory_bytes > static_cast<std::size_t>(optional_size) -
                                  kOptionalHeader64BaseSize) {
            return fail(ParseStatus::Truncated,
                        "diretórios de dados excedem o optional header");
        }
        const std::size_t directory_offset = opt_offset + kOptionalHeader64BaseSize;

        if (directory_count > kDirImport) {
            reader_.read_u32(directory_offset + kDirImport * kDataDirectoryEntrySize,
                             info.import_directory_rva);
            reader_.read_u32(directory_offset + kDirImport * kDataDirectoryEntrySize + 4,
                             info.import_directory_size);
        }
        if (directory_count > kDirResource) {
            reader_.read_u32(directory_offset + kDirResource * kDataDirectoryEntrySize,
                             info.resource_directory_rva);
            reader_.read_u32(directory_offset + kDirResource * kDataDirectoryEntrySize + 4,
                             info.resource_directory_size);
        }
        if (directory_count > kDirBaseReloc) {
            reader_.read_u32(directory_offset + kDirBaseReloc * kDataDirectoryEntrySize,
                             info.relocation_directory_rva);
            reader_.read_u32(directory_offset + kDirBaseReloc * kDataDirectoryEntrySize + 4,
                             info.relocation_directory_size);
        }
        if (directory_count > kDirDelayImport) {
            reader_.read_u32(directory_offset + kDirDelayImport * kDataDirectoryEntrySize,
                             info.delay_import_directory_rva);
            reader_.read_u32(directory_offset + kDirDelayImport * kDataDirectoryEntrySize + 4,
                             info.delay_import_directory_size);
        }

        for (std::size_t index = 0; index < section_count; ++index) {
            const std::size_t offset = section_table_offset + index * kSectionHeaderSize;
            SectionInfo section;
            std::array<std::byte, 8> name_bytes{};
            const std::optional<std::span<const std::byte>> raw_name =
                reader_.read_bytes(offset, 8);
            if (raw_name.has_value()) {
                std::copy_n(raw_name->begin(), 8, name_bytes.begin());
            }
            std::size_t name_length = 8;
            while (name_length > 0 && name_bytes[name_length - 1] == std::byte{0}) {
                --name_length;
            }
            section.name.assign(reinterpret_cast<const char*>(name_bytes.data()), name_length);
            reader_.read_u32(offset + 8, section.virtual_size);
            reader_.read_u32(offset + 12, section.virtual_address);
            reader_.read_u32(offset + 16, section.raw_data_size);
            reader_.read_u32(offset + 20, section.raw_data_pointer);
            reader_.read_u32(offset + 36, section.characteristics);

            const std::uint64_t section_span =
                std::max<std::uint64_t>(section.virtual_size, section.raw_data_size);
            const std::uint64_t section_end =
                static_cast<std::uint64_t>(section.virtual_address) + section_span;
            if (section_span != 0 &&
                (section.virtual_address < info.size_of_headers ||
                 section_end > info.size_of_image)) {
                return fail(ParseStatus::Malformed,
                            "seção " + section.name + " excede os limites da imagem");
            }

            if (section.raw_data_size > 0) {
                const std::uint64_t raw_end =
                    static_cast<std::uint64_t>(section.raw_data_pointer) + section.raw_data_size;
                if (raw_end > reader_.size()) {
                    return fail(ParseStatus::Malformed,
                                "seção " + section.name + ": dados crus excedem o arquivo");
                }
            }
            info.sections.push_back(std::move(section));
        }

        parser_state_ = std::move(info);
        if (auto error = parse_imports()) {
            return *error;
        }
        if (auto error = parse_delay_imports()) {
            return *error;
        }
        if (auto error = parse_relocations()) {
            return *error;
        }
        return {.status = ParseStatus::Success, .error_message = {}, .info = std::move(parser_state_)};
    }

private:
    [[nodiscard]] std::optional<std::size_t> rva_to_file_offset(
        const RvaRange& range) const {
        // Headers: RVA [0, SizeOfHeaders) mapeia direto para offset de arquivo [0, SizeOfHeaders)
        // Necessário para diretórios que o linker posiciona nos headers (legal em Windows).
        if (static_cast<std::uint64_t>(range.rva) + range.length <= parser_state_.size_of_headers &&
            static_cast<std::uint64_t>(range.rva) + range.length <= reader_.size()) {
            return static_cast<std::size_t>(range.rva);
        }
        for (const SectionInfo& section : parser_state_.sections) {
            const std::uint64_t section_span =
                std::max<std::uint64_t>(section.virtual_size, section.raw_data_size);
            const std::uint64_t section_end =
                static_cast<std::uint64_t>(section.virtual_address) + section_span;
            if (static_cast<std::uint64_t>(range.rva) >= section.virtual_address &&
                range.rva < section_end) {
                const std::uint64_t delta =
                    static_cast<std::uint64_t>(range.rva) - section.virtual_address;
                if (delta + range.length > section.raw_data_size) {
                    return std::nullopt;
                }
                const std::uint64_t file_offset =
                    static_cast<std::uint64_t>(section.raw_data_pointer) + delta;
                if (file_offset + range.length > reader_.size()) {
                    return std::nullopt;
                }
                return static_cast<std::size_t>(file_offset);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ParseResult> parse_imports() {
        if (parser_state_.import_directory_rva == 0 && parser_state_.import_directory_size == 0) {
            return std::nullopt;
        }
        if (parser_state_.import_directory_size < kImportDescriptorSize) {
            return fail(ParseStatus::Malformed,
                        "diretório de imports menor que um descritor (20 bytes)");
        }
        const std::optional<std::size_t> directory = rva_to_file_offset(
            {parser_state_.import_directory_rva, parser_state_.import_directory_size});
        if (!directory.has_value()) {
            return fail(ParseStatus::Malformed,
                        "diretório de imports em RVA " +
                            util::format_hex(parser_state_.import_directory_rva) +
                            " fora da imagem");
        }

        const std::size_t descriptor_limit =
            std::min<std::size_t>(parser_state_.import_directory_size / kImportDescriptorSize,
                                  kMaxImportDlls);
        bool descriptor_terminated = false;

        for (std::size_t descriptor_index = 0; descriptor_index < descriptor_limit;
             ++descriptor_index) {
            const std::size_t descriptor_offset =
                *directory + descriptor_index * kImportDescriptorSize;
            std::uint32_t original_first_thunk{};
            std::uint32_t name_rva{};
            std::uint32_t first_thunk{};
            reader_.read_u32(descriptor_offset, original_first_thunk);
            reader_.read_u32(descriptor_offset + 12, name_rva);
            reader_.read_u32(descriptor_offset + 16, first_thunk);

            if (original_first_thunk == 0 && name_rva == 0 && first_thunk == 0) {
                descriptor_terminated = true;
                break;
            }
            if (name_rva == 0) {
                return fail(ParseStatus::Malformed,
                            "descritor de import sem nome de DLL (índice " +
                                std::to_string(descriptor_index) + ")");
            }

            const std::optional<std::size_t> name_offset = rva_to_file_offset({name_rva, 1});
            if (!name_offset.has_value()) {
                return fail(ParseStatus::Malformed,
                            "nome de DLL em RVA " + util::format_hex(name_rva) + " fora da imagem");
            }
            const std::optional<std::string> dll_name = reader_.read_cstring(*name_offset);
            if (!dll_name.has_value()) {
                return fail(ParseStatus::Malformed,
                            "nome de DLL sem terminação nula (RVA " + util::format_hex(name_rva) + ")");
            }

            ImportedDll dll{.name = *dll_name, .symbols = {}};
            const std::uint32_t thunk_rva =
                original_first_thunk != 0 ? original_first_thunk : first_thunk;
            if (thunk_rva == 0 || first_thunk == 0) {
                return fail(ParseStatus::Malformed,
                            "descritor de import sem tabela de thunks (índice " +
                                std::to_string(descriptor_index) + ")");
            }
            const std::optional<std::size_t> thunk_table =
                rva_to_file_offset({thunk_rva, kThunkEntrySize});
            if (!thunk_table.has_value()) {
                return fail(ParseStatus::Malformed,
                            "tabela de thunks em RVA " + util::format_hex(thunk_rva) +
                                " fora da imagem");
            }
            bool thunk_terminated = false;
            for (std::size_t symbol_index = 0; symbol_index < kMaxSymbolsPerDll;
                     ++symbol_index) {
                    const std::size_t thunk_offset =
                        *thunk_table + symbol_index * kThunkEntrySize;
                    if (!reader_.has_range(thunk_offset, kThunkEntrySize)) {
                        break;
                    }
                    std::uint64_t thunk_value{};
                    reader_.read_u64(thunk_offset, thunk_value);
                    if (thunk_value == 0) {
                        thunk_terminated = true;
                        break;
                    }

                    ImportedSymbol symbol;
                    // first_thunk é uint32; o cálculo em uint64 evita o
                    // overflow que faria a IAT de um descritor hostil apontar
                    // para dentro dos cabeçalhos ou para fora da imagem.
                    const std::uint64_t iat_slot_rva =
                        static_cast<std::uint64_t>(first_thunk) +
                        static_cast<std::uint64_t>(symbol_index) * kThunkEntrySize;
                    if (iat_slot_rva > parser_state_.size_of_image ||
                        iat_slot_rva + kThunkEntrySize > parser_state_.size_of_image) {
                        return fail(ParseStatus::Malformed,
                                    "IAT em RVA " + util::format_hex(iat_slot_rva) +
                                        " fora da imagem (DLL " + dll.name + ")");
                    }
                    symbol.iat_rva = static_cast<std::uint32_t>(iat_slot_rva);
                    if ((thunk_value & kOrdinalFlag64) != 0) {
                        symbol.by_ordinal = true;
                        symbol.ordinal =
                            static_cast<std::uint16_t>(thunk_value & 0xFFFFULL);
                    } else {
                        const std::optional<std::size_t> by_name_offset = rva_to_file_offset(
                            {static_cast<std::uint32_t>(thunk_value), kImportByNameHintSize});
                        if (!by_name_offset.has_value()) {
                            return fail(ParseStatus::Malformed,
                                        "nome de símbolo em RVA " + util::format_hex(thunk_value) +
                                            " fora da imagem");
                        }
                        const std::optional<std::string> symbol_name = reader_.read_cstring(
                            *by_name_offset + kImportByNameHintSize);
                        if (!symbol_name.has_value()) {
                            return fail(ParseStatus::Malformed,
                                        "nome de símbolo sem terminação nula (RVA " +
                                            util::format_hex(thunk_value) + ")");
                        }
                        symbol.name = *symbol_name;
                    }
                    dll.symbols.push_back(std::move(symbol));
                }
            if (!thunk_terminated) {
                return fail(ParseStatus::Malformed,
                            "tabela de thunks sem terminador nulo (DLL " + dll.name + ")");
            }
            parser_state_.imports.push_back(std::move(dll));
        }
        if (!descriptor_terminated) {
            return fail(ParseStatus::Malformed,
                        "diretório de imports sem descritor terminador");
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ParseResult> parse_delay_imports() {
        if (parser_state_.delay_import_directory_rva == 0 &&
            parser_state_.delay_import_directory_size == 0) {
            return std::nullopt;
        }
        if (parser_state_.delay_import_directory_size < kDelayImportDescriptorSize) {
            return fail(ParseStatus::Malformed,
                        "diretório de delay imports menor que um descritor (32 bytes)");
        }
        const std::optional<std::size_t> directory = rva_to_file_offset(
            {parser_state_.delay_import_directory_rva, parser_state_.delay_import_directory_size});
        if (!directory.has_value()) {
            return fail(ParseStatus::Malformed,
                        "diretório de delay imports em RVA " +
                            util::format_hex(parser_state_.delay_import_directory_rva) +
                            " fora da imagem");
        }

        const std::size_t descriptor_limit =
            std::min<std::size_t>(parser_state_.delay_import_directory_size /
                                      kDelayImportDescriptorSize,
                                  kMaxImportDlls);
        bool descriptor_terminated = false;

        for (std::size_t descriptor_index = 0; descriptor_index < descriptor_limit;
             ++descriptor_index) {
            const std::size_t descriptor_offset =
                *directory + descriptor_index * kDelayImportDescriptorSize;
            std::uint32_t attributes{};
            std::uint32_t name_rva{};
            std::uint32_t module_handle_rva{};
            std::uint32_t iat_rva{};
            std::uint32_t int_rva{};
            std::uint32_t bound_iat_rva{};
            std::uint32_t unload_iat_rva{};
            std::uint32_t timestamp{};
            reader_.read_u32(descriptor_offset, attributes);
            reader_.read_u32(descriptor_offset + 4, name_rva);
            reader_.read_u32(descriptor_offset + 8, module_handle_rva);
            reader_.read_u32(descriptor_offset + 12, iat_rva);
            reader_.read_u32(descriptor_offset + 16, int_rva);
            reader_.read_u32(descriptor_offset + 20, bound_iat_rva);
            reader_.read_u32(descriptor_offset + 24, unload_iat_rva);
            reader_.read_u32(descriptor_offset + 28, timestamp);

            if (attributes == 0 && name_rva == 0 && module_handle_rva == 0 &&
                iat_rva == 0 && int_rva == 0 && bound_iat_rva == 0 &&
                unload_iat_rva == 0 && timestamp == 0) {
                descriptor_terminated = true;
                break;
            }
            if (attributes != kDelayImportAttrRva) {
                return fail(ParseStatus::UnsupportedMechanism,
                            "atributos de delay import não suportados (esperado 0x1, índice " +
                                std::to_string(descriptor_index) + ")");
            }
            if (name_rva == 0 || iat_rva == 0 || int_rva == 0) {
                return fail(ParseStatus::Malformed,
                            "descritor de delay import sem nome, INT ou IAT (índice " +
                                std::to_string(descriptor_index) + ")");
            }

            const std::optional<std::size_t> name_offset = rva_to_file_offset({name_rva, 1});
            if (!name_offset.has_value()) {
                return fail(ParseStatus::Malformed,
                            "nome de DLL de delay import em RVA " + util::format_hex(name_rva) +
                                " fora da imagem");
            }
            const std::optional<std::string> dll_name = reader_.read_cstring(*name_offset);
            if (!dll_name.has_value()) {
                return fail(ParseStatus::Malformed,
                            "nome de DLL de delay import sem terminação nula (RVA " +
                                util::format_hex(name_rva) + ")");
            }

            const std::optional<std::size_t> thunk_table =
                rva_to_file_offset({int_rva, kThunkEntrySize});
            if (!thunk_table.has_value()) {
                return fail(ParseStatus::Malformed,
                            "tabela INT de delay import em RVA " + util::format_hex(int_rva) +
                                " fora da imagem");
            }

            ImportedDll dll{.name = *dll_name, .symbols = {}};
            bool thunk_terminated = false;
            for (std::size_t symbol_index = 0; symbol_index < kMaxSymbolsPerDll;
                 ++symbol_index) {
                const std::size_t thunk_offset =
                    *thunk_table + symbol_index * kThunkEntrySize;
                if (!reader_.has_range(thunk_offset, kThunkEntrySize)) {
                    break;
                }
                std::uint64_t thunk_value{};
                reader_.read_u64(thunk_offset, thunk_value);
                if (thunk_value == 0) {
                    thunk_terminated = true;
                    break;
                }

                const std::uint64_t iat_slot_rva =
                    static_cast<std::uint64_t>(iat_rva) +
                    static_cast<std::uint64_t>(symbol_index) * kThunkEntrySize;
                if (iat_slot_rva > parser_state_.size_of_image ||
                    iat_slot_rva + kThunkEntrySize > parser_state_.size_of_image ||
                    !rva_to_file_offset({static_cast<std::uint32_t>(iat_slot_rva),
                                         kThunkEntrySize})
                         .has_value()) {
                    return fail(ParseStatus::Malformed,
                                "IAT de delay import em RVA " + util::format_hex(iat_slot_rva) +
                                    " fora da imagem (DLL " + dll.name + ")");
                }

                ImportedSymbol symbol;
                symbol.iat_rva = static_cast<std::uint32_t>(iat_slot_rva);
                if ((thunk_value & kOrdinalFlag64) != 0) {
                    symbol.by_ordinal = true;
                    symbol.ordinal = static_cast<std::uint16_t>(thunk_value & 0xFFFFULL);
                } else {
                    if (thunk_value > UINT32_MAX) {
                        return fail(ParseStatus::Malformed,
                                    "nome de símbolo de delay import em RVA " +
                                        util::format_hex(thunk_value) + " fora da imagem");
                    }
                    const std::optional<std::size_t> by_name_offset = rva_to_file_offset(
                        {static_cast<std::uint32_t>(thunk_value), kImportByNameHintSize});
                    if (!by_name_offset.has_value()) {
                        return fail(ParseStatus::Malformed,
                                    "nome de símbolo de delay import em RVA " +
                                        util::format_hex(thunk_value) + " fora da imagem");
                    }
                    const std::optional<std::string> symbol_name = reader_.read_cstring(
                        *by_name_offset + kImportByNameHintSize);
                    if (!symbol_name.has_value()) {
                        return fail(ParseStatus::Malformed,
                                    "nome de símbolo de delay import sem terminação nula (RVA " +
                                        util::format_hex(thunk_value) + ")");
                    }
                    symbol.name = *symbol_name;
                }
                dll.symbols.push_back(std::move(symbol));
            }
            if (!thunk_terminated) {
                return fail(ParseStatus::Malformed,
                            "tabela INT de delay import sem terminador nulo (DLL " + dll.name +
                                ")");
            }
            parser_state_.delay_imports.push_back(std::move(dll));
        }
        if (!descriptor_terminated) {
            return fail(ParseStatus::Malformed,
                        "diretório de delay imports sem descritor terminador");
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ParseResult> parse_relocations() {
        if (parser_state_.relocation_directory_rva == 0 &&
            parser_state_.relocation_directory_size == 0) {
            return std::nullopt;
        }
        if (parser_state_.relocation_directory_size < kBaseRelocBlockHeaderSize) {
            return fail(ParseStatus::Malformed,
                        "diretório de relocations menor que o cabeçalho de bloco (8 bytes)");
        }
        const std::optional<std::size_t> directory = rva_to_file_offset(
            {parser_state_.relocation_directory_rva, parser_state_.relocation_directory_size});
        if (!directory.has_value()) {
            return fail(ParseStatus::Malformed,
                        "diretório de relocations em RVA " +
                            util::format_hex(parser_state_.relocation_directory_rva) +
                            " fora da imagem");
        }

        std::size_t consumed = 0;
        std::size_t block_count = 0;
        while (consumed + kBaseRelocBlockHeaderSize <= parser_state_.relocation_directory_size) {
            if (block_count >= kMaxRelocBlocks) {
                return fail(ParseStatus::Malformed,
                            "número excessivo de blocos de relocations");
            }
            const std::size_t block_offset = *directory + consumed;
            std::uint32_t page_rva{};
            std::uint32_t block_size{};
            reader_.read_u32(block_offset, page_rva);
            reader_.read_u32(block_offset + 4, block_size);

            if (block_size < kBaseRelocBlockHeaderSize) {
                return fail(ParseStatus::Malformed,
                            "bloco de relocations com tamanho inválido (" +
                                std::to_string(block_size) + " bytes)");
            }
            if (static_cast<std::uint64_t>(block_size) > parser_state_.relocation_directory_size - consumed) {
                return fail(ParseStatus::Malformed,
                            "bloco de relocations excede o diretório");
            }
            const std::size_t entry_bytes = static_cast<std::size_t>(block_size) - kBaseRelocBlockHeaderSize;
            if (entry_bytes % kBaseRelocEntrySize != 0) {
                return fail(ParseStatus::Malformed,
                            "bloco de relocations com entradas truncadas");
            }

            BaseRelocBlock block{.page_rva = page_rva, .entries = {}};
            for (std::size_t entry_index = 0; entry_index < entry_bytes / kBaseRelocEntrySize;
                 ++entry_index) {
                std::uint16_t raw_entry{};
                reader_.read_u16(block_offset + kBaseRelocBlockHeaderSize + entry_index * kBaseRelocEntrySize,
                                 raw_entry);
                BaseRelocEntry entry;
                entry.type = static_cast<std::uint16_t>((raw_entry >> 12) & 0xF);
                entry.offset = static_cast<std::uint16_t>(raw_entry & 0xFFF);
                block.entries.push_back(entry);
            }
            parser_state_.relocations.push_back(std::move(block));
            consumed += static_cast<std::size_t>(block_size);
            ++block_count;
        }

        if (consumed != parser_state_.relocation_directory_size) {
            return fail(ParseStatus::Malformed,
                        "diretório de relocations com bytes residuais");
        }
        return std::nullopt;
    }

    ByteReader reader_;
    PeInfo parser_state_;
};

}  // namespace

ParseResult parse_pe(const std::span<const std::byte> data) {
    PeParser parser{data};
    return parser.run();
}

}  // namespace tradutorlinux::pe
