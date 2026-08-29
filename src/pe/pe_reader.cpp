#include "tradutorlinux/pe/pe_reader.hpp"

#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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
constexpr std::size_t kRuntimeFunctionSize = 12;
constexpr std::size_t kUnwindInfoHeaderSize = 4;
constexpr std::size_t kUnwindCodeSize = 2;
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
constexpr std::size_t kDirException = 3;
constexpr std::size_t kDirBaseReloc = 5;
constexpr std::size_t kDirDelayImport = 13;

constexpr std::size_t kMaxImportDlls = 1024;
constexpr std::size_t kMaxSymbolsPerDll = 4096;
constexpr std::size_t kMaxRelocBlocks = 4096;
constexpr std::size_t kMaxRuntimeFunctions = 65536;
constexpr std::size_t kMaxCString = 65535;

constexpr std::uint8_t kUnwindVersion1 = 1;
constexpr std::uint8_t kUnwindVersion2 = 2;
constexpr std::uint8_t kUnwindFlagEHandler = 0x1;
constexpr std::uint8_t kUnwindFlagUHandler = 0x2;
constexpr std::uint8_t kUnwindFlagChainInfo = 0x4;
constexpr std::uint8_t kUnwindKnownFlags = kUnwindFlagEHandler | kUnwindFlagUHandler |
                                           kUnwindFlagChainInfo;
constexpr std::uint8_t kUnwindOpEpilog = 6;
constexpr std::uint8_t kUnwindV2EpilogAtFunctionEnd = 0x1;

[[nodiscard]] constexpr bool is_valid_gpr(const std::uint8_t register_number) {
    return register_number <= 15 && register_number != 4;
}

[[nodiscard]] constexpr bool is_nonvolatile_register(const std::uint8_t register_number) {
    return register_number == 3 || register_number == 5 || register_number == 6 ||
           register_number == 7 || (register_number >= 12 && register_number <= 15);
}

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
        if (directory_count > kDirException) {
            reader_.read_u32(directory_offset + kDirException * kDataDirectoryEntrySize,
                             info.exception_directory_rva);
            reader_.read_u32(directory_offset + kDirException * kDataDirectoryEntrySize + 4,
                             info.exception_directory_size);
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
        if (auto error = parse_runtime_functions()) {
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
                        if (thunk_value > UINT32_MAX) {
                            return fail(ParseStatus::Malformed,
                                        "nome de símbolo de import em RVA " +
                                            util::format_hex(thunk_value) + " fora da imagem");
                        }
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

    [[nodiscard]] std::optional<ParseResult> parse_unwind_info(
        const std::uint32_t unwind_rva, const std::uint32_t function_begin_rva,
        const std::uint32_t function_end_rva, UnwindInfo& unwind) {
        if ((unwind_rva & 0x3U) != 0U) {
            return fail(ParseStatus::Malformed,
                        "UNWIND_INFO sem alinhamento de 4 bytes em RVA " +
                            util::format_hex(unwind_rva));
        }
        const std::optional<std::size_t> header =
            rva_to_file_offset({unwind_rva, kUnwindInfoHeaderSize});
        if (!header.has_value()) {
            return fail(ParseStatus::Malformed,
                        "UNWIND_INFO fora da imagem em RVA " + util::format_hex(unwind_rva));
        }

        std::uint32_t header_word{};
        reader_.read_u32(*header, header_word);
        unwind.version = static_cast<std::uint8_t>(header_word & 0x7U);
        unwind.flags = static_cast<std::uint8_t>((header_word >> 3U) & 0x1FU);
        unwind.prolog_size = static_cast<std::uint8_t>((header_word >> 8U) & 0xFFU);
        const std::uint8_t code_count = static_cast<std::uint8_t>((header_word >> 16U) & 0xFFU);
        unwind.frame_register = static_cast<std::uint8_t>((header_word >> 24U) & 0x0FU);
        unwind.frame_offset = static_cast<std::uint8_t>((header_word >> 28U) & 0x0FU);

        if (unwind.version != kUnwindVersion1 && unwind.version != kUnwindVersion2) {
            return fail(ParseStatus::UnsupportedMechanism,
                        "versão de UNWIND_INFO " + std::to_string(unwind.version) +
                            " não suportada em RVA " + util::format_hex(unwind_rva) +
                            " (esperado 1 ou 2)");
        }
        if ((unwind.flags & ~kUnwindKnownFlags) != 0U) {
            return fail(ParseStatus::UnsupportedMechanism,
                        "flags de UNWIND_INFO não suportadas: " +
                            util::format_hex(unwind.flags));
        }

        const std::uint64_t code_bytes = static_cast<std::uint64_t>(code_count) * kUnwindCodeSize;
        const std::uint64_t code_end = static_cast<std::uint64_t>(unwind_rva) +
                                       kUnwindInfoHeaderSize + code_bytes;
        if (code_end > parser_state_.size_of_image ||
            code_end > std::numeric_limits<std::uint32_t>::max()) {
            return fail(ParseStatus::Malformed, "códigos de UNWIND_INFO excedem a imagem");
        }
        const std::optional<std::size_t> codes = rva_to_file_offset(
            {unwind_rva + static_cast<std::uint32_t>(kUnwindInfoHeaderSize),
             static_cast<std::uint32_t>(code_bytes)});
        if (!codes.has_value() && code_count != 0U) {
            return fail(ParseStatus::Malformed, "códigos de UNWIND_INFO truncados");
        }

        const auto read_raw_code = [&](const std::size_t slot, std::uint16_t& raw_code) {
            return reader_.read_u16(*codes + slot * kUnwindCodeSize, raw_code);
        };
        const auto raw_operation = [](const std::uint16_t raw_code) {
            return static_cast<std::uint8_t>((raw_code >> 8U) & 0x0FU);
        };
        const auto code_offset = [](const std::uint16_t raw_code) {
            return static_cast<std::uint8_t>(raw_code & 0xFFU);
        };
        const auto operation_info = [](const std::uint16_t raw_code) {
            return static_cast<std::uint8_t>((raw_code >> 12U) & 0x0FU);
        };

        std::size_t slot = 0;
        if (unwind.version == kUnwindVersion2 && code_count != 0U) {
            std::uint16_t first_descriptor{};
            read_raw_code(slot, first_descriptor);
            if (raw_operation(first_descriptor) == kUnwindOpEpilog) {
                const std::uint8_t epilog_size = code_offset(first_descriptor);
                const std::uint8_t epilog_flags = operation_info(first_descriptor);
                if (epilog_size == 0U) {
                    return fail(ParseStatus::Malformed,
                                "descritor de epílogo V2 com tamanho zero em RVA " +
                                    util::format_hex(unwind_rva));
                }
                if ((epilog_flags & ~kUnwindV2EpilogAtFunctionEnd) != 0U) {
                    return fail(ParseStatus::UnsupportedMechanism,
                                "flags de descritor de epílogo V2 não suportadas em RVA " +
                                    util::format_hex(unwind_rva));
                }

                const auto append_epilog = [&](const std::uint32_t distance_from_end)
                    -> std::optional<ParseResult> {
                    const std::uint32_t function_size = function_end_rva - function_begin_rva;
                    if (distance_from_end < epilog_size || distance_from_end > function_size) {
                        return fail(ParseStatus::Malformed,
                                    "epílogo V2 fora do intervalo da RUNTIME_FUNCTION em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                    const std::uint32_t begin_rva = function_end_rva - distance_from_end;
                    const std::uint64_t end_rva = static_cast<std::uint64_t>(begin_rva) + epilog_size;
                    if (end_rva > function_end_rva) {
                        return fail(ParseStatus::Malformed,
                                    "epílogo V2 excede a RUNTIME_FUNCTION em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                    unwind.epilogs.push_back(
                        {.begin_rva = begin_rva, .end_rva = static_cast<std::uint32_t>(end_rva)});
                    return std::nullopt;
                };

                ++slot;
                if ((epilog_flags & kUnwindV2EpilogAtFunctionEnd) != 0U) {
                    if (auto error = append_epilog(epilog_size)) {
                        return error;
                    }
                } else {
                    if (slot == code_count) {
                        return fail(ParseStatus::Malformed,
                                    "descritor de epílogo V2 sem offset em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                    std::uint16_t offset_descriptor{};
                    read_raw_code(slot, offset_descriptor);
                    if (raw_operation(offset_descriptor) != kUnwindOpEpilog) {
                        return fail(ParseStatus::Malformed,
                                    "offset de epílogo V2 sem UOP_Epilog em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                    const std::uint32_t distance_from_end =
                        (static_cast<std::uint32_t>(operation_info(offset_descriptor)) << 8U) |
                        code_offset(offset_descriptor);
                    if (distance_from_end == 0U) {
                        return fail(ParseStatus::Malformed,
                                    "offset de epílogo V2 nulo em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                    if (auto error = append_epilog(distance_from_end)) {
                        return error;
                    }
                    ++slot;
                }

                while (slot < code_count) {
                    std::uint16_t offset_descriptor{};
                    read_raw_code(slot, offset_descriptor);
                    if (raw_operation(offset_descriptor) != kUnwindOpEpilog) {
                        break;
                    }
                    const std::uint32_t distance_from_end =
                        (static_cast<std::uint32_t>(operation_info(offset_descriptor)) << 8U) |
                        code_offset(offset_descriptor);
                    if (distance_from_end == 0U) {
                        ++slot;
                        break;
                    }
                    if (auto error = append_epilog(distance_from_end)) {
                        return error;
                    }
                    ++slot;
                }

                std::sort(unwind.epilogs.begin(), unwind.epilogs.end(),
                          [](const UnwindEpilog& left, const UnwindEpilog& right) {
                              return left.begin_rva < right.begin_rva;
                          });
                for (std::size_t index = 1; index < unwind.epilogs.size(); ++index) {
                    if (unwind.epilogs[index].begin_rva < unwind.epilogs[index - 1U].end_rva) {
                        return fail(ParseStatus::Malformed,
                                    "epílogos V2 sobrepostos em RVA " +
                                        util::format_hex(unwind_rva));
                    }
                }
            }
        }

        std::uint8_t previous_code_offset = 0;
        bool has_previous_code = false;
        for (; slot < code_count;) {
            std::uint16_t raw_code{};
            read_raw_code(slot, raw_code);
            UnwindCode code;
            code.code_offset = code_offset(raw_code);
            const std::uint8_t operation = raw_operation(raw_code);
            code.operation_info = operation_info(raw_code);
            if (has_previous_code && code.code_offset > previous_code_offset) {
                return fail(ParseStatus::Malformed,
                            "códigos de UNWIND_INFO fora de ordem decrescente");
            }
            previous_code_offset = code.code_offset;
            has_previous_code = true;

            std::size_t extra_slots = 0;
            switch (operation) {
                case 0:
                    code.operation = UnwindOperation::PushNonVol;
                    if (!is_valid_gpr(code.operation_info)) {
                        return fail(ParseStatus::Malformed,
                                    "UWOP_PUSH_NONVOL usa registrador inválido");
                    }
                    break;
                case 1:
                    code.operation = UnwindOperation::AllocLarge;
                    if (code.operation_info == 0U) {
                        extra_slots = 1;
                    } else if (code.operation_info == 1U) {
                        extra_slots = 2;
                    } else {
                        return fail(ParseStatus::Malformed, "UWOP_ALLOC_LARGE com OpInfo inválido");
                    }
                    break;
                case 2:
                    code.operation = UnwindOperation::AllocSmall;
                    code.operand = static_cast<std::uint32_t>(code.operation_info) * 8U + 8U;
                    break;
                case 3:
                    code.operation = UnwindOperation::SetFpReg;
                    if (code.operation_info != 0U) {
                        unwind.has_extended_set_fpreg = true;
                    }
                    if (!is_valid_gpr(unwind.frame_register)) {
                        return fail(ParseStatus::Malformed,
                                    "UWOP_SET_FPREG inválido em RVA " +
                                        util::format_hex(unwind_rva) +
                                        " (FrameRegister=" +
                                        std::to_string(unwind.frame_register) + ")");
                    }
                    break;
                case 4:
                    code.operation = UnwindOperation::SaveNonVol;
                    if (!is_valid_gpr(code.operation_info)) {
                        return fail(ParseStatus::Malformed,
                                    "UWOP_SAVE_NONVOL usa registrador inválido");
                    }
                    extra_slots = 1;
                    break;
                case 5:
                    code.operation = UnwindOperation::SaveNonVolFar;
                    if (!is_valid_gpr(code.operation_info)) {
                        return fail(ParseStatus::Malformed,
                                    "UWOP_SAVE_NONVOL_FAR usa registrador inválido");
                    }
                    extra_slots = 2;
                    break;
                case 8:
                    code.operation = UnwindOperation::SaveXmm128;
                    extra_slots = 1;
                    break;
                case 9:
                    code.operation = UnwindOperation::SaveXmm128Far;
                    extra_slots = 2;
                    break;
                case 10:
                    code.operation = UnwindOperation::PushMachFrame;
                    if (code.operation_info > 1U) {
                        return fail(ParseStatus::Malformed,
                                    "UWOP_PUSH_MACHFRAME com OpInfo inválido");
                    }
                    break;
                default:
                    return fail(ParseStatus::UnsupportedMechanism,
                                "operação de UNWIND_INFO não suportada: " +
                                    std::to_string(operation) + " em RVA " +
                                    util::format_hex(unwind_rva));
            }
            if (extra_slots > static_cast<std::size_t>(code_count) - slot - 1U) {
                return fail(ParseStatus::Malformed, "operando de UNWIND_INFO truncado");
            }
            if (extra_slots == 1U) {
                std::uint16_t value{};
                reader_.read_u16(*codes + (slot + 1U) * kUnwindCodeSize, value);
                switch (code.operation) {
                    case UnwindOperation::AllocLarge:
                    case UnwindOperation::SaveNonVol:
                        code.operand = static_cast<std::uint32_t>(value) * 8U;
                        break;
                    case UnwindOperation::SaveXmm128:
                        code.operand = static_cast<std::uint32_t>(value) * 16U;
                        break;
                    default:
                        break;
                }
            } else if (extra_slots == 2U) {
                std::uint32_t value{};
                reader_.read_u32(*codes + (slot + 1U) * kUnwindCodeSize, value);
                code.operand = value;
                if (code.operation == UnwindOperation::SaveXmm128Far &&
                    (code.operand & 0xFU) != 0U) {
                    return fail(ParseStatus::Malformed,
                                "UWOP_SAVE_XMM128_FAR com offset não alinhado");
                }
            }
            unwind.codes.push_back(code);
            slot += extra_slots + 1U;
        }

        const std::uint64_t aligned_tail = (code_end + 3U) & ~std::uint64_t{3};
        if (aligned_tail > parser_state_.size_of_image ||
            aligned_tail > std::numeric_limits<std::uint32_t>::max()) {
            return fail(ParseStatus::Malformed, "cauda de UNWIND_INFO excede a imagem");
        }
        const std::uint32_t tail_rva = static_cast<std::uint32_t>(aligned_tail);
        if ((unwind.flags & kUnwindFlagChainInfo) != 0U) {
            if ((unwind.flags & (kUnwindFlagEHandler | kUnwindFlagUHandler)) != 0U) {
                return fail(ParseStatus::Malformed,
                            "UNWIND_INFO não pode combinar handler e CHAININFO");
            }
            const std::optional<std::size_t> chain = rva_to_file_offset({tail_rva, kRuntimeFunctionSize});
            if (!chain.has_value()) {
                return fail(ParseStatus::Malformed, "RUNTIME_FUNCTION encadeada truncada");
            }
            reader_.read_u32(*chain, unwind.chained_begin_rva);
            reader_.read_u32(*chain + 4, unwind.chained_end_rva);
            reader_.read_u32(*chain + 8, unwind.chained_unwind_info_rva);
            unwind.has_chained_function = true;
        } else if ((unwind.flags & (kUnwindFlagEHandler | kUnwindFlagUHandler)) != 0U) {
            const std::optional<std::size_t> handler = rva_to_file_offset({tail_rva, 4});
            if (!handler.has_value()) {
                return fail(ParseStatus::Malformed, "handler de UNWIND_INFO truncado");
            }
            reader_.read_u32(*handler, unwind.handler_rva);
            if (unwind.handler_rva == 0U ||
                !rva_to_file_offset({unwind.handler_rva, 1}).has_value()) {
                return fail(ParseStatus::Malformed, "handler de UNWIND_INFO fora da imagem");
            }
            const std::uint64_t data_rva = static_cast<std::uint64_t>(tail_rva) + 4U;
            if (data_rva > parser_state_.size_of_image) {
                return fail(ParseStatus::Malformed, "dados de handler fora da imagem");
            }
            unwind.handler_data_rva = static_cast<std::uint32_t>(data_rva);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ParseResult> parse_runtime_functions() {
        if (parser_state_.exception_directory_rva == 0U &&
            parser_state_.exception_directory_size == 0U) {
            return std::nullopt;
        }
        if (parser_state_.exception_directory_rva == 0U ||
            parser_state_.exception_directory_size == 0U ||
            parser_state_.exception_directory_size % kRuntimeFunctionSize != 0U) {
            return fail(ParseStatus::Malformed,
                        "diretório de exceções deve conter RUNTIME_FUNCTIONs completos");
        }
        const std::optional<std::size_t> directory = rva_to_file_offset(
            {parser_state_.exception_directory_rva, parser_state_.exception_directory_size});
        if (!directory.has_value()) {
            bool in_virtual_section = false;
            for (const auto& sec : parser_state_.sections) {
                if (parser_state_.exception_directory_rva >= sec.virtual_address &&
                    parser_state_.exception_directory_rva + parser_state_.exception_directory_size <=
                        sec.virtual_address + sec.virtual_size) {
                    in_virtual_section = true;
                    break;
                }
            }
            if (in_virtual_section) {
                return std::nullopt;
            }
            return fail(ParseStatus::Malformed, "diretório de exceções fora da imagem (RVA " +
                        util::format_hex(parser_state_.exception_directory_rva) + " size " +
                        util::format_hex(parser_state_.exception_directory_size) + ")");
        }
        const std::size_t count = parser_state_.exception_directory_size / kRuntimeFunctionSize;
        if (count > kMaxRuntimeFunctions) {
            return fail(ParseStatus::Malformed, "número excessivo de RUNTIME_FUNCTIONs");
        }
        std::uint32_t previous_end = 0;
        bool has_previous = false;
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t offset = *directory + index * kRuntimeFunctionSize;
            RuntimeFunction function;
            reader_.read_u32(offset, function.begin_rva);
            reader_.read_u32(offset + 4, function.end_rva);
            reader_.read_u32(offset + 8, function.unwind_info_rva);
            if (function.begin_rva >= function.end_rva || function.end_rva > parser_state_.size_of_image) {
                return fail(ParseStatus::Malformed, "RUNTIME_FUNCTION com intervalo inválido");
            }
            if (has_previous && function.begin_rva < previous_end) {
                return fail(ParseStatus::Malformed,
                            "RUNTIME_FUNCTIONs fora de ordem ou sobrepostos");
            }
            if (auto error = parse_unwind_info(function.unwind_info_rva, function.begin_rva,
                                               function.end_rva, function.unwind)) {
                return error;
            }
            previous_end = function.end_rva;
            has_previous = true;
            parser_state_.runtime_functions.push_back(std::move(function));
        }

        for (const RuntimeFunction& function : parser_state_.runtime_functions) {
            if (!function.unwind.has_chained_function) {
                continue;
            }
            const auto chained = std::find_if(
                parser_state_.runtime_functions.begin(), parser_state_.runtime_functions.end(),
                [&function](const RuntimeFunction& candidate) {
                    return candidate.begin_rva == function.unwind.chained_begin_rva &&
                           candidate.end_rva == function.unwind.chained_end_rva &&
                           candidate.unwind_info_rva == function.unwind.chained_unwind_info_rva;
                });
            if (chained == parser_state_.runtime_functions.end()) {
                return fail(ParseStatus::Malformed,
                            "CHAININFO não referencia uma RUNTIME_FUNCTION da tabela");
            }
        }
        for (std::size_t start = 0; start < parser_state_.runtime_functions.size(); ++start) {
            std::size_t current = start;
            for (std::size_t depth = 0; depth <= parser_state_.runtime_functions.size(); ++depth) {
                const RuntimeFunction& function = parser_state_.runtime_functions[current];
                if (!function.unwind.has_chained_function) {
                    break;
                }
                const auto chained = std::find_if(
                    parser_state_.runtime_functions.begin(), parser_state_.runtime_functions.end(),
                    [&function](const RuntimeFunction& candidate) {
                        return candidate.begin_rva == function.unwind.chained_begin_rva &&
                               candidate.end_rva == function.unwind.chained_end_rva &&
                               candidate.unwind_info_rva == function.unwind.chained_unwind_info_rva;
                    });
                current = static_cast<std::size_t>(
                    std::distance(parser_state_.runtime_functions.begin(), chained));
                if (depth == parser_state_.runtime_functions.size()) {
                    return fail(ParseStatus::Malformed, "ciclo em CHAININFO de UNWIND_INFO");
                }
            }
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
