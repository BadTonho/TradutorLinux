#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::pe {

enum class ParseStatus {
    Success,
    Truncated,
    Malformed,
    UnsupportedArchitecture,
    UnsupportedFormat,
    UnsupportedMechanism,
};

struct SectionInfo {
    std::string name;
    std::uint32_t virtual_address{};
    std::uint32_t virtual_size{};
    std::uint32_t raw_data_pointer{};
    std::uint32_t raw_data_size{};
    std::uint32_t characteristics{};
};

struct ImportedSymbol {
    bool by_ordinal{};
    std::uint16_t ordinal{};
    std::string name;
    std::uint32_t iat_rva{};
};

struct ImportedDll {
    std::string name;
    std::vector<ImportedSymbol> symbols;
};

struct BaseRelocEntry {
    std::uint16_t type{};
    std::uint16_t offset{};
};

struct BaseRelocBlock {
    std::uint32_t page_rva{};
    std::vector<BaseRelocEntry> entries;
};

// IMAGE_UNWIND_INFO / UNWIND_CODE para AMD64. `operand` já está expandido em
// bytes para as operações SAVE/ALLOC que carregam um operando adicional.
enum class UnwindOperation : std::uint8_t {
    PushNonVol = 0,
    AllocLarge = 1,
    AllocSmall = 2,
    SetFpReg = 3,
    SaveNonVol = 4,
    SaveNonVolFar = 5,
    SaveXmm128 = 8,
    SaveXmm128Far = 9,
    PushMachFrame = 10,
};

struct UnwindCode {
    std::uint8_t code_offset{};
    UnwindOperation operation{UnwindOperation::PushNonVol};
    std::uint8_t operation_info{};
    std::uint32_t operand{};
};

// Intervalo [begin_rva, end_rva) de um epílogo descrito por UNWIND_INFO V2.
// Os intervalos são normalizados em ordem crescente de RVA pelo leitor.
struct UnwindEpilog {
    std::uint32_t begin_rva{};
    std::uint32_t end_rva{};
};

struct UnwindInfo {
    std::uint8_t version{};
    std::uint8_t flags{};
    std::uint8_t prolog_size{};
    std::uint8_t frame_register{};
    std::uint8_t frame_offset{};
    std::vector<UnwindCode> codes;
    std::vector<UnwindEpilog> epilogs;
    // Algumas imagens geradas no ecossistema Windows repetem FrameOffset em
    // OpInfo de UWOP_SET_FPREG. O leitor só aceita essa forma quando os dois
    // valores coincidem e a marca para diagnóstico.
    bool has_extended_set_fpreg{};
    std::uint32_t handler_rva{};
    std::uint32_t handler_data_rva{};
    bool has_chained_function{};
    std::uint32_t chained_begin_rva{};
    std::uint32_t chained_end_rva{};
    std::uint32_t chained_unwind_info_rva{};
};

// IMAGE_RUNTIME_FUNCTION_ENTRY no diretório IMAGE_DIRECTORY_ENTRY_EXCEPTION.
struct RuntimeFunction {
    std::uint32_t begin_rva{};
    std::uint32_t end_rva{};
    std::uint32_t unwind_info_rva{};
    UnwindInfo unwind;
};

struct TlsDirectoryInfo {
    std::uint64_t start_address_of_raw_data{};
    std::uint64_t end_address_of_raw_data{};
    std::uint64_t address_of_index{};
    std::uint64_t address_of_callbacks{};
    std::uint32_t size_of_zero_fill{};
    std::uint32_t characteristics{};
    std::vector<std::uint64_t> callback_vas;
};

struct PeInfo {
    bool is_pe32_plus{};
    std::uint16_t machine{};
    std::uint16_t number_of_sections{};
    std::uint32_t address_of_entry_point{};
    std::uint64_t image_base{};
    std::uint32_t section_alignment{};
    std::uint32_t size_of_image{};
    std::uint32_t size_of_headers{};
    std::uint16_t subsystem{};
    std::uint32_t import_directory_rva{};
    std::uint32_t import_directory_size{};
    std::uint32_t resource_directory_rva{};
    std::uint32_t resource_directory_size{};
    std::uint32_t exception_directory_rva{};
    std::uint32_t exception_directory_size{};
    std::uint32_t relocation_directory_rva{};
    std::uint32_t relocation_directory_size{};
    std::uint32_t delay_import_directory_rva{};
    std::uint32_t delay_import_directory_size{};
    std::uint32_t tls_directory_rva{};
    std::uint32_t tls_directory_size{};
    TlsDirectoryInfo tls_info;
    std::vector<SectionInfo> sections;
    std::vector<ImportedDll> imports;
    // Imports descritos por IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT. Eles mantêm
    // o mesmo contrato de símbolo/IAT dos imports estáticos, mas são
    // identificados separadamente pelo resolvedor e pelo diagnóstico.
    std::vector<ImportedDll> delay_imports;
    std::vector<RuntimeFunction> runtime_functions;
    std::vector<BaseRelocBlock> relocations;
};

struct ParseResult {
    ParseStatus status{ParseStatus::Success};
    std::string error_message;
    PeInfo info;
};

[[nodiscard]] ParseResult parse_pe(std::span<const std::byte> data);

}  // namespace tradutorlinux::pe
