#include "tradutorlinux/catalog/app_catalog.hpp"

#if defined(TRADUTORLINUX_RUST_APP_CATALOG_PARSER)
#include "tradutorlinux/catalog/rust_app_catalog_parser.hpp"
#include "tradutorlinux/diagnostics/trace.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace tradutorlinux::catalog {
namespace {

std::string escape_json_string(std::string_view input) {
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
                    std::ostringstream ss;
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(static_cast<unsigned char>(c));
                    output += ss.str();
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

#if !defined(TRADUTORLINUX_RUST_APP_CATALOG_PARSER)
std::string unescape_json_string(std::string_view input) {
    const auto hex_digit = [](const char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    const auto append_utf8 = [](std::string& output, const std::uint32_t codepoint) {
        if (codepoint <= 0x7FU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0x10FFFFU) {
            output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    };
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            ++i;
            switch (input[i]) {
                case '"':  output += '"';  break;
                case '\\': output += '\\'; break;
                case '/':  output += '/';  break;
                case 'b':  output += '\b'; break;
                case 'f':  output += '\f'; break;
                case 'n':  output += '\n'; break;
                case 'r':  output += '\r'; break;
                case 't':  output += '\t'; break;
                case 'u': {
                    if (i + 4 >= input.size()) {
                        output += 'u';
                        break;
                    }
                    std::uint32_t codepoint = 0;
                    bool valid = true;
                    for (std::size_t digit = 1; digit <= 4; ++digit) {
                        const int value = hex_digit(input[i + digit]);
                        if (value < 0) {
                            valid = false;
                            break;
                        }
                        codepoint = (codepoint << 4U) | static_cast<std::uint32_t>(value);
                    }
                    if (!valid) {
                        output += 'u';
                        break;
                    }
                    i += 4;
                    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU &&
                        i + 6 < input.size() && input[i + 1] == '\\' && input[i + 2] == 'u') {
                        std::uint32_t low = 0;
                        bool low_valid = true;
                        for (std::size_t digit = 3; digit <= 6; ++digit) {
                            const int value = hex_digit(input[i + digit]);
                            if (value < 0) {
                                low_valid = false;
                                break;
                            }
                            low = (low << 4U) | static_cast<std::uint32_t>(value);
                        }
                        if (low_valid && low >= 0xDC00U && low <= 0xDFFFU) {
                            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) +
                                        (low - 0xDC00U);
                            i += 6;
                        }
                    }
                    if (codepoint < 0xD800U || codepoint > 0xDFFFU) {
                        append_utf8(output, codepoint);
                    }
                    break;
                }
                default:   output += input[i]; break;
            }
        } else {
            output += input[i];
        }
    }
    return output;
}
#endif

[[nodiscard]] bool is_safe_app_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128 || id == "." || id == "..") {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](const char value) {
        const bool ascii_alphanumeric = (value >= 'A' && value <= 'Z') ||
                                        (value >= 'a' && value <= 'z') ||
                                        (value >= '0' && value <= '9');
        return ascii_alphanumeric ||
               value == '_' || value == '-' || value == '.';
    });
}

#if !defined(TRADUTORLINUX_RUST_APP_CATALOG_PARSER)
void skip_whitespace(std::string_view& src) {
    while (!src.empty() && std::isspace(static_cast<unsigned char>(src.front())) != 0) {
        src.remove_prefix(1);
    }
}

std::optional<std::string> parse_json_string(std::string_view& src) {
    skip_whitespace(src);
    if (src.empty() || src.front() != '"') {
        return std::nullopt;
    }
    src.remove_prefix(1); // Consumir '"'

    std::size_t end = 0;
    bool in_escape = false;
    while (end < src.size()) {
        if (in_escape) {
            in_escape = false;
        } else if (src[end] == '\\') {
            in_escape = true;
        } else if (src[end] == '"') {
            break;
        }
        ++end;
    }

    if (end >= src.size()) {
        return std::nullopt;
    }

    const std::string raw{src.substr(0, end)};
    src.remove_prefix(end + 1); // Consumir string e fechar '"'
    return unescape_json_string(raw);
}

std::optional<std::uint64_t> parse_json_uint64(std::string_view& src) {
    skip_whitespace(src);
    if (src.empty() || src.front() < '0' || src.front() > '9') {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    while (!src.empty() && src.front() >= '0' && src.front() <= '9') {
        const std::uint64_t digit = static_cast<std::uint64_t>(src.front() - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        value = value * 10U + digit;
        src.remove_prefix(1);
    }
    return value;
}
#endif

#if defined(TRADUTORLINUX_RUST_APP_CATALOG_PARSER)
[[nodiscard]] std::string rust_catalog_status_name(
    const tl_app_catalog_status_t status) noexcept {
    switch (status) {
        case TL_APP_CATALOG_STATUS_SUCCESS: return "success";
        case TL_APP_CATALOG_STATUS_MALFORMED: return "malformed";
        case TL_APP_CATALOG_STATUS_UNSUPPORTED_FORMAT: return "unsupported-format";
        case TL_APP_CATALOG_STATUS_INVALID_ARGUMENT: return "invalid-argument";
        case TL_APP_CATALOG_STATUS_BUFFER_TOO_SMALL: return "buffer-too-small";
        case TL_APP_CATALOG_STATUS_INPUT_TOO_LARGE: return "input-too-large";
        case TL_APP_CATALOG_STATUS_OUTPUT_TOO_LARGE: return "output-too-large";
        case TL_APP_CATALOG_STATUS_INTERNAL: return "internal";
        default: return "internal";
    }
}

void write_rust_catalog_trace(const std::filesystem::path& path,
                              const RustAppCatalogParseResult& parsed) {
    if (!diagnostics::is_trace_requested() ||
        !diagnostics::is_trace_enabled(diagnostics::TraceComponent::Runtime)) {
        return;
    }
    std::vector<diagnostics::TraceField> fields;
    fields.reserve(parsed.status == TL_APP_CATALOG_STATUS_SUCCESS ? 4U : 9U);
    fields.push_back({"path", path.string()});
    fields.push_back({"backend", "rust"});
    fields.push_back({"parser-status", rust_catalog_status_name(parsed.status)});
    fields.push_back({"apps", std::to_string(parsed.apps.size())});
    if (parsed.status != TL_APP_CATALOG_STATUS_SUCCESS) {
        fields.push_back({"code", std::to_string(parsed.error.code)});
        fields.push_back({"phase", std::to_string(parsed.error.phase)});
        fields.push_back({"input-offset", std::to_string(parsed.error.input_offset)});
        fields.push_back({"detail-value", std::to_string(parsed.error.detail_value)});
        fields.push_back({"detail", parsed.error_message});
    }
    const diagnostics::TraceLevel level =
        parsed.internal_failure || parsed.status == TL_APP_CATALOG_STATUS_INTERNAL
            ? diagnostics::TraceLevel::Error
            : parsed.status == TL_APP_CATALOG_STATUS_SUCCESS ? diagnostics::TraceLevel::Info
                                                              : diagnostics::TraceLevel::Warning;
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime, level,
                             "catalog-parse", fields);
}

[[nodiscard]] RustAppCatalogParseResult rust_catalog_input_limit_result(
    const std::uint64_t input_size) {
    RustAppCatalogParseResult result;
    result.status = TL_APP_CATALOG_STATUS_INPUT_TOO_LARGE;
    result.error = {TL_APP_CATALOG_ERROR_INPUT_TOO_LARGE, TL_APP_CATALOG_ERROR_PHASE_INPUT,
                    TL_APP_CATALOG_ERROR_OFFSET_UNKNOWN, input_size};
    result.error_message = "library.json excede o limite de entrada TLAC";
    return result;
}
#endif

}  // namespace

std::filesystem::path AppCatalog::default_catalog_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "tradutorlinux" / "library.json";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "tradutorlinux" / "library.json";
    }
    const char* appdata = std::getenv("APPDATA");
    if (appdata != nullptr && *appdata != '\0') {
        return std::filesystem::path(appdata) / "tradutorlinux" / "library.json";
    }
    return std::filesystem::current_path() / "library.json";
}

std::string AppCatalog::generate_id(std::string_view name_or_filename) {
    std::string slug;
    slug.reserve(name_or_filename.size());

    // Se tiver extensão .exe, remover para o ID
    std::string_view base = name_or_filename;
    if (base.ends_with(".exe") || base.ends_with(".EXE")) {
        base.remove_suffix(4);
    }

    for (const char c : base) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
            if (!slug.empty() && slug.back() != '_') {
                slug.push_back('_');
            }
        }
    }
    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    return slug.empty() ? "app" : slug;
}

bool AppCatalog::add_app(const AppEntry& app) {
    if (!is_safe_app_id(app.id) || app.executable_path.empty()) {
        return false;
    }

    for (auto& existing : apps_) {
        if (existing.id == app.id) {
            existing = app;
            return true;
        }
    }

    AppEntry entry = app;
    if (entry.created_at.empty()) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
#else
        gmtime_r(&t, &tm_buf);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
        entry.created_at = ss.str();
    }

    apps_.push_back(std::move(entry));
    return true;
}

bool AppCatalog::remove_app(std::string_view id) {
    const auto it = std::remove_if(apps_.begin(), apps_.end(), [id](const AppEntry& e) {
        return e.id == id;
    });
    if (it != apps_.end()) {
        apps_.erase(it, apps_.end());
        return true;
    }
    return false;
}

std::optional<AppEntry> AppCatalog::find_app(std::string_view id_or_name) const {
    for (const auto& app : apps_) {
        if (app.id == id_or_name || app.name == id_or_name) {
            return app;
        }
    }
    return std::nullopt;
}

bool AppCatalog::save_to_file(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        return false;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"apps\": [\n";

    for (std::size_t i = 0; i < apps_.size(); ++i) {
        const auto& app = apps_[i];
        file << "    {\n";
        file << "      \"id\": \"" << escape_json_string(app.id) << "\",\n";
        file << "      \"name\": \"" << escape_json_string(app.name) << "\",\n";
        file << "      \"executable_path\": \"" << escape_json_string(app.executable_path) << "\",\n";
        file << "      \"prefix_path\": \"" << escape_json_string(app.prefix_path) << "\",\n";
        file << "      \"icon_path\": \"" << escape_json_string(app.icon_path) << "\",\n";
        file << "      \"working_directory\": \"" << escape_json_string(app.working_directory) << "\",\n";
        file << "      \"app_sha256\": \"" << escape_json_string(app.app_sha256) << "\",\n";
        file << "      \"app_version\": \"" << escape_json_string(app.app_version) << "\",\n";
        file << "      \"created_at\": \"" << escape_json_string(app.created_at) << "\",\n";
        file << "      \"cpu_limit_seconds\": " << app.cpu_limit_seconds << ",\n";
        file << "      \"memory_limit_mib\": " << app.memory_limit_mib << ",\n";
        file << "      \"args\": [";
        for (std::size_t j = 0; j < app.args.size(); ++j) {
            file << "\"" << escape_json_string(app.args[j]) << "\"";
            if (j + 1 < app.args.size()) {
                file << ", ";
            }
        }
        file << "]\n";
        file << "    }";
        if (i + 1 < apps_.size()) {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    return true;
}

bool AppCatalog::load_from_file(const std::filesystem::path& path) {
    apps_.clear();
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return false;
    }

#if defined(TRADUTORLINUX_RUST_APP_CATALOG_PARSER)
    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    if (file_size < 0) {
        return false;
    }
    const auto input_size = static_cast<std::uint64_t>(file_size);
    if (input_size > TL_APP_CATALOG_LIMIT_MAX_INPUT_BYTES ||
        input_size > std::numeric_limits<std::size_t>::max()) {
        const RustAppCatalogParseResult parsed = rust_catalog_input_limit_result(input_size);
        write_rust_catalog_trace(path, parsed);
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> content(static_cast<std::size_t>(input_size));
    if (!content.empty()) {
        file.read(reinterpret_cast<char*>(content.data()),
                  static_cast<std::streamsize>(content.size()));
        if (!file) {
            return false;
        }
    }

    RustAppCatalogParseResult parsed;
    try {
        parsed = parse_app_catalog_rust(content);
    } catch (const std::exception& exception) {
        parsed.status = TL_APP_CATALOG_STATUS_INTERNAL;
        parsed.internal_failure = true;
        parsed.error = {TL_APP_CATALOG_ERROR_INTERNAL, TL_APP_CATALOG_ERROR_PHASE_INPUT,
                        TL_APP_CATALOG_ERROR_OFFSET_UNKNOWN, 0};
        parsed.error_message = exception.what();
    } catch (...) {
        parsed.status = TL_APP_CATALOG_STATUS_INTERNAL;
        parsed.internal_failure = true;
        parsed.error = {TL_APP_CATALOG_ERROR_INTERNAL, TL_APP_CATALOG_ERROR_PHASE_INPUT,
                        TL_APP_CATALOG_ERROR_OFFSET_UNKNOWN, 0};
        parsed.error_message = "falha desconhecida ao carregar catalogo TLAC";
    }
    write_rust_catalog_trace(path, parsed);
    if (parsed.status != TL_APP_CATALOG_STATUS_SUCCESS || parsed.internal_failure) {
        return false;
    }
    apps_ = std::move(parsed.apps);
    return true;
#else
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    std::string_view view = content;

    // Parser simples e resiliente de JSON para o array "apps"
    const auto apps_pos = view.find("\"apps\"");
    if (apps_pos == std::string_view::npos) {
        return false;
    }
    view.remove_prefix(apps_pos + 6);

    const auto open_bracket = view.find('[');
    if (open_bracket == std::string_view::npos) {
        return false;
    }
    view.remove_prefix(open_bracket + 1);

    while (!view.empty()) {
        skip_whitespace(view);
        if (view.empty() || view.front() == ']') {
            break;
        }

        const auto obj_open = view.find('{');
        if (obj_open == std::string_view::npos) {
            break;
        }
        view.remove_prefix(obj_open + 1);

        AppEntry entry;
        while (!view.empty()) {
            skip_whitespace(view);
            if (view.empty() || view.front() == '}') {
                if (!view.empty()) view.remove_prefix(1);
                break;
            }

            auto key_opt = parse_json_string(view);
            if (!key_opt) {
                view.remove_prefix(1);
                continue;
            }

            skip_whitespace(view);
            if (!view.empty() && view.front() == ':') {
                view.remove_prefix(1);
            }
            skip_whitespace(view);

            if (*key_opt == "args") {
                if (!view.empty() && view.front() == '[') {
                    view.remove_prefix(1);
                    while (!view.empty()) {
                        skip_whitespace(view);
                        if (view.empty() || view.front() == ']') {
                            if (!view.empty()) view.remove_prefix(1);
                            break;
                        }
                        if (view.front() == ',') {
                            view.remove_prefix(1);
                            continue;
                        }
                        auto arg_val = parse_json_string(view);
                        if (arg_val) {
                            entry.args.push_back(*arg_val);
                        } else {
                            break;
                        }
                    }
                }
            } else if (*key_opt == "cpu_limit_seconds" || *key_opt == "memory_limit_mib") {
                const auto value = parse_json_uint64(view);
                if (value.has_value()) {
                    if (*key_opt == "cpu_limit_seconds") {
                        entry.cpu_limit_seconds = *value;
                    } else {
                        entry.memory_limit_mib = *value;
                    }
                }
            } else {
                auto val_opt = parse_json_string(view);
                if (val_opt) {
                    if (*key_opt == "id") entry.id = *val_opt;
                    else if (*key_opt == "name") entry.name = *val_opt;
                    else if (*key_opt == "executable_path") entry.executable_path = *val_opt;
                    else if (*key_opt == "prefix_path") entry.prefix_path = *val_opt;
                    else if (*key_opt == "icon_path") entry.icon_path = *val_opt;
                    else if (*key_opt == "working_directory") entry.working_directory = *val_opt;
                    else if (*key_opt == "app_sha256") entry.app_sha256 = *val_opt;
                    else if (*key_opt == "app_version") entry.app_version = *val_opt;
                    else if (*key_opt == "created_at") entry.created_at = *val_opt;
                }
            }

            skip_whitespace(view);
            if (!view.empty() && view.front() == ',') {
                view.remove_prefix(1);
            }
        }

        if (is_safe_app_id(entry.id) && !entry.executable_path.empty()) {
            apps_.push_back(std::move(entry));
        }

        skip_whitespace(view);
        if (!view.empty() && view.front() == ',') {
            view.remove_prefix(1);
        }
    }

    return true;
#endif
}

std::filesystem::path AppCatalog::default_desktop_entries_dir() {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / "applications";
    }
    return {};
}

bool AppCatalog::create_desktop_entry(const AppEntry& app, const std::filesystem::path& destination_dir) {
    if (!is_safe_app_id(app.id)) {
        return false;
    }
    std::filesystem::path dir = destination_dir.empty() ? default_desktop_entries_dir() : destination_dir;
    if (dir.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    const std::filesystem::path file_path = dir / ("tradutorlinux-" + app.id + ".desktop");
    std::ofstream out(file_path, std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    auto desktop_escape = [](std::string value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char ch : value) {
            if (ch == '\\' || ch == '"' || ch == '`' || ch == '$') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        return escaped;
    };
    out << "Name=" << desktop_escape(app.name) << "\n";
    out << "Comment=Executado via TradutorLinux\n";
    out << "TryExec=/usr/bin/tradutorlinux\n";
    out << "Exec=/usr/bin/tradutorlinux app run " << desktop_escape(app.id) << "\n";
    if (!app.icon_path.empty()) {
        out << "Icon=" << app.icon_path << "\n";
    }
    out << "Terminal=false\n";
    out << "Categories=Utility;Game;Wine;\n";
    out << "StartupNotify=true\n";

    return true;
}

}  // namespace tradutorlinux::catalog
