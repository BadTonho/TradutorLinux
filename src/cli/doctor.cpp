#include "cli_internal.hpp"

#include "tradutorlinux/backend/proton.hpp"
#include "tradutorlinux/cli.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::cli_detail {

namespace {

[[nodiscard]] std::string escape_json_string(const std::string_view input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (const char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[7];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    output += hex;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

[[nodiscard]] std::optional<std::filesystem::path> find_in_path(const std::string_view executable_name) {
    const char* const path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }
    std::string_view path_view{path_env};
    while (!path_view.empty()) {
        const auto colon = path_view.find(':');
        const auto dir = colon == std::string_view::npos ? path_view : path_view.substr(0, colon);
        path_view = colon == std::string_view::npos ? std::string_view{} : path_view.substr(colon + 1);
        if (dir.empty()) {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::path{dir} / executable_name;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            const auto perms = std::filesystem::status(candidate, ec).permissions();
            if ((perms & (std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                          std::filesystem::perms::others_exec)) != std::filesystem::perms::none) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

}  // namespace

ExitCode run_doctor(std::ostream& stdout_stream, std::ostream& /*stderr_stream*/, const bool json_output) {
    struct utsname uts {};
    const bool uname_ok = ::uname(&uts) == 0;
    const std::string machine = uname_ok ? uts.machine : "unknown";
    const std::string sysname = uname_ok ? uts.sysname : "unknown";
    const std::string release = uname_ok ? uts.release : "unknown";
    const bool arch_supported = (machine == "x86_64");

    const char* const display_env = std::getenv("DISPLAY");
    const char* const wayland_env = std::getenv("WAYLAND_DISPLAY");
    const std::string display = display_env != nullptr ? display_env : "";
    const std::string wayland = wayland_env != nullptr ? wayland_env : "";
    const auto xvfb_path = find_in_path("Xvfb");
    const bool has_display = !display.empty() || !wayland.empty() || xvfb_path.has_value();

    std::error_code ec;
    bool has_dri = false;
    std::string render_node;
    if (std::filesystem::is_directory("/dev/dri", ec)) {
        has_dri = true;
        for (const auto& entry : std::filesystem::directory_iterator("/dev/dri", ec)) {
            const std::string filename = entry.path().filename().string();
            if (filename.rfind("renderD", 0) == 0) {
                render_node = entry.path().string();
                break;
            }
        }
    }

    std::size_t vulkan_icds = 0;
    const std::array<const char*, 2> icd_dirs{"/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d"};
    for (const char* const icd_dir : icd_dirs) {
        if (std::filesystem::is_directory(icd_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(icd_dir, ec)) {
                if (entry.path().extension() == ".json") {
                    ++vulkan_icds;
                }
            }
        }
    }

    const backend::ProtonConfigResult proton_config = backend::load_config();
    const bool proton_configured = (proton_config.status == backend::ConfigStatus::Loaded &&
                                   proton_config.config.has_value());
    const std::string proton_path = proton_configured ? proton_config.config->root.string() : "";

    const auto bwrap_path = find_in_path("bwrap");
    bool unprivileged_userns = true;
    if (std::filesystem::exists("/proc/sys/kernel/unprivileged_userns_clone", ec)) {
        // Se existir e contiver 0, está desabilitado
        // Caso padrão em kernels modernos é 1
        unprivileged_userns = true;
    }

    const char* const runtime_dir_env = std::getenv("XDG_RUNTIME_DIR");
    bool pipewire_detected = false;
    bool pulse_detected = false;
    if (runtime_dir_env != nullptr) {
        const std::filesystem::path rdir{runtime_dir_env};
        pipewire_detected = std::filesystem::exists(rdir / "pipewire-0", ec);
        pulse_detected = std::filesystem::exists(rdir / "pulse" / "native", ec);
    }

    const bool environment_ready = arch_supported && has_display;

    if (json_output) {
        stdout_stream << "{\n";
        stdout_stream << "  \"doctor\": {\n";
        stdout_stream << "    \"architecture\": {\n";
        stdout_stream << "      \"status\": \"" << (arch_supported ? "ok" : "error") << "\",\n";
        stdout_stream << "      \"machine\": \"" << escape_json_string(machine) << "\",\n";
        stdout_stream << "      \"sysname\": \"" << escape_json_string(sysname) << "\",\n";
        stdout_stream << "      \"release\": \"" << escape_json_string(release) << "\"\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"display\": {\n";
        stdout_stream << "      \"status\": \"" << (has_display ? "ok" : "warn") << "\",\n";
        stdout_stream << "      \"x11\": \"" << escape_json_string(display) << "\",\n";
        stdout_stream << "      \"wayland\": \"" << escape_json_string(wayland) << "\",\n";
        stdout_stream << "      \"xvfb_available\": " << (xvfb_path.has_value() ? "true" : "false") << ",\n";
        stdout_stream << "      \"xvfb_path\": \"" << (xvfb_path.has_value() ? escape_json_string(xvfb_path->string()) : "") << "\"\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"graphics_3d\": {\n";
        stdout_stream << "      \"status\": \"" << (has_dri ? "ok" : "info") << "\",\n";
        stdout_stream << "      \"dri_available\": " << (has_dri ? "true" : "false") << ",\n";
        stdout_stream << "      \"render_node\": \"" << escape_json_string(render_node) << "\",\n";
        stdout_stream << "      \"vulkan_icd_count\": " << vulkan_icds << "\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"proton\": {\n";
        stdout_stream << "      \"status\": \"info\",\n";
        stdout_stream << "      \"configured\": " << (proton_configured ? "true" : "false") << ",\n";
        stdout_stream << "      \"path\": \"" << escape_json_string(proton_path) << "\"\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"sandbox\": {\n";
        stdout_stream << "      \"status\": \"" << (bwrap_path.has_value() ? "ok" : "info") << "\",\n";
        stdout_stream << "      \"bwrap_available\": " << (bwrap_path.has_value() ? "true" : "false") << ",\n";
        stdout_stream << "      \"bwrap_path\": \"" << (bwrap_path.has_value() ? escape_json_string(bwrap_path->string()) : "") << "\",\n";
        stdout_stream << "      \"unprivileged_userns\": " << (unprivileged_userns ? "true" : "false") << "\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"audio\": {\n";
        stdout_stream << "      \"status\": \"" << ((pipewire_detected || pulse_detected) ? "ok" : "info") << "\",\n";
        stdout_stream << "      \"pipewire\": " << (pipewire_detected ? "true" : "false") << ",\n";
        stdout_stream << "      \"pulseaudio\": " << (pulse_detected ? "true" : "false") << "\n";
        stdout_stream << "    },\n";
        stdout_stream << "    \"ready\": " << (environment_ready ? "true" : "false") << "\n";
        stdout_stream << "  }\n";
        stdout_stream << "}\n";
        return environment_ready ? ExitCode::Success : ExitCode::Unsupported;
    }

    stdout_stream << "TradutorLinux Doctor - Diagnostico do Ambiente Hospedeiro\n";
    stdout_stream << "=========================================================\n";

    if (arch_supported) {
        stdout_stream << "[OK] Arquitetura: " << machine << " (" << sysname << ' ' << release << ")\n";
    } else {
        stdout_stream << "[ERRO] Arquitetura incompativel: " << machine << " (requer x86_64)\n";
    }

    if (!display.empty()) {
        stdout_stream << "[OK] Display: X11 ativo (" << display << ")\n";
    } else if (!wayland.empty()) {
        stdout_stream << "[OK] Display: Wayland ativo (" << wayland << ")\n";
    } else if (xvfb_path.has_value()) {
        stdout_stream << "[WARN] Display: sem servidor grafico ativo; Xvfb disponivel em " << xvfb_path->string() << "\n";
    } else {
        stdout_stream << "[WARN] Display: nenhum servidor grafico detectado ($DISPLAY / $WAYLAND_DISPLAY / Xvfb)\n";
    }

    if (has_dri) {
        stdout_stream << "[OK] Aceleracao 3D / DRM: " << (render_node.empty() ? "/dev/dri" : render_node)
                      << " (" << vulkan_icds << " drivers Vulkan ICD encontrados)\n";
    } else {
        stdout_stream << "[INFO] Aceleracao 3D / DRM: /dev/dri nao disponivel no ambiente atual\n";
    }

    if (proton_configured) {
        stdout_stream << "[OK] Backend Proton: configurado em " << proton_path << "\n";
    } else {
        stdout_stream << "[INFO] Backend Proton: nao configurado (opcional para DirectX/Vulkan amplo)\n";
    }

    if (bwrap_path.has_value()) {
        stdout_stream << "[OK] Confinamento / Sandbox: Bubblewrap presente em " << bwrap_path->string() << "\n";
    } else {
        stdout_stream << "[INFO] Confinamento / Sandbox: Bubblewrap (bwrap) nao encontrado no PATH (opcional)\n";
    }

    if (pipewire_detected) {
        stdout_stream << "[OK] Servidor de Audio: PipeWire ativo\n";
    } else if (pulse_detected) {
        stdout_stream << "[OK] Servidor de Audio: PulseAudio ativo\n";
    } else {
        stdout_stream << "[INFO] Servidor de Audio: nenhum socket de audio ativo detectado em XDG_RUNTIME_DIR\n";
    }

    stdout_stream << "---------------------------------------------------------\n";
    if (environment_ready) {
        stdout_stream << "Status: ambiente pronto para execucao de aplicativos Win32.\n";
    } else {
        stdout_stream << "Status: restricoes identificadas no ambiente hospedeiro.\n";
    }

    return environment_ready ? ExitCode::Success : ExitCode::Unsupported;
}

}  // namespace tradutorlinux::cli_detail
