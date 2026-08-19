#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace tradutorlinux::loader {

enum class DllOverrideMode {
    BuiltinFirst,  // Usa a implementação embutida do runtime primeiro (padrão)
    NativeFirst,   // Tenta carregar o arquivo .dll do disco primeiro; se falhar, usa builtin
    BuiltinOnly,   // Força apenas a implementação embutida
    NativeOnly     // Força apenas a DLL nativa do disco
};

// Define o modo de override para uma DLL específica (ex: "msvcp140.dll", DllOverrideMode::NativeFirst)
void set_dll_override(std::string_view dll_name, DllOverrideMode mode) noexcept;

// Obtém o modo de override configurado para a DLL
[[nodiscard]] DllOverrideMode get_dll_override(std::string_view dll_name) noexcept;

// Carrega as configurações de override a partir da variável de ambiente TL_DLL_OVERRIDES
// Formato: "msvcp140=n,d3dcompiler=n,kernel32=b" (n = native, b = builtin)
void load_dll_overrides_from_env() noexcept;

}  // namespace tradutorlinux::loader
