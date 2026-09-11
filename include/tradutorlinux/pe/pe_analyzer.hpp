#pragma once

#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::pe {

// Constantes padrão PE DllCharacteristics
inline constexpr std::uint16_t kDllCharHighEntropyVa = 0x0020;
inline constexpr std::uint16_t kDllCharDynamicBase   = 0x0040;
inline constexpr std::uint16_t kDllCharForceIntegrity= 0x0080;
inline constexpr std::uint16_t kDllCharNxCompat      = 0x0100;
inline constexpr std::uint16_t kDllCharNoIsolation   = 0x0200;
inline constexpr std::uint16_t kDllCharNoSeh         = 0x0400;
inline constexpr std::uint16_t kDllCharNoBind        = 0x0800;
inline constexpr std::uint16_t kDllCharAppContainer  = 0x1000;
inline constexpr std::uint16_t kDllCharWdmDriver     = 0x2000;
inline constexpr std::uint16_t kDllCharGuardCf       = 0x4000;
inline constexpr std::uint16_t kDllCharTerminalServer= 0x8000;

struct MitigationInfo {
    bool has_mitigations{false};
    std::uint16_t raw_characteristics{0};
    bool aslr{false};            // DYNAMIC_BASE (0x0040)
    bool high_entropy_va{false}; // HIGH_ENTROPY_VA (0x0020)
    bool dep{false};             // NX_COMPAT (0x0100)
    bool cfg{false};             // GUARD_CF (0x4000)
    bool no_seh{false};          // NO_SEH (0x0400)
    bool app_container{false};   // APPCONTAINER (0x1000)
    bool force_integrity{false}; // FORCE_INTEGRITY (0x0080)
};

// Inspeciona mitigações de segurança do PE a partir dos bytes brutos do arquivo.
[[nodiscard]] MitigationInfo inspect_pe_mitigations(
    std::span<const std::byte> file_bytes,
    const PeInfo& info);

struct PackerInspectionResult {
    bool is_packed{false};
    std::string packer_name;
    std::vector<std::string> indicators;
};

// Detecta empacotadores conhecidos (UPX, VMProtect, Themida, ASPack, etc.) e seções W+X anômalas.
[[nodiscard]] PackerInspectionResult inspect_pe_packers(const PeInfo& info);

struct FrameworkInspectionResult {
    std::string toolchain;
    bool is_dotnet{false};
    std::string dotnet_details;
    std::vector<std::string> gui_frameworks;
};

// Identifica a toolchain CRT (MSVC, MinGW, Clang), runtime (.NET/CLR) e frameworks de interface (Qt, MFC, etc.).
[[nodiscard]] FrameworkInspectionResult inspect_pe_frameworks(
    const PeInfo& info,
    std::span<const std::byte> file_bytes = {});

struct SecurityServiceInspectionResult {
    bool has_anticheat{false};
    std::string anticheat_name;
    std::vector<std::string> anticheat_indicators;

    bool has_service_apis{false};
    std::vector<std::string> service_apis;
};

// Detecta componentes de anticheat (EasyAntiCheat, BattlEye, Vanguard) e APIs de serviços/drivers de kernel.
[[nodiscard]] SecurityServiceInspectionResult inspect_pe_security_services(const PeInfo& info);

}  // namespace tradutorlinux::pe

