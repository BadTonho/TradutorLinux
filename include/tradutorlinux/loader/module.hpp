#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace tradutorlinux::loader {

// Descreve o contrato comportamental de uma exportação. Resolver o endereço
// de uma função é uma operação diferente de garantir que sua semântica esteja
// implementada; o relatório usa este nível para não confundir as duas coisas.
enum class ExportSupport : std::uint8_t {
    Full,
    Limited,
    Stub,
};

struct ExportedFunction {
    constexpr ExportedFunction(std::string_view export_name, const std::uint16_t export_ordinal,
                               const std::uintptr_t export_address, const ExportSupport export_support,
                               std::string_view export_forwarder = {}) noexcept
        : name(export_name),
          ordinal(export_ordinal),
          address(export_address),
          support(export_support),
          forwarder(export_forwarder) {}

    std::string_view name;
    std::uint16_t ordinal{};
    std::uintptr_t address{};
    ExportSupport support;
    // Texto de um export forwarder no formato "DLL.Simbolo" ou
    // "DLL.#ordinal". Quando preenchido, address deve ser zero.
    std::string_view forwarder{};
};

struct InternalModule {
    std::string_view name;
    std::span<const ExportedFunction> exports;
};

struct ExportLookup {
    bool found{};
    std::uint16_t ordinal{};
    std::uintptr_t address{};
    ExportSupport support{ExportSupport::Full};
    std::string detail;
    std::string forwarder;
};

struct ExportQuery {
    std::string_view dll;
    std::string_view symbol;
};

// Registra um módulo interno. Cópias são feitas dos nomes. Retorna false se um
// módulo com o mesmo nome (case-insensitive) já estiver registrado.
bool register_module(const InternalModule& module);

// Remove todos os módulos registrados. Usado em testes.
void clear_modules();

// Registra os módulos internos embutidos (KERNEL32.dll). Idempotente.
void register_builtin_modules();

bool is_module_registered(std::string_view dll);

ExportLookup find_export(const ExportQuery& query);
ExportLookup find_export_by_ordinal(std::string_view dll, std::uint16_t ordinal);
ExportLookup find_export_global(std::string_view symbol);
ExportLookup find_export_by_ordinal_global(std::uint16_t ordinal);

bool is_valid_module_handle(void* handle) noexcept;

// API Sets e exports forwarders são resolvidos separadamente: um API Set é um
// alias de módulo conhecido pelo runtime; um forwarder é uma cadeia explícita
// registrada como "DLL.Simbolo" ou "DLL.#ordinal". A cadeia possui limite de
// profundidade e rejeita ciclos/destinos ausentes.
bool is_api_set_dll(std::string_view dll) noexcept;
bool is_kernelbase_dll(std::string_view dll) noexcept;
ExportLookup find_export_forwarded(const ExportQuery& query);
ExportLookup find_export_by_ordinal_forwarded(std::string_view dll, std::uint16_t ordinal);
bool is_module_registered_forwarded(std::string_view dll) noexcept;

std::size_t registered_module_count();

}  // namespace tradutorlinux::loader
