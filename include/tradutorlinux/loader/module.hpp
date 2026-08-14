#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>

namespace tradutorlinux::loader {

struct ExportedFunction {
    std::string_view name;
    std::uint16_t ordinal{};
    std::uintptr_t address{};
};

struct InternalModule {
    std::string_view name;
    std::span<const ExportedFunction> exports;
};

struct ExportLookup {
    bool found{};
    std::uint16_t ordinal{};
    std::uintptr_t address{};
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

std::size_t registered_module_count();

}  // namespace tradutorlinux::loader
