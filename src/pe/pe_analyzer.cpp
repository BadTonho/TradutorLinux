#include "tradutorlinux/pe/pe_analyzer.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tradutorlinux::pe {

MitigationInfo inspect_pe_mitigations(
    const std::span<const std::byte> file_bytes,
    [[maybe_unused]] const PeInfo& info) {
    MitigationInfo result{};
    if (file_bytes.size() < 0x40) {
        return result;
    }

    // Validação do cabeçalho DOS ('MZ')
    if (std::to_integer<unsigned char>(file_bytes[0]) != 'M' ||
        std::to_integer<unsigned char>(file_bytes[1]) != 'Z') {
        return result;
    }

    // Offset do PE em e_lfanew (0x3C, 4 bytes, little-endian)
    std::uint32_t pe_offset = 0;
    for (int i = 0; i < 4; ++i) {
        pe_offset |= static_cast<std::uint32_t>(
            std::to_integer<unsigned char>(file_bytes[0x3C + static_cast<std::size_t>(i)])) << (i * 8);
    }

    // OptionalHeader64 DllCharacteristics fica no offset 70 dentro do Optional Header.
    // Estrutura: PE_SIGNATURE (4 bytes) + COFF_HEADER (20 bytes) + OPTIONAL_HEADER.
    constexpr std::size_t kSignatureSize = 4;
    constexpr std::size_t kCoffHeaderSize = 20;
    constexpr std::size_t kDllCharOptOffset = 70;
    const std::size_t dll_char_file_offset =
        static_cast<std::size_t>(pe_offset) + kSignatureSize + kCoffHeaderSize + kDllCharOptOffset;

    if (file_bytes.size() < dll_char_file_offset + 2) {
        return result;
    }

    // Validação da assinatura PE ('PE\0\0')
    if (std::to_integer<unsigned char>(file_bytes[pe_offset]) != 'P' ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 1]) != 'E' ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 2]) != 0 ||
        std::to_integer<unsigned char>(file_bytes[pe_offset + 3]) != 0) {
        return result;
    }

    // Leitura segura do campo DllCharacteristics (uint16_t little-endian)
    const std::uint16_t chars = static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(file_bytes[dll_char_file_offset]) |
        (static_cast<std::uint16_t>(std::to_integer<unsigned char>(file_bytes[dll_char_file_offset + 1])) << 8));

    result.has_mitigations = true;
    result.raw_characteristics = chars;
    result.aslr = (chars & kDllCharDynamicBase) != 0;
    result.high_entropy_va = (chars & kDllCharHighEntropyVa) != 0;
    result.dep = (chars & kDllCharNxCompat) != 0;
    result.cfg = (chars & kDllCharGuardCf) != 0;
    result.no_seh = (chars & kDllCharNoSeh) != 0;
    result.app_container = (chars & kDllCharAppContainer) != 0;
    result.force_integrity = (chars & kDllCharForceIntegrity) != 0;

    return result;
}

PackerInspectionResult inspect_pe_packers(const PeInfo& info) {
    PackerInspectionResult result{};
    if (info.sections.empty()) {
        return result;
    }

    auto to_lower = [](std::string_view text) {
        std::string s;
        s.reserve(text.size());
        for (const char c : text) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return s;
    };

    bool has_upx = false;
    bool has_vmp = false;
    bool has_themida = false;
    bool has_aspack = false;
    bool has_enigma = false;
    bool has_mpress = false;
    bool has_pecompact = false;

    constexpr std::uint32_t kMemExecute = 0x20000000;
    constexpr std::uint32_t kMemWrite   = 0x80000000;

    for (const SectionInfo& sec : info.sections) {
        const std::string lower_name = to_lower(sec.name);

        if (lower_name.starts_with("upx") || lower_name == ".upx") {
            has_upx = true;
        } else if (lower_name.starts_with(".vmp") || lower_name.starts_with("vmp")) {
            has_vmp = true;
        } else if (lower_name.find("themida") != std::string::npos) {
            has_themida = true;
        } else if (lower_name.find("aspack") != std::string::npos || lower_name == ".adata" || lower_name == "adata") {
            has_aspack = true;
        } else if (lower_name.find("enigma") != std::string::npos) {
            has_enigma = true;
        } else if (lower_name.find("mpress") != std::string::npos) {
            has_mpress = true;
        } else if (lower_name.find("pec2") != std::string::npos || lower_name.find("pecompact") != std::string::npos) {
            has_pecompact = true;
        }

        // Análise de seções anômalas W+X (executável e gravável)
        const bool is_wx = (sec.characteristics & kMemExecute) != 0 && (sec.characteristics & kMemWrite) != 0;
        if (is_wx) {
            result.indicators.push_back("secao W+X (" + sec.name + ")");
        }

        // Análise de seções executáveis descompactadas em memória (raw_size == 0 && virtual_size >= 0x1000)
        if ((sec.characteristics & kMemExecute) != 0 && sec.raw_data_size == 0 && sec.virtual_size >= 0x1000) {
            result.indicators.push_back("secao executavel sem raw_data (" + sec.name + ")");
        }
    }

    if (has_upx) {
        result.is_packed = true;
        result.packer_name = "UPX";
    } else if (has_vmp) {
        result.is_packed = true;
        result.packer_name = "VMProtect";
    } else if (has_themida) {
        result.is_packed = true;
        result.packer_name = "Themida";
    } else if (has_aspack) {
        result.is_packed = true;
        result.packer_name = "ASPack";
    } else if (has_enigma) {
        result.is_packed = true;
        result.packer_name = "Enigma";
    } else if (has_mpress) {
        result.is_packed = true;
        result.packer_name = "MPRESS";
    } else if (has_pecompact) {
        result.is_packed = true;
        result.packer_name = "PECompact";
    } else if (!result.indicators.empty()) {
        result.is_packed = true;
        result.packer_name = "Generic/Packer";
    }

    return result;
}

FrameworkInspectionResult inspect_pe_frameworks(
    const PeInfo& info,
    const std::span<const std::byte> file_bytes) {
    FrameworkInspectionResult result{};

    auto to_lower = [](std::string_view text) {
        std::string s;
        s.reserve(text.size());
        for (char c : text) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return s;
    };

    std::vector<std::string> imported_dlls;
    auto add_dll = [&](const std::string& name) {
        imported_dlls.push_back(to_lower(name));
    };
    for (const auto& imp : info.imports) {
        add_dll(imp.name);
    }
    for (const auto& imp : info.delay_imports) {
        add_dll(imp.name);
    }

    auto has_dll_prefix = [&](std::string_view prefix) {
        return std::any_of(imported_dlls.begin(), imported_dlls.end(),
                           [prefix](const std::string& d) { return d.starts_with(prefix); });
    };
    auto has_dll_contains = [&](std::string_view sub) {
        return std::any_of(imported_dlls.begin(), imported_dlls.end(),
                           [sub](const std::string& d) { return d.find(sub) != std::string::npos; });
    };

    // 1. Toolchain CRT
    if (has_dll_prefix("vcruntime140") || has_dll_prefix("msvcp140")) {
        result.toolchain = "MSVC CRT (Visual Studio 2015-2022 / v14x)";
    } else if (has_dll_prefix("msvcr120") || has_dll_prefix("msvcp120")) {
        result.toolchain = "MSVC CRT (Visual Studio 2013 / v120)";
    } else if (has_dll_prefix("msvcr110") || has_dll_prefix("msvcp110")) {
        result.toolchain = "MSVC CRT (Visual Studio 2012 / v110)";
    } else if (has_dll_prefix("msvcr100") || has_dll_prefix("msvcp100")) {
        result.toolchain = "MSVC CRT (Visual Studio 2010 / v100)";
    } else if (has_dll_prefix("msvcrt")) {
        result.toolchain = "Legacy MSVC CRT (msvcrt.dll)";
    } else if (has_dll_prefix("libgcc_s") || has_dll_prefix("libstdc++") || has_dll_prefix("libwinpthread")) {
        result.toolchain = "MinGW-w64 (GCC runtime)";
    }

    // 2. .NET / CLR (Managed code)
    if (has_dll_prefix("mscoree")) {
        result.is_dotnet = true;
        result.dotnet_details = ".NET CLR (Managed via mscoree.dll)";
    } else if (file_bytes.size() >= 0x100) {
        if (std::to_integer<unsigned char>(file_bytes[0]) == 'M' &&
            std::to_integer<unsigned char>(file_bytes[1]) == 'Z') {
            std::uint32_t pe_offset = 0;
            for (int i = 0; i < 4; ++i) {
                pe_offset |= static_cast<std::uint32_t>(
                    std::to_integer<unsigned char>(file_bytes[0x3C + static_cast<std::size_t>(i)])) << (i * 8);
            }
            constexpr std::size_t kComDescriptorDirOffset = 4 + 20 + 112 + 14 * 8;
            const std::size_t com_dir_file_offset = static_cast<std::size_t>(pe_offset) + kComDescriptorDirOffset;
            if (file_bytes.size() >= com_dir_file_offset + 8) {
                std::uint32_t clr_rva = 0;
                for (int i = 0; i < 4; ++i) {
                    clr_rva |= static_cast<std::uint32_t>(
                        std::to_integer<unsigned char>(file_bytes[com_dir_file_offset + static_cast<std::size_t>(i)])) << (i * 8);
                }
                if (clr_rva != 0) {
                    result.is_dotnet = true;
                    result.dotnet_details = ".NET CLR (COM Descriptor header)";
                }
            }
        }
    }

    // 3. Frameworks GUI
    auto add_framework = [&](std::string name) {
        if (std::find(result.gui_frameworks.begin(), result.gui_frameworks.end(), name) == result.gui_frameworks.end()) {
            result.gui_frameworks.push_back(std::move(name));
        }
    };

    if (has_dll_contains("qt6core") || has_dll_contains("qt6gui") || has_dll_contains("qt6widgets")) {
        add_framework("Qt 6");
    }
    if (has_dll_contains("qt5core") || has_dll_contains("qt5gui") || has_dll_contains("qt5widgets")) {
        add_framework("Qt 5");
    }
    if (has_dll_prefix("mfc")) {
        add_framework("MFC (Microsoft Foundation Classes)");
    }
    if (has_dll_prefix("wxmsw")) {
        add_framework("wxWidgets");
    }
    if (has_dll_prefix("node") || has_dll_prefix("chrome_elf")) {
        add_framework("Electron / Chromium Embedded Framework");
    }
    if (has_dll_contains("microsoft.ui.xaml") || has_dll_contains("windowsappruntime")) {
        add_framework("WinUI 3 / Windows App SDK");
    }

    return result;
}

SecurityServiceInspectionResult inspect_pe_security_services(const PeInfo& info) {
    SecurityServiceInspectionResult result{};

    auto to_lower = [](std::string_view text) {
        std::string s;
        s.reserve(text.size());
        for (char c : text) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return s;
    };

    auto check_dll = [&](const ImportedDll& dll) {
        const std::string lower = to_lower(dll.name);

        // Checagem de Anticheat conhecido
        if (lower.find("easyanticheat") != std::string::npos) {
            result.has_anticheat = true;
            result.anticheat_name = "EasyAntiCheat";
            result.anticheat_indicators.push_back(dll.name);
        } else if (lower.find("beservice") != std::string::npos ||
                   lower.find("beclient") != std::string::npos ||
                   lower.find("battleye") != std::string::npos) {
            result.has_anticheat = true;
            result.anticheat_name = "BattlEye";
            result.anticheat_indicators.push_back(dll.name);
        } else if (lower.find("vgk") != std::string::npos || lower.find("vgc") != std::string::npos) {
            result.has_anticheat = true;
            result.anticheat_name = "Riot Vanguard";
            result.anticheat_indicators.push_back(dll.name);
        } else if (lower.find("punkbuster") != std::string::npos || lower.find("pbcl") != std::string::npos) {
            result.has_anticheat = true;
            result.anticheat_name = "PunkBuster";
            result.anticheat_indicators.push_back(dll.name);
        } else if (lower.find("denuvo") != std::string::npos) {
            result.has_anticheat = true;
            result.anticheat_name = "Denuvo";
            result.anticheat_indicators.push_back(dll.name);
        }

        // Checagem de APIs de controle de serviços e instalação de drivers (advapi32)
        if (lower == "advapi32.dll" || lower == "advapi32") {
            for (const ImportedSymbol& sym : dll.symbols) {
                if (sym.name == "CreateServiceA" || sym.name == "CreateServiceW" ||
                    sym.name == "OpenSCManagerA" || sym.name == "OpenSCManagerW" ||
                    sym.name == "StartServiceA" || sym.name == "StartServiceW" ||
                    sym.name == "ControlService") {
                    result.has_service_apis = true;
                    if (std::find(result.service_apis.begin(), result.service_apis.end(), sym.name) ==
                        result.service_apis.end()) {
                        result.service_apis.push_back(sym.name);
                    }
                }
            }
        }
    };

    for (const auto& imp : info.imports) {
        check_dll(imp);
    }
    for (const auto& imp : info.delay_imports) {
        check_dll(imp);
    }

    return result;
}

}  // namespace tradutorlinux::pe

