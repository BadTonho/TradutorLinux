#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/runtime/security.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/util/unicode.hpp"
#include "core/runtime_state_common.hpp"
#include "core/runtime_memory_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

using SidBytes = std::vector<std::uint8_t>;

constexpr std::array<char, 8> kStoreMagic{'T', 'L', 'S', 'I', 'D', '1', '0', '\0'};
constexpr std::uint32_t kStoreVersion = 1;
constexpr std::size_t kMaxSecurityFileSize = 8U * 1024U * 1024U;
constexpr std::size_t kMaxAclEntries = 256;
constexpr std::size_t kMaxExplicitEntries = 128;

struct Ace {
    std::uint8_t type{};
    std::uint32_t mask{};
    SidBytes sid;
};

struct SecurityState {
    std::filesystem::path prefix;
    SidBytes user_sid;
    std::unordered_map<std::string, std::vector<std::uint8_t>> dacl_by_path;
    bool loaded{false};
};

struct TokenSlot {
    bool open{false};
};

std::mutex g_security_mutex;
SecurityState g_state;
std::array<TokenSlot, 16> g_tokens{};
std::unordered_set<void*> g_allocated_sids;

[[nodiscard]] bool mapped_range(const void* const address, const std::size_t size,
                                const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

void trace_security(const char* const operation, const char* const status,
                    const char* const detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"scope", "prefix"},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, "security", fields);
}

template <typename T>
void append_value(std::vector<std::uint8_t>& bytes, const T value) {
    const auto* const source = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), source, source + sizeof(value));
}

template <typename T>
[[nodiscard]] bool read_value(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                              T& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

[[nodiscard]] SidBytes make_sid(const std::array<std::uint8_t, 6>& authority,
                                const std::vector<std::uint32_t>& sub_authorities) {
    SidBytes sid(sizeof(abi::GuestSidHeader) + sub_authorities.size() * sizeof(std::uint32_t));
    sid[0] = abi::kSecurityDescriptorRevision;
    sid[1] = static_cast<std::uint8_t>(sub_authorities.size());
    std::copy(authority.begin(), authority.end(), sid.begin() + 2);
    for (std::size_t index = 0; index < sub_authorities.size(); ++index) {
        std::memcpy(sid.data() + sizeof(abi::GuestSidHeader) + index * sizeof(std::uint32_t),
                    &sub_authorities[index], sizeof(std::uint32_t));
    }
    return sid;
}

[[nodiscard]] SidBytes builtin_world_sid() {
    return make_sid({0, 0, 0, 0, 0, 1}, {0});
}

[[nodiscard]] SidBytes builtin_administrators_sid() {
    return make_sid({0, 0, 0, 0, 0, 5}, {32, 544});
}

[[nodiscard]] bool sid_length(const void* const sid, std::size_t& length) noexcept {
    length = 0;
    if (!mapped_range(sid, sizeof(abi::GuestSidHeader), false)) {
        return false;
    }
    const auto* const header = static_cast<const abi::GuestSidHeader*>(sid);
    if (header->revision != abi::kSecurityDescriptorRevision || header->sub_authority_count > 15U) {
        return false;
    }
    length = sizeof(abi::GuestSidHeader) +
             static_cast<std::size_t>(header->sub_authority_count) * sizeof(std::uint32_t);
    return mapped_range(sid, length, false);
}

[[nodiscard]] bool copy_sid(const void* const sid, SidBytes& output) noexcept {
    std::size_t length = 0;
    if (!sid_length(sid, length)) {
        return false;
    }
    const auto* const first = static_cast<const std::uint8_t*>(sid);
    output.assign(first, first + length);
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> make_acl(const std::vector<Ace>& aces) {
    std::size_t total = sizeof(abi::GuestAcl);
    for (const Ace& ace : aces) {
        total += 8U + ace.sid.size();
    }
    if (total > std::numeric_limits<std::uint16_t>::max() ||
        aces.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }
    std::vector<std::uint8_t> result(total, 0);
    auto* const header = reinterpret_cast<abi::GuestAcl*>(result.data());
    header->revision = static_cast<std::uint8_t>(abi::kAclRevision);
    header->acl_size = static_cast<std::uint16_t>(total);
    header->ace_count = static_cast<std::uint16_t>(aces.size());
    std::size_t offset = sizeof(abi::GuestAcl);
    for (const Ace& ace : aces) {
        result[offset] = ace.type;
        const std::uint16_t ace_size = static_cast<std::uint16_t>(8U + ace.sid.size());
        std::memcpy(result.data() + offset + 2, &ace_size, sizeof(ace_size));
        std::memcpy(result.data() + offset + 4, &ace.mask, sizeof(ace.mask));
        std::copy(ace.sid.begin(), ace.sid.end(), result.begin() +
                                                   static_cast<std::ptrdiff_t>(offset + 8));
        offset += ace_size;
    }
    return result;
}

[[nodiscard]] std::uint32_t parse_acl(const void* const acl, std::vector<Ace>& output) noexcept {
    output.clear();
    if (!mapped_range(acl, sizeof(abi::GuestAcl), false)) {
        return abi::kErrorInvalidParameter;
    }
    const auto* const header = static_cast<const abi::GuestAcl*>(acl);
    if (header->revision != abi::kAclRevision || header->acl_size < sizeof(*header) ||
        header->ace_count > kMaxAclEntries || !mapped_range(acl, header->acl_size, false)) {
        return abi::kErrorInvalidParameter;
    }
    const auto* const bytes = static_cast<const std::uint8_t*>(acl);
    std::size_t offset = sizeof(*header);
    for (std::uint16_t index = 0; index < header->ace_count; ++index) {
        if (offset > header->acl_size || header->acl_size - offset < 8U) {
            return abi::kErrorInvalidParameter;
        }
        const std::uint8_t type = bytes[offset];
        const std::uint8_t flags = bytes[offset + 1];
        std::uint16_t ace_size = 0;
        std::uint32_t mask = 0;
        std::memcpy(&ace_size, bytes + offset + 2, sizeof(ace_size));
        std::memcpy(&mask, bytes + offset + 4, sizeof(mask));
        if ((type != abi::kAccessAllowedAceType && type != abi::kAccessDeniedAceType) ||
            flags != 0 || ace_size < 8U || ace_size > header->acl_size - offset) {
            return abi::kErrorNotSupported;
        }
        const void* const sid = bytes + offset + 8;
        std::size_t sid_size = 0;
        if (!sid_length(sid, sid_size) || ace_size != 8U + sid_size) {
            return abi::kErrorInvalidParameter;
        }
        SidBytes sid_copy;
        if (!copy_sid(sid, sid_copy)) {
            return abi::kErrorInvalidParameter;
        }
        output.push_back(Ace{type, mask, std::move(sid_copy)});
        offset += ace_size;
    }
    return offset == header->acl_size ? abi::kErrorSuccess : abi::kErrorInvalidParameter;
}

[[nodiscard]] std::vector<std::uint8_t> default_acl(const SidBytes& user_sid) {
    return make_acl({Ace{static_cast<std::uint8_t>(abi::kAccessAllowedAceType), abi::kGenericAll,
                         user_sid}});
}

[[nodiscard]] std::filesystem::path state_path(const std::filesystem::path& prefix) {
    return prefix / ".tradutorlinux-security.bin";
}

[[nodiscard]] std::uint32_t fallback_seed(const std::string& value, const std::uint32_t salt) noexcept {
    std::uint32_t hash = 2166136261U ^ salt;
    for (const char ch : value) {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
        hash *= 16777619U;
    }
    return hash;
}

[[nodiscard]] std::array<std::uint32_t, 3> make_identity_seed(const std::filesystem::path& prefix) {
    std::array<std::uint32_t, 3> values{};
    std::ifstream random{"/dev/urandom", std::ios::binary};
    if (random) {
        random.read(reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(sizeof(values)));
        if (random.gcount() == static_cast<std::streamsize>(sizeof(values))) {
            return values;
        }
    }
    const std::string text = prefix.string();
    values = {fallback_seed(text, 0x51A3U), fallback_seed(text, 0x92D7U),
              fallback_seed(text, 0x3C19U)};
    return values;
}

[[nodiscard]] bool save_state_locked() {
    if (!g_state.loaded || g_state.user_sid.empty()) {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kStoreMagic.begin(), kStoreMagic.end());
    append_value(bytes, kStoreVersion);
    append_value(bytes, static_cast<std::uint32_t>(g_state.user_sid.size()));
    bytes.insert(bytes.end(), g_state.user_sid.begin(), g_state.user_sid.end());
    append_value(bytes, static_cast<std::uint32_t>(g_state.dacl_by_path.size()));
    for (const auto& [path, dacl] : g_state.dacl_by_path) {
        if (path.size() > std::numeric_limits<std::uint32_t>::max() ||
            dacl.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        append_value(bytes, static_cast<std::uint32_t>(path.size()));
        append_value(bytes, static_cast<std::uint32_t>(dacl.size()));
        bytes.insert(bytes.end(), path.begin(), path.end());
        bytes.insert(bytes.end(), dacl.begin(), dacl.end());
    }
    const std::filesystem::path path = state_path(g_state.prefix);
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::error_code error;
    std::filesystem::create_directories(g_state.prefix, error);
    if (error) {
        return false;
    }
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

[[nodiscard]] bool load_state_locked() {
    const std::filesystem::path prefix = guest_prefix_root();
    if (g_state.loaded && g_state.prefix == prefix) {
        return true;
    }
    g_state = {};
    g_state.prefix = prefix;
    const std::filesystem::path path = state_path(prefix);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        const auto seed = make_identity_seed(prefix);
        g_state.user_sid = make_sid({0, 0, 0, 0, 0, 5},
                                    {21, seed[0], seed[1], seed[2], 1000});
        g_state.loaded = true;
        return save_state_locked();
    }
    if (error || std::filesystem::file_size(path, error) > kMaxSecurityFileSize || error) {
        return false;
    }
    std::ifstream input{path, std::ios::binary};
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    if (!input || bytes.size() < kStoreMagic.size() + 2U * sizeof(std::uint32_t) ||
        !std::equal(kStoreMagic.begin(), kStoreMagic.end(), bytes.begin())) {
        return false;
    }
    std::size_t offset = kStoreMagic.size();
    std::uint32_t version = 0;
    std::uint32_t sid_size = 0;
    if (!read_value(bytes, offset, version) || !read_value(bytes, offset, sid_size) ||
        version != kStoreVersion || sid_size < sizeof(abi::GuestSidHeader) ||
        sid_size > bytes.size() - offset) {
        return false;
    }
    g_state.user_sid.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(offset + sid_size));
    std::size_t checked_length = 0;
    // A cópia está em memória do host; validar a forma sem depender do validator do guest.
    if (g_state.user_sid[0] != abi::kSecurityDescriptorRevision || g_state.user_sid[1] > 15U ||
        g_state.user_sid.size() != sizeof(abi::GuestSidHeader) +
                                       static_cast<std::size_t>(g_state.user_sid[1]) * 4U) {
        return false;
    }
    (void)checked_length;
    offset += sid_size;
    std::uint32_t record_count = 0;
    if (!read_value(bytes, offset, record_count) || record_count > kMaxAclEntries) {
        return false;
    }
    for (std::uint32_t index = 0; index < record_count; ++index) {
        std::uint32_t path_size = 0;
        std::uint32_t dacl_size = 0;
        if (!read_value(bytes, offset, path_size) || !read_value(bytes, offset, dacl_size) ||
            path_size > bytes.size() - offset || dacl_size > bytes.size() - offset - path_size) {
            return false;
        }
        std::string key(reinterpret_cast<const char*>(bytes.data() + offset), path_size);
        offset += path_size;
        std::vector<std::uint8_t> dacl(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + dacl_size));
        offset += dacl_size;
        // ACL persistida já foi produzida pelo runtime; validar tamanho mínimo para rejeitar corrupção.
        if (dacl.size() < sizeof(abi::GuestAcl)) {
            return false;
        }
        g_state.dacl_by_path.emplace(std::move(key), std::move(dacl));
    }
    if (offset != bytes.size()) {
        return false;
    }
    g_state.loaded = true;
    return true;
}

enum class PathStatus { Success, InvalidParameter, AccessDenied, FileNotFound, InternalError };

[[nodiscard]] std::uint32_t path_error(const PathStatus status) noexcept {
    switch (status) {
        case PathStatus::Success: return abi::kErrorSuccess;
        case PathStatus::InvalidParameter: return abi::kErrorInvalidParameter;
        case PathStatus::AccessDenied: return abi::kErrorAccessDenied;
        case PathStatus::FileNotFound: return abi::kErrorFileNotFound;
        case PathStatus::InternalError: return abi::kErrorNotSupported;
    }
    return abi::kErrorInvalidParameter;
}

[[nodiscard]] PathStatus key_for_path(const std::filesystem::path& input, std::string& key,
                                      const bool require_existing = true) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(input, error);
    if (error) {
        return PathStatus::InternalError;
    }
    const std::filesystem::path drive_c = prefix::get_environment_paths(guest_prefix_root()).drive_c;
    if (!prefix::is_path_within(canonical, drive_c)) {
        return PathStatus::AccessDenied;
    }
    if (require_existing && !std::filesystem::exists(canonical, error)) {
        return error ? PathStatus::InternalError : PathStatus::FileNotFound;
    }
    const std::filesystem::path canonical_drive = std::filesystem::weakly_canonical(drive_c, error);
    if (error) {
        return PathStatus::InternalError;
    }
    const std::filesystem::path relative = std::filesystem::relative(canonical, canonical_drive, error);
    if (error || relative.empty() || relative == ".") {
        return PathStatus::InvalidParameter;
    }
    key = relative.generic_string();
    return PathStatus::Success;
}

[[nodiscard]] PathStatus key_for_wide_path(const std::uint16_t* const input, std::string& key) {
    if (input == nullptr || !runtime::validate_mapped_wstring(input)) {
        return PathStatus::InvalidParameter;
    }
    const std::filesystem::path resolved =
        prefix::resolve_windows_path(util::wide_to_utf8(input), guest_prefix_root());
    if (resolved.empty()) {
        return PathStatus::InvalidParameter;
    }
    return key_for_path(resolved, key);
}

[[nodiscard]] bool valid_token_locked(const void* const token) noexcept {
    return std::any_of(g_tokens.begin(), g_tokens.end(), [token](const TokenSlot& slot) {
        return slot.open && token == static_cast<const void*>(&slot);
    });
}

[[nodiscard]] std::uint32_t set_dacl_for_key_locked(const std::string& key,
                                                     const void* const dacl) {
    std::vector<Ace> parsed;
    const std::uint32_t parse_status = parse_acl(dacl, parsed);
    if (parse_status != abi::kErrorSuccess) {
        return parse_status;
    }
    std::vector<std::uint8_t> copied = make_acl(parsed);
    if (copied.empty()) {
        return abi::kErrorNotEnoughMemory;
    }
    g_state.dacl_by_path.insert_or_assign(key, std::move(copied));
    return save_state_locked() ? abi::kErrorSuccess : abi::kErrorAccessDenied;
}

[[nodiscard]] std::vector<std::uint8_t> dacl_for_key_locked(const std::string& key) {
    const auto found = g_state.dacl_by_path.find(key);
    return found != g_state.dacl_by_path.end() ? found->second : default_acl(g_state.user_sid);
}

[[nodiscard]] void* allocate_descriptor(const SidBytes& owner, const SidBytes& group,
                                         const std::vector<std::uint8_t>& dacl,
                                         const std::uint32_t information,
                                         void** owner_out, void** group_out,
                                         void** dacl_out) noexcept {
    const bool include_owner = (information & abi::kOwnerSecurityInformation) != 0;
    const bool include_group = (information & abi::kGroupSecurityInformation) != 0;
    const bool include_dacl = (information & abi::kDaclSecurityInformation) != 0;
    const std::size_t size = sizeof(abi::GuestSecurityDescriptor) +
                             (include_owner ? owner.size() : 0U) +
                             (include_group ? group.size() : 0U) +
                             (include_dacl ? dacl.size() : 0U);
    auto* const block = static_cast<std::uint8_t*>(std::malloc(size));
    if (block == nullptr) {
        return nullptr;
    }
    if (!register_local_free_block(block)) {
        std::free(block);
        return nullptr;
    }
    std::memset(block, 0, size);
    auto* const descriptor = reinterpret_cast<abi::GuestSecurityDescriptor*>(block);
    descriptor->revision = static_cast<std::uint8_t>(abi::kSecurityDescriptorRevision);
    std::size_t offset = sizeof(*descriptor);
    if (include_owner) {
        descriptor->owner = block + offset;
        std::memcpy(descriptor->owner, owner.data(), owner.size());
        offset += owner.size();
    }
    if (include_group) {
        descriptor->group = block + offset;
        std::memcpy(descriptor->group, group.data(), group.size());
        offset += group.size();
    }
    if (include_dacl) {
        descriptor->control = static_cast<std::uint16_t>(abi::kSeDaclPresent);
        descriptor->dacl = block + offset;
        std::memcpy(descriptor->dacl, dacl.data(), dacl.size());
    }
    if (owner_out != nullptr) *owner_out = descriptor->owner;
    if (group_out != nullptr) *group_out = descriptor->group;
    if (dacl_out != nullptr) *dacl_out = descriptor->dacl;
    return descriptor;
}

[[nodiscard]] bool valid_output_pointer(void** const value) noexcept {
    return value == nullptr || mapped_range(value, sizeof(*value), true);
}

}  // namespace

namespace runtime::security {

void reset_prefix_cache() noexcept {
    std::lock_guard lock(g_security_mutex);
    g_state = {};
    for (TokenSlot& slot : g_tokens) {
        slot = {};
    }
}

bool close_token_handle(const void* const handle) noexcept {
    std::lock_guard lock(g_security_mutex);
    for (TokenSlot& slot : g_tokens) {
        if (slot.open && handle == static_cast<const void*>(&slot)) {
            slot = {};
            return true;
        }
    }
    return false;
}

void remove_path(const std::filesystem::path& path) noexcept {
    try {
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            return;
        }
        std::string key;
        const PathStatus result = key_for_path(path, key, false);
        if (result != PathStatus::Success) {
            return;
        }
        bool changed = false;
        const std::string descendant = key + "/";
        for (auto it = g_state.dacl_by_path.begin(); it != g_state.dacl_by_path.end();) {
            if (it->first == key || it->first.starts_with(descendant)) {
                it = g_state.dacl_by_path.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) {
            static_cast<void>(save_state_locked());
        }
    } catch (...) {
    }
}

void rename_path(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
    try {
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            return;
        }
        std::string old_key;
        std::string new_key;
        if (key_for_path(from, old_key, false) != PathStatus::Success ||
            key_for_path(to, new_key, false) != PathStatus::Success) {
            return;
        }
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> moved;
        const std::string descendant = old_key + "/";
        for (auto it = g_state.dacl_by_path.begin(); it != g_state.dacl_by_path.end();) {
            if (it->first == old_key || it->first.starts_with(descendant)) {
                const std::string suffix = it->first.substr(old_key.size());
                moved.emplace_back(new_key + suffix, std::move(it->second));
                it = g_state.dacl_by_path.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& item : moved) {
            g_state.dacl_by_path.insert_or_assign(std::move(item.first), std::move(item.second));
        }
        if (!moved.empty()) {
            static_cast<void>(save_state_locked());
        }
    } catch (...) {
    }
}

}  // namespace runtime::security

extern "C" {

TL_ADVAPI_MSABI int tl_OpenProcessToken(const void* const process, const std::uint32_t desired_access,
                                        void** const token) noexcept {
    try {
        if (process != reinterpret_cast<const void*>(~static_cast<std::uintptr_t>(0)) ||
            token == nullptr || !mapped_range(token, sizeof(*token), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if ((desired_access & abi::kTokenQuery) == 0) {
            set_last_error(abi::kErrorAccessDenied);
            return 0;
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            set_last_error(abi::kErrorAccessDenied);
            return 0;
        }
        const auto found = std::find_if(g_tokens.begin(), g_tokens.end(),
                                        [](const TokenSlot& slot) { return !slot.open; });
        if (found == g_tokens.end()) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        found->open = true;
        *token = &*found;
        set_last_error(abi::kErrorSuccess);
        trace_security("open-token", "success", "current-process");
        return 1;
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

TL_ADVAPI_MSABI int tl_GetTokenInformation(const void* const token,
                                           const std::uint32_t information_class,
                                           void* const information,
                                           const std::uint32_t information_length,
                                           std::uint32_t* const return_length) noexcept {
    try {
        if (return_length == nullptr || !mapped_range(return_length, sizeof(*return_length), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked() || !valid_token_locked(token)) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        std::size_t required = 0;
        if (information_class == abi::kTokenUser) {
            required = sizeof(abi::GuestTokenUser) + g_state.user_sid.size();
        } else if (information_class == abi::kTokenElevation) {
            required = sizeof(abi::GuestTokenElevation);
        } else {
            set_last_error(abi::kErrorNotSupported);
            return 0;
        }
        *return_length = static_cast<std::uint32_t>(required);
        if (information == nullptr || information_length < required) {
            set_last_error(abi::kErrorInsufficientBuffer);
            return 0;
        }
        if (!mapped_range(information, required, true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if (information_class == abi::kTokenUser) {
            auto* const result = static_cast<abi::GuestTokenUser*>(information);
            result->user = {};
            result->user.sid = static_cast<std::uint8_t*>(information) + sizeof(*result);
            std::memcpy(result->user.sid, g_state.user_sid.data(), g_state.user_sid.size());
        } else {
            *static_cast<abi::GuestTokenElevation*>(information) = {};
        }
        set_last_error(abi::kErrorSuccess);
        trace_security("token-information", "success",
                       information_class == abi::kTokenUser ? "user" : "elevation");
        return 1;
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

TL_ADVAPI_MSABI int tl_AllocateAndInitializeSid(const void* const identifier_authority,
                                                const std::uint8_t sub_authority_count,
                                                const std::uint32_t sub_authority0,
                                                const std::uint32_t sub_authority1,
                                                const std::uint32_t sub_authority2,
                                                const std::uint32_t sub_authority3,
                                                const std::uint32_t sub_authority4,
                                                const std::uint32_t sub_authority5,
                                                const std::uint32_t sub_authority6,
                                                const std::uint32_t sub_authority7,
                                                void** const sid) noexcept {
    try {
        if (identifier_authority == nullptr || sub_authority_count > 8U || sid == nullptr ||
            !mapped_range(identifier_authority, 6, false) || !mapped_range(sid, sizeof(*sid), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::array<std::uint32_t, 8> subs{sub_authority0, sub_authority1, sub_authority2,
                                                 sub_authority3, sub_authority4, sub_authority5,
                                                 sub_authority6, sub_authority7};
        std::array<std::uint8_t, 6> authority{};
        std::memcpy(authority.data(), identifier_authority, authority.size());
        const std::vector<std::uint32_t> selected{subs.begin(),
                                                  subs.begin() + sub_authority_count};
        SidBytes data = make_sid(authority, selected);
        void* const allocated = std::malloc(data.size());
        if (allocated == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::memcpy(allocated, data.data(), data.size());
        {
            std::lock_guard lock(g_security_mutex);
            g_allocated_sids.insert(allocated);
        }
        *sid = allocated;
        set_last_error(abi::kErrorSuccess);
        return 1;
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

TL_ADVAPI_MSABI void* tl_FreeSid(void* const sid) noexcept {
    std::lock_guard lock(g_security_mutex);
    const auto found = g_allocated_sids.find(sid);
    if (found == g_allocated_sids.end()) {
        set_last_error(abi::kErrorInvalidParameter);
        return sid;
    }
    std::free(*found);
    g_allocated_sids.erase(found);
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_ADVAPI_MSABI std::uint32_t tl_GetLengthSid(const void* const sid) noexcept {
    std::size_t length = 0;
    if (!sid_length(sid, length)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(length);
}

TL_ADVAPI_MSABI int tl_CopySid(const std::uint32_t destination_length, void* const destination,
                               const void* const source) noexcept {
    std::size_t source_length = 0;
    if (!sid_length(source, source_length) || destination == nullptr ||
        destination_length < source_length || !mapped_range(destination, source_length, true)) {
        set_last_error(destination != nullptr && destination_length < source_length
                           ? abi::kErrorInsufficientBuffer
                           : abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(destination, source, source_length);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_EqualSid(const void* const first, const void* const second) noexcept {
    SidBytes left;
    SidBytes right;
    if (!copy_sid(first, left) || !copy_sid(second, right)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return left == right ? 1 : 0;
}

TL_ADVAPI_MSABI int tl_IsValidSid(const void* const sid) noexcept {
    std::size_t length = 0;
    const int valid = sid_length(sid, length) ? 1 : 0;
    set_last_error(valid != 0 ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
    return valid;
}

TL_ADVAPI_MSABI int tl_CreateWellKnownSid(const std::uint32_t well_known_sid_type,
                                          const void* const domain_sid, void* const sid,
                                          std::uint32_t* const sid_size) noexcept {
    if (domain_sid != nullptr || sid_size == nullptr || !mapped_range(sid_size, sizeof(*sid_size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    SidBytes result;
    if (well_known_sid_type == abi::kWinWorldSid) {
        result = builtin_world_sid();
    } else if (well_known_sid_type == abi::kWinBuiltinAdministratorsSid) {
        result = builtin_administrators_sid();
    } else {
        set_last_error(abi::kErrorNotSupported);
        return 0;
    }
    const std::uint32_t required = static_cast<std::uint32_t>(result.size());
    if (sid == nullptr || *sid_size < required) {
        *sid_size = required;
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    if (!mapped_range(sid, required, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(sid, result.data(), result.size());
    *sid_size = required;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_CheckTokenMembership(const void* const token, const void* const sid,
                                            int* const is_member) noexcept {
    try {
        if (is_member == nullptr || !mapped_range(is_member, sizeof(*is_member), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        SidBytes checked;
        if (!copy_sid(sid, checked)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked() || (token != nullptr && !valid_token_locked(token))) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        *is_member = checked == g_state.user_sid ? 1 : 0;
        set_last_error(abi::kErrorSuccess);
        return 1;
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

TL_ADVAPI_MSABI void tl_BuildTrusteeWithSidW(void* const trustee, void* const sid) noexcept {
    std::size_t sid_size = 0;
    if (trustee == nullptr || !mapped_range(trustee, sizeof(abi::GuestTrusteeW), true) ||
        !sid_length(sid, sid_size)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    auto* const result = static_cast<abi::GuestTrusteeW*>(trustee);
    *result = {};
    result->multiple_trustee_operation = abi::kNoMultipleTrustee;
    result->trustee_form = abi::kTrusteeIsSid;
    result->trustee_type = abi::kTrusteeIsUnknown;
    result->name = sid;
    set_last_error(abi::kErrorSuccess);
}

TL_ADVAPI_MSABI int tl_InitializeSecurityDescriptor(void* const descriptor,
                                                     const std::uint32_t revision) noexcept {
    if (descriptor == nullptr || revision != abi::kSecurityDescriptorRevision ||
        !mapped_range(descriptor, sizeof(abi::GuestSecurityDescriptor), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const result = static_cast<abi::GuestSecurityDescriptor*>(descriptor);
    *result = {};
    result->revision = static_cast<std::uint8_t>(revision);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI int tl_SetSecurityDescriptorDacl(void* const descriptor, const int dacl_present,
                                                 void* const dacl, const int dacl_defaulted) noexcept {
    (void)dacl_defaulted;
    std::vector<Ace> parsed;
    if (descriptor == nullptr || dacl_present == 0 || dacl == nullptr ||
        !mapped_range(descriptor, sizeof(abi::GuestSecurityDescriptor), true) ||
        static_cast<abi::GuestSecurityDescriptor*>(descriptor)->revision !=
            abi::kSecurityDescriptorRevision) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t acl_status = parse_acl(dacl, parsed);
    if (acl_status != abi::kErrorSuccess) {
        set_last_error(acl_status);
        return 0;
    }
    auto* const result = static_cast<abi::GuestSecurityDescriptor*>(descriptor);
    result->control = static_cast<std::uint16_t>(result->control | abi::kSeDaclPresent);
    result->dacl = dacl;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_ADVAPI_MSABI std::uint32_t tl_SetEntriesInAclW(const std::uint32_t entry_count,
                                                  const void* const entries, const void* const old_acl,
                                                  void** const new_acl) noexcept {
    try {
        if (new_acl == nullptr || !mapped_range(new_acl, sizeof(*new_acl), true) ||
            entry_count > kMaxExplicitEntries ||
            (entry_count != 0 && (entries == nullptr ||
                                  !mapped_range(entries, entry_count * sizeof(abi::GuestExplicitAccessW),
                                                false)))) {
            return abi::kErrorInvalidParameter;
        }
        std::vector<Ace> result;
        if (old_acl != nullptr) {
            const std::uint32_t parsed = parse_acl(old_acl, result);
            if (parsed != abi::kErrorSuccess) {
                return parsed;
            }
        }
        const auto* const list = static_cast<const abi::GuestExplicitAccessW*>(entries);
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            const abi::GuestExplicitAccessW& entry = list[index];
            if (entry.inheritance != 0 || entry.trustee.multiple_trustee != nullptr ||
                entry.trustee.multiple_trustee_operation != abi::kNoMultipleTrustee ||
                entry.trustee.trustee_form != abi::kTrusteeIsSid ||
                entry.trustee.trustee_type != abi::kTrusteeIsUnknown) {
                return abi::kErrorNotSupported;
            }
            SidBytes sid;
            if (!copy_sid(entry.trustee.name, sid)) {
                return abi::kErrorInvalidParameter;
            }
            const auto same_sid = [&sid](const Ace& ace) { return ace.sid == sid; };
            if (entry.access_mode == abi::kRevokeAccess) {
                std::erase_if(result, same_sid);
                continue;
            }
            if (entry.access_mode == abi::kSetAccess) {
                std::erase_if(result, same_sid);
            }
            const std::uint8_t type = entry.access_mode == abi::kDenyAccess
                                          ? static_cast<std::uint8_t>(abi::kAccessDeniedAceType)
                                          : static_cast<std::uint8_t>(abi::kAccessAllowedAceType);
            if (entry.access_mode != abi::kGrantAccess && entry.access_mode != abi::kSetAccess &&
                entry.access_mode != abi::kDenyAccess) {
                return abi::kErrorNotSupported;
            }
            Ace new_ace{type, entry.access_permissions, std::move(sid)};
            if (type == abi::kAccessDeniedAceType) {
                result.insert(result.begin(), std::move(new_ace));
            } else {
                const auto first_allow = std::find_if(result.begin(), result.end(), [](const Ace& ace) {
                    return ace.type == abi::kAccessAllowedAceType;
                });
                result.insert(first_allow, std::move(new_ace));
            }
        }
        std::vector<std::uint8_t> encoded = make_acl(result);
        if (encoded.empty()) {
            return abi::kErrorNotEnoughMemory;
        }
        void* const allocation = std::malloc(encoded.size());
        if (allocation == nullptr) {
            return abi::kErrorNotEnoughMemory;
        }
        std::memcpy(allocation, encoded.data(), encoded.size());
        if (!register_local_free_block(allocation)) {
            std::free(allocation);
            return abi::kErrorNotEnoughMemory;
        }
        *new_acl = allocation;
        trace_security("set-entries", "success", "dacl");
        return abi::kErrorSuccess;
    } catch (...) {
        return abi::kErrorNotEnoughMemory;
    }
}

TL_ADVAPI_MSABI std::uint32_t tl_GetNamedSecurityInfoW(
    const std::uint16_t* const object_name, const std::uint32_t object_type,
    const std::uint32_t security_information, void** const owner, void** const group,
    void** const dacl, void** const sacl, void** const descriptor) noexcept {
    try {
        if (object_type != abi::kSeFileObject || security_information == 0 || descriptor == nullptr ||
            !valid_output_pointer(owner) ||
            !valid_output_pointer(group) || !valid_output_pointer(dacl) ||
            !mapped_range(descriptor, sizeof(*descriptor), true)) {
            return abi::kErrorInvalidParameter;
        }
        if (sacl != nullptr || (security_information & abi::kSaclSecurityInformation) != 0) {
            return abi::kErrorNotSupported;
        }
        if ((security_information & ~(abi::kOwnerSecurityInformation | abi::kGroupSecurityInformation |
                                     abi::kDaclSecurityInformation)) != 0) {
            return abi::kErrorInvalidParameter;
        }
        std::string key;
        const PathStatus path_status = key_for_wide_path(object_name, key);
        if (path_status != PathStatus::Success) {
            return path_error(path_status);
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            return abi::kErrorAccessDenied;
        }
        if (owner != nullptr) *owner = nullptr;
        if (group != nullptr) *group = nullptr;
        if (dacl != nullptr) *dacl = nullptr;
        *descriptor = allocate_descriptor(g_state.user_sid, g_state.user_sid, dacl_for_key_locked(key),
                                          security_information, owner, group, dacl);
        if (*descriptor == nullptr) {
            return abi::kErrorNotEnoughMemory;
        }
        trace_security("named-get", "success", "descriptor");
        return abi::kErrorSuccess;
    } catch (...) {
        return abi::kErrorNotEnoughMemory;
    }
}

TL_ADVAPI_MSABI std::uint32_t tl_SetNamedSecurityInfoW(
    std::uint16_t* const object_name, const std::uint32_t object_type,
    const std::uint32_t security_information, void* const owner, void* const group,
    void* const dacl, void* const sacl, const std::uint32_t inheritance) noexcept {
    try {
        if (object_type != abi::kSeFileObject || security_information != abi::kDaclSecurityInformation ||
            owner != nullptr || group != nullptr || sacl != nullptr || inheritance != 0 || dacl == nullptr) {
            return abi::kErrorNotSupported;
        }
        std::string key;
        const PathStatus path_status = key_for_wide_path(object_name, key);
        if (path_status != PathStatus::Success) {
            return path_error(path_status);
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            return abi::kErrorAccessDenied;
        }
        const std::uint32_t result = set_dacl_for_key_locked(key, dacl);
        if (result == abi::kErrorSuccess) {
            trace_security("named-set", "success", "dacl");
        }
        return result;
    } catch (...) {
        return abi::kErrorNotEnoughMemory;
    }
}

TL_ADVAPI_MSABI int tl_SetFileSecurityW(const std::uint16_t* const file_name,
                                        const std::uint32_t security_information,
                                        const void* const security_descriptor) noexcept {
    try {
        if ((security_information & abi::kSaclSecurityInformation) != 0) {
            set_last_error(abi::kErrorNotSupported);
            return 0;
        }
        if (security_information != abi::kDaclSecurityInformation || security_descriptor == nullptr ||
            !mapped_range(security_descriptor, sizeof(abi::GuestSecurityDescriptor), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto* const descriptor = static_cast<const abi::GuestSecurityDescriptor*>(security_descriptor);
        if (descriptor->revision != abi::kSecurityDescriptorRevision ||
            (descriptor->control & abi::kSeDaclPresent) == 0 || descriptor->dacl == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        std::string key;
        const PathStatus path_status = key_for_wide_path(file_name, key);
        if (path_status != PathStatus::Success) {
            set_last_error(path_error(path_status));
            return 0;
        }
        std::lock_guard lock(g_security_mutex);
        if (!load_state_locked()) {
            set_last_error(abi::kErrorAccessDenied);
            return 0;
        }
        const std::uint32_t result = set_dacl_for_key_locked(key, descriptor->dacl);
        set_last_error(result);
        if (result == abi::kErrorSuccess) {
            trace_security("file-set", "success", "dacl");
            return 1;
        }
        return 0;
    } catch (...) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
}

}  // extern "C"

}  // namespace tradutorlinux
