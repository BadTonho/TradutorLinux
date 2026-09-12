#include "tradutorlinux/compat/profile.hpp"

#include "tradutorlinux/prefix/prefix.hpp"

#include "path_rules.hpp"

#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
#include "tradutorlinux/compat/rust_profile_parser.hpp"
#endif

#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
#include "rust_path_validator.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace tradutorlinux::compat {
namespace {

constexpr std::size_t kMaxProfileSize = 1024U * 1024U;

class JsonParser {
public:
    explicit JsonParser(const std::string_view input) noexcept : input_(input) {}

    [[nodiscard]] bool parse(Profile& profile, std::string& error) {
        skip_whitespace();
        if (!consume_raw('{')) return fail(error, "o perfil deve começar com um objeto JSON");

        bool schema_seen = false;
        bool app_id_seen = false;
        bool app_sha256_seen = false;
        bool app_version_seen = false;
        bool files_seen = false;
        bool dlls_seen = false;
        bool backend_seen = false;
        bool extension_seen = false;

        skip_whitespace();
        if (consume_raw('}')) return fail(error, "o perfil não pode ser vazio");

        while (true) {
            std::string key;
            if (!parse_string(key)) return fail(error, "chave JSON inválida");
            skip_whitespace();
            if (!consume_raw(':')) return fail(error, "faltou ':' após chave JSON");

            if (key == "schema") {
                if (schema_seen || !parse_uint32(profile.schema)) {
                    return fail(error, "campo schema inválido ou repetido");
                }
                schema_seen = true;
            } else if (key == "app_id") {
                if (app_id_seen || !parse_string(profile.app_id)) {
                    return fail(error, "campo app_id inválido ou repetido");
                }
                app_id_seen = true;
            } else if (key == "app_sha256") {
                if (app_sha256_seen || !parse_string(profile.app_sha256)) {
                    return fail(error, "campo app_sha256 inválido ou repetido");
                }
                app_sha256_seen = true;
            } else if (key == "app_version") {
                if (app_version_seen || !parse_string(profile.app_version)) {
                    return fail(error, "campo app_version inválido ou repetido");
                }
                app_version_seen = true;
            } else if (key == "files") {
                if (files_seen || !parse_files(profile.files)) {
                    return fail(error, "campo files inválido ou repetido");
                }
                files_seen = true;
            } else if (key == "dlls") {
                if (dlls_seen || !parse_dlls(profile.dlls)) {
                    return fail(error, "campo dlls inválido ou repetido");
                }
                dlls_seen = true;
            } else if (key == "backend") {
                if (backend_seen || !parse_backend(profile.backend)) {
                    return fail(error, "campo backend inválido ou repetido");
                }
                backend_seen = true;
                profile.backend_declared = true;
            } else if (key == "extension") {
                if (extension_seen || !parse_string(profile.extension)) {
                    return fail(error, "campo extension inválido ou repetido");
                }
                extension_seen = true;
                profile.extension_declared = true;
            } else {
                return fail(error, "campo desconhecido: " + key);
            }

            skip_whitespace();
            if (consume_raw('}')) break;
            if (!consume_raw(',')) return fail(error, "faltou ',' entre campos JSON");
            skip_whitespace();
            if (peek_raw('}')) return fail(error, "vírgula final não permitida");
        }

        skip_whitespace();
        if (position_ != input_.size()) {
            return fail(error, "conteúdo após o objeto JSON");
        }
        if (!schema_seen || !app_id_seen) {
            return fail(error, "schema e app_id são obrigatórios");
        }
        return true;
    }

private:
    [[nodiscard]] bool peek_raw(const char expected) const noexcept {
        return position_ < input_.size() && input_[position_] == expected;
    }

    [[nodiscard]] bool consume_raw(const char expected) noexcept {
        if (!peek_raw(expected)) return false;
        ++position_;
        return true;
    }

    void skip_whitespace() noexcept {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool parse_string(std::string& output) {
        skip_whitespace();
        if (!consume_raw('"')) return false;
        output.clear();
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return true;
            if (character == '\\') {
                if (position_ >= input_.size()) return false;
                const char escaped = input_[position_++];
                switch (escaped) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    default: return false;
                }
            } else if (static_cast<unsigned char>(character) < 0x20U) {
                return false;
            } else {
                output.push_back(character);
            }
        }
        return false;
    }

    [[nodiscard]] bool parse_uint32(std::uint32_t& output) {
        skip_whitespace();
        if (position_ >= input_.size() ||
            std::isdigit(static_cast<unsigned char>(input_[position_])) == 0) {
            return false;
        }
        std::uint64_t value = 0;
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            const auto digit = static_cast<std::uint32_t>(input_[position_] - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
                return false;
            }
            value = value * 10U + digit;
            ++position_;
        }
        output = static_cast<std::uint32_t>(value);
        return true;
    }

    [[nodiscard]] bool parse_files(std::vector<FileMapping>& files) {
        skip_whitespace();
        if (!consume_raw('[')) return false;
        skip_whitespace();
        if (consume_raw(']')) return true;

        while (true) {
            if (!consume_raw('{')) return false;
            FileMapping mapping;
            bool source_seen = false;
            bool target_seen = false;
            skip_whitespace();
            if (peek_raw('}')) return false;

            while (true) {
                std::string key;
                if (!parse_string(key)) return false;
                skip_whitespace();
                if (!consume_raw(':')) return false;
                if (key == "source") {
                    std::string source;
                    if (source_seen || !parse_string(source)) return false;
                    mapping.source = std::filesystem::path{source};
                    source_seen = true;
                } else if (key == "target") {
                    if (target_seen || !parse_string(mapping.target)) return false;
                    target_seen = true;
                } else {
                    return false;
                }

                skip_whitespace();
                if (consume_raw('}')) break;
                if (!consume_raw(',')) return false;
                skip_whitespace();
                if (peek_raw('}')) return false;
            }
            if (!source_seen || !target_seen) return false;
            files.push_back(std::move(mapping));

            skip_whitespace();
            if (consume_raw(']')) return true;
            if (!consume_raw(',')) return false;
            skip_whitespace();
            if (peek_raw(']')) return false;
        }
    }

    [[nodiscard]] bool parse_dlls(std::vector<DllMapping>& dlls) {
        skip_whitespace();
        if (!consume_raw('[')) return false;
        skip_whitespace();
        if (consume_raw(']')) return true;

        while (true) {
            if (!consume_raw('{')) return false;
            DllMapping mapping;
            bool module_seen = false;
            bool source_seen = false;
            skip_whitespace();
            if (peek_raw('}')) return false;

            while (true) {
                std::string key;
                if (!parse_string(key)) return false;
                skip_whitespace();
                if (!consume_raw(':')) return false;
                if (key == "module") {
                    if (module_seen || !parse_string(mapping.module)) return false;
                    module_seen = true;
                } else if (key == "source") {
                    std::string source;
                    if (source_seen || !parse_string(source)) return false;
                    mapping.source = std::filesystem::path{source};
                    source_seen = true;
                } else {
                    return false;
                }

                skip_whitespace();
                if (consume_raw('}')) break;
                if (!consume_raw(',')) return false;
                skip_whitespace();
                if (peek_raw('}')) return false;
            }
            if (!module_seen || !source_seen) return false;
            dlls.push_back(std::move(mapping));

            skip_whitespace();
            if (consume_raw(']')) return true;
            if (!consume_raw(',')) return false;
            skip_whitespace();
            if (peek_raw(']')) return false;
        }
    }

    [[nodiscard]] bool parse_backend(BackendSelection& backend) {
        skip_whitespace();
        if (!consume_raw('{')) return false;
        bool kind_seen = false;
        bool min_version_seen = false;
        skip_whitespace();
        if (consume_raw('}')) return false;

        while (true) {
            std::string key;
            if (!parse_string(key)) return false;
            skip_whitespace();
            if (!consume_raw(':')) return false;
            if (key == "kind") {
                std::string kind;
                if (kind_seen || !parse_string(kind)) return false;
                if (kind == "native") {
                    backend.kind = BackendKind::Native;
                } else if (kind == "proton") {
                    backend.kind = BackendKind::Proton;
                } else {
                    return false;
                }
                kind_seen = true;
            } else if (key == "min_version") {
                if (min_version_seen || !parse_string(backend.min_version)) return false;
                min_version_seen = true;
            } else {
                return false;
            }

            skip_whitespace();
            if (consume_raw('}')) break;
            if (!consume_raw(',')) return false;
            skip_whitespace();
            if (peek_raw('}')) return false;
        }
        return kind_seen;
    }

    [[nodiscard]] bool fail(std::string& error, std::string message) {
        error = std::move(message) + " (posição " + std::to_string(position_) + ")";
        return false;
    }

    std::string_view input_;
    std::size_t position_{0};
};

#if !defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
[[nodiscard]] bool is_safe_app_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U || value.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_hex_string(const std::string_view value, const std::size_t length) noexcept {
    if (value.size() != length) return false;
    for (const char character : value) {
        if (std::isxdigit(static_cast<unsigned char>(character)) == 0) return false;
    }
    return true;
}

[[nodiscard]] bool is_valid_version(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) return false;
    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 0x20U) return false;
    }
    return true;
}

[[nodiscard]] bool is_valid_backend_min_version(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 64U) return false;
    bool digit_seen = false;
    bool component_digit_seen = false;
    std::size_t components = 0;
    for (const char character : value) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digit_seen = true;
            component_digit_seen = true;
        } else if (character == '.') {
            if (!component_digit_seen) return false;
            ++components;
            component_digit_seen = false;
        } else {
            return false;
        }
    }
    return digit_seen && component_digit_seen && components >= 1U && components <= 2U;
}
#endif

[[nodiscard]] std::string lowercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

[[nodiscard]] bool normalize_dll_module(std::string& module) {
    if (module.empty() || module.size() > 255U || module.find_first_of("/\\:") != std::string::npos) {
        return false;
    }
    for (const char character : module) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    module = lowercase(std::move(module));
    if (!module.ends_with(".dll")) module += ".dll";
    return module.size() > 4U && module != ".dll";
}

[[nodiscard]] bool same_target(const std::string_view first,
                               const std::string_view second) {
    auto normalize = [](std::string value) {
        for (char& character : value) {
            if (character == '/') character = '\\';
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    };
    return normalize(std::string{first}) == normalize(std::string{second});
}

[[nodiscard]] ProfileLoadResult invalid_result(std::string error) {
    ProfileLoadResult result;
    result.status = ProfileStatus::Invalid;
    result.error = std::move(error);
    return result;
}

[[maybe_unused]] [[nodiscard]] ProfileLoadResult invalid_result(
    std::string error, const PathValidationMetrics& path_validation) {
    ProfileLoadResult result = invalid_result(std::move(error));
    result.path_validation = path_validation;
    return result;
}

[[maybe_unused]] [[nodiscard]] ProfileLoadResult internal_result(
    std::string error, const PathValidationMetrics& path_validation) {
    ProfileLoadResult result;
    result.status = ProfileStatus::InternalError;
    result.error = std::move(error);
    result.path_validation = path_validation;
    return result;
}

#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
[[nodiscard]] ProfileParserStatus profile_parser_status(const tl_profile_status_t status) noexcept {
    switch (status) {
        case TL_PROFILE_STATUS_SUCCESS: return ProfileParserStatus::Success;
        case TL_PROFILE_STATUS_MALFORMED: return ProfileParserStatus::Malformed;
        case TL_PROFILE_STATUS_UNSUPPORTED_FORMAT:
            return ProfileParserStatus::UnsupportedFormat;
        case TL_PROFILE_STATUS_INVALID_ARGUMENT:
            return ProfileParserStatus::InvalidArgument;
        case TL_PROFILE_STATUS_BUFFER_TOO_SMALL: return ProfileParserStatus::BufferTooSmall;
        case TL_PROFILE_STATUS_INPUT_TOO_LARGE: return ProfileParserStatus::InputTooLarge;
        case TL_PROFILE_STATUS_OUTPUT_TOO_LARGE: return ProfileParserStatus::OutputTooLarge;
        case TL_PROFILE_STATUS_INTERNAL: return ProfileParserStatus::Internal;
        default: return ProfileParserStatus::Internal;
    }
}

void set_parser_diagnostics(ProfileLoadResult& result,
                            const RustProfileParseResult& parsed) noexcept {
    result.parser.attempted = true;
    result.parser.backend = ProfileParserBackend::Rust;
    result.parser.status = profile_parser_status(parsed.status);
    result.parser.code = parsed.error.code;
    result.parser.phase = parsed.error.phase;
    result.parser.input_offset = parsed.error.input_offset;
    result.parser.detail_value = parsed.error.detail_value;
}

[[nodiscard]] bool is_fallback_profile_status(const tl_profile_status_t status) noexcept {
    return status == TL_PROFILE_STATUS_MALFORMED ||
           status == TL_PROFILE_STATUS_UNSUPPORTED_FORMAT ||
           status == TL_PROFILE_STATUS_INPUT_TOO_LARGE ||
           status == TL_PROFILE_STATUS_OUTPUT_TOO_LARGE;
}
#endif

}  // namespace

std::filesystem::path profile_path(const std::filesystem::path& prefix_root) {
    return prefix::get_environment_paths(prefix_root).compat_dir / "profile.json";
}

std::filesystem::path files_directory(const std::filesystem::path& prefix_root) {
    return prefix::get_environment_paths(prefix_root).compat_files_dir;
}

std::filesystem::path dlls_directory(const std::filesystem::path& prefix_root) {
    return prefix::get_environment_paths(prefix_root).compat_dlls_dir;
}

ProfileLoadResult load_profile(const std::filesystem::path& prefix_root,
                               const std::string_view expected_app_id,
                               const std::string_view expected_app_sha256,
                               const std::string_view expected_app_version) {
    const std::filesystem::path path = profile_path(prefix_root);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) return invalid_result("não foi possível consultar profile.json: " + ec.message());
        ProfileLoadResult result;
        result.status = ProfileStatus::Missing;
        return result;
    }
    if (ec || !std::filesystem::is_regular_file(path, ec)) {
        return invalid_result("profile.json não é um arquivo regular");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) return invalid_result("profile.json não pôde ser aberto");
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxProfileSize) {
        return invalid_result("profile.json excede o limite de tamanho");
    }
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!input) return invalid_result("profile.json não pôde ser lido completamente");
    }

    Profile profile;
    ProfileParserDiagnostics parser_diagnostics;
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
    const auto profile_bytes = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(contents.data()), contents.size()};
    const RustProfileParseResult parsed = parse_profile_rust(
        profile_bytes, expected_app_id, expected_app_sha256, expected_app_version);
    ProfileLoadResult parser_result;
    set_parser_diagnostics(parser_result, parsed);
    parser_diagnostics = parser_result.parser;
    if (parsed.status != TL_PROFILE_STATUS_SUCCESS) {
        parser_result.status = parsed.internal_failure ||
                                       !is_fallback_profile_status(parsed.status)
                                   ? ProfileStatus::InternalError
                                   : ProfileStatus::Invalid;
        parser_result.error = parsed.error_message.empty()
                                  ? "parser Rust rejeitou profile.json"
                                  : parsed.error_message;
        return parser_result;
    }
    profile = parsed.profile;
#else
    std::string parse_error;
    if (!JsonParser{contents}.parse(profile, parse_error)) {
        return invalid_result(parse_error);
    }
    if (profile.schema != 1U && profile.schema != 2U && profile.schema != 3U &&
        profile.schema != 4U) {
        return invalid_result("schema de perfil não suportado");
    }
    if (profile.schema == 1U && !profile.dlls.empty()) {
        return invalid_result("campo dlls requer schema 2");
    }
    if (profile.schema != 3U && profile.schema != 4U && profile.backend_declared) {
        return invalid_result("campo backend requer schema 3");
    }
    if (profile.schema != 4U && profile.extension_declared) {
        return invalid_result("campo extension requer schema 4");
    }
    if (profile.extension_declared && !is_safe_app_id(profile.extension)) {
        return invalid_result("extension inválida");
    }
    if (profile.backend.kind == BackendKind::Native && !profile.backend.min_version.empty()) {
        return invalid_result("min_version requer backend proton");
    }
    if (profile.backend.kind == BackendKind::Proton &&
        !profile.backend.min_version.empty() &&
        !is_valid_backend_min_version(profile.backend.min_version)) {
        return invalid_result("min_version do Proton inválida");
    }
    if (!is_safe_app_id(profile.app_id) || profile.app_id != expected_app_id) {
        return invalid_result("app_id do perfil não corresponde ao aplicativo");
    }
    if (!profile.app_sha256.empty() && !is_hex_string(profile.app_sha256, 64U)) {
        return invalid_result("app_sha256 deve conter 64 dígitos hexadecimais");
    }
    if (!profile.app_version.empty() && !is_valid_version(profile.app_version)) {
        return invalid_result("app_version inválida");
    }
    if (!profile.app_sha256.empty() &&
        lowercase(profile.app_sha256) != lowercase(std::string{expected_app_sha256})) {
        return invalid_result("hash SHA-256 do perfil não corresponde ao aplicativo");
    }
    if (!profile.app_version.empty() && profile.app_version != expected_app_version) {
        return invalid_result("versão do perfil não corresponde ao aplicativo");
    }
#endif

    const auto invalid_profile_result = [&](std::string error) {
        ProfileLoadResult result = invalid_result(std::move(error));
        result.parser = parser_diagnostics;
        return result;
    };
#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
    const auto invalid_profile_result_with_metrics =
        [&](std::string error, const PathValidationMetrics& metrics) {
            ProfileLoadResult result = invalid_result(std::move(error), metrics);
            result.parser = parser_diagnostics;
            return result;
        };
    const auto internal_profile_result = [&](std::string error,
                                              const PathValidationMetrics& metrics) {
        ProfileLoadResult result = internal_result(std::move(error), metrics);
        result.parser = parser_diagnostics;
        return result;
    };
#endif

    const auto paths = prefix::get_environment_paths(prefix_root);
#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
    detail::RustPathValidationSession path_validation;
    if (!path_validation.available()) {
        return internal_profile_result("não foi possível criar a sessão de validação Rust",
                                       path_validation.metrics());
    }
#endif
    std::vector<std::string> normalized_dll_modules;
    normalized_dll_modules.reserve(profile.dlls.size());
    for (DllMapping& mapping : profile.dlls) {
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
        std::string canonical_module = mapping.module;
        if (!normalize_dll_module(canonical_module)) {
#else
        if (!normalize_dll_module(mapping.module)) {
#endif
            return invalid_profile_result("módulo de DLL inválido");
        }
        if (std::find(normalized_dll_modules.begin(), normalized_dll_modules.end(), mapping.module) !=
            normalized_dll_modules.end()) {
            return invalid_profile_result("mapeamento de DLL duplicado");
        }
        normalized_dll_modules.push_back(mapping.module);
#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
        std::string rust_error;
        const auto validation = path_validation.validate_relative_path(
            mapping.source.string(), rust_error);
        if (validation != detail::RustPathValidationResult::Accepted) {
            const std::string detail = "origem de DLL deve ser relativa e usar apenas '/' " +
                                       std::string{"(validação Rust): "} + rust_error;
            if (validation == detail::RustPathValidationResult::InternalError) {
                return internal_profile_result(detail, path_validation.metrics());
            }
            return invalid_profile_result_with_metrics(detail, path_validation.metrics());
        }
#endif
        if (!path_rules::is_relative_source(mapping.source)) {
            return invalid_profile_result("origem de DLL deve ser relativa e usar apenas '/'");
        }
        const std::filesystem::path source = paths.compat_dlls_dir / mapping.source;
        if (!prefix::is_path_within(source, paths.compat_dlls_dir)) {
            return invalid_profile_result("origem de DLL fora de compat/dlls");
        }
        const bool source_is_symlink = std::filesystem::is_symlink(source, ec);
        if (source_is_symlink ||
            (std::filesystem::exists(source, ec) &&
             !std::filesystem::is_regular_file(source, ec))) {
            return invalid_profile_result("origem de DLL deve ser um arquivo regular sem symlink");
        }
    }
    for (const FileMapping& mapping : profile.files) {
#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
        std::string rust_error;
        const auto source_validation = path_validation.validate_relative_path(
            mapping.source.string(), rust_error);
        if (source_validation != detail::RustPathValidationResult::Accepted) {
            const std::string detail = "origem de arquivo deve ser relativa e usar apenas '/' " +
                                       std::string{"(validação Rust): "} + rust_error;
            if (source_validation == detail::RustPathValidationResult::InternalError) {
                return internal_profile_result(detail, path_validation.metrics());
            }
            return invalid_profile_result_with_metrics(detail, path_validation.metrics());
        }
        const auto target_validation = path_validation.validate_c_drive_path(
            mapping.target, rust_error);
        if (target_validation != detail::RustPathValidationResult::Accepted) {
            const std::string detail = "destino de arquivo fora de drive_c (validação Rust): " +
                                       rust_error;
            if (target_validation == detail::RustPathValidationResult::InternalError) {
                return internal_profile_result(detail, path_validation.metrics());
            }
            return invalid_profile_result_with_metrics(detail, path_validation.metrics());
        }
#endif
        if (!path_rules::is_relative_source(mapping.source)) {
            return invalid_profile_result("origem de arquivo deve ser relativa e usar apenas '/' ");
        }
        const std::filesystem::path source = paths.compat_files_dir / mapping.source;
        if (!std::filesystem::is_regular_file(source, ec) ||
            !prefix::is_path_within(source, paths.compat_files_dir)) {
            return invalid_profile_result("arquivo de origem ausente ou fora de compat/files");
        }
        if (!path_rules::is_c_drive_target(mapping.target) ||
            !path_rules::has_target_filename(mapping.target)) {
            return invalid_profile_result("destino deve ser um arquivo dentro de C:\\");
        }
        const std::filesystem::path target =
            prefix::resolve_windows_path(mapping.target, prefix_root);
        if (target.empty() || !prefix::is_path_within(target, paths.drive_c)) {
            return invalid_profile_result("destino de arquivo fora de drive_c");
        }
        for (const FileMapping& previous : profile.files) {
            if (&previous == &mapping) break;
            if (previous.source == mapping.source || same_target(previous.target, mapping.target)) {
                return invalid_profile_result("mapeamento de arquivo duplicado");
            }
        }
    }

    ProfileLoadResult result;
    result.status = ProfileStatus::Loaded;
    result.profile = std::move(profile);
#if defined(TRADUTORLINUX_RUST_PATH_VALIDATOR)
    result.path_validation = path_validation.metrics();
#endif
#if defined(TRADUTORLINUX_RUST_PROFILE_PARSER)
    result.parser = parser_diagnostics;
#endif
    return result;
}

}  // namespace tradutorlinux::compat
