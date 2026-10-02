#include "tradutorlinux/runtime/version.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"
#include "tradutorlinux/pe/resource_inspector.hpp"
#include "tradutorlinux/util/unicode.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "core/runtime_state_common.hpp"
#include "core/runtime_process_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace tradutorlinux {

namespace {

std::mutex g_version_cache_mutex;
std::unordered_map<std::string, std::vector<std::byte>> g_version_cache;

[[nodiscard]] std::string resolve_target_file_path(const std::string_view filename_hint) {
    std::error_code ec;
    if (!filename_hint.empty()) {
        const std::filesystem::path hint_path(filename_hint);
        if (std::filesystem::exists(hint_path, ec) && std::filesystem::is_regular_file(hint_path, ec)) {
            return std::filesystem::canonical(hint_path, ec).string();
        }
        const std::filesystem::path prefix_root = guest_prefix_root();
        const std::string resolved = prefix::resolve_windows_path(filename_hint, prefix_root);
        const std::filesystem::path res_path(resolved);
        if (std::filesystem::exists(res_path, ec) && std::filesystem::is_regular_file(res_path, ec)) {
            return std::filesystem::canonical(res_path, ec).string();
        }
    }
    if (!g_module_file_name.empty()) {
        const std::filesystem::path mod_path(g_module_file_name);
        if (std::filesystem::exists(mod_path, ec) && std::filesystem::is_regular_file(mod_path, ec)) {
            return std::filesystem::canonical(mod_path, ec).string();
        }
    }
    return {};
}

[[nodiscard]] std::vector<std::byte> load_version_bytes_for_file(const std::string& host_path) {
    if (host_path.empty()) {
        return {};
    }
    {
        std::lock_guard<std::mutex> lock(g_version_cache_mutex);
        const auto it = g_version_cache.find(host_path);
        if (it != g_version_cache.end()) {
            return it->second;
        }
    }

    std::ifstream file(host_path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    file.seekg(0, std::ios::end);
    const auto file_size = file.tellg();
    if (file_size <= 0 || file_size > 512 * 1024 * 1024) {
        return {};
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> file_data(static_cast<std::size_t>(file_size));
    file.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_data.size()));
    if (!file) {
        return {};
    }

    const auto parse_res = pe::parse_pe(file_data);
    if (parse_res.status != pe::ParseStatus::Success) {
        return {};
    }

    std::vector<std::byte> version_res = pe::extract_version_resource_bytes(file_data, parse_res.info);
    if (!version_res.empty()) {
        std::lock_guard<std::mutex> lock(g_version_cache_mutex);
        g_version_cache[host_path] = version_res;
    }
    return version_res;
}

[[nodiscard]] std::uint32_t get_file_version_info_size_internal(const std::string_view filename_hint,
                                                                 std::uint32_t* const handle) noexcept {
    if (handle != nullptr) {
        if (!write_guest_value(handle, std::uint32_t{0})) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    const std::string path = resolve_target_file_path(filename_hint);
    if (path.empty()) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }
    const std::vector<std::byte> bytes = load_version_bytes_for_file(path);
    if (bytes.empty()) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(bytes.size());
}

[[nodiscard]] int get_file_version_info_internal(const std::string_view filename_hint,
                                                 const std::uint32_t len,
                                                 void* const data) noexcept {
    if (data == nullptr || len == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string path = resolve_target_file_path(filename_hint);
    if (path.empty()) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }
    const std::vector<std::byte> bytes = load_version_bytes_for_file(path);
    if (bytes.empty()) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }
    const std::size_t to_copy = std::min<std::size_t>(len, bytes.size());
    if (runtime::write_guest_memory(data, bytes.data(), to_copy).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

[[nodiscard]] std::string extract_query_key(std::string_view sub_block) {
    while (!sub_block.empty() && (sub_block.front() == '\\' || sub_block.front() == '/')) {
        sub_block.remove_prefix(1);
    }
    while (!sub_block.empty() && (sub_block.back() == '\\' || sub_block.back() == '/')) {
        sub_block.remove_suffix(1);
    }
    const auto last_slash = sub_block.find_last_of("\\/");
    if (last_slash != std::string_view::npos) {
        return std::string(sub_block.substr(last_slash + 1));
    }
    return std::string(sub_block);
}

[[nodiscard]] int ver_query_value_internal(const void* const block,
                                           const std::string_view sub_block,
                                           void** const buffer,
                                           std::uint32_t* const len,
                                           const bool wide_output) noexcept {
    if (buffer == nullptr || len == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!write_guest_value(buffer, static_cast<void*>(nullptr))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!write_guest_value(len, std::uint32_t{0})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (block == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    const auto* const block_bytes = static_cast<const std::byte*>(block);

    // Consulta à raiz: VS_FIXEDFILEINFO ("" ou "\\")
    if (sub_block.empty() || sub_block == "\\" || sub_block == "/") {
        const std::array<std::byte, 4> kFixedSig{std::byte{0xBD}, std::byte{0x04}, std::byte{0xEF}, std::byte{0xFE}};
        const auto* const it = std::search(block_bytes, block_bytes + 128, kFixedSig.begin(), kFixedSig.end());
        if (it == block_bytes + 128) {
            set_last_error(abi::kErrorResourceNotFound);
            return 0;
        }
        if (!write_guest_value(buffer, const_cast<void*>(static_cast<const void*>(it))) ||
            !write_guest_value(len, std::uint32_t{52})) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }

    // Consulta de Translation ("VarFileInfo\\Translation" ou "Translation")
    if (sub_block.find("Translation") != std::string_view::npos ||
        sub_block.find("translation") != std::string_view::npos) {
        const std::u16string key_u16 = u"Translation";
        const auto* const key_raw = reinterpret_cast<const std::byte*>(key_u16.data());
        const std::size_t key_byte_len = (key_u16.size() + 1) * sizeof(char16_t);
        std::vector<std::byte> needle(key_byte_len);
        std::memcpy(needle.data(), key_raw, key_u16.size() * sizeof(char16_t));
        needle[needle.size() - 2] = std::byte{0};
        needle[needle.size() - 1] = std::byte{0};

        const auto* const it = std::search(block_bytes, block_bytes + 8192, needle.begin(), needle.end());
        if (it != block_bytes + 8192) {
            const std::size_t key_offset = static_cast<std::size_t>(it - block_bytes);
            if (key_offset >= 6) {
                const auto val_len = static_cast<std::uint32_t>(
                    static_cast<std::uint8_t>(block_bytes[key_offset - 4]) |
                    (static_cast<std::uint8_t>(block_bytes[key_offset - 3]) << 8));
                const std::size_t after_key = key_offset + needle.size();
                const std::size_t val_offset = (after_key + 3) & ~std::size_t{3};
                if (!write_guest_value(buffer, const_cast<void*>(static_cast<const void*>(block_bytes + val_offset))) ||
                    !write_guest_value(len, val_len > 0 ? val_len : std::uint32_t{4})) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }

    // Consulta a string (ex: "ProductVersion", "FileVersion", etc.)
    const std::string key_str = extract_query_key(sub_block);
    if (key_str.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }

    const std::u16string wide_key = util::utf8_to_wide(key_str);
    const std::size_t key_byte_len = (wide_key.size() + 1) * sizeof(char16_t);
    std::vector<std::byte> needle(key_byte_len);
    std::memcpy(needle.data(), wide_key.data(), wide_key.size() * sizeof(char16_t));
    needle[needle.size() - 2] = std::byte{0};
    needle[needle.size() - 1] = std::byte{0};

    const auto* const it = std::search(block_bytes, block_bytes + 32768, needle.begin(), needle.end());
    if (it == block_bytes + 32768) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }

    const std::size_t key_offset = static_cast<std::size_t>(it - block_bytes);
    if (key_offset < 6) {
        set_last_error(abi::kErrorResourceNotFound);
        return 0;
    }

    const auto val_chars = static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(block_bytes[key_offset - 4]) |
        (static_cast<std::uint8_t>(block_bytes[key_offset - 3]) << 8));
    const std::size_t after_key = key_offset + needle.size();
    const std::size_t val_offset = (after_key + 3) & ~std::size_t{3};

    if (wide_output) {
        if (!write_guest_value(buffer, const_cast<void*>(static_cast<const void*>(block_bytes + val_offset))) ||
            !write_guest_value(len, val_chars)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        static thread_local std::string s_ansi_result;
        const auto* const val_u16 = reinterpret_cast<const std::uint16_t*>(block_bytes + val_offset);
        const std::size_t num_chars = val_chars > 0 ? (val_chars - 1) : 0;
        s_ansi_result = util::wide_to_utf8(val_u16, num_chars);
        if (!write_guest_value(buffer, const_cast<void*>(static_cast<const void*>(s_ansi_result.c_str()))) ||
            !write_guest_value(len, static_cast<std::uint32_t>(s_ansi_result.size() + 1))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }

    set_last_error(abi::kErrorSuccess);
    return 1;
}

}  // namespace

extern "C" {

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeA(const char* const filename, std::uint32_t* const handle) noexcept {
    const std::string_view hint = filename != nullptr ? filename : "";
    return get_file_version_info_size_internal(hint, handle);
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeW(const std::uint16_t* const filename, std::uint32_t* const handle) noexcept {
    std::string hint;
    if (filename != nullptr) {
        std::u16string wide;
        if (runtime::copy_guest_wstring(filename, 4096U, wide)) {
            hint = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(wide.data()), wide.size());
        }
    }
    return get_file_version_info_size_internal(hint, handle);
}

TL_VER_MSABI int tl_GetFileVersionInfoA(const char* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)handle;
    const std::string_view hint = filename != nullptr ? filename : "";
    return get_file_version_info_internal(hint, len, data);
}

TL_VER_MSABI int tl_GetFileVersionInfoW(const std::uint16_t* const filename, const std::uint32_t handle,
                                        const std::uint32_t len, void* const data) noexcept {
    (void)handle;
    std::string hint;
    if (filename != nullptr) {
        std::u16string wide;
        if (runtime::copy_guest_wstring(filename, 4096U, wide)) {
            hint = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(wide.data()), wide.size());
        }
    }
    return get_file_version_info_internal(hint, len, data);
}

TL_VER_MSABI int tl_VerQueryValueA(const void* const block, const char* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    const std::string_view sub = sub_block != nullptr ? sub_block : "";
    return ver_query_value_internal(block, sub, buffer, len, false);
}

TL_VER_MSABI int tl_VerQueryValueW(const void* const block, const std::uint16_t* const sub_block,
                                   void** const buffer, std::uint32_t* const len) noexcept {
    std::string sub;
    if (sub_block != nullptr) {
        std::u16string wide;
        if (runtime::copy_guest_wstring(sub_block, 4096U, wide)) {
            sub = util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(wide.data()), wide.size());
        }
    }
    return ver_query_value_internal(block, sub, buffer, len, true);
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExA(const std::uint32_t flags, const char* const filename,
                                                        std::uint32_t* const handle) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoSizeA(filename, handle);
}

TL_VER_MSABI std::uint32_t tl_GetFileVersionInfoSizeExW(const std::uint32_t flags, const std::uint16_t* const filename,
                                                        std::uint32_t* const handle) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoSizeW(filename, handle);
}

TL_VER_MSABI int tl_GetFileVersionInfoExA(const std::uint32_t flags, const char* const filename,
                                          const std::uint32_t handle, const std::uint32_t len,
                                          void* const data) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoA(filename, handle, len, data);
}

TL_VER_MSABI int tl_GetFileVersionInfoExW(const std::uint32_t flags, const std::uint16_t* const filename,
                                          const std::uint32_t handle, const std::uint32_t len,
                                          void* const data) noexcept {
    (void)flags;
    return tl_GetFileVersionInfoW(filename, handle, len, data);
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_version_module() {
    static const ExportedFunction kVersionExports[] = {
        {"GetFileVersionInfoSizeA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeA), ExportSupport::Full},
        {"GetFileVersionInfoSizeW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeW), ExportSupport::Full},
        {"GetFileVersionInfoA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoA), ExportSupport::Full},
        {"GetFileVersionInfoW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoW), ExportSupport::Full},
        {"VerQueryValueA", 5, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueA), ExportSupport::Full},
        {"VerQueryValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueW), ExportSupport::Full},
        {"GetFileVersionInfoSizeExA", 7, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExA), ExportSupport::Full},
        {"GetFileVersionInfoSizeExW", 8, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExW), ExportSupport::Full},
        {"GetFileVersionInfoExA", 9, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExA), ExportSupport::Full},
        {"GetFileVersionInfoExW", 10, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExW), ExportSupport::Full},
    };
    static const InternalModule kVersionModule{"version.dll", kVersionExports};
    register_module(kVersionModule);
}

}  // namespace tradutorlinux::loader
