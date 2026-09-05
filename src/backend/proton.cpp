#include "tradutorlinux/backend/proton.hpp"

#include "tradutorlinux/util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tradutorlinux::backend {
namespace {

constexpr std::size_t kMaxConfigSize = 64U * 1024U;
constexpr std::size_t kMaxFingerprintSize = 64U * 1024U * 1024U;

class JsonParser {
public:
    explicit JsonParser(const std::string_view input) noexcept : input_(input) {}

    [[nodiscard]] bool parse(ProtonConfig& config, std::string& error) {
        skip_whitespace();
        if (!consume('{')) return fail(error, "a configuração deve começar com um objeto JSON");

        bool schema_seen = false;
        bool proton_seen = false;
        skip_whitespace();
        if (consume('}')) return fail(error, "a configuração não pode ser vazia");

        while (true) {
            std::string key;
            if (!parse_string(key)) return fail(error, "chave JSON inválida");
            skip_whitespace();
            if (!consume(':')) return fail(error, "faltou ':' após chave JSON");

            if (key == "schema") {
                std::uint32_t schema = 0;
                if (schema_seen || !parse_uint32(schema) || schema != 1U) {
                    return fail(error, "campo schema inválido ou repetido");
                }
                schema_seen = true;
            } else if (key == "proton") {
                if (proton_seen || !parse_proton(config)) {
                    return fail(error, "campo proton inválido ou repetido");
                }
                proton_seen = true;
            } else {
                return fail(error, "campo desconhecido: " + key);
            }

            skip_whitespace();
            if (consume('}')) break;
            if (!consume(',')) return fail(error, "faltou ',' entre campos JSON");
            skip_whitespace();
            if (peek('}')) return fail(error, "vírgula final não permitida");
        }

        skip_whitespace();
        if (position_ != input_.size()) return fail(error, "conteúdo após o objeto JSON");
        if (!schema_seen || !proton_seen) {
            return fail(error, "schema e proton são obrigatórios");
        }
        return true;
    }

private:
    [[nodiscard]] bool peek(const char expected) const noexcept {
        return position_ < input_.size() && input_[position_] == expected;
    }

    [[nodiscard]] bool consume(const char expected) noexcept {
        if (!peek(expected)) return false;
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
        if (!consume('"')) return false;
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

    [[nodiscard]] bool parse_proton(ProtonConfig& config) {
        skip_whitespace();
        if (!consume('{')) return false;
        bool root_seen = false;
        bool sha256_seen = false;
        skip_whitespace();
        if (consume('}')) return false;

        while (true) {
            std::string key;
            if (!parse_string(key)) return false;
            skip_whitespace();
            if (!consume(':')) return false;
            if (key == "root") {
                std::string value;
                if (root_seen || !parse_string(value)) return false;
                config.root = std::filesystem::path{value};
                root_seen = true;
            } else if (key == "sha256") {
                if (sha256_seen || !parse_string(config.sha256)) return false;
                sha256_seen = true;
            } else {
                return false;
            }

            skip_whitespace();
            if (consume('}')) break;
            if (!consume(',')) return false;
            skip_whitespace();
            if (peek('}')) return false;
        }
        return root_seen;
    }

    [[nodiscard]] bool fail(std::string& error, std::string message) {
        error = std::move(message) + " (posição " + std::to_string(position_) + ")";
        return false;
    }

    std::string_view input_;
    std::size_t position_{0};
};

[[nodiscard]] bool is_hex_string(const std::string_view value) noexcept {
    if (value.size() != 64U) return false;
    for (const char character : value) {
        if (std::isxdigit(static_cast<unsigned char>(character)) == 0) return false;
    }
    return true;
}

[[nodiscard]] bool is_executable_file(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    const auto permissions = std::filesystem::status(path, ec).permissions();
    if (ec) return false;
    using std::filesystem::perms;
    return (permissions & (perms::owner_exec | perms::group_exec | perms::others_exec)) !=
           perms::none;
}

[[nodiscard]] bool read_first_line(const std::filesystem::path& path,
                                   std::string& line) {
    std::ifstream input(path);
    if (!input || !std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return !line.empty();
}

struct Version {
    std::array<std::uint64_t, 3> components{};
};

[[nodiscard]] bool parse_version(const std::string_view value, Version& version,
                                  const bool allow_suffix) noexcept {
    if (value.empty()) return false;
    std::size_t position = 0;
    for (std::size_t component = 0; component < version.components.size(); ++component) {
        if (position >= value.size() ||
            std::isdigit(static_cast<unsigned char>(value[position])) == 0) {
            return false;
        }
        std::uint64_t number = 0;
        while (position < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[position])) != 0) {
            const auto digit = static_cast<std::uint64_t>(value[position] - '0');
            if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                return false;
            }
            number = number * 10U + digit;
            ++position;
        }
        version.components[component] = number;
        if (position == value.size()) return component >= 1U;
        if (value[position] != '.') {
            return allow_suffix && component >= 1U && value[position] == '-';
        }
        ++position;
    }
    return allow_suffix && position < value.size() && value[position] == '-';
}

[[nodiscard]] bool version_at_least(const Version& actual, const Version& minimum) noexcept {
    for (std::size_t index = 0; index < actual.components.size(); ++index) {
        if (actual.components[index] != minimum.components[index]) {
            return actual.components[index] > minimum.components[index];
        }
    }
    return true;
}

[[nodiscard]] bool validate_elf_x86_64(const std::filesystem::path& path) {
    std::array<unsigned char, 20> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) return false;
    return header[0] == 0x7fU && header[1] == 'E' && header[2] == 'L' && header[3] == 'F' &&
           header[4] == 2U && header[5] == 1U && header[18] == 62U && header[19] == 0U;
}

[[nodiscard]] std::optional<std::string> fingerprint_tree(
    const std::filesystem::path& root, std::string& error) {
    std::vector<std::string> entries;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        error = "não foi possível percorrer a instalação Proton: " + ec.message();
        return std::nullopt;
    }
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            error = "não foi possível percorrer a instalação Proton: " + ec.message();
            return std::nullopt;
        }
        const auto relative = std::filesystem::relative(iterator->path(), root, ec);
        if (ec || relative.empty()) {
            error = "não foi possível normalizar a instalação Proton";
            return std::nullopt;
        }
        const std::string name = relative.generic_string();
        std::string entry;
        if (iterator->is_symlink(ec)) {
            if (ec) return std::nullopt;
            const auto target = std::filesystem::read_symlink(iterator->path(), ec);
            if (ec) {
                error = "não foi possível ler symlink da instalação Proton: " + ec.message();
                return std::nullopt;
            }
            entry = "L\n" + name + "\n" + target.generic_string() + "\n";
        } else if (iterator->is_directory(ec)) {
            if (ec) return std::nullopt;
            entry = "D\n" + name + "\n";
        } else if (iterator->is_regular_file(ec)) {
            if (ec) return std::nullopt;
            const auto digest = util::sha256_file(iterator->path());
            if (!digest.has_value()) {
                error = "não foi possível calcular o hash da instalação Proton";
                return std::nullopt;
            }
            const auto size = std::filesystem::file_size(iterator->path(), ec);
            if (ec) {
                error = "não foi possível consultar o tamanho da instalação Proton";
                return std::nullopt;
            }
            entry = "F\n" + name + "\n" + std::to_string(size) + "\n" + *digest + "\n";
        } else {
            error = "a instalação Proton contém um tipo de arquivo não suportado";
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
        if (entries.size() > 1'000'000U) {
            error = "a instalação Proton excede o limite de entradas do inventário";
            return std::nullopt;
        }
    }
    if (ec) {
        error = "não foi possível finalizar a leitura da instalação Proton: " + ec.message();
        return std::nullopt;
    }
    std::sort(entries.begin(), entries.end());
    std::string inventory;
    for (const std::string& entry : entries) {
        if (inventory.size() > kMaxFingerprintSize - entry.size()) {
            error = "o inventário da instalação Proton excede o limite de tamanho";
            return std::nullopt;
        }
        inventory += entry;
    }
    return util::sha256_string(inventory);
}

[[nodiscard]] ProtonConfigResult invalid_config(std::string error) {
    ProtonConfigResult result;
    result.status = ConfigStatus::Invalid;
    result.error = std::move(error);
    return result;
}

}  // namespace

std::filesystem::path default_config_path() {
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config_home != nullptr && *xdg_config_home != '\0') {
        return std::filesystem::path{xdg_config_home} / "tradutorlinux" / "backends.json";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / ".config" / "tradutorlinux" / "backends.json";
    }
    return std::filesystem::current_path() / ".config" / "tradutorlinux" / "backends.json";
}

ProtonConfigResult load_config_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) return invalid_config("não foi possível consultar backends.json: " + ec.message());
        return ProtonConfigResult{};
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return invalid_config("backends.json não é um arquivo regular");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) return invalid_config("backends.json não pôde ser aberto");
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxConfigSize) {
        return invalid_config("backends.json excede o limite de tamanho");
    }
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!input) return invalid_config("backends.json não pôde ser lido completamente");
    }

    ProtonConfig config;
    std::string parse_error;
    if (!JsonParser{contents}.parse(config, parse_error)) return invalid_config(parse_error);
    if (config.root.empty() || !config.root.is_absolute()) {
        return invalid_config("root do Proton deve ser um caminho absoluto");
    }
    if (!config.sha256.empty() && !is_hex_string(config.sha256)) {
        return invalid_config("sha256 do Proton deve conter 64 dígitos hexadecimais");
    }

    ProtonConfigResult result;
    result.status = ConfigStatus::Loaded;
    result.config = std::move(config);
    return result;
}

ProtonConfigResult load_config() {
    const char* environment_root = std::getenv("TL_PROTON_ROOT");
    if (environment_root != nullptr && *environment_root != '\0') {
        ProtonConfigResult result;
        result.status = ConfigStatus::Loaded;
        result.config = ProtonConfig{std::filesystem::path{environment_root}, {}};
        return result;
    }
    return load_config_file(default_config_path());
}

ProtonValidationResult validate_proton(const ProtonConfig& config,
                                       const std::string_view minimum_version) {
    ProtonValidationResult result;
    if (config.root.empty() || !config.root.is_absolute()) {
        result.error = "root do Proton deve ser um caminho absoluto";
        return result;
    }
    if (!config.sha256.empty() && !is_hex_string(config.sha256)) {
        result.error = "sha256 do Proton deve conter 64 dígitos hexadecimais";
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(config.root, ec) || ec) {
        result.error = "raiz do Proton ausente ou não é um diretório";
        return result;
    }

    const std::filesystem::path launcher = config.root / "proton";
    const std::filesystem::path wine = config.root / "files" / "bin" / "wine";
    const std::filesystem::path wineserver = config.root / "files" / "bin" / "wineserver";
    const std::filesystem::path version_file = config.root / "version";
    const std::filesystem::path wine_inf = config.root / "files" / "share" / "wine" / "wine.inf";
    const std::filesystem::path default_prefix =
        config.root / "files" / "share" / "default_pfx";
    if (!is_executable_file(launcher) || !is_executable_file(wine) ||
        !is_executable_file(wineserver) || !std::filesystem::is_regular_file(version_file, ec) ||
        !std::filesystem::is_regular_file(wine_inf, ec) ||
        !std::filesystem::is_directory(default_prefix, ec)) {
        result.error = "a instalação Proton não contém os componentes obrigatórios";
        return result;
    }
    if (!validate_elf_x86_64(wine) || !validate_elf_x86_64(wineserver)) {
        result.error = "Wine do Proton não é ELF x86-64";
        return result;
    }
    if (!read_first_line(version_file, result.version)) {
        result.error = "não foi possível ler a versão do Proton";
        return result;
    }

    Version actual;
    if (!parse_version(result.version, actual, true)) {
        result.error = "versão do Proton inválida";
        return result;
    }
    if (!minimum_version.empty()) {
        Version minimum;
        if (!parse_version(minimum_version, minimum, false) ||
            !version_at_least(actual, minimum)) {
            result.error = "a versão do Proton não atende à versão mínima solicitada";
            return result;
        }
    }

    if (!config.sha256.empty()) {
        std::string fingerprint_error;
        const auto fingerprint = fingerprint_tree(config.root, fingerprint_error);
        if (!fingerprint.has_value()) {
            result.error = std::move(fingerprint_error);
            return result;
        }
        result.fingerprint = *fingerprint;
        if (!std::equal(config.sha256.begin(), config.sha256.end(), result.fingerprint.begin(),
                        [](const char expected, const char fingerprint_character) {
                            return static_cast<char>(std::tolower(static_cast<unsigned char>(expected))) ==
                                   static_cast<char>(std::tolower(static_cast<unsigned char>(fingerprint_character)));
                        })) {
            result.error = "hash da instalação Proton não corresponde à configuração";
            return result;
        }
    }

    result.valid = true;
    return result;
}

}  // namespace tradutorlinux::backend
