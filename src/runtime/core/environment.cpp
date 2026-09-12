#include "tradutorlinux/runtime/environment.hpp"

#include "environment_internal.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_set>
#include <utility>

extern "C" char** environ;

namespace tradutorlinux::runtime {
namespace {

struct EnvironmentEntry {
    std::string name;
    std::string value;
};

std::mutex& environment_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, EnvironmentEntry>& environment_entries() {
    static std::map<std::string, EnvironmentEntry> entries;
    return entries;
}

std::unordered_set<std::uint16_t*>& environment_blocks() {
    static std::unordered_set<std::uint16_t*> blocks;
    return blocks;
}

std::vector<std::string>& environment_entries_a() {
    static std::vector<std::string> entries;
    return entries;
}

std::vector<char*>& environment_pointers_a() {
    static std::vector<char*> pointers;
    return pointers;
}

bool& environment_initialized() {
    static bool initialized = false;
    return initialized;
}

std::string normalized_name(const std::string_view name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (const char ch : name) {
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

bool valid_name(const std::string_view name) noexcept {
    return !name.empty() && name.find('=') == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

void set_entry(std::map<std::string, EnvironmentEntry>& entries, const std::string_view name,
               const std::string_view value) {
    const std::string normalized = normalized_name(name);
    entries.insert_or_assign(normalized, EnvironmentEntry{std::string{name}, std::string{value}});
}

void initialize_locked(const std::filesystem::path& prefix_root) {
    std::map<std::string, EnvironmentEntry>& entries = environment_entries();
    entries.clear();
    for (char** current = ::environ; current != nullptr && *current != nullptr; ++current) {
        const char* const equal = std::strchr(*current, '=');
        if (equal == nullptr || equal == *current) {
            continue;
        }
        const std::string_view name{*current, static_cast<std::size_t>(equal - *current)};
        // Configuração exclusiva do backend WinINet de testes. O caminho da
        // CA é consumido pelo host e nunca deve ser observável pelo convidado.
        if (normalized_name(name) == "TL_WININET_CA_FILE") {
            continue;
        }
        set_entry(entries, name, equal + 1);
    }

    const prefix::EnvironmentPaths paths = prefix::get_environment_paths(prefix_root);
    set_entry(entries, "APPDATA", prefix::to_windows_path(paths.app_data_roaming, prefix_root));
    set_entry(entries, "LOCALAPPDATA", prefix::to_windows_path(paths.app_data_local, prefix_root));
    set_entry(entries, "USERPROFILE", "C:\\users\\guest");
    const std::string temp = prefix::to_windows_path(paths.temp_dir, prefix_root);
    set_entry(entries, "TEMP", temp);
    set_entry(entries, "TMP", temp);
    set_entry(entries, "HOMEDRIVE", "C:");
    set_entry(entries, "HOMEPATH", "\\users\\guest");
    environment_initialized() = true;
}

void ensure_initialized_locked() {
    if (!environment_initialized()) {
        initialize_locked(prefix::default_prefix_root());
    }
}

void rebuild_ansi_block_locked() {
    std::vector<std::string>& strings = environment_entries_a();
    strings.clear();
    strings.reserve(environment_entries().size());
    for (const auto& [key, entry] : environment_entries()) {
        (void)key;
        strings.push_back(entry.name + "=" + entry.value);
    }
    std::vector<char*>& pointers = environment_pointers_a();
    pointers.clear();
    pointers.reserve(strings.size() + 1U);
    for (std::string& entry : strings) {
        pointers.push_back(entry.data());
    }
    pointers.push_back(nullptr);
}

}  // namespace

bool checked_environment_block_units(const std::span<const std::size_t> entry_lengths,
                                     std::size_t& units) noexcept {
    units = 1;
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    for (const std::size_t length : entry_lengths) {
        if (length == kMax) {
            return false;
        }
        const std::size_t contribution = length + 1U;
        if (units > kMax - contribution) {
            return false;
        }
        units += contribution;
    }
    return units <= kMax / sizeof(std::uint16_t);
}

void initialize_guest_environment(const std::filesystem::path& prefix_root) {
    std::lock_guard lock(environment_mutex());
    initialize_locked(prefix_root);
}

void clear_guest_environment() noexcept {
    std::lock_guard lock(environment_mutex());
    for (std::uint16_t* const block : environment_blocks()) {
        delete[] block;
    }
    environment_blocks().clear();
    environment_entries().clear();
    environment_entries_a().clear();
    environment_pointers_a().clear();
    environment_initialized() = false;
}

bool guest_environment_is_initialized() noexcept {
    std::lock_guard lock(environment_mutex());
    return environment_initialized();
}

std::optional<std::string> guest_environment_value(const std::string_view name) {
    if (!valid_name(name)) {
        return std::nullopt;
    }
    std::lock_guard lock(environment_mutex());
    ensure_initialized_locked();
    const auto found = environment_entries().find(normalized_name(name));
    if (found == environment_entries().end()) {
        return std::nullopt;
    }
    return found->second.value;
}

const char* guest_environment_cstring(const std::string_view name) noexcept {
    thread_local std::string value;
    const std::optional<std::string> found = guest_environment_value(name);
    if (!found.has_value()) {
        return nullptr;
    }
    value = *found;
    return value.c_str();
}

bool set_guest_environment_value(const std::string_view name,
                                 const std::optional<std::string>& value) noexcept {
    if (!valid_name(name)) {
        return false;
    }
    std::lock_guard lock(environment_mutex());
    ensure_initialized_locked();
    const std::string normalized = normalized_name(name);
    if (!value.has_value()) {
        environment_entries().erase(normalized);
        rebuild_ansi_block_locked();
        return true;
    }
    environment_entries().insert_or_assign(normalized,
                                            EnvironmentEntry{std::string{name}, *value});
    rebuild_ansi_block_locked();
    return true;
}

char** guest_environment_block_a() noexcept {
    std::lock_guard lock(environment_mutex());
    ensure_initialized_locked();
    rebuild_ansi_block_locked();
    return environment_pointers_a().data();
}

std::vector<std::u16string> guest_environment_entries_w() {
    std::lock_guard lock(environment_mutex());
    ensure_initialized_locked();
    std::vector<std::u16string> entries;
    entries.reserve(environment_entries().size());
    for (const auto& [key, entry] : environment_entries()) {
        (void)key;
        std::u16string wide = util::utf8_to_wide(entry.name);
        wide.push_back(u'=');
        wide.append(util::utf8_to_wide(entry.value));
        entries.push_back(std::move(wide));
    }
    return entries;
}

std::uint16_t* allocate_environment_block_w() noexcept {
    std::uint16_t* block = nullptr;
    try {
        const std::vector<std::u16string> entries = guest_environment_entries_w();
        std::vector<std::size_t> entry_lengths;
        entry_lengths.reserve(entries.size());
        for (const std::u16string& entry : entries) {
            entry_lengths.push_back(entry.size());
        }
        std::size_t units = 0;
        if (!checked_environment_block_units(entry_lengths, units)) {
            return nullptr;
        }

        block = new (std::nothrow) std::uint16_t[units]{};
        if (block == nullptr) {
            return nullptr;
        }
        std::size_t offset = 0;
        for (const std::u16string& entry : entries) {
            for (const char16_t unit : entry) {
                block[offset++] = static_cast<std::uint16_t>(unit);
            }
            block[offset++] = 0;
        }
        block[offset] = 0;
        std::lock_guard lock(environment_mutex());
        environment_blocks().insert(block);
        return block;
    } catch (...) {
        delete[] block;
        return nullptr;
    }
}

bool free_environment_block_w(std::uint16_t* const block) noexcept {
    if (block == nullptr) {
        return false;
    }
    std::lock_guard lock(environment_mutex());
    const auto found = environment_blocks().find(block);
    if (found == environment_blocks().end()) {
        return false;
    }
    environment_blocks().erase(found);
    delete[] block;
    return true;
}

}  // namespace tradutorlinux::runtime
