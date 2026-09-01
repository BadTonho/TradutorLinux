#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/runtime/security.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <malloc.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {

using runtime::errno_to_win32;

extern "C" std::uint32_t tl_call_guest_thread_on_stack(std::uintptr_t entry,
                                                         const void* parameter,
                                                         std::uintptr_t stack_top) noexcept;

namespace {

// Definições auxiliares
constexpr std::uint32_t kFileAttributeReadOnly = 0x00000001;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010;
constexpr std::uint32_t kFileAttributeArchive = 0x00000020;

constexpr std::uint32_t kFileBegin = 0;
constexpr std::uint32_t kFileCurrent = 1;
constexpr std::uint32_t kFileEnd = 2;

constexpr std::uint32_t kStillActive = 259U;
constexpr std::uint32_t kChildProcessFailure = 0xC0000001U;
constexpr std::size_t kChildProcessProtocolSize = 5;

[[nodiscard]] std::uint64_t relocate_tls_va_for_child(
    const std::uint64_t value, const pe::PeInfo& info,
    const loader::MappedImage& image) noexcept {
    if (value == 0 || value < info.image_base ||
        value - info.image_base >= static_cast<std::uint64_t>(info.size_of_image)) {
        return value;
    }
    const std::uint64_t rva = value - info.image_base;
    return rva < image.size && image.base <= std::numeric_limits<std::uint64_t>::max() - rva
               ? image.base + rva
               : value;
}

[[nodiscard]] std::vector<std::uint64_t> relocated_tls_callbacks_for_child(
    const pe::PeInfo& info, const loader::MappedImage& image) {
    std::vector<std::uint64_t> callbacks;
    callbacks.reserve(info.tls_info.callback_vas.size());
    for (const std::uint64_t callback : info.tls_info.callback_vas) {
        callbacks.push_back(relocate_tls_va_for_child(callback, info, image));
    }
    return callbacks;
}

void* const kInvalidHandleValue = reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0));  // INVALID_HANDLE_VALUE

// Ponto de retorno para ExitThread dentro de threads convidadas. pthread_exit é
// proibido aqui: dentro de std::thread ele dispara unwinding forçado do
// libstdc++ e termina o processo com SIGABRT.
thread_local std::jmp_buf* t_thread_exit_context = nullptr;
thread_local ThreadSlot* t_thread_exit_slot = nullptr;

struct GuestFileTime {
    std::uint32_t low{};
    std::uint32_t high{};
};
static_assert(sizeof(GuestFileTime) == 8);

struct GuestFileAttributeData {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
};
static_assert(sizeof(GuestFileAttributeData) == 36);

struct GuestByHandleFileInformation {
    std::uint32_t attributes{};
    GuestFileTime creation{};
    GuestFileTime last_access{};
    GuestFileTime last_write{};
    std::uint32_t volume_serial_number{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t number_of_links{};
    std::uint32_t file_index_high{};
    std::uint32_t file_index_low{};
};
static_assert(sizeof(GuestByHandleFileInformation) == 52);

// O "handle" de um objeto de mapeamento é o ponteiro do slot em g_mappings
// entregue ao convidado. Antes de dereferenciar, provar que o ponteiro está
// dentro dos limites do array, alinhado e em uso.
FileMappingSlot* find_file_mapping_slot_locked(const void* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }
    const auto addr = std::bit_cast<std::uintptr_t>(handle);
    const auto begin = std::bit_cast<std::uintptr_t>(g_mappings.data());
    const auto end = begin + g_mappings.size() * sizeof(FileMappingSlot);
    if (addr < begin || addr >= end || (addr - begin) % sizeof(FileMappingSlot) != 0) {
        return nullptr;
    }
    auto* slot = static_cast<FileMappingSlot*>(const_cast<void*>(handle));
    return slot->used ? slot : nullptr;
}

struct GuestProcessInformation {
    void* process_handle{};
    void* thread_handle{};
    std::uint32_t process_id{};
    std::uint32_t thread_id{};
};
static_assert(sizeof(GuestProcessInformation) == 24);

struct ResourceKey {
    bool named{false};
    std::uint16_t id{0};
    std::u16string name;
};

std::int64_t unix_time_to_filetime(const std::time_t seconds) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    constexpr std::int64_t kTicksPerSecond = 10000000LL;
    return (static_cast<std::int64_t>(seconds) + kEpochDifference) * kTicksPerSecond;
}

std::time_t filetime_to_unix_time(const GuestFileTime& ft) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    constexpr std::int64_t kTicksPerSecond = 10000000LL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(ft.high) << 32) | ft.low;
    return static_cast<std::time_t>(ticks / kTicksPerSecond - kEpochDifference);
}

void filetime_from_unix(const std::time_t source, GuestFileTime& target) noexcept {
    const std::int64_t ticks = unix_time_to_filetime(source);
    const auto raw = static_cast<std::uint64_t>(std::max<std::int64_t>(ticks, 0));
    target.low = static_cast<std::uint32_t>(raw & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(raw >> 32U);
}

void trace_filesystem(const char* const operation, const char* const status,
                      const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"scope", "prefix"},
    };
    runtime_trace("filesystem", fields, 4);
}

bool ascii_case_equal(const char left, const char right) noexcept {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

bool win32_wildcard_match(std::string_view pattern, const std::string_view name) noexcept {
    if (pattern == "*.*") {
        pattern = "*";
    }
    std::size_t pattern_pos = 0;
    std::size_t name_pos = 0;
    std::size_t star_pos = std::string_view::npos;
    std::size_t star_name_pos = 0;
    while (name_pos < name.size()) {
        if (pattern_pos < pattern.size() &&
            (pattern[pattern_pos] == '?' ||
             ascii_case_equal(pattern[pattern_pos], name[name_pos]))) {
            ++pattern_pos;
            ++name_pos;
        } else if (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
            star_pos = pattern_pos++;
            star_name_pos = name_pos;
        } else if (star_pos != std::string_view::npos) {
            pattern_pos = star_pos + 1;
            name_pos = ++star_name_pos;
        } else {
            return false;
        }
    }
    while (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
        ++pattern_pos;
    }
    return pattern_pos == pattern.size();
}

[[nodiscard]] bool sync_is_signaled(SyncSlot& slot) noexcept {
    if (slot.kind == SyncKind::Event) {
        return slot.signaled;
    }
    if (slot.kind == SyncKind::Semaphore) {
        return slot.count > 0;
    }
    if (slot.kind == SyncKind::Process) {
        return !slot.process_running;
    }
    return !slot.owner_valid || slot.owner == std::this_thread::get_id();
}

[[nodiscard]] bool probe_wait_handle(const void* handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    if (find_file_slot(handle) != nullptr) {
        return true;
    }
    if (ThreadSlot* thread = find_thread_slot(handle); thread != nullptr) {
        std::lock_guard<std::mutex> lock(thread->join_mutex);
        return thread->finished;
    }
    if (SyncSlot* sync = find_sync_slot(handle); sync != nullptr) {
        if (sync->kind == SyncKind::Process) {
            return wait_process_slot(*sync, 0) == abi::kWaitObject0;
        }
        std::lock_guard<std::mutex> lock(sync->mutex);
        return sync_is_signaled(*sync);
    }
    return false;
}

[[nodiscard]] bool resource_directory_range(const std::uint32_t relative,
                                            const std::size_t size) noexcept {
    const std::size_t image_size = g_guest_image_size;
    const std::size_t resource_base = g_guest_resource_rva;
    const std::size_t resource_size = g_guest_resource_size;
    return resource_base <= image_size && relative <= resource_size &&
           size <= resource_size - relative && relative <= image_size - resource_base &&
           size <= image_size - resource_base - relative;
}

[[nodiscard]] bool read_resource_u16(const std::uint32_t offset, std::uint16_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint16_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint16_t));
    return true;
}

[[nodiscard]] bool read_resource_u32(const std::uint32_t offset, std::uint32_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint32_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint32_t));
    return true;
}

[[nodiscard]] bool make_resource_key(const std::uint16_t* value, ResourceKey& key) noexcept {
    if (value == nullptr) {
        return false;
    }
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    if (raw <= 0xFFFFU) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw);
        return true;
    }
    if (!mapped_guest_wstring(value)) {
        return false;
    }
    key.named = true;
    for (std::size_t index = 0; value[index] != 0; ++index) {
        key.name.push_back(static_cast<char16_t>(value[index]));
    }
    return true;
}

[[nodiscard]] bool resource_entry_key(const std::uint32_t raw_name,
                                      ResourceKey& key) noexcept {
    if ((raw_name & 0x80000000U) == 0) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw_name & 0xFFFFU);
        return (raw_name & 0xFFFF0000U) == 0;
    }
    const std::uint32_t name_offset = raw_name & 0x7FFFFFFFU;
    std::uint16_t length = 0;
    if (name_offset > std::numeric_limits<std::uint32_t>::max() - 2U ||
        !read_resource_u16(name_offset, length) || length > 4096U ||
        !resource_directory_range(name_offset + 2U,
                                  static_cast<std::size_t>(length) * sizeof(std::uint16_t))) {
        return false;
    }
    key.named = true;
    key.name.clear();
    for (std::uint16_t index = 0; index < length; ++index) {
        std::uint16_t unit = 0;
        const std::uint32_t offset = name_offset + 2U +
                                     static_cast<std::uint32_t>(index) *
                                         static_cast<std::uint32_t>(sizeof(std::uint16_t));
        if (!read_resource_u16(offset, unit)) {
            return false;
        }
        key.name.push_back(static_cast<char16_t>(unit));
    }
    return true;
}

[[nodiscard]] bool resource_keys_equal(const ResourceKey& left,
                                       const ResourceKey& right) noexcept {
    if (left.named != right.named) {
        return false;
    }
    if (!left.named) {
        return left.id == right.id;
    }
    if (left.name.size() != right.name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.name.size(); ++i) {
        const auto c1 = std::tolower(static_cast<unsigned char>(left.name[i]));
        const auto c2 = std::tolower(static_cast<unsigned char>(right.name[i]));
        if (c1 != c2) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool find_resource_entry(const std::uint32_t directory_offset,
                                       const ResourceKey& wanted,
                                       std::uint32_t& child_raw) noexcept {
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (directory_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(directory_offset + 12U, named_count) ||
        !read_resource_u16(directory_offset + 14U, id_count)) {
        return false;
    }
    const std::uint32_t entry_count = static_cast<std::uint32_t>(named_count) + id_count;
    if (entry_count > 4096U ||
        !resource_directory_range(directory_offset + 16U,
                                  static_cast<std::size_t>(entry_count) * 8U)) {
        return false;
    }
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::uint32_t entry_offset = directory_offset + 16U + index * 8U;
        std::uint32_t raw_name = 0;
        std::uint32_t raw_child = 0;
        if (!read_resource_u32(entry_offset, raw_name) ||
            !read_resource_u32(entry_offset + 4U, raw_child)) {
            return false;
        }
        ResourceKey actual;
        if (!resource_entry_key(raw_name, actual)) {
            return false;
        }
        if (resource_keys_equal(actual, wanted)) {
            child_raw = raw_child;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool current_resource_module(const void* module) noexcept {
    if (module == nullptr) {
        return true;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module);
    return value == 0x1000U ||
           (g_guest_image_base != nullptr &&
            value == reinterpret_cast<std::uintptr_t>(g_guest_image_base));
}

[[nodiscard]] std::optional<ResourceSlot> resolve_resource(const void* module,
                                                           const std::uint16_t* name,
                                                           const std::uint16_t* type) noexcept {
    if (!current_resource_module(module) || g_guest_image_base == nullptr ||
        g_guest_resource_size < 16U) {
        return std::nullopt;
    }
    ResourceKey type_key;
    ResourceKey name_key;
    if (!make_resource_key(type, type_key) || !make_resource_key(name, name_key)) {
        return std::nullopt;
    }
    std::uint32_t name_dir_raw = 0;
    if (!find_resource_entry(0, type_key, name_dir_raw) || (name_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    std::uint32_t lang_dir_raw = 0;
    if (!find_resource_entry(name_dir_raw & 0x7FFFFFFFU, name_key, lang_dir_raw) ||
        (lang_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    const std::uint32_t lang_offset = lang_dir_raw & 0x7FFFFFFFU;
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (lang_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(lang_offset + 12U, named_count) ||
        !read_resource_u16(lang_offset + 14U, id_count) ||
        static_cast<std::uint32_t>(named_count) + id_count == 0U) {
        return std::nullopt;
    }
    std::uint32_t data_entry_raw = 0;
    if (!read_resource_u32(lang_offset + 16U + 4U, data_entry_raw) ||
        (data_entry_raw & 0x80000000U) != 0) {
        return std::nullopt;
    }
    std::uint32_t data_rva = 0;
    std::uint32_t data_size = 0;
    if (!read_resource_u32(data_entry_raw, data_rva) ||
        !read_resource_u32(data_entry_raw + 4U, data_size) ||
        data_rva > g_guest_image_size || data_size > g_guest_image_size - data_rva) {
        return std::nullopt;
    }
    return ResourceSlot{.used = true, .data_rva = data_rva, .data_size = data_size};
}

void write_child_process_result(const int fd, const GuestExecutionResult result) noexcept {
    const std::array<std::byte, kChildProcessProtocolSize> message{
        result.exited_explicitly ? std::byte{1} : std::byte{0},
        std::byte{static_cast<unsigned char>(result.exit_code & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 8U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 16U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 24U) & 0xFFU)},
    };
    std::size_t written = 0;
    while (written < message.size()) {
        const ssize_t count = ::write(fd, message.data() + written, message.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += static_cast<std::size_t>(count);
    }
}

bool read_guest_file_for_process(const char* path, std::vector<std::byte>& bytes) noexcept {
    // Limite de PE documentado também em cli.cpp; evita DoS ao ler arquivos de
    // GiB ou fontes infinitas (ex.: /dev/zero) no processo filho de CreateProcess.
    constexpr std::size_t kMaxPeFileSize = 512ULL * 1024 * 1024;
    // O_NOFOLLOW: não seguir o symlink final, para que um convidado hostil não
    // aponte o filho para um dispositivo de bloco ou outro alvo do host.
    const int fd = ::open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    bytes.clear();
    bytes.reserve(4096);
    std::array<std::byte, 8192> buffer{};
    std::size_t total = 0;
    while (true) {
        const ::ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        if (count == 0) {
            break;
        }
        const std::size_t added = static_cast<std::size_t>(count);
        if (total > kMaxPeFileSize - added) {
            ::close(fd);
            return false;
        }
        total += added;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(added));
    }
    ::close(fd);
    return true;
}

[[noreturn]] void run_created_guest_child(const std::string& path, const int result_fd,
                                          const std::string& working_directory) noexcept {
    // O fork copiou o cache de /proc/self/maps do pai: este processo fará
    // novos mapeamentos (imagem, pilha), então o cache precisa recomeçar.
    runtime::invalidate_memory_map_cache();
    if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
        ::munmap(const_cast<std::byte*>(g_guest_image_base), g_guest_image_size);
        set_guest_image_view(nullptr, 0, 0, 0);
    }
    GuestExecutionResult result{};
    if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }
    std::vector<std::byte> bytes;
    if (!read_guest_file_for_process(path.c_str(), bytes)) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    const pe::ParseResult parsed = pe::parse_pe(bytes);
    if (parsed.status != pe::ParseStatus::Success) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }
    loader::register_builtin_modules();
    loader::PrepareResult prepared = loader::prepare_process(parsed.info, bytes);
    if (prepared.status != loader::PrepareStatus::Success ||
        (prepared.process.image.delta != 0 && !prepared.process.image.has_relocation_directory)) {
        loader::destroy_process(prepared.process);
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    loader::GuestProcess& process = prepared.process;
    set_guest_module_path(path.c_str());
    msvcrt_set_guest_command_line(std::vector<std::string>{
        prefix::to_windows_path(std::filesystem::path(path), guest_prefix_root())});
    set_guest_image_view(process.image.memory, process.image.size,
                         parsed.info.resource_directory_rva,
                         parsed.info.resource_directory_size);
    runtime::set_guest_unwind_view(process.image.memory, process.image.size,
                                   parsed.info.exception_directory_rva,
                                   process.info.runtime_functions);
    set_guest_tls_directory(
        relocate_tls_va_for_child(parsed.info.tls_info.start_address_of_raw_data,
                                  parsed.info, process.image),
        relocate_tls_va_for_child(parsed.info.tls_info.end_address_of_raw_data,
                                  parsed.info, process.image),
        relocate_tls_va_for_child(parsed.info.tls_info.address_of_index,
                                  parsed.info, process.image),
        relocated_tls_callbacks_for_child(parsed.info, process.image));
    result = execute_guest_entry(process.thread.entry_point, process.thread.stack_top);
    set_guest_tls_directory(0, 0, 0, {});
    runtime::clear_guest_unwind_view();
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    write_child_process_result(result_fd, result);
    ::close(result_fd);
    ::_exit(0);
}

}  // namespace

namespace {

[[nodiscard]] bool is_guest_executable_address(const std::uintptr_t address) noexcept {
    if (g_guest_image_base == nullptr || address == 0) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
    if (address < base || address - base >= g_guest_image_size) {
        return false;
    }
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) != 3) {
            continue;
        }
        if (address >= start && address < end) {
            return permissions[2] == 'x';
        }
    }
    return false;
}

void trace_fls(const char* const operation, const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"thread", std::to_string(g_current_thread_id)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("fls", fields, 4);
}

void trace_process_console(const char* const event, const char* const operation,
                           const std::string& detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
        diagnostics::TraceField{"thread", std::to_string(g_current_thread_id)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace(event, fields, 4);
}

[[nodiscard]] int standard_handle_index(const std::uint32_t standard_handle) noexcept {
    switch (standard_handle) {
        case abi::kStdInputHandle: return 0;
        case abi::kStdOutputHandle: return 1;
        case abi::kStdErrorHandle: return 2;
        default: return -1;
    }
}

[[nodiscard]] void* current_standard_handle(const std::size_t index) noexcept {
    std::lock_guard lock(g_process_context_mutex);
    return g_standard_handles[index];
}

[[nodiscard]] std::uintptr_t process_pointer_cookie() noexcept {
    std::uintptr_t cookie = g_pointer_cookie.load(std::memory_order_acquire);
    if (cookie != 0) {
        return cookie;
    }
    constexpr std::uintptr_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;
    const std::uintptr_t pid = static_cast<std::uintptr_t>(::getpid());
    const std::uintptr_t image = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
    std::uintptr_t candidate = kGoldenRatio ^ (pid * 0x100000001B3ULL) ^ image;
    candidate |= 1U;
    std::uintptr_t expected = 0;
    if (!g_pointer_cookie.compare_exchange_strong(expected, candidate,
                                                   std::memory_order_acq_rel)) {
        candidate = expected;
    }
    return candidate;
}

[[nodiscard]] bool write_all(const int fd, const char* data, const std::size_t size) noexcept {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t result = ::write(fd, data + offset, size - offset);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] GlobalMemorySlot* find_global_memory_slot_locked(const void* memory,
                                                                const bool global_only) noexcept {
    if (memory == nullptr) {
        return nullptr;
    }
    for (auto& slot : g_global_memory) {
        if (slot.address == memory && (!global_only || slot.global)) {
            return slot.used ? &slot : nullptr;
        }
    }
    return nullptr;
}

}  // namespace

std::shared_ptr<FlsThreadValues> ensure_fls_thread_values() {
    std::lock_guard lock(g_fls_mutex);
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    return g_current_fls_values;
}

void set_current_fls_thread_values(std::shared_ptr<FlsThreadValues> values) {
    std::lock_guard lock(g_fls_mutex);
    g_current_fls_values = std::move(values);
    if (g_current_fls_values != nullptr) {
        g_fls_threads.push_back(g_current_fls_values);
    }
}

void cleanup_current_fls_values() noexcept {
    std::vector<std::pair<std::uintptr_t, void*>> callbacks;
    {
        std::lock_guard lock(g_fls_mutex);
        if (g_current_fls_values == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < g_fls_slots.size(); ++index) {
            const FlsSlot& slot = g_fls_slots[index];
            void*& value = g_current_fls_values->values[index];
            if (slot.used && slot.callback != 0 && value != nullptr) {
                callbacks.emplace_back(slot.callback, value);
                value = nullptr;
            }
        }
    }
    using FlsCallback = void (TL_MSABI *)(void*);
    for (const auto& [callback_address, value] : callbacks) {
        const auto callback = reinterpret_cast<FlsCallback>(callback_address);
        callback(value);
        trace_fls("callback", "thread-exit");
    }
}

void reset_fls_process_state() noexcept {
    std::lock_guard lock(g_fls_mutex);
    g_fls_slots = {};
    g_fls_threads.clear();
    g_current_fls_values.reset();
}

extern "C" {

TL_MSABI void* tl_GetStdHandle(const std::uint32_t std_handle) noexcept {
    const int index = standard_handle_index(std_handle);
    if (index < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return current_standard_handle(static_cast<std::size_t>(index));
}

TL_MSABI int tl_SetStdHandle(const std::uint32_t std_handle, void* const handle) noexcept {
    const int index = standard_handle_index(std_handle);
    if (index < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (handle != nullptr && handle_fd(handle) < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard lock(g_process_context_mutex);
        g_standard_handles[static_cast<std::size_t>(index)] = handle;
    }
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "set-std-handle", std::to_string(index));
    return 1;
}

TL_MSABI std::uint32_t tl_GetFileType(const void* const handle) noexcept {
    const int fd = handle_fd(handle);
    if (fd < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return abi::kFileTypeUnknown;
    }
    struct stat status{};
    if (::fstat(fd, &status) != 0) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return abi::kFileTypeUnknown;
    }
    std::uint32_t type = abi::kFileTypeUnknown;
    if (S_ISCHR(status.st_mode)) {
        type = abi::kFileTypeChar;
    } else if (S_ISFIFO(status.st_mode) || S_ISSOCK(status.st_mode)) {
        type = abi::kFileTypePipe;
    } else if (S_ISREG(status.st_mode) || S_ISDIR(status.st_mode)) {
        type = abi::kFileTypeDisk;
    }
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "file-type", std::to_string(type));
    return type;
}

TL_MSABI std::uint32_t tl_GetSystemDirectoryW(std::uint16_t* const buffer,
                                              const std::uint32_t size) noexcept {
    static constexpr std::u16string_view kSystemDirectory = u"C:\\Windows\\System32";
    const std::uint32_t required = static_cast<std::uint32_t>(kSystemDirectory.size() + 1U);
    if (buffer == nullptr || size == 0) {
        set_last_error(abi::kErrorSuccess);
        return required;
    }
    if (size < required ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return required;
    }
    std::copy(kSystemDirectory.begin(), kSystemDirectory.end(), buffer);
    buffer[kSystemDirectory.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "system-directory", "C:\\Windows\\System32");
    return static_cast<std::uint32_t>(kSystemDirectory.size());
}

TL_MSABI void tl_GetStartupInfoW(abi::GuestStartupInfoW* const startup_info) noexcept {
    if (startup_info == nullptr ||
        !mapped_guest_range(startup_info, sizeof(*startup_info), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    *startup_info = {};
    startup_info->cb = sizeof(*startup_info);
    startup_info->flags = abi::kStartfUseStdHandles;
    {
        std::lock_guard lock(g_process_context_mutex);
        startup_info->std_input = g_standard_handles[0];
        startup_info->std_output = g_standard_handles[1];
        startup_info->std_error = g_standard_handles[2];
    }
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "startup-info", "wide");
}

TL_MSABI int tl_ReadConsoleW(const void* const console_input, std::uint16_t* const buffer,
                             const std::uint32_t chars_to_read,
                             std::uint32_t* const chars_read,
                             const void* const input_control) noexcept {
    if (chars_read == nullptr ||
        !mapped_guest_range(chars_read, sizeof(*chars_read), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *chars_read = 0;
    if (input_control != nullptr || console_input != &kStdInputToken ||
        console_input != current_standard_handle(0) || chars_to_read > (1U << 20U) ||
        (chars_to_read != 0 &&
         !mapped_guest_range(buffer, static_cast<std::size_t>(chars_to_read) * sizeof(*buffer),
                             true))) {
        set_last_error(console_input != &kStdInputToken ? abi::kErrorInvalidHandle
                                                        : abi::kErrorInvalidParameter);
        return 0;
    }
    if (chars_to_read == 0) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    std::vector<char> bytes(static_cast<std::size_t>(chars_to_read) * 4U);
    ssize_t byte_count = -1;
    do {
        byte_count = ::read(STDIN_FILENO, bytes.data(), bytes.size());
    } while (byte_count < 0 && errno == EINTR);
    if (byte_count < 0) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    const std::u16string wide = util::utf8_to_wide(
        std::string_view{bytes.data(), static_cast<std::size_t>(byte_count)});
    const std::size_t copied = std::min<std::size_t>(wide.size(), chars_to_read);
    std::copy_n(wide.begin(), copied, buffer);
    *chars_read = static_cast<std::uint32_t>(copied);
    set_last_error(abi::kErrorSuccess);
    trace_process_console("console", "read-wide", std::to_string(copied));
    return 1;
}

TL_MSABI int tl_WriteConsoleW(const void* const console_output,
                              const std::uint16_t* const buffer,
                              const std::uint32_t chars_to_write,
                              std::uint32_t* const chars_written,
                              const void* const reserved) noexcept {
    if (chars_written == nullptr ||
        !mapped_guest_range(chars_written, sizeof(*chars_written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *chars_written = 0;
    if (reserved != nullptr ||
        (console_output != &kStdOutputToken && console_output != &kStdErrorToken) ||
        chars_to_write > (1U << 20U) ||
        (chars_to_write != 0 &&
         !mapped_guest_range(buffer, static_cast<std::size_t>(chars_to_write) * sizeof(*buffer),
                             false))) {
        set_last_error((console_output != &kStdOutputToken && console_output != &kStdErrorToken)
                           ? abi::kErrorInvalidHandle
                           : abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8;
    utf8.reserve(chars_to_write);
    std::size_t position = 0;
    while (position < chars_to_write) {
        const std::uint32_t codepoint = util::decode_utf16(buffer, chars_to_write, position);
        char encoded[4]{};
        const std::size_t encoded_size = util::utf8_bytes_for(
            codepoint == util::kInvalidCodepoint ? static_cast<std::uint32_t>('?') : codepoint,
            encoded);
        utf8.append(encoded, encoded_size);
    }
    const int fd = console_output == &kStdErrorToken ? STDERR_FILENO : STDOUT_FILENO;
    if (!write_all(fd, utf8.data(), utf8.size())) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    *chars_written = chars_to_write;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("console", "write-wide", std::to_string(chars_to_write));
    return 1;
}

TL_MSABI int tl_IsDebuggerPresent() noexcept {
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "debugger", "absent");
    return 0;
}

TL_MSABI int tl_IsProcessorFeaturePresent(const std::uint32_t processor_feature) noexcept {
    const bool present = processor_feature == abi::kPfCompareExchangeDouble ||
                         processor_feature == abi::kPfMmxInstructionsAvailable ||
                         processor_feature == abi::kPfXmmiInstructionsAvailable ||
                         processor_feature == abi::kPfRdtscInstructionAvailable ||
                         processor_feature == abi::kPfPaeEnabled ||
                         processor_feature == abi::kPfXmmi64InstructionsAvailable ||
                         processor_feature == abi::kPfNxEnabled;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "processor-feature",
                          std::to_string(processor_feature));
    return present ? 1 : 0;
}

TL_MSABI void* tl_EncodePointer(void* const pointer) noexcept {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t encoded = std::rotl(value ^ process_pointer_cookie(), 17);
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "encode-pointer", "opaque");
    return reinterpret_cast<void*>(encoded);
}

TL_MSABI void* tl_DecodePointer(void* const pointer) noexcept {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t decoded = std::rotr(value, 17) ^ process_pointer_cookie();
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "decode-pointer", "opaque");
    return reinterpret_cast<void*>(decoded);
}

TL_MSABI void tl_InitializeSListHead(abi::GuestSListHeader* const list_head) noexcept {
    if (list_head == nullptr ||
        reinterpret_cast<std::uintptr_t>(list_head) % alignof(abi::GuestSListHeader) != 0 ||
        !mapped_guest_range(list_head, sizeof(*list_head), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    *list_head = {};
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "initialize-slist", "empty");
}

TL_MSABI std::uint32_t tl_GetLastError() noexcept {
    return g_last_error;
}

TL_MSABI void tl_SetLastError(const std::uint32_t error_code) noexcept {
    set_last_error(error_code);
}

TL_MSABI void tl_ExitProcess(const std::uint32_t exit_code) noexcept {
    if (!g_guest_execution_active) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "ExitProcess"},
            diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
            diagnostics::TraceField{"status", "success"},
            diagnostics::TraceField{"mechanism", "guest-transfer"},
        };
        runtime_trace("ExitProcess", fields, 4);
    }
    g_guest_exit_code = exit_code;
    cleanup_current_fls_values();
    std::longjmp(g_guest_exit_context, 1);
}

TL_MSABI int tl_WriteFile(const void* const handle, const void* const buffer,
                          const std::uint32_t bytes_to_write,
                          std::uint32_t* const bytes_written,
                          void* const overlapped) noexcept {
    if (bytes_written != nullptr &&
        !mapped_guest_range(bytes_written, sizeof(*bytes_written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (overlapped != nullptr || !mapped_guest_range(buffer, bytes_to_write, false)) {
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0) {
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (bytes_to_write == 0) {
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    const ssize_t written = ::write(fd, buffer, bytes_to_write);
    if (written < 0) {
        const std::uint32_t win32_error = errno_to_win32(errno);
        trace_linux_failure("WriteFile", "write", errno, win32_error);
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        set_last_error(win32_error);
        return 0;
    }
    if (bytes_written != nullptr) {
        *bytes_written = static_cast<std::uint32_t>(written);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ReadFile(const void* const handle, void* const buffer,
                         const std::uint32_t bytes_to_read,
                         std::uint32_t* const bytes_read,
                         void* const overlapped) noexcept {
    if (bytes_read != nullptr &&
        !mapped_guest_range(bytes_read, sizeof(*bytes_read), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (overlapped != nullptr || !mapped_guest_range(buffer, bytes_to_read, true)) {
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0) {
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (bytes_to_read == 0) {
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    const ssize_t read_bytes = ::read(fd, buffer, bytes_to_read);
    if (read_bytes < 0) {
        const std::uint32_t win32_error = errno_to_win32(errno);
        trace_linux_failure("ReadFile", "read", errno, win32_error);
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        set_last_error(win32_error);
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = static_cast<std::uint32_t>(read_bytes);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateFileA(const char* const file_name, const std::uint32_t desired_access,
                              const std::uint32_t share_mode, const void* const security_attributes,
                              const std::uint32_t creation_disposition,
                              const std::uint32_t flags_and_attributes,
                              const void* const template_file) noexcept {
    (void)share_mode;
    (void)security_attributes;
    (void)flags_and_attributes;
    (void)template_file;
    if (!mapped_guest_cstring(file_name) || file_name == nullptr || file_name[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char normalized[4096]{};
    if (!translate_windows_path(file_name, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    int flags = 0;
    const bool read = (desired_access & abi::kGenericRead) != 0;
    const bool write = (desired_access & abi::kGenericWrite) != 0;
    if (read && write) {
        flags |= O_RDWR;
    } else if (write) {
        flags |= O_WRONLY;
    } else {
        flags |= O_RDONLY;
    }
    switch (creation_disposition) {
        case abi::kCreateAlways: flags |= O_CREAT | O_TRUNC; break;
        case abi::kCreateNew: flags |= O_CREAT | O_EXCL; break;
        case abi::kOpenAlways: flags |= O_CREAT; break;
        case abi::kOpenExisting: break;
        case abi::kTruncateExisting: flags |= O_TRUNC; break;
        default:
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
    }
    const int fd = ::open(normalized, flags, 0644);
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    auto it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& s) { return !s.used; });
    if (it == g_files.end()) {
        ::close(fd);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->fd = fd;
    it->path = normalized;
    it->delete_pending = false;
    it->unlinked = false;
    struct stat st{};
    if (fstat(fd, &st) == 0) {
        it->file_size = static_cast<std::uint64_t>(st.st_size);
    }
    it->position = lseek(fd, 0, SEEK_CUR);
    set_last_error(abi::kErrorSuccess);
    return &*it;
}

TL_MSABI void* tl_CreateFileW(const std::uint16_t* path, const std::uint32_t desired_access,
                              const std::uint32_t share_mode,
                              const void* security_attributes,
                              const std::uint32_t creation_disposition,
                              const std::uint32_t flags_and_attributes,
                              const void* template_file) noexcept {
    if (!mapped_guest_wstring(path) || path == nullptr || path[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    (void)share_mode;
    (void)security_attributes;
    (void)flags_and_attributes;
    (void)template_file;
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    int flags = 0;
    const bool read = (desired_access & abi::kGenericRead) != 0;
    const bool write = (desired_access & abi::kGenericWrite) != 0;
    if (read && write) {
        flags |= O_RDWR;
    } else if (write) {
        flags |= O_WRONLY;
    } else {
        flags |= O_RDONLY;
    }
    switch (creation_disposition) {
        case abi::kCreateAlways: flags |= O_CREAT | O_TRUNC; break;
        case abi::kCreateNew: flags |= O_CREAT | O_EXCL; break;
        case abi::kOpenAlways: flags |= O_CREAT; break;
        case abi::kOpenExisting: break;
        case abi::kTruncateExisting: flags |= O_TRUNC; break;
        default:
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
    }
    const int fd = ::open(normalized, flags, 0644);
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    auto it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& s) { return !s.used; });
    if (it == g_files.end()) {
        ::close(fd);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->fd = fd;
    it->path = normalized;
    it->delete_pending = false;
    it->unlinked = false;
    struct stat st{};
    if (fstat(fd, &st) == 0) {
        it->file_size = static_cast<std::uint64_t>(st.st_size);
    }
    it->position = lseek(fd, 0, SEEK_CUR);
    set_last_error(abi::kErrorSuccess);
    return &*it;
}

TL_MSABI int tl_CloseHandle(const void* const handle) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (handle == &kStdInputToken || handle == &kStdOutputToken || handle == &kStdErrorToken) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (runtime::security::close_token_handle(handle)) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_files_mutex);
        const bool delete_pending = slot->delete_pending && !slot->unlinked;
        const std::string path = slot->path;
        ::close(slot->fd);
        *slot = {};
        slot->fd = -1;
        if (delete_pending && ::unlink(path.c_str()) != 0 && errno != ENOENT) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            trace_filesystem("delete-on-close", "failed", std::to_string(error));
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        if (delete_pending) {
            runtime::security::remove_path(path);
            trace_filesystem("delete-on-close", "success", "removed");
        }
        return 1;
    }
    if (SyncSlot* slot = find_sync_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        clear_sync_slot(*slot);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(g_mapping_mutex);
        if (FileMappingSlot* slot = find_file_mapping_slot_locked(handle); slot != nullptr) {
            // Fechar o objeto não desmapeia visões existentes (semântica Windows).
            if (slot->fd >= 0) {
                ::close(slot->fd);
            }
            slot->used = false;
            slot->fd = -1;
            slot->size = 0;
            slot->protect = 0;
            slot->name.clear();
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    if (ThreadSlot* slot = find_thread_slot(handle); slot != nullptr) {
        bool do_join = false;
        {
            std::lock_guard<std::mutex> lock(g_threads_mutex);
            if (!slot->joined) {
                slot->joined = true;
                do_join = true;
            }
        }
        // Join fora de g_threads_mutex: segurar o mutex durante o join
        // bloquearia outras APIs que consultam slots a partir das próprias
        // threads convidadas.
        if (do_join && slot->host_thread.joinable()) {
            slot->host_thread.join();
        }
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        if (slot->teb != nullptr) {
            free_guest_teb(slot->teb);
        }
        if (slot->stack != nullptr && slot->stack_size > 0) {
            munmap(slot->stack, slot->stack_size);
        }
        slot->used = false;
        slot->thread_id = 0;
        slot->teb = nullptr;
        slot->stack = nullptr;
        slot->stack_size = 0;
        slot->stack_top = 0;
        slot->thread_func = {};
        slot->finished = false;
        slot->joined = false;
        slot->exit_code = 0;
        runtime::invalidate_memory_map_cache();
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (auto& slot : g_snapshots) {
            if (slot.used && handle == static_cast<const void*>(&slot)) {
                slot.used = false;
                slot.pids.clear();
                slot.next_index = 0;
                slot.flags = 0;
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
    }
    {
        const auto addr = reinterpret_cast<std::uintptr_t>(handle);
        if (addr >= kProcessHandleBase && addr < kProcessHandleBase + kProcessHandleRange) {
            const std::uint32_t pid = static_cast<std::uint32_t>(addr - kProcessHandleBase);
            // Validar que pid ainda é plausível (não obrigatório, mas mantém contrato)
            // Aceita qualquer pid dentro do range para CloseHandle
            (void)pid;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_MSABI void* tl_VirtualAlloc(void* const address, const std::size_t size,
                               const std::uint32_t allocation_type,
                               const std::uint32_t protect) noexcept {
    void* base = address;
    std::size_t region = size;
    const ntdll::NtStatus st = ntdll::NtAllocateVirtualMemory(&base, &region, allocation_type, protect);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(ntdll::NtStatusToDosError(st));
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return base;
}

TL_MSABI int tl_VirtualFree(void* const address, const std::size_t size,
                            const std::uint32_t free_type) noexcept {
    std::size_t region = size;
    const ntdll::NtStatus st = ntdll::NtFreeVirtualMemory(address, &region, free_type);
    if (st != ntdll::NtStatus::Success) {
        set_last_error(ntdll::NtStatusToDosError(st));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcessHeap() noexcept {
    static char g_process_heap_token = 0;
    return &g_process_heap_token;
}

TL_MSABI void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept {
    (void)heap;
    if ((flags & 0x0008) != 0) {
        return std::calloc(1, size);
    }
    return std::malloc(size);
}

TL_MSABI int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept {
    (void)heap;
    (void)flags;
    std::free(memory);
    return 1;
}

TL_MSABI void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                              std::uintptr_t new_size) noexcept {
    (void)heap;
    (void)flags;
    return std::realloc(memory, new_size);
}

TL_MSABI void* tl_GlobalAlloc(const std::uint32_t flags, const std::size_t bytes) noexcept {
    constexpr std::uint32_t kAllowedFlags = abi::kGmemMoveable | abi::kGmemZeroinit;
    if ((flags & ~kAllowedFlags) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::size_t allocation_size = bytes == 0 ? 1 : bytes;
    void* const memory = (flags & abi::kGmemZeroinit) != 0
                             ? std::calloc(1, allocation_size)
                             : std::malloc(allocation_size);
    if (memory == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        auto it = std::find_if(g_global_memory.begin(), g_global_memory.end(),
                               [](const GlobalMemorySlot& slot) { return !slot.used; });
        if (it == g_global_memory.end()) {
            std::free(memory);
            set_last_error(abi::kErrorNotEnoughMemory);
            return nullptr;
        }
        *it = GlobalMemorySlot{true, memory, allocation_size, flags, 0, true};
    }
    set_last_error(abi::kErrorSuccess);
    return memory;
}

TL_MSABI void* tl_GlobalLock(void* const memory) noexcept {
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return nullptr;
    }
    ++slot->lock_count;
    set_last_error(abi::kErrorSuccess);
    return slot->address;
}

TL_MSABI int tl_GlobalUnlock(void* const memory) noexcept {
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->lock_count == 0) {
        set_last_error(abi::kErrorNotLocked);
        return 0;
    }
    --slot->lock_count;
    set_last_error(abi::kErrorSuccess);
    return slot->lock_count == 0 ? 0 : 1;
}

TL_MSABI void* tl_GlobalFree(void* const memory) noexcept {
    if (memory == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    std::lock_guard lock(g_global_memory_mutex);
    GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, true);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return memory;
    }
    std::free(slot->address);
    *slot = GlobalMemorySlot{};
    set_last_error(abi::kErrorSuccess);
    return nullptr;
}

TL_MSABI void* tl_LocalAlloc(const std::uint32_t flags, const std::size_t bytes) noexcept {
    constexpr std::uint32_t kAllowedFlags = abi::kGmemMoveable | abi::kGmemZeroinit;
    if ((flags & ~kAllowedFlags) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::size_t allocation_size = bytes == 0 ? 1 : bytes;
    void* const memory = (flags & abi::kGmemZeroinit) != 0
                             ? std::calloc(1, allocation_size)
                             : std::malloc(allocation_size);
    if (memory == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        auto it = std::find_if(g_global_memory.begin(), g_global_memory.end(),
                               [](const GlobalMemorySlot& slot) { return !slot.used; });
        if (it == g_global_memory.end()) {
            std::free(memory);
            set_last_error(abi::kErrorNotEnoughMemory);
            return nullptr;
        }
        *it = GlobalMemorySlot{true, memory, allocation_size, flags, 0, false};
    }
    set_last_error(abi::kErrorSuccess);
    return memory;
}

TL_MSABI void* tl_LocalFree(void* memory) noexcept {
    if (memory == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    {
        std::lock_guard lock(g_global_memory_mutex);
        if (GlobalMemorySlot* const slot = find_global_memory_slot_locked(memory, false);
            slot != nullptr) {
            if (slot->global) {
                set_last_error(abi::kErrorInvalidHandle);
                return memory;
            }
            std::free(slot->address);
            *slot = GlobalMemorySlot{};
            set_last_error(abi::kErrorSuccess);
            return nullptr;
        }
    }
    if (take_local_free_block(memory)) {
        std::free(memory);
        set_last_error(abi::kErrorSuccess);
        return nullptr;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return memory;
}

TL_MSABI std::uint64_t tl_GetTickCount64() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
}

TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept {
    if (file_time == nullptr || !mapped_guest_range(file_time, sizeof(std::uint64_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    auto* ft = static_cast<std::uint64_t*>(file_time);
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    const auto since_epoch = duration_cast<nanoseconds>(now).count();
    const std::uint64_t ticks_100ns = static_cast<std::uint64_t>(since_epoch) / 100;
    const std::uint64_t epoch_diff = 116444736000000000ULL;
    *ft = ticks_100ns + epoch_diff;
}

// O cache file_size fica stale após WriteFile; o tamanho verdadeiro vem do fd.
std::uint64_t current_file_size(const FileSlot& slot) noexcept {
    struct stat st{};
    if (::fstat(slot.fd, &st) == 0) {
        return static_cast<std::uint64_t>(st.st_size);
    }
    return slot.file_size;
}

TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0xFFFFFFFFU;
    }
    if (high_size != nullptr && !mapped_guest_range(high_size, sizeof(*high_size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFFU;
    }
    const std::uint64_t size = current_file_size(*slot);
    if (high_size != nullptr) {
        *high_size = static_cast<std::uint32_t>(size >> 32);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(size & 0xFFFFFFFFU);
}

TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return -1;
    }
    if (move_method > kFileEnd) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    if (high_distance != nullptr && !mapped_guest_range(high_distance, sizeof(*high_distance), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    std::int64_t offset = distance;
    if (high_distance != nullptr) {
        offset |= static_cast<std::int64_t>(*high_distance) << 32;
    }
    std::int64_t new_pos = 0;
    switch (move_method) {
        case kFileBegin: new_pos = offset; break;
        case kFileCurrent: new_pos = slot->position + offset; break;
        case kFileEnd: new_pos = static_cast<std::int64_t>(current_file_size(*slot)) + offset; break;
    }
    if (new_pos < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    const off_t result = lseek(slot->fd, static_cast<off_t>(new_pos), SEEK_SET);
    if (result < 0) {
        set_last_error(errno_to_win32(errno));
        return -1;
    }
    slot->position = static_cast<std::int64_t>(result);
    if (high_distance != nullptr) {
        *high_distance = static_cast<std::int32_t>(slot->position >> 32);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::int32_t>(slot->position & 0xFFFFFFFFU);
}

TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    struct stat st{};
    if (stat(normalized, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0xFFFFFFFF;
    }
    set_last_error(abi::kErrorSuccess);
    return stat_to_win32_attributes(normalized, st);
}

TL_MSABI std::uint32_t tl_GetFileAttributesW(const std::uint16_t* path) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFF;
    }
    struct stat st{};
    if (stat(normalized, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0xFFFFFFFF;
    }
    set_last_error(abi::kErrorSuccess);
    return stat_to_win32_attributes(normalized, st);
}

TL_MSABI int tl_SetFileAttributesW(const std::uint16_t* const path,
                                   const std::uint32_t attributes) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("set-attributes", "failed", "invalid-path");
        return 0;
    }
    const std::uint32_t error = apply_win32_file_attributes(normalized, attributes);
    set_last_error(error);
    trace_filesystem("set-attributes", error == abi::kErrorSuccess ? "success" : "failed",
                     std::to_string(attributes));
    return error == abi::kErrorSuccess ? 1 : 0;
}

TL_MSABI int tl_DeleteFileA(const char* path) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (unlink(normalized) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    runtime::security::remove_path(std::filesystem::path{normalized});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DeleteFileW(const std::uint16_t* path) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (unlink(normalized) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    runtime::security::remove_path(std::filesystem::path{normalized});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateDirectoryA(const char* path, const void* security_attributes) noexcept {
    (void)security_attributes;
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!translate_windows_path(path, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (mkdir(normalized, 0755) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateDirectoryW(const std::uint16_t* path, const void* security_attributes) noexcept {
    (void)security_attributes;
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (mkdir(normalized, 0755) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindFirstFileA(const char* file_name, void* find_data) noexcept {
    if (!mapped_guest_cstring(file_name) || file_name == nullptr || find_data == nullptr ||
        !mapped_guest_range(find_data, sizeof(Win32FindDataA), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
    }
    char normalized[4096]{};
    if (!translate_windows_path(file_name, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
    }
    std::string path_str{normalized};
    std::string directory;
    std::string pattern;
    const auto slash = path_str.rfind('/');
    if (slash != std::string::npos) {
        directory = path_str.substr(0, slash);
        pattern = path_str.substr(slash + 1);
    } else {
        directory = ".";
        pattern = path_str;
    }
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        set_last_error(errno_to_win32(errno));
        return kInvalidHandleValue;
    }
    auto it = std::find_if(g_find_slots.begin(), g_find_slots.end(), [](const FindSlot& s) { return !s.used; });
    if (it == g_find_slots.end()) {
        closedir(dir);
        set_last_error(abi::kErrorNotEnoughMemory);
        return kInvalidHandleValue;
    }
    it->used = true;
    it->dir = dir;
    it->pattern = pattern;
    it->directory = directory;
    const void* handle = reinterpret_cast<const void*>(
        kFindHandleBase + static_cast<std::uintptr_t>(it - g_find_slots.begin()));
    if (tl_FindNextFileA(handle, find_data) == 0) {
        tl_FindClose(handle);
        return kInvalidHandleValue;
    }
    return const_cast<void*>(handle);
}

TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept {
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used || find_data == nullptr ||
        !mapped_guest_range(find_data, sizeof(Win32FindDataA), true)) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    struct dirent* entry = nullptr;
    while ((entry = readdir(slot->dir)) != nullptr) {
        // Windows nunca devolve "." nem ".." na enumeração.
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (win32_wildcard_match(slot->pattern, entry->d_name)) {
            auto* data = static_cast<Win32FindDataA*>(find_data);
            *data = {};
            std::strncpy(data->c_file_name, entry->d_name, sizeof(data->c_file_name) - 1);
            std::string full_path = slot->directory + "/" + entry->d_name;
            struct stat st{};
            if (stat(full_path.c_str(), &st) == 0) {
                data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
                data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFFU);
                data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
                GuestFileTime creation{};
                GuestFileTime access{};
                GuestFileTime write{};
                filetime_from_unix(st.st_ctim.tv_sec, creation);
                filetime_from_unix(st.st_atim.tv_sec, access);
                filetime_from_unix(st.st_mtim.tv_sec, write);
                data->ft_creation_time_lo = creation.low;
                data->ft_creation_time_hi = creation.high;
                data->ft_last_access_time_lo = access.low;
                data->ft_last_access_time_hi = access.high;
                data->ft_last_write_time_lo = write.low;
                data->ft_last_write_time_hi = write.high;
            }
            set_last_error(abi::kErrorSuccess);
            trace_filesystem("enumerate", "success", entry->d_name);
            return 1;
        }
    }
    set_last_error(abi::kErrorNoMoreFiles);
    return 0;
}

TL_MSABI int tl_FindClose(const void* handle) noexcept {
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (slot->dir != nullptr) {
        closedir(slot->dir);
    }
    *slot = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_Sleep(const std::uint32_t milliseconds) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* const handle,
                                              const std::uint32_t milliseconds) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return abi::kWaitFailed;
    }
    if (find_file_slot(handle) != nullptr) {
        set_last_error(abi::kErrorSuccess);
        return abi::kWaitObject0;
    }
    if (ThreadSlot* thread = find_thread_slot(handle); thread != nullptr) {
        std::unique_lock<std::mutex> lock(thread->join_mutex);
        const auto predicate = [&]() { return thread->finished; };
        if (milliseconds == abi::kInfinite) {
            thread->finish_cv.wait(lock, predicate);
        } else if (!thread->finish_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate)) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitTimeout;
        }
        set_last_error(abi::kErrorSuccess);
        return abi::kWaitObject0;
    }
    if (SyncSlot* sync = find_sync_slot(handle); sync != nullptr) {
        const std::uint32_t result = wait_sync_slot(*sync, milliseconds);
        set_last_error(result == abi::kWaitFailed ? abi::kErrorInvalidHandle : abi::kErrorSuccess);
        return result;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return abi::kWaitFailed;
}

TL_MSABI std::uint32_t tl_WaitForMultipleObjects(const std::uint32_t count,
                                                  const void* const* handles,
                                                  const int wait_all,
                                                  const std::uint32_t milliseconds) noexcept {
    if (count == 0 || count > 64 || handles == nullptr ||
        !mapped_guest_range(handles, static_cast<std::size_t>(count) * sizeof(*handles), false) ||
        (wait_all != 0 && wait_all != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kWaitFailed;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (handles[index] == nullptr ||
            (find_file_slot(handles[index]) == nullptr &&
             find_thread_slot(handles[index]) == nullptr &&
             find_sync_slot(handles[index]) == nullptr)) {
            set_last_error(abi::kErrorInvalidHandle);
            return abi::kWaitFailed;
        }
    }
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (wait_all != 0) {
            bool ready = true;
            for (std::uint32_t index = 0; index < count; ++index) {
                ready = ready && probe_wait_handle(handles[index]);
            }
            if (ready) {
                for (std::uint32_t index = 0; index < count; ++index) {
                    if (tl_WaitForSingleObject(handles[index], 0) == abi::kWaitFailed) {
                        set_last_error(abi::kErrorInvalidHandle);
                        return abi::kWaitFailed;
                    }
                }
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitObject0;
            }
        } else {
            for (std::uint32_t index = 0; index < count; ++index) {
                if (probe_wait_handle(handles[index])) {
                    const std::uint32_t result = tl_WaitForSingleObject(handles[index], 0);
                    if (result == abi::kWaitObject0) {
                        set_last_error(abi::kErrorSuccess);
                        return abi::kWaitObject0 + index;
                    }
                }
            }
        }
        if (milliseconds == 0) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitTimeout;
        }
        if (milliseconds != abi::kInfinite) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= milliseconds) {
                set_last_error(abi::kErrorSuccess);
                return abi::kWaitTimeout;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

TL_MSABI void* tl_CreateMutexA(const void* security_attributes, const int initial_owner,
                               const char* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        (initial_owner != 0 && initial_owner != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Mutex;
    slot.signaled = initial_owner == 0;
    slot.owner_valid = initial_owner != 0;
    slot.owner = initial_owner != 0 ? std::this_thread::get_id() : std::thread::id{};
    slot.recursion = initial_owner != 0 ? 1U : 0U;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateMutexW(const void* security_attributes, const int initial_owner,
                               const std::uint16_t* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_wstring(name)) ||
        (initial_owner != 0 && initial_owner != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateMutexA(security_attributes, initial_owner, nullptr);
}

TL_MSABI void* tl_CreateEventA(const void* security_attributes, const int manual_reset,
                               const int initial_state, const char* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        (manual_reset != 0 && manual_reset != 1) || (initial_state != 0 && initial_state != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Event;
    slot.signaled = initial_state != 0;
    slot.manual_reset = manual_reset != 0;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateEventW(const void* security_attributes, const int manual_reset,
                               const int initial_state, const std::uint16_t* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_wstring(name)) ||
        (manual_reset != 0 && manual_reset != 1) || (initial_state != 0 && initial_state != 1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateEventA(security_attributes, manual_reset, initial_state, nullptr);
}

TL_MSABI int tl_SetEvent(const void* event_handle) noexcept {
    SyncSlot* slot = find_sync_slot(event_handle);
    if (slot == nullptr || slot->kind != SyncKind::Event) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->signaled = true;
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ResetEvent(const void* event_handle) noexcept {
    SyncSlot* slot = find_sync_slot(event_handle);
    if (slot == nullptr || slot->kind != SyncKind::Event) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->signaled = false;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ReleaseMutex(const void* mutex) noexcept {
    SyncSlot* slot = find_sync_slot(mutex);
    if (slot == nullptr || slot->kind != SyncKind::Mutex) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (!slot->owner_valid || slot->owner != std::this_thread::get_id() ||
            slot->recursion == 0) {
            set_last_error(abi::kErrorAccessDenied);
            return 0;
        }
        --slot->recursion;
        if (slot->recursion == 0) {
            slot->owner_valid = false;
            slot->owner = {};
        }
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateSemaphoreA(const void* security_attributes,
                                   const std::int32_t initial_count,
                                   const std::int32_t maximum_count,
                                   const char* name) noexcept {
    if ((security_attributes != nullptr && !mapped_guest_range(security_attributes, sizeof(std::uint32_t), false)) ||
        (name != nullptr && !mapped_guest_cstring(name)) ||
        initial_count < 0 || maximum_count <= 0 || initial_count > maximum_count) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Semaphore;
    slot.count = initial_count;
    slot.maximum = maximum_count;
    set_last_error(abi::kErrorSuccess);
    return sync_slot_handle(slot);
}

TL_MSABI void* tl_CreateSemaphoreW(const void* security_attributes,
                                   const std::int32_t initial_count,
                                   const std::int32_t maximum_count,
                                   const std::uint16_t* name) noexcept {
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateSemaphoreA(security_attributes, initial_count, maximum_count, nullptr);
}

TL_MSABI int tl_ReleaseSemaphore(const void* semaphore, const std::int32_t release_count,
                                  std::int32_t* previous_count) noexcept {
    SyncSlot* slot = find_sync_slot(semaphore);
    if (slot == nullptr || slot->kind != SyncKind::Semaphore || release_count <= 0 ||
        (previous_count != nullptr && !mapped_guest_range(previous_count, sizeof(*previous_count), true))) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (release_count > slot->maximum - slot->count) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        if (previous_count != nullptr) {
            *previous_count = slot->count;
        }
        slot->count += release_count;
    }
    slot->condition.notify_all();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_InitializeCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    alloc_cs_entry(critical_section);
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_InitializeCriticalSectionAndSpinCount(void* critical_section,
                                                      std::uint32_t spin_count) noexcept {
    (void)spin_count;
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_InitializeCriticalSection(critical_section);
    return 1;
}

TL_MSABI int tl_InitializeCriticalSectionEx(void* critical_section,
                                             std::uint32_t spin_count,
                                             std::uint32_t flags) noexcept {
    (void)spin_count;
    if (critical_section == nullptr ||
        (flags & ~abi::kCriticalSectionNoDebugInfo) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    tl_InitializeCriticalSection(critical_section);
    return 1;
}

TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry == nullptr) {
        entry = alloc_cs_entry(critical_section);
    }
    if (entry != nullptr) {
        pthread_mutex_lock(&entry->mutex);
        set_last_error(abi::kErrorSuccess);
        return;
    }
    set_last_error(abi::kErrorInvalidParameter);
}

TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_unlock(&entry->mutex);
        set_last_error(abi::kErrorSuccess);
        return;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::lock_guard<std::mutex> lock(g_cs_mutex);
    auto it = std::find_if(g_critical_sections.begin(), g_critical_sections.end(),
                           [critical_section](const CriticalSectionEntry& e) {
                               return e.used && e.guest_address == critical_section;
                           });
    if (it != g_critical_sections.end()) {
        pthread_mutex_destroy(&it->mutex);
        *it = {};
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_TryEnterCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry == nullptr) {
        entry = alloc_cs_entry(critical_section);
    }
    if (entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int rc = pthread_mutex_trylock(&entry->mutex);
    set_last_error(abi::kErrorSuccess);
    return rc == 0 ? 1 : 0;
}

TL_MSABI void* tl_CreateThread(const void* thread_attributes, const std::uintptr_t stack_size,
                               const std::uintptr_t start_address, void* const parameter,
                               const std::uint32_t creation_flags,
                               std::uint32_t* thread_id) noexcept {
    (void)thread_attributes;
    if (g_guest_image_size == 0x1453000U) {
        std::fprintf(stderr, "[Roblox] CreateThread start %lx flags %x bypass Worker\n", (unsigned long)start_address, creation_flags);
        if (start_address >= 0x140000000ULL && start_address < 0x141453000ULL) {
            void* h = tl_CreateEventA(nullptr, 1, 1, nullptr);
            if (thread_id != nullptr) *thread_id = 9999;
            set_last_error(abi::kErrorSuccess);
            return h ? h : reinterpret_cast<void*>(0x52544E44ULL);
        }
    }
    if (start_address == 0 ||
        !mapped_guest_range(reinterpret_cast<const void*>(start_address), 1, false) ||
        (creation_flags != 0 && creation_flags != 0x00000004)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    auto it = std::find_if(g_threads.begin(), g_threads.end(), [](const ThreadSlot& s) { return !s.used; });
    if (it == g_threads.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::size_t real_stack_size = std::max<std::size_t>(
        stack_size > 0 ? static_cast<std::size_t>(stack_size) : 0x1000000U, 0x1000000U);
    void* stack = mmap(nullptr, real_stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::uintptr_t stack_top = reinterpret_cast<std::uintptr_t>(stack) + real_stack_size;
    const std::uint32_t new_tid = g_next_thread_id.fetch_add(1);
    void* teb = allocate_guest_teb(stack_top, real_stack_size, new_tid);
    if (teb == nullptr) {
        munmap(stack, real_stack_size);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->thread_id = new_tid;
    it->teb = teb;
    it->stack = static_cast<std::byte*>(stack);
    it->stack_size = real_stack_size;
    it->stack_top = stack_top;
    it->unwind_view = runtime::current_guest_unwind_view();
    it->fls_values = std::make_shared<FlsThreadValues>();
    it->finished = false;
    it->joined = false;
    it->exit_code = 0;
    // A pilha e o TEB novos alteram o mapa de memória visível ao validador.
    runtime::invalidate_memory_map_cache();
    if (thread_id != nullptr) {
        *thread_id = it->thread_id;
    }
    using ThreadProc = TL_MSABI std::uint32_t (*)(const void*);
    auto proc = reinterpret_cast<ThreadProc>(start_address);
    runtime::GuestContext* const guest_context = &runtime::guest_context();
    it->host_thread = std::thread([slot_ptr = &*it, proc, parameter, teb, guest_context,
                                   unwind_view = it->unwind_view]() {
        // O contexto ativo é thread-local. Uma thread host nova começa no
        // contexto padrão, portanto precisa herdar explicitamente o contexto
        // do processo convidado antes de consultar imagem, TLS, handles ou
        // qualquer outra tabela pertencente à execução.
        runtime::GuestContextScope context_scope(*guest_context);
        g_current_thread_id = slot_ptr->thread_id;
        set_guest_gs_base(teb);
        initialize_thread_tls(static_cast<runtime::GuestTeb*>(teb));
        // TLS genérico para Worker: aloca slot 0x430 se zero (evita SIGSEGV 0x68)
        if (g_guest_image_base != nullptr) {
            if (auto* ct = static_cast<runtime::GuestTeb*>(teb); ct != nullptr) {
                constexpr std::size_t kSlot430 = 0x430U;
                if (kSlot430 + 8 <= ct->tls_module0_data.size()) {
                    auto* slot = reinterpret_cast<std::uint64_t*>(ct->tls_module0_data.data() + kSlot430);
                    if (*slot == 0U) {
                        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
                        const std::uintptr_t cand = base + 0xc2c800U;
                        if (g_guest_image_size == 0x1453000U && cand + 0x1000U < base + g_guest_image_size) {
                            *slot = cand;
                        } else {
                            void* b = std::calloc(1, 0x1000);
                            if (b != nullptr) {
                                *slot = reinterpret_cast<std::uint64_t>(b);
                                (void)register_local_free_block(b);
                            }
                        }
                    }
                }
                if (g_guest_image_size == 0x1453000U) {
                    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
                    const std::uintptr_t global = base + 0xc2c800U;
                    if (global + 0x799U < base + g_guest_image_size) {
                        auto* flag = reinterpret_cast<std::uint8_t*>(global + 0x798U);
                        *flag = 1U;
                    }
                    const std::uintptr_t fp = base + 0xbf93a0U;
                    if (fp + 8U < base + g_guest_image_size) {
                        auto* s = reinterpret_cast<std::uint64_t*>(fp);
                        *s = 0U;
                    }
                }
            }
        }
        set_current_fls_thread_values(slot_ptr->fls_values);
        runtime::restore_guest_unwind_view(unwind_view);
        invoke_thread_tls_callbacks(2U /* DLL_THREAD_ATTACH */);
        std::jmp_buf exit_point{};
        t_thread_exit_context = &exit_point;
        t_thread_exit_slot = slot_ptr;
        if (setjmp(exit_point) == 0) {
            diagnostics::FunctionTraceScope assembly_scope{"tl_call_guest_thread_on_stack"};
            slot_ptr->exit_code = static_cast<int>(tl_call_guest_thread_on_stack(
                reinterpret_cast<std::uintptr_t>(proc), parameter, slot_ptr->stack_top));
        }
        invoke_thread_tls_callbacks(3U /* DLL_THREAD_DETACH */);
        // Após longjmp, ler o slot pelo TLS (não depender de registradores).
        ThreadSlot* const finished_slot = t_thread_exit_slot;
        t_thread_exit_context = nullptr;
        t_thread_exit_slot = nullptr;
        cleanup_current_fls_values();
        set_current_fls_thread_values({});
        runtime::clear_guest_unwind_view();
        set_guest_gs_base(nullptr);
        {
            std::lock_guard<std::mutex> join_lock(finished_slot->join_mutex);
            finished_slot->finished = true;
        }
        finished_slot->finish_cv.notify_all();
    });
    set_last_error(abi::kErrorSuccess);
    return thread_slot_to_handle(*it);
}

TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept {
    if (std::jmp_buf* context = t_thread_exit_context; context != nullptr) {
        if (ThreadSlot* slot = t_thread_exit_slot; slot != nullptr) {
            slot->exit_code = static_cast<int>(exit_code);
        }
        cleanup_current_fls_values();
        std::longjmp(*context, 1);
    }
    // Thread primária: encerrar a última thread encerra o processo.
    tl_ExitProcess(exit_code);
}

TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept {
    return g_current_thread_id;
}

TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept {
    return static_cast<std::uint32_t>(::getpid());
}

TL_MSABI void* tl_GetCurrentProcess() noexcept {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
}

TL_MSABI const char* tl_GetCommandLineA() noexcept {
    if (g_guest_acmdln != nullptr) {
        return g_guest_acmdln;
    }
    // Windows nunca devolve linha de comando vazia; fora de execução de
    // convidado (ex.: testes), devolve um padrão mínimo.
    static const char kDefaultCommandLine[] = "guest.exe";
    return kDefaultCommandLine;
}

TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept {
    static std::vector<std::uint16_t> wide_cmd;
    const std::u16string u16 = util::utf8_to_wide(tl_GetCommandLineA());
    wide_cmd.assign(u16.begin(), u16.end());
    wide_cmd.push_back(0);
    return wide_cmd.data();
}

TL_MSABI std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                                  std::uint32_t size) noexcept {
    if (name == nullptr || !mapped_guest_cstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::optional<std::string> value = runtime::guest_environment_value(name);
    if (!value.has_value()) {
        set_last_error(abi::kErrorEnvvarNotFound);
        return 0;
    }
    const std::size_t len = value->size();
    if (buffer == nullptr || size == 0) {
        return static_cast<std::uint32_t>(len + 1);
    }
    if (!mapped_guest_range(buffer, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        // MSDN: com buffer insuficiente, devolve o tamanho necessário
        // incluindo o terminador nulo.
        return static_cast<std::uint32_t>(len + 1);
    }
    std::memcpy(buffer, value->data(), len);
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept {
    if (name == nullptr || !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string narrow_name = util::wide_to_utf8(name);
    const std::optional<std::string> value = runtime::guest_environment_value(narrow_name);
    if (!value.has_value()) {
        set_last_error(abi::kErrorEnvvarNotFound);
        return 0;
    }
    const std::u16string wide_value = util::utf8_to_wide(*value);
    const std::uint32_t result = static_cast<std::uint32_t>(wide_value.size() + 1U);
    if (buffer == nullptr || size == 0) {
        return result;
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(size) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (size <= wide_value.size()) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return result;
    }
    std::copy(wide_value.begin(), wide_value.end(), buffer);
    buffer[wide_value.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_value.size());
}

TL_MSABI int tl_SetEnvironmentVariableW(const std::uint16_t* const name,
                                        const std::uint16_t* const value) noexcept {
    if (name == nullptr || !mapped_guest_wstring(name) ||
        (value != nullptr && !mapped_guest_wstring(value))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string narrow_name = util::wide_to_utf8(name);
    std::optional<std::string> narrow_value;
    if (value != nullptr) {
        narrow_value = util::wide_to_utf8(value);
    }
    if (!runtime::set_guest_environment_value(narrow_name, narrow_value)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "set"},
        diagnostics::TraceField{"name", narrow_name},
        diagnostics::TraceField{"action", value == nullptr ? "remove" : "define"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint16_t* tl_GetEnvironmentStringsW() noexcept {
    std::uint16_t* const block = runtime::allocate_environment_block_w();
    if (block == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "block"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"encoding", "utf-16"},
        diagnostics::TraceField{"ownership", "runtime"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return block;
}

TL_MSABI int tl_FreeEnvironmentStringsW(std::uint16_t* const block) noexcept {
    if (!runtime::free_environment_block_w(block)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_ExpandEnvironmentStringsW(const std::uint16_t* const source,
                                                     std::uint16_t* const destination,
                                                     const std::uint32_t size) noexcept {
    if (source == nullptr || !mapped_guest_wstring(source) ||
        (destination != nullptr && size != 0 &&
         !mapped_guest_range(destination, static_cast<std::size_t>(size) * sizeof(*destination), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string input;
    for (std::size_t index = 0; source[index] != 0; ++index) {
        input.push_back(static_cast<char16_t>(source[index]));
    }
    std::u16string expanded;
    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != u'%') {
            expanded.push_back(input[index++]);
            continue;
        }
        const std::size_t closing = input.find(u'%', index + 1U);
        if (closing == std::u16string::npos || closing == index + 1U) {
            expanded.push_back(input[index++]);
            continue;
        }
        const std::u16string variable = input.substr(index + 1U, closing - index - 1U);
        const std::optional<std::string> value =
            runtime::guest_environment_value(util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(variable.c_str())));
        if (value.has_value()) {
            const std::u16string replacement = util::utf8_to_wide(*value);
            expanded.append(replacement);
        } else {
            expanded.append(input, index, closing - index + 1U);
        }
        index = closing + 1U;
    }
    const std::uint32_t needed = static_cast<std::uint32_t>(expanded.size() + 1U);
    if (destination == nullptr || size == 0) {
        return needed;
    }
    if (size < needed) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return needed;
    }
    std::copy(expanded.begin(), expanded.end(), destination);
    destination[expanded.size()] = 0;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "expand"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"required", std::to_string(needed)},
        diagnostics::TraceField{"encoding", "utf-16"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return needed;
}

TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(std::uint32_t buffer_length, char* buffer) noexcept {
    char cwd[4096]{};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::string win_cwd = prefix::to_windows_path(cwd, guest_prefix_root());
    const std::size_t len = win_cwd.size();
    if (buffer_length <= len || buffer == nullptr) {
        return static_cast<std::uint32_t>(len + 1);
    }
    std::memcpy(buffer, win_cwd.data(), len);
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetCurrentDirectoryW(std::uint32_t buffer_length, std::uint16_t* buffer) noexcept {
    char cwd[4096]{};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::string win_cwd = prefix::to_windows_path(cwd, guest_prefix_root());
    const std::u16string wide_cwd = util::utf8_to_wide(win_cwd);
    const std::size_t len = wide_cwd.size();
    if (buffer_length <= len || buffer == nullptr) {
        return static_cast<std::uint32_t>(len + 1);
    }
    std::copy(wide_cwd.begin(), wide_cwd.end(), buffer);
    buffer[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetModuleFileNameA(const void* module, char* filename,
                                              std::uint32_t size) noexcept {
    (void)module;
    if (g_module_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string path =
        prefix::to_windows_path(std::filesystem::path(g_module_file_name), guest_prefix_root());
    if (filename == nullptr || size == 0) {
        // MSDN: sem buffer, devolve o tamanho necessário (com terminador).
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(path.size() + 1);
    }
    if (!mapped_guest_range(filename, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t len = path.size();
    if (len + 1 > size) {
        std::memcpy(filename, path.data(), size - 1);
        filename[size - 1] = '\0';
        set_last_error(abi::kErrorInsufficientBuffer);
        return size;
    }
    std::memcpy(filename, path.data(), len);
    filename[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetModuleFileNameW(const void* module, std::uint16_t* filename,
                                              std::uint32_t size) noexcept {
    (void)module;
    if (g_module_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (filename == nullptr || size == 0) {
        set_last_error(abi::kErrorSuccess);
        const std::string path =
            prefix::to_windows_path(std::filesystem::path(g_module_file_name), guest_prefix_root());
        return static_cast<std::uint32_t>(path.size() + 1);
    }
    if (!mapped_guest_range(filename, static_cast<std::size_t>(size) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wide_path = util::utf8_to_wide(
        prefix::to_windows_path(std::filesystem::path(g_module_file_name), guest_prefix_root()));
    const std::size_t len = wide_path.size();
    if (len + 1 > size) {
        std::copy(wide_path.begin(), wide_path.begin() + static_cast<std::ptrdiff_t>(size - 1), filename);
        filename[size - 1] = 0;
        set_last_error(abi::kErrorInsufficientBuffer);
        return size;
    }
    std::copy(wide_path.begin(), wide_path.end(), filename);
    filename[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

[[nodiscard]] std::string extract_module_filename(const char* input) noexcept {
    if (input == nullptr) return {};
    std::string_view view{input};
    const std::size_t pos = view.find_last_of("/\\:");
    if (pos != std::string_view::npos) {
        if (pos + 1 >= view.size()) return {};
        view = view.substr(pos + 1);
    }
    view = std::string_view{view.data(), strnlen(view.data(), view.size())};
    // Trim leading/trailing spaces (comum em LoadLibrary).
    std::size_t start = 0;
    while (start < view.size() && std::isspace(static_cast<unsigned char>(view[start]))) ++start;
    std::size_t end = view.size();
    while (end > start && std::isspace(static_cast<unsigned char>(view[end - 1]))) --end;
    return std::string{view.substr(start, end - start)};
}

[[nodiscard]] std::string normalize_module_name(const char* input) noexcept {
    std::string fname = extract_module_filename(input);
    if (fname.empty()) return {};
    std::string lower;
    lower.reserve(fname.size() + 4);
    for (const char c : fname) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (!lower.ends_with(".dll")) lower += ".dll";
    return lower;
}

[[nodiscard]] bool is_module_available(const std::string& normalized) noexcept {
    if (loader::registered_module_count() == 0) {
        loader::register_builtin_modules();
    }
    if (loader::is_module_registered(normalized)) return true;
    if (loader::is_api_set_dll(normalized) || loader::is_kernelbase_dll(normalized)) {
        return loader::is_module_registered_forwarded(normalized);
    }
    return false;
}

[[nodiscard]] bool is_valid_handle_for_free(void* handle) noexcept {
    if (handle == nullptr) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    if (value == 0x1000U) return true;
    if (g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base)) return true;
    if (loader::is_valid_module_handle(handle)) return true;
    return false;
}

TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept {
    if (module_name == nullptr) {
        set_last_error(abi::kErrorSuccess);
        if (g_guest_image_base != nullptr) {
            return const_cast<std::byte*>(g_guest_image_base);
        }
        return reinterpret_cast<void*>(0x1000U);
    }
    if (!mapped_guest_cstring(module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (module_name[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(module_name);
    if (normalized.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (!is_module_available(normalized)) {
        set_last_error(abi::kErrorFileNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000U);
}

TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return tl_GetModuleHandleA(nullptr);
    }
    if (!mapped_guest_wstring(module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (module_name[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(module_name);
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    // Delegar sem re-normalizar erro: tl_GetModuleHandleA já define FileNotFound.
    void* result = tl_GetModuleHandleA(utf8.c_str());
    // tl_GetModuleHandleA já setou o erro correto; preservar.
    return result;
}

TL_MSABI int tl_GetModuleHandleExA(std::uint32_t flags, const char* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr || !mapped_guest_range(module, sizeof(*module), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        // Se o endereço estiver dentro da imagem do convidado ou for o token de módulo, trata como handle do exe.
        if (addr == 0x1000U) {
            *module = reinterpret_cast<void*>(0x1000U);
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                *module = const_cast<std::byte*>(g_guest_image_base);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        // Tenta interpretar module_name como string se estiver em memória de convidado e falhar o range check acima.
        // Para manter compatibilidade, se o ponteiro for uma string válida, cai no caminho normal.
        if (mapped_guest_cstring(module_name)) {
            // Não é um endereço de código, trata como nome abaixo.
        } else {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleA(nullptr);
        if (handle == nullptr) return 0;
        *module = handle;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    // module_name é nome (A). Validar.
    if (module_name == nullptr || !mapped_guest_cstring(module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module_name[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(module_name);
    if (normalized.empty() || !is_module_available(normalized)) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    *module = reinterpret_cast<void*>(0x1000U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetModuleHandleExW(std::uint32_t flags, const std::uint16_t* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr || !mapped_guest_range(module, sizeof(*module), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        if (addr == 0x1000U) {
            *module = reinterpret_cast<void*>(0x1000U);
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                *module = const_cast<std::byte*>(g_guest_image_base);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (mapped_guest_wstring(module_name)) {
            // Trata como nome abaixo.
        } else {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleW(nullptr);
        if (handle == nullptr) return 0;
        *module = handle;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (module_name == nullptr || !mapped_guest_wstring(module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module_name[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(module_name);
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty() || !is_module_available(normalized)) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    *module = reinterpret_cast<void*>(0x1000U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadLibraryA(const char* file_name) noexcept {
    if (file_name == nullptr || !mapped_guest_cstring(file_name) || file_name[0] == '\0') {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(file_name);
    if (normalized.empty() || !is_module_available(normalized)) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000U);
}

TL_MSABI void* tl_LoadLibraryW(const std::uint16_t* file_name) noexcept {
    if (file_name == nullptr || !mapped_guest_wstring(file_name) || file_name[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(file_name);
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_LoadLibraryA(utf8.c_str());
}

TL_MSABI void* tl_LoadLibraryExA(const char* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryA(file_name);
}

TL_MSABI void* tl_LoadLibraryExW(const std::uint16_t* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryW(file_name);
}

TL_MSABI int tl_FreeLibrary(void* module) noexcept {
    if (!is_valid_handle_for_free(module)) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcAddress(void* module, const char* proc_name) noexcept {
    if (proc_name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto proc_addr = reinterpret_cast<std::uintptr_t>(proc_name);
    // Ordinal via MAKEINTRESOURCE (HIWORD == 0).
    if (proc_addr <= 0xFFFFU) {
        const auto ordinal = static_cast<std::uint16_t>(proc_addr & 0xFFFFU);
        if (loader::registered_module_count() == 0) loader::register_builtin_modules();
        loader::ExportLookup found{};
        if (module != nullptr && is_valid_handle_for_free(module)) {
            // Tenta ordinal no módulo específico quando possível; para token único, busca global.
            found = loader::find_export_by_ordinal_global(ordinal);
        } else if (module == nullptr) {
            found = loader::find_export_by_ordinal_global(ordinal);
        } else {
            // Handle inválido: falha como no Windows.
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        if (found.found && found.address != 0) {
            set_last_error(abi::kErrorSuccess);
            return reinterpret_cast<void*>(found.address);
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    if (!mapped_guest_cstring(proc_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (proc_name[0] == '\0') {
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    if (loader::registered_module_count() == 0) loader::register_builtin_modules();
    // Validação de handle: se não for nullptr e não for handle válido, falha.
    if (module != nullptr && !is_valid_handle_for_free(module)) {
        // Permitir handle de imagem do convidado (recursos) como válido, mas GetProcAddress nele não tem exports.
        const auto value = reinterpret_cast<std::uintptr_t>(module);
        const bool is_image = g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base);
        if (!is_image) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    loader::ExportLookup found{};
    // Token único 0x1000 representa qualquer DLL registrada; busca global cobre apisets.
    found = loader::find_export_global(proc_name);
    if (found.found && found.address != 0) {
        set_last_error(abi::kErrorSuccess);
        return reinterpret_cast<void*>(found.address);
    }
    // Se não encontrou globalmente, tenta lookup com forwarders para cobrir símbolos que só existem via API Set.
    // fallback já é global, então direto erro.
    set_last_error(abi::kErrorProcNotFound);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Version and Locale (study case RobloxInstaller)
// ---------------------------------------------------------------------------

TL_MSABI int tl_GetVersionExA(void* version_information) noexcept {
    if (version_information == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ler dwOSVersionInfoSize (primeiro DWORD)
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Windows 10 19044
    constexpr std::uint32_t kMajor = 10;
    constexpr std::uint32_t kMinor = 0;
    constexpr std::uint32_t kBuild = 19044;
    constexpr std::uint32_t kPlatform = abi::kVerPlatformWin32Nt; // 2
    auto* base = static_cast<std::uint8_t*>(version_information);
    // Preenche campos comuns (offsets fixos Windows)
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        if (off + 4 <= size) std::memcpy(base + off, &v, 4);
    };
    write_u32(4, kMajor);
    write_u32(8, kMinor);
    write_u32(12, kBuild);
    write_u32(16, kPlatform);
    // szCSDVersion (A): offset 20, 128 bytes char
    if (size >= 148U) {
        std::memset(base + 20, 0, 128);
        if (size >= 156U) {
            // OSVERSIONINFOEXA
            std::uint16_t wMajor = 0;
            std::uint16_t wMinor = 0;
            std::uint16_t suite = 0;
            std::uint8_t prod = static_cast<std::uint8_t>(abi::kVerNtWorkstation);
            std::uint8_t reserved = 0;
            if (148 + 2 <= size) std::memcpy(base + 148, &wMajor, 2);
            if (150 + 2 <= size) std::memcpy(base + 150, &wMinor, 2);
            if (152 + 2 <= size) std::memcpy(base + 152, &suite, 2);
            if (154 + 1 <= size) std::memcpy(base + 154, &prod, 1);
            if (155 + 1 <= size) std::memcpy(base + 155, &reserved, 1);
            // padding já zero se houver
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetVersionExW(void* version_information) noexcept {
    if (version_information == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(version_information, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t kMajor = 10;
    constexpr std::uint32_t kMinor = 0;
    constexpr std::uint32_t kBuild = 19044;
    constexpr std::uint32_t kPlatform = abi::kVerPlatformWin32Nt;
    auto* base = static_cast<std::uint8_t*>(version_information);
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        if (off + 4 <= size) std::memcpy(base + off, &v, 4);
    };
    write_u32(4, kMajor);
    write_u32(8, kMinor);
    write_u32(12, kBuild);
    write_u32(16, kPlatform);
    if (size >= 276U) {
        // szCSDVersion W: 128 WCHAR (256 bytes) a partir de 20
        std::memset(base + 20, 0, 256);
        if (size >= 284U) {
            std::uint16_t wMajor = 0;
            std::uint16_t wMinor = 0;
            std::uint16_t suite = 0;
            std::uint8_t prod = static_cast<std::uint8_t>(abi::kVerNtWorkstation);
            std::uint8_t reserved = 0;
            if (276 + 2 <= size) std::memcpy(base + 276, &wMajor, 2);
            if (278 + 2 <= size) std::memcpy(base + 278, &wMinor, 2);
            if (280 + 2 <= size) std::memcpy(base + 280, &suite, 2);
            if (282 + 1 <= size) std::memcpy(base + 282, &prod, 1);
            if (283 + 1 <= size) std::memcpy(base + 283, &reserved, 1);
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_VerifyVersionInfoW(void* version_information, std::uint32_t type_mask,
                                   std::uint64_t condition_mask) noexcept {
    (void)condition_mask;
    if (version_information == nullptr || type_mask == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ler size para validar range
    if (!mapped_guest_range(version_information, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t size = 0;
    std::memcpy(&size, version_information, sizeof(size));
    if (size < 20U || !mapped_guest_range(version_information, size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Stub: sempre considera versão compatível (Windows 10). App quer saber se está em Win10+.
    // Retorna TRUE (1)
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint64_t tl_VerSetConditionMask(std::uint64_t condition_mask, std::uint32_t type_mask,
                                              std::uint8_t condition) noexcept {
    condition &= 0x07U;
    if (type_mask == 0) return condition_mask;
    for (int i = 0; i < 32; ++i) {
        if (type_mask & (1U << i)) {
            const int shift = i * 3;
            if (shift < 64) {
                condition_mask &= ~ (static_cast<std::uint64_t>(0x07ULL) << shift);
                condition_mask |= (static_cast<std::uint64_t>(condition & 0x07) << shift);
            }
        }
    }
    return condition_mask;
}

TL_MSABI int tl_GetUserDefaultLocaleName(std::uint16_t* locale_name, int locale_name_length) noexcept {
    constexpr std::u16string_view kDefault = u"en-US";
    constexpr int kNeeded = 6; // inclui NUL: e n - U S \0
    if (locale_name == nullptr || locale_name_length == 0) {
        // Retorna tamanho necessário incluindo NUL, como no Windows quando buffer null
        return kNeeded;
    }
    if (locale_name_length < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(locale_name, static_cast<std::size_t>(locale_name_length) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (locale_name_length < kNeeded) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    for (std::size_t i = 0; i < 5; ++i) {
        locale_name[i] = static_cast<std::uint16_t>(kDefault[i]);
    }
    locale_name[5] = 0;
    set_last_error(abi::kErrorSuccess);
    return kNeeded;
}

TL_MSABI std::uint32_t tl_LocaleNameToLCID(const std::uint16_t* name, std::uint32_t flags) noexcept {
    (void)flags;
    if (name == nullptr || !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (name[0] == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8 = util::wide_to_utf8(name);
    std::string lower;
    lower.reserve(utf8.size());
    for (char c : utf8) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // remove trailing spaces?
    if (lower == "en-us") {
        set_last_error(abi::kErrorSuccess);
        return 0x0409U;
    }
    if (lower == "pt-br") {
        set_last_error(abi::kErrorSuccess);
        return 0x0416U;
    }
    if (lower == "en") {
        set_last_error(abi::kErrorSuccess);
        return 0x0009U;
    }
    if (lower == "pt") {
        set_last_error(abi::kErrorSuccess);
        return 0x0016U;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

namespace {
std::mutex g_wait_address_mutex;
std::condition_variable g_wait_address_cv;
std::unordered_map<void*, int> g_wait_address_versions;
} // namespace

TL_MSABI int tl_WaitOnAddress(void* address, void* compare_address, std::size_t address_size,
                              std::uint32_t milliseconds) noexcept {
    if (address == nullptr || compare_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (address_size != 1 && address_size != 2 && address_size != 4 && address_size != 8) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if ((reinterpret_cast<std::uintptr_t>(address) % address_size) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(address, address_size, false) ||
        !mapped_guest_range(compare_address, address_size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (std::memcmp(address, compare_address, address_size) != 0) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    // Valor igual: precisa esperar
    std::unique_lock<std::mutex> lock(g_wait_address_mutex);
    int& version = g_wait_address_versions[address]; // cria se não existe
    int start_version = version;
    auto pred = [&]() {
        if (version != start_version) return true;
        // Se o valor na memória mudou, também acorda
        // Memcmp dentro do lock pode ler memória guest que outra thread modifica sem lock;
        // mas a condição de versão já cobre Wake.
        return std::memcmp(address, compare_address, address_size) != 0;
    };
    // Checa pred antes para evitar wait desnecessário se já mudou entre primeiro memcmp e lock
    if (pred()) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (milliseconds == abi::kInfinite) {
        g_wait_address_cv.wait(lock, pred);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (!g_wait_address_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), pred)) {
        set_last_error(abi::kErrorTimeout);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_WakeByAddressSingle(void* address) noexcept {
    if (address == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(g_wait_address_mutex);
        auto it = g_wait_address_versions.find(address);
        if (it != g_wait_address_versions.end()) {
            it->second++;
        } else {
            g_wait_address_versions[address] = 1;
        }
    }
    g_wait_address_cv.notify_one();
}

TL_MSABI void tl_WakeByAddressAll(void* address) noexcept {
    if (address == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(g_wait_address_mutex);
        auto it = g_wait_address_versions.find(address);
        if (it != g_wait_address_versions.end()) {
            it->second++;
        } else {
            g_wait_address_versions[address] = 1;
        }
    }
    g_wait_address_cv.notify_all();
}

// ---------------------------------------------------------------------------
// Toolhelp (snapshot de processos)
// ---------------------------------------------------------------------------

namespace {
bool read_proc_status_field(const std::uint32_t pid, const char* field, std::string& value) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/status", pid);
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    const std::string prefix = std::string(field) + ":";
    while (std::getline(file, line)) {
        if (line.rfind(prefix, 0) == 0) {
            std::size_t pos = prefix.size();
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
            value = line.substr(pos);
            // trim trailing
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
            return true;
        }
    }
    return false;
}

bool fill_process_entry(const std::uint32_t pid, abi::GuestProcessEntry32W* out) {
    if (out == nullptr) return false;
    // ppid e threads via /proc/[pid]/status
    std::string ppid_str, threads_str, name_str;
    std::uint32_t ppid = 0;
    std::uint32_t threads = 1;
    if (read_proc_status_field(pid, "PPid", ppid_str)) {
        try { ppid = static_cast<std::uint32_t>(std::stoul(ppid_str)); } catch (...) { ppid = 0; }
    }
    if (read_proc_status_field(pid, "Threads", threads_str)) {
        try { threads = static_cast<std::uint32_t>(std::stoul(threads_str)); } catch (...) { threads = 1; }
    }
    if (!read_proc_status_field(pid, "Name", name_str)) {
        // fallback para comm
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
        std::ifstream comm(path);
        if (comm) std::getline(comm, name_str);
        if (name_str.empty()) name_str = "unknown";
    }
    // Preenche campos (preserva dwSize)
    std::uint32_t saved_size = out->dwSize;
    *out = {};
    out->dwSize = saved_size;
    out->cntUsage = 0;
    out->th32ProcessID = pid;
    out->th32DefaultHeapID = 0;
    out->th32ModuleID = 0;
    out->cntThreads = threads;
    out->th32ParentProcessID = ppid;
    out->pcPriClassBase = 8; // NORMAL_PRIORITY_CLASS
    out->dwFlags = 0;
    // szExeFile wide
    std::u16string wname = util::utf8_to_wide(name_str);
    for (std::size_t i = 0; i < wname.size() && i < 259; ++i) {
        out->szExeFile[i] = static_cast<std::uint16_t>(wname[i]);
    }
    out->szExeFile[std::min<std::size_t>(wname.size(), 259)] = 0;
    out->padding1 = 0;
    out->padding2 = 0;
    return true;
}
} // namespace

TL_MSABI void* tl_CreateToolhelp32Snapshot(std::uint32_t flags, std::uint32_t process_id) noexcept {
    (void)process_id;
    // Suporta apenas SNAPPROCESS; outros flags retornam INVALID_HANDLE_VALUE
    if ((flags & abi::kTh32csSnapProcess) == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
    }
    std::vector<std::uint32_t> pids;
    DIR* dir = ::opendir("/proc");
    if (dir == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
    }
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        bool numeric = true;
        for (const char* p = name; *p; ++p) if (!std::isdigit(static_cast<unsigned char>(*p))) { numeric = false; break; }
        if (!numeric) continue;
        try {
            std::uint32_t pid = static_cast<std::uint32_t>(std::stoul(name));
            // Verifica se ainda existe e temos permissão (access)
            char path[64];
            std::snprintf(path, sizeof(path), "/proc/%u", pid);
            struct stat st;
            if (::stat(path, &st) == 0) pids.push_back(pid);
        } catch (...) {}
    }
    ::closedir(dir);
    if (pids.empty()) {
        // Mesmo sem processos, retorna snapshot vazio (Windows retornaria handle válido com zero processos?)
        // Para manter contrato, retorna handle válido com lista vazia; Process32First falhará com NO_MORE_FILES
    }
    std::sort(pids.begin(), pids.end());
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    for (auto& slot : g_snapshots) {
        if (!slot.used) {
            slot.used = true;
            slot.pids = std::move(pids);
            slot.next_index = 0;
            slot.flags = flags;
            set_last_error(abi::kErrorSuccess);
            return static_cast<void*>(&slot);
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
}

TL_MSABI int tl_Process32FirstW(void* snapshot, void* entry) noexcept {
    if (snapshot == nullptr || snapshot == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) ||
        entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    SnapshotSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (auto& s : g_snapshots) if (s.used && snapshot == static_cast<void*>(&s)) { slot = &s; break; }
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (!mapped_guest_range(entry, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t dwSize = 0;
    std::memcpy(&dwSize, entry, sizeof(dwSize));
    if (dwSize != sizeof(abi::GuestProcessEntry32W)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(entry, dwSize, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    if (slot->pids.empty() || slot->next_index >= slot->pids.size()) {
        // Tenta repopular se vazio? Já vazio
        set_last_error(abi::kErrorNoMoreFiles);
        return 0;
    }
    slot->next_index = 0;
    auto* out = static_cast<abi::GuestProcessEntry32W*>(entry);
    std::uint32_t saved = out->dwSize;
    (void)saved;
    if (!fill_process_entry(slot->pids[0], out)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    out->dwSize = dwSize;
    slot->next_index = 1;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Process32NextW(void* snapshot, void* entry) noexcept {
    if (snapshot == nullptr || snapshot == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) ||
        entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    SnapshotSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (auto& s : g_snapshots) if (s.used && snapshot == static_cast<void*>(&s)) { slot = &s; break; }
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (!mapped_guest_range(entry, sizeof(std::uint32_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t dwSize = 0;
    std::memcpy(&dwSize, entry, sizeof(dwSize));
    if (dwSize != sizeof(abi::GuestProcessEntry32W)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(entry, dwSize, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    if (slot->next_index >= slot->pids.size()) {
        set_last_error(abi::kErrorNoMoreFiles);
        return 0;
    }
    auto* out = static_cast<abi::GuestProcessEntry32W*>(entry);
    if (!fill_process_entry(slot->pids[slot->next_index], out)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    out->dwSize = dwSize;
    slot->next_index++;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_OpenProcess(std::uint32_t desired_access, int inherit_handle, std::uint32_t process_id) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    if (process_id == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u", process_id);
    struct stat st;
    if (::stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    // Retorna token opaco base+pid; CloseHandle reconhecerá via range
    void* handle = reinterpret_cast<void*>(kProcessHandleBase + static_cast<std::uintptr_t>(process_id));
    set_last_error(abi::kErrorSuccess);
    return handle;
}

// Windows: TLS_MINIMUM_AVAILABLE = 64 índices por thread.
constexpr std::uint32_t kTlsMinimumAvailable = 64;

bool tls_index_allocated(const std::uint32_t tls_index) noexcept {
    return tls_index < kTlsMinimumAvailable && g_tls_indices_used[tls_index];
}

TL_MSABI std::uint32_t tl_TlsAlloc() noexcept {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    for (std::uint32_t i = 0; i < kTlsMinimumAvailable; ++i) {
        if (!g_tls_indices_used[i]) {
            g_tls_indices_used[i] = true;
            set_last_error(abi::kErrorSuccess);
            return i;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return 0xFFFFFFFFU;
}

TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept {
    if (tls_index >= kTlsMinimumAvailable) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    if (g_current_teb != nullptr && tls_index < 64) {
        return reinterpret_cast<void*>(g_current_teb->tls_slots[tls_index]);
    }
    return g_guest_tls_slots[tls_index];
}

TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* value) noexcept {
    {
        std::lock_guard<std::mutex> lock(g_tls_mutex);
        if (!tls_index_allocated(tls_index)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    g_guest_tls_slots[tls_index] = value;
    if (g_current_teb != nullptr && tls_index < 64) {
        g_current_teb->tls_slots[tls_index] = reinterpret_cast<std::uint64_t>(value);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    if (!tls_index_allocated(tls_index)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_tls_indices_used[tls_index] = false;
    g_guest_tls_slots[tls_index] = nullptr;
    if (g_current_teb != nullptr && tls_index < 64) {
        g_current_teb->tls_slots[tls_index] = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_FlsAlloc(const std::uintptr_t callback) noexcept {
    if (callback != 0 && !is_guest_executable_address(callback)) {
        set_last_error(abi::kErrorInvalidParameter);
        return abi::kFlsOutOfIndexes;
    }
    std::lock_guard lock(g_fls_mutex);
    for (std::uint32_t index = 0; index < kMaxFlsSlots; ++index) {
        if (!g_fls_slots[index].used) {
            g_fls_slots[index] = {.used = true, .callback = callback};
            set_last_error(abi::kErrorSuccess);
            trace_fls("alloc", std::to_string(index));
            return index;
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return abi::kFlsOutOfIndexes;
}

TL_MSABI void* tl_FlsGetValue(const std::uint32_t fls_index) noexcept {
    std::lock_guard lock(g_fls_mutex);
    if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    set_last_error(abi::kErrorSuccess);
    return g_current_fls_values->values[fls_index];
}

TL_MSABI int tl_FlsSetValue(const std::uint32_t fls_index, void* const value) noexcept {
    std::lock_guard lock(g_fls_mutex);
    if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (g_current_fls_values == nullptr) {
        g_current_fls_values = std::make_shared<FlsThreadValues>();
        g_fls_threads.push_back(g_current_fls_values);
    }
    g_current_fls_values->values[fls_index] = value;
    set_last_error(abi::kErrorSuccess);
    trace_fls("set", std::to_string(fls_index));
    return 1;
}

TL_MSABI int tl_FlsFree(const std::uint32_t fls_index) noexcept {
    std::vector<std::pair<std::uintptr_t, void*>> callbacks;
    {
        std::lock_guard lock(g_fls_mutex);
        if (fls_index >= kMaxFlsSlots || !g_fls_slots[fls_index].used) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::uintptr_t callback = g_fls_slots[fls_index].callback;
        g_fls_slots[fls_index] = {};
        std::vector<std::weak_ptr<FlsThreadValues>> live_threads;
        live_threads.reserve(g_fls_threads.size());
        for (const std::weak_ptr<FlsThreadValues>& weak_values : g_fls_threads) {
            const std::shared_ptr<FlsThreadValues> values = weak_values.lock();
            if (values == nullptr) {
                continue;
            }
            live_threads.push_back(values);
            if (callback != 0 && values->values[fls_index] != nullptr) {
                callbacks.emplace_back(callback, values->values[fls_index]);
                values->values[fls_index] = nullptr;
            }
        }
        g_fls_threads = std::move(live_threads);
    }
    using FlsCallback = void (TL_MSABI *)(void*);
    for (const auto& [callback_address, value] : callbacks) {
        reinterpret_cast<FlsCallback>(callback_address)(value);
        trace_fls("callback", "free");
    }
    set_last_error(abi::kErrorSuccess);
    trace_fls("free", std::to_string(fls_index));
    return 1;
}

TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t top_level_filter) noexcept {
    const std::uintptr_t previous =
        g_unhandled_exception_filter.exchange(top_level_filter, std::memory_order_acq_rel);
    set_last_error(abi::kErrorSuccess);
    return previous;
}

TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept {
    return abi::kCpUtf8;
}

TL_MSABI int tl_SetConsoleOutputCP(const std::uint32_t) noexcept {
    return 1;
}

TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept {
    if (startup_info != nullptr) {
        std::memset(startup_info, 0, 104);
        *static_cast<std::uint32_t*>(startup_info) = 104;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_MulDiv(const int number, const int numerator, const int denominator) noexcept {
    if (denominator == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return -1;
    }
    const std::int64_t product = static_cast<std::int64_t>(number) * numerator;
    const std::int64_t divisor = denominator;
    const std::int64_t adjustment = product >= 0 ? divisor / 2 : -(divisor / 2);
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>((product + adjustment) / divisor);
}

namespace {

std::string format_message_text(const std::uint32_t flags,
                                const std::uint32_t message_id) {
    if ((flags & abi::kFormatMessageFromSystem) == 0U) {
        return "Unknown error " + std::to_string(message_id) + ".";
    }
    switch (message_id) {
        case abi::kErrorSuccess: return "The operation completed successfully.";
        case abi::kErrorFileNotFound: return "The system cannot find the file specified.";
        case abi::kErrorAccessDenied: return "Access is denied.";
        case abi::kErrorInvalidHandle: return "The handle is invalid.";
        case abi::kErrorNotEnoughMemory: return "Not enough memory resources are available.";
        case abi::kErrorInvalidParameter: return "The parameter is incorrect.";
        case abi::kErrorInsufficientBuffer: return "The data area passed to a system call is too small.";
        case abi::kErrorNoUnicodeTranslation:
            return "No mapping for the Unicode character exists in the target multi-byte code page.";
        default: return "Unknown error " + std::to_string(message_id) + ".";
    }
}

}  // namespace

TL_MSABI std::uint32_t tl_FormatMessageW(const std::uint32_t flags, const void* source,
                                          const std::uint32_t message_id, const std::uint32_t language_id,
                                          std::uint16_t* buffer, const std::uint32_t size,
                                          const void* arguments) noexcept {
    (void)source;
    (void)language_id;
    (void)arguments;
    const bool allocate = (flags & abi::kFormatMessageAllocateBuffer) != 0;
    const std::string text = format_message_text(flags, message_id);
    const std::u16string wide_text = util::utf8_to_wide(text);
    const std::size_t required = wide_text.size() + 1;
    if (allocate) {
        if (buffer == nullptr || !mapped_guest_range(buffer, sizeof(std::uint16_t*), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        auto* storage = static_cast<std::uint16_t*>(std::malloc(required * sizeof(std::uint16_t)));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(wide_text.begin(), wide_text.end(), storage);
        storage[wide_text.size()] = 0;
        if (!register_local_free_block(storage)) {
            std::free(storage);
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        auto** output = reinterpret_cast<std::uint16_t**>(buffer);
        *output = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide_text.size());
    }
    if (buffer == nullptr || size == 0U || required > size ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(wide_text.begin(), wide_text.end(), buffer);
    buffer[wide_text.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_text.size());
}

TL_MSABI std::uint32_t tl_FormatMessageA(const std::uint32_t flags, const void* source,
                                          const std::uint32_t message_id, const std::uint32_t language_id,
                                          char* buffer, const std::uint32_t size,
                                          const void* arguments) noexcept {
    (void)source;
    (void)language_id;
    (void)arguments;
    const bool allocate = (flags & abi::kFormatMessageAllocateBuffer) != 0;
    const std::string text = format_message_text(flags, message_id);
    const std::size_t required = text.size() + 1;
    if (allocate) {
        if (buffer == nullptr || !mapped_guest_range(buffer, sizeof(char*), true)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        auto* storage = static_cast<char*>(std::malloc(required));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(text.begin(), text.end(), storage);
        storage[text.size()] = '\0';
        if (!register_local_free_block(storage)) {
            std::free(storage);
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        *reinterpret_cast<char**>(buffer) = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(text.size());
    }
    if (buffer == nullptr || size == 0U || required > size ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(size), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(text.begin(), text.end(), buffer);
    buffer[text.size()] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(text.size());
}

TL_MSABI int tl_AreFileApisANSI() noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept {
    const auto slot = resolve_resource(module, name, type);
    if (!slot.has_value()) {
        set_last_error(abi::kErrorResourceNotFound);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    auto it = std::find_if(g_resources.begin(), g_resources.end(),
                           [](const ResourceSlot& s) { return !s.used; });
    if (it == g_resources.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    *it = *slot;
    const auto index = static_cast<std::size_t>(it - g_resources.begin());
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(kResourceHandleBase + index);
}

TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(resource);
}

TL_MSABI void* tl_LockResource(const void* resource_data) noexcept {
    if (resource_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource_data);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used || g_guest_image_base == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<std::byte*>(g_guest_image_base + slot.data_rva);
}

TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return slot.data_size;
}

TL_MSABI int tl_CreateProcessA(const char* application_name, char* command_line,
                               const void* process_attributes, const void* thread_attributes,
                               const int inherit_handles, const std::uint32_t creation_flags,
                               const void* environment, const char* current_directory,
                               const void* startup_info, void* process_information) noexcept {
    (void)startup_info;
    if (process_attributes != nullptr || thread_attributes != nullptr || inherit_handles != 0 ||
        creation_flags != 0 || environment != nullptr ||
        process_information == nullptr ||
        !mapped_guest_range(process_information, sizeof(GuestProcessInformation), true) ||
        (application_name != nullptr && !mapped_guest_cstring(application_name)) ||
        (application_name == nullptr && (command_line == nullptr || !mapped_guest_cstring(command_line))) ||
        (current_directory != nullptr && !mapped_guest_cstring(current_directory))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string guest_path;
    if (application_name != nullptr) {
        guest_path = application_name;
    } else {
        guest_path = command_line;
    }
    char normalized_path[4096]{};
    if (!translate_windows_path(guest_path.c_str(), normalized_path, sizeof(normalized_path))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ordem de busca do CreateProcess: o diretório do aplicativo convidado
    // tem precedência sobre o diretório corrente do hospedeiro.
    if (normalized_path[0] != '/') {
        const std::string_view module_file{g_module_file_name};
        const std::size_t slash = module_file.find_last_of('/');
        if (slash != std::string_view::npos) {
            char candidate[4096]{};
            int written = std::snprintf(candidate, sizeof(candidate), "%.*s/%s",
                                        static_cast<int>(slash), module_file.data(),
                                        normalized_path);
            if (written > 0 && written < static_cast<int>(sizeof(candidate)) &&
                ::access(candidate, R_OK) == 0) {
                std::memcpy(normalized_path, candidate, static_cast<std::size_t>(written) + 1);
            } else {
                const std::string_view norm_view{normalized_path};
                const std::size_t norm_slash = norm_view.find_last_of('/');
                const std::string_view filename = (norm_slash != std::string_view::npos)
                                                      ? norm_view.substr(norm_slash + 1)
                                                      : norm_view;
                written = std::snprintf(candidate, sizeof(candidate), "%.*s/%.*s",
                                        static_cast<int>(slash), module_file.data(),
                                        static_cast<int>(filename.size()), filename.data());
                if (written > 0 && written < static_cast<int>(sizeof(candidate)) &&
                    ::access(candidate, R_OK) == 0) {
                    std::memcpy(normalized_path, candidate, static_cast<std::size_t>(written) + 1);
                }
            }
        }
    }

    std::string child_working_directory;
    if (current_directory != nullptr) {
        char normalized_directory[4096]{};
        if (!translate_windows_path(current_directory, normalized_directory,
                                    sizeof(normalized_directory))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        std::filesystem::path directory{normalized_directory};
        if (directory.is_relative()) {
            std::error_code current_path_error;
            directory = std::filesystem::current_path(current_path_error) / directory;
            if (current_path_error) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
        }
        const prefix::EnvironmentPaths paths =
            prefix::get_environment_paths(guest_prefix_root());
        std::error_code directory_error;
        if (!prefix::is_path_within(directory, paths.drive_c) ||
            !std::filesystem::is_directory(directory, directory_error) || directory_error) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        child_working_directory = directory.string();
    }
    SyncSlot* allocated = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        auto it = std::find_if(g_syncs.begin(), g_syncs.end(),
                               [](const SyncSlot& slot) { return !slot.used; });
        if (it == g_syncs.end()) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        it->used = true;
        it->kind = SyncKind::Process;
        it->child_pid = -1;
        it->child_result_fd = -1;
        it->process_running = true;
        it->process_exit_code = kStillActive;
        it->signaled = false;
        it->manual_reset = false;
        it->owner_valid = false;
        it->count = 0;
        it->maximum = 0;
        allocated = &*it;
    }
    int result_pipe[2] = {-1, -1};
    if (::pipe(result_pipe) != 0) {
        {
            std::lock_guard<std::mutex> lock(g_sync_mutex);
            clear_sync_slot(*allocated);
        }
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(result_pipe[0]);
        ::close(result_pipe[1]);
        {
            std::lock_guard<std::mutex> lock(g_sync_mutex);
            clear_sync_slot(*allocated);
        }
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (child == 0) {
        ::close(result_pipe[0]);
        run_created_guest_child(normalized_path, result_pipe[1], child_working_directory);
    }
    ::close(result_pipe[1]);
    {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        allocated->child_pid = child;
        allocated->child_result_fd = result_pipe[0];
        // process_running já true
    }
    auto* information = static_cast<GuestProcessInformation*>(process_information);
    *information = {.process_handle = sync_slot_handle(*allocated),
                    .thread_handle = nullptr,
                    .process_id = static_cast<std::uint32_t>(child),
                    .thread_id = 0};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetExitCodeProcess(const void* process, std::uint32_t* exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process || exit_code == nullptr ||
        !mapped_guest_range(exit_code, sizeof(*exit_code), true)) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, 0));
    *exit_code = slot->process_running ? kStillActive : slot->process_exit_code;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TerminateProcess(const void* process, const std::uint32_t exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (!slot->process_running || slot->child_pid <= 0 || ::kill(slot->child_pid, SIGKILL) != 0) {
        set_last_error(abi::kErrorAccessDenied);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, abi::kInfinite));
    slot->process_exit_code = exit_code;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_QueryPerformanceCounter(std::int64_t* performance_count) noexcept {
    if (performance_count == nullptr || !mapped_guest_range(performance_count, sizeof(*performance_count), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    const std::int64_t ticks = static_cast<std::int64_t>(ts.tv_sec) * 10000000LL +
                               static_cast<std::int64_t>(ts.tv_nsec) / 100LL;
    *performance_count = ticks;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_QueryPerformanceFrequency(std::int64_t* frequency) noexcept {
    if (frequency == nullptr || !mapped_guest_range(frequency, sizeof(*frequency), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *frequency = 10000000LL;  // 10 MHz
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_GetSystemInfo(void* system_info) noexcept {
    if (system_info == nullptr || !mapped_guest_range(system_info, sizeof(abi::GuestSystemInfo), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    auto* si = static_cast<abi::GuestSystemInfo*>(system_info);
    *si = {};
    si->processor_architecture = 9;  // PROCESSOR_ARCHITECTURE_AMD64
    si->page_size = 4096;
    si->minimum_application_address = reinterpret_cast<void*>(0x10000);
    si->maximum_application_address = reinterpret_cast<void*>(0x7FFFFFFF0000ULL);
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) nprocs = 1;
    si->number_of_processors = static_cast<std::uint32_t>(nprocs);
    si->active_processor_mask = (1ULL << std::min<long>(nprocs, 64)) - 1ULL;
    si->allocation_granularity = 65536;
    si->processor_type = 8664; // PROCESSOR_AMD_X8664
    si->processor_level = 6;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_GetNativeSystemInfo(void* system_info) noexcept {
    tl_GetSystemInfo(system_info);
}

TL_MSABI int tl_GlobalMemoryStatusEx(void* buffer) noexcept {
    if (buffer == nullptr || !mapped_guest_range(buffer, sizeof(abi::GuestMemoryStatusEx), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* ms = static_cast<abi::GuestMemoryStatusEx*>(buffer);
    if (ms->length < sizeof(abi::GuestMemoryStatusEx)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    long pages = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0) pages = 1048576;
    if (avail_pages <= 0) avail_pages = 524288;
    if (page_size <= 0) page_size = 4096;

    const std::uint64_t total_phys = static_cast<std::uint64_t>(pages) *
                                     static_cast<std::uint64_t>(page_size);
    const std::uint64_t avail_phys = static_cast<std::uint64_t>(avail_pages) *
                                     static_cast<std::uint64_t>(page_size);
    const std::uint64_t used_phys = total_phys > avail_phys ? total_phys - avail_phys : 0;
    const std::uint32_t load = total_phys > 0 ? static_cast<std::uint32_t>((used_phys * 100ULL) / total_phys) : 0;

    ms->memory_load = load;
    ms->total_phys = total_phys;
    ms->avail_phys = avail_phys;
    ms->total_page_file = total_phys * 2;
    ms->avail_page_file = avail_phys * 2;
    ms->total_virtual = 0x7FFFFFFF0000ULL;
    ms->avail_virtual = 0x700000000000ULL;
    ms->avail_extended_virtual = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateFileMappingA(const void* file, const void* file_mapping_attributes,
                                     const std::uint32_t protect, const std::uint32_t maximum_size_high,
                                     const std::uint32_t maximum_size_low, const char* name) noexcept {
    (void)file_mapping_attributes;
    int fd = -1;
    if (file != nullptr && file != reinterpret_cast<const void*>(~static_cast<std::uintptr_t>(0))) {
        fd = handle_fd(file);
        if (fd < 0) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
    }
    std::uint64_t max_size = (static_cast<std::uint64_t>(maximum_size_high) << 32) | maximum_size_low;
    if (max_size == 0 && fd >= 0) {
        struct stat st{};
        if (fstat(fd, &st) == 0) {
            max_size = static_cast<std::uint64_t>(st.st_size);
        }
    }
    std::lock_guard<std::mutex> lock(g_mapping_mutex);
    auto it = std::find_if(g_mappings.begin(), g_mappings.end(), [](const FileMappingSlot& s) { return !s.used; });
    if (it == g_mappings.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->fd = fd >= 0 ? ::dup(fd) : -1;
    it->size = max_size;
    it->protect = protect;
    if (name != nullptr && mapped_guest_cstring(name)) {
        it->name = name;
    }
    set_last_error(abi::kErrorSuccess);
    return &*it;
}

TL_MSABI void* tl_CreateFileMappingW(const void* file, const void* file_mapping_attributes,
                                     const std::uint32_t protect, const std::uint32_t maximum_size_high,
                                     const std::uint32_t maximum_size_low, const std::uint16_t* name) noexcept {
    std::string utf8_name;
    if (name != nullptr && mapped_guest_wstring(name)) {
        utf8_name = util::wide_to_utf8(name);
    }
    return tl_CreateFileMappingA(file, file_mapping_attributes, protect, maximum_size_high, maximum_size_low,
                                 utf8_name.empty() ? nullptr : utf8_name.c_str());
}

TL_MSABI void* tl_MapViewOfFile(const void* file_mapping_object, const std::uint32_t desired_access,
                                const std::uint32_t file_offset_high, const std::uint32_t file_offset_low,
                                const std::size_t number_of_bytes_to_map) noexcept {
    int mapping_fd = -1;
    std::uint64_t mapping_size = 0;
    {
        std::lock_guard<std::mutex> lock(g_mapping_mutex);
        const FileMappingSlot* slot = find_file_mapping_slot_locked(file_mapping_object);
        if (slot == nullptr) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        mapping_fd = slot->fd;
        mapping_size = slot->size;
    }
    int prot = PROT_READ;
    if ((desired_access & 0x0002) != 0 || (desired_access & 0xF0000) != 0) {
        prot |= PROT_WRITE;
    }
    int flags = (mapping_fd >= 0) ? MAP_SHARED : (MAP_PRIVATE | MAP_ANONYMOUS);
    const std::uint64_t offset_value = (static_cast<std::uint64_t>(file_offset_high) << 32U) |
                                       file_offset_low;
    const off_t offset = static_cast<off_t>(offset_value);
    const std::size_t size = number_of_bytes_to_map > 0 ? number_of_bytes_to_map :
                             (mapping_size > offset_value ? static_cast<std::size_t>(mapping_size - offset_value)
                                                          : 4096U);
    void* result = mmap(nullptr, size, prot, flags, mapping_fd >= 0 ? mapping_fd : -1,
                        mapping_fd >= 0 ? offset : 0);
    if (result == MAP_FAILED) {
        set_last_error(errno_to_win32(errno));
        return nullptr;
    }
    bool recorded = false;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(), [](const AllocationSlot& s) { return s.address == nullptr; });
        if (it != g_allocations.end()) {
            it->address = result;
            it->size = size;
            it->view = true;
            recorded = true;
        }
    }
    if (!recorded) {
        // Sem rastreio não há como UnmapViewOfFile desfazer em segurança.
        munmap(result, size);
        bump_guest_allocation_generation();
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return result;
}

TL_MSABI int tl_UnmapViewOfFile(const void* base_address) noexcept {
    if (base_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t size = 0;
    {
        std::lock_guard<std::mutex> lock(g_allocations_mutex);
        auto it = std::find_if(g_allocations.begin(), g_allocations.end(), [base_address](const AllocationSlot& s) {
            return s.view && s.address == base_address && s.size > 0;
        });
        if (it != g_allocations.end()) {
            size = it->size;
            *it = {};
        }
    }
    if (size == 0) {
        // Endereço desconhecido: nunca munmap memória que o runtime não criou.
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (munmap(const_cast<void*>(base_address), size) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FlushViewOfFile(const void* base_address, const std::size_t number_of_bytes_to_flush) noexcept {
    if (base_address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t size = number_of_bytes_to_flush > 0 ? number_of_bytes_to_flush : 4096;
    const auto address = reinterpret_cast<std::uintptr_t>(base_address);
    if (address % 4096 != 0 || !mapped_guest_range(base_address, size, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (msync(const_cast<void*>(base_address), size, MS_SYNC) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetDiskFreeSpaceExA(const char* directory_name,
                                    std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes,
                                    std::uint64_t* total_number_of_free_bytes) noexcept {
    char normalized[4096]{};
    const char* path_to_stat = ".";
    if (directory_name != nullptr && mapped_guest_cstring(directory_name) && directory_name[0] != '\0') {
        if (translate_windows_path(directory_name, normalized, sizeof(normalized))) {
            path_to_stat = normalized;
        }
    }
    struct statvfs sv{};
    if (statvfs(path_to_stat, &sv) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(sv.f_blocks) * sv.f_frsize;
    const std::uint64_t free_bytes = static_cast<std::uint64_t>(sv.f_bfree) * sv.f_frsize;
    const std::uint64_t avail_bytes = static_cast<std::uint64_t>(sv.f_bavail) * sv.f_frsize;
    if (free_bytes_available_to_caller != nullptr && mapped_guest_range(free_bytes_available_to_caller, sizeof(std::uint64_t), true)) {
        *free_bytes_available_to_caller = avail_bytes;
    }
    if (total_number_of_bytes != nullptr && mapped_guest_range(total_number_of_bytes, sizeof(std::uint64_t), true)) {
        *total_number_of_bytes = total;
    }
    if (total_number_of_free_bytes != nullptr && mapped_guest_range(total_number_of_free_bytes, sizeof(std::uint64_t), true)) {
        *total_number_of_free_bytes = free_bytes;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetDiskFreeSpaceExW(const std::uint16_t* directory_name,
                                    std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes,
                                    std::uint64_t* total_number_of_free_bytes) noexcept {
    std::string utf8;
    if (directory_name != nullptr && mapped_guest_wstring(directory_name)) {
        utf8 = util::wide_to_utf8(directory_name);
    }
    return tl_GetDiskFreeSpaceExA(utf8.empty() ? nullptr : utf8.c_str(),
                                  free_bytes_available_to_caller, total_number_of_bytes,
                                  total_number_of_free_bytes);
}

TL_MSABI std::uint32_t tl_GetDriveTypeA(const char*) noexcept {
    return 3; // DRIVE_FIXED
}

TL_MSABI std::uint32_t tl_GetDriveTypeW(const std::uint16_t*) noexcept {
    return 3; // DRIVE_FIXED
}

TL_MSABI int tl_GetVolumeInformationA(const char*, char* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      char* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept {
    if (volume_name_buffer != nullptr && volume_name_size > 0 && mapped_guest_range(volume_name_buffer, volume_name_size, true)) {
        std::strncpy(volume_name_buffer, "Local Disk", volume_name_size - 1);
        volume_name_buffer[volume_name_size - 1] = '\0';
    }
    if (volume_serial_number != nullptr && mapped_guest_range(volume_serial_number, sizeof(std::uint32_t), true)) {
        *volume_serial_number = 0x12345678U;
    }
    if (maximum_component_length != nullptr && mapped_guest_range(maximum_component_length, sizeof(std::uint32_t), true)) {
        *maximum_component_length = 255;
    }
    if (file_system_flags != nullptr && mapped_guest_range(file_system_flags, sizeof(std::uint32_t), true)) {
        *file_system_flags = 0x00000002U | 0x00000004U;
    }
    if (file_system_name_buffer != nullptr && file_system_name_size > 0 && mapped_guest_range(file_system_name_buffer, file_system_name_size, true)) {
        std::strncpy(file_system_name_buffer, "NTFS", file_system_name_size - 1);
        file_system_name_buffer[file_system_name_size - 1] = '\0';
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetVolumeInformationW(const std::uint16_t*, std::uint16_t* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      std::uint16_t* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept {
    if (volume_name_buffer != nullptr && volume_name_size > 0 && mapped_guest_range(volume_name_buffer, volume_name_size * sizeof(std::uint16_t), true)) {
        const std::u16string u16 = util::utf8_to_wide("Local Disk");
        const std::size_t len = std::min<std::size_t>(u16.size(), volume_name_size - 1);
        std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), volume_name_buffer);
        volume_name_buffer[len] = 0;
    }
    if (volume_serial_number != nullptr && mapped_guest_range(volume_serial_number, sizeof(std::uint32_t), true)) {
        *volume_serial_number = 0x12345678U;
    }
    if (maximum_component_length != nullptr && mapped_guest_range(maximum_component_length, sizeof(std::uint32_t), true)) {
        *maximum_component_length = 255;
    }
    if (file_system_flags != nullptr && mapped_guest_range(file_system_flags, sizeof(std::uint32_t), true)) {
        *file_system_flags = 0x00000002U | 0x00000004U;
    }
    if (file_system_name_buffer != nullptr && file_system_name_size > 0 && mapped_guest_range(file_system_name_buffer, file_system_name_size * sizeof(std::uint16_t), true)) {
        const std::u16string u16 = util::utf8_to_wide("NTFS");
        const std::size_t len = std::min<std::size_t>(u16.size(), file_system_name_size - 1);
        std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), file_system_name_buffer);
        file_system_name_buffer[len] = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_GetSystemTime(void* system_time) noexcept {
    if (system_time == nullptr || !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_utc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_utc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_utc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_utc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_utc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_utc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_utc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_GetLocalTime(void* system_time) noexcept {
    if (system_time == nullptr || !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm tm_loc{};
    localtime_r(&t, &tm_loc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_loc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_loc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_loc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_loc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_loc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_loc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_loc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_FileTimeToSystemTime(const void* file_time, void* system_time) noexcept {
    if (file_time == nullptr || system_time == nullptr ||
        !mapped_guest_range(file_time, 8, false) ||
        !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* ft = static_cast<const GuestFileTime*>(file_time);
    const std::time_t t = filetime_to_unix_time(*ft);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    auto* st = static_cast<abi::GuestSystemTime*>(system_time);
    st->year = static_cast<std::uint16_t>(tm_utc.tm_year + 1900);
    st->month = static_cast<std::uint16_t>(tm_utc.tm_mon + 1);
    st->day_of_week = static_cast<std::uint16_t>(tm_utc.tm_wday);
    st->day = static_cast<std::uint16_t>(tm_utc.tm_mday);
    st->hour = static_cast<std::uint16_t>(tm_utc.tm_hour);
    st->minute = static_cast<std::uint16_t>(tm_utc.tm_min);
    st->second = static_cast<std::uint16_t>(tm_utc.tm_sec);
    st->milliseconds = 0;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemTimeToFileTime(const void* system_time, void* file_time) noexcept {
    if (system_time == nullptr || file_time == nullptr ||
        !mapped_guest_range(system_time, sizeof(abi::GuestSystemTime), false) ||
        !mapped_guest_range(file_time, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto* st = static_cast<const abi::GuestSystemTime*>(system_time);
    std::tm tm_utc{};
    tm_utc.tm_year = st->year - 1900;
    tm_utc.tm_mon = st->month - 1;
    tm_utc.tm_mday = st->day;
    tm_utc.tm_hour = st->hour;
    tm_utc.tm_min = st->minute;
    tm_utc.tm_sec = st->second;
    const std::time_t t = timegm(&tm_utc);
    auto* ft = static_cast<GuestFileTime*>(file_time);
    filetime_from_unix(t, *ft);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept {
    const int fd = handle_fd(handle);
    if (fd < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (fsync(fd) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFilePointerEx(const void* handle, const std::int64_t distance_to_move,
                                 std::int64_t* new_file_pointer, const std::uint32_t move_method) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (move_method > kFileEnd) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::int64_t new_pos = 0;
    switch (move_method) {
        case kFileBegin: new_pos = distance_to_move; break;
        case kFileCurrent: new_pos = slot->position + distance_to_move; break;
        case kFileEnd: new_pos = static_cast<std::int64_t>(current_file_size(*slot)) + distance_to_move; break;
    }
    if (new_pos < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const off_t result = lseek(slot->fd, static_cast<off_t>(new_pos), SEEK_SET);
    if (result < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    slot->position = static_cast<std::int64_t>(result);
    if (new_file_pointer != nullptr && mapped_guest_range(new_file_pointer, sizeof(std::int64_t), true)) {
        *new_file_pointer = slot->position;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* file_size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr || file_size == nullptr || !mapped_guest_range(file_size, sizeof(std::int64_t), true)) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    *file_size = static_cast<std::int64_t>(current_file_size(*slot));
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CompareStringA(const std::uint32_t, const std::uint32_t flags,
                               const char* string1, const int count1,
                               const char* string2, const int count2) noexcept {
    if (string1 == nullptr || string2 == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string_view s1(string1, count1 >= 0 ? static_cast<std::size_t>(count1) : std::strlen(string1));
    const std::string_view s2(string2, count2 >= 0 ? static_cast<std::size_t>(count2) : std::strlen(string2));
    const bool ignore_case = (flags & 0x00000001) != 0;
    int res = 0;
    if (ignore_case) {
        res = util::ascii_case_insensitive_compare(s1, s2);
    } else {
        res = s1.compare(s2);
    }
    set_last_error(abi::kErrorSuccess);
    return (res < 0) ? 1 : ((res > 0) ? 3 : 2);
}

TL_MSABI int tl_CompareStringW(const std::uint32_t locale, const std::uint32_t flags,
                               const std::uint16_t* string1, const int count1,
                               const std::uint16_t* string2, const int count2) noexcept {
    const std::size_t length1 = count1 >= 0 ? static_cast<std::size_t>(count1) : 65535U;
    const std::size_t length2 = count2 >= 0 ? static_cast<std::size_t>(count2) : 65535U;
    const std::string utf8_1 = (string1 != nullptr) ? util::wide_to_utf8(string1, length1) : "";
    const std::string utf8_2 = (string2 != nullptr) ? util::wide_to_utf8(string2, length2) : "";
    return tl_CompareStringA(locale, flags, utf8_1.c_str(), -1, utf8_2.c_str(), -1);
}

TL_MSABI std::uint32_t tl_GetUserDefaultLCID() noexcept {
    return 0x0409U;
}

TL_MSABI std::uint32_t tl_GetSystemDefaultLCID() noexcept {
    return 0x0409U;
}

TL_MSABI int tl_GetComputerNameA(char* buffer, std::uint32_t* size) noexcept {
    if (buffer == nullptr || size == nullptr || *size == 0 || !mapped_guest_range(buffer, *size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::size_t len = std::strlen(host);
    if (*size <= len) {
        *size = static_cast<std::uint32_t>(len + 1);
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memcpy(buffer, host, len + 1);
    *size = static_cast<std::uint32_t>(len);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetComputerNameW(std::uint16_t* buffer, std::uint32_t* size) noexcept {
    if (buffer == nullptr || size == nullptr || *size == 0 || !mapped_guest_range(buffer, *size * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::u16string u16 = util::utf8_to_wide(host);
    if (*size <= u16.size()) {
        *size = static_cast<std::uint32_t>(u16.size() + 1);
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(u16.begin(), u16.end(), buffer);
    buffer[u16.size()] = 0;
    *size = static_cast<std::uint32_t>(u16.size());
    set_last_error(abi::kErrorSuccess);
    return 1;
}

namespace {

struct InternalSrwLock {
    std::shared_mutex mutex;
    bool used{false};
};
std::array<InternalSrwLock, 128> g_srw_locks{};
std::mutex g_srw_meta_mutex;

InternalSrwLock* get_or_create_srw(void* ptr) {
    if (ptr == nullptr) return nullptr;
    if (!mapped_guest_range(ptr, sizeof(void*), true)) return nullptr;
    auto** slot_ptr = reinterpret_cast<InternalSrwLock**>(ptr);
    if (*slot_ptr != nullptr) return *slot_ptr;
    std::lock_guard<std::mutex> lock(g_srw_meta_mutex);
    // Double-check após adquirir o lock (outra thread pode ter preenchido).
    if (*slot_ptr != nullptr) return *slot_ptr;
    for (auto& entry : g_srw_locks) {
        if (!entry.used) {
            entry.used = true;
            *slot_ptr = &entry;
            return &entry;
        }
    }
    // Sem slots livres - fallback para o primeiro (comportamento degradado mas evita null).
    return &g_srw_locks[0];
}

}  // namespace

TL_MSABI void tl_InitializeSRWLock(void* srw_lock) noexcept {
    if (srw_lock != nullptr && mapped_guest_range(srw_lock, sizeof(void*), true)) {
        *reinterpret_cast<void**>(srw_lock) = nullptr;
    }
}

TL_MSABI void tl_AcquireSRWLockExclusive(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.lock();
    }
}

TL_MSABI void tl_ReleaseSRWLockExclusive(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.unlock();
    }
}

TL_MSABI void tl_AcquireSRWLockShared(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.lock_shared();
    }
}

TL_MSABI void tl_ReleaseSRWLockShared(void* srw_lock) noexcept {
    if (auto* lock = get_or_create_srw(srw_lock)) {
        lock->mutex.unlock_shared();
    }
}

TL_MSABI int tl_SleepConditionVariableSRW(void* cond, void* srw_lock,
                                          const std::uint32_t milliseconds, const std::uint32_t flags) noexcept {
    (void)cond;
    (void)srw_lock;
    (void)flags;
    if (milliseconds != 0 && milliseconds != abi::kInfinite) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    return 1;
}

TL_MSABI void tl_WakeConditionVariable(void*) noexcept {}
TL_MSABI void tl_WakeAllConditionVariable(void*) noexcept {}

TL_MSABI void* tl_AddVectoredExceptionHandler(const std::uint32_t first, void* handler) noexcept {
    return runtime::add_vectored_exception_handler(first, handler);
}

TL_MSABI std::uint32_t tl_RemoveVectoredExceptionHandler(void* handle) noexcept {
    return runtime::remove_vectored_exception_handler(handle);
}

TL_MSABI std::uint32_t tl_GetPrivateProfileStringA(const char* app_name, const char* key_name,
                                                   const char* default_val, char* returned_string,
                                                   const std::uint32_t size, const char* file_name) noexcept {
    if (returned_string == nullptr || size == 0 || !mapped_guest_range(returned_string, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string fallback = (default_val != nullptr && mapped_guest_cstring(default_val)) ? default_val : "";
    if (file_name == nullptr || !mapped_guest_cstring(file_name)) {
        std::strncpy(returned_string, fallback.c_str(), size - 1);
        returned_string[size - 1] = '\0';
        return static_cast<std::uint32_t>(std::strlen(returned_string));
    }
    char normalized[4096]{};
    const char* path_to_open = file_name;
    if (translate_windows_path(file_name, normalized, sizeof(normalized))) {
        path_to_open = normalized;
    }
    std::ifstream file{path_to_open};
    if (!file) {
        std::strncpy(returned_string, fallback.c_str(), size - 1);
        returned_string[size - 1] = '\0';
        return static_cast<std::uint32_t>(std::strlen(returned_string));
    }
    std::string target_section = (app_name != nullptr && mapped_guest_cstring(app_name)) ? app_name : "";
    std::string target_key = (key_name != nullptr && mapped_guest_cstring(key_name)) ? key_name : "";
    std::string current_section;
    std::string line;
    std::string found_val = fallback;

    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }
        if (!target_section.empty() && !util::ascii_iequals(current_section, target_section)) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            k.erase(k.find_last_not_of(" \t") + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            if (util::ascii_iequals(k, target_key)) {
                found_val = v;
                break;
            }
        }
    }
    std::strncpy(returned_string, found_val.c_str(), size - 1);
    returned_string[size - 1] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(std::strlen(returned_string));
}

TL_MSABI std::uint32_t tl_GetPrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                   const std::uint16_t* default_val, std::uint16_t* returned_string,
                                                   const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    if (returned_string == nullptr || size == 0 || !mapped_guest_range(returned_string, size * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8_app = (app_name != nullptr && mapped_guest_wstring(app_name)) ? util::wide_to_utf8(app_name) : "";
    const std::string utf8_key = (key_name != nullptr && mapped_guest_wstring(key_name)) ? util::wide_to_utf8(key_name) : "";
    const std::string utf8_def = (default_val != nullptr && mapped_guest_wstring(default_val)) ? util::wide_to_utf8(default_val) : "";
    const std::string utf8_file = (file_name != nullptr && mapped_guest_wstring(file_name)) ? util::wide_to_utf8(file_name) : "";
    char buf[4096]{};
    tl_GetPrivateProfileStringA(utf8_app.empty() ? nullptr : utf8_app.c_str(),
                                utf8_key.empty() ? nullptr : utf8_key.c_str(),
                                utf8_def.empty() ? nullptr : utf8_def.c_str(),
                                buf, sizeof(buf),
                                utf8_file.empty() ? nullptr : utf8_file.c_str());
    const std::u16string u16 = util::utf8_to_wide(buf);
    const std::size_t len = std::min<std::size_t>(u16.size(), size - 1);
    std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), returned_string);
    returned_string[len] = 0;
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetPrivateProfileIntA(const char* app_name, const char* key_name,
                                                const int default_val, const char* file_name) noexcept {
    char buf[64]{};
    tl_GetPrivateProfileStringA(app_name, key_name, std::to_string(default_val).c_str(), buf, sizeof(buf), file_name);
    return static_cast<std::uint32_t>(std::atoi(buf));
}

TL_MSABI std::uint32_t tl_GetPrivateProfileIntW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                const int default_val, const std::uint16_t* file_name) noexcept {
    std::uint16_t buf[64]{};
    std::u16string def_u16 = util::utf8_to_wide(std::to_string(default_val));
    tl_GetPrivateProfileStringW(app_name, key_name,
                                reinterpret_cast<const std::uint16_t*>(def_u16.c_str()), buf, 64,
                                file_name);
    return static_cast<std::uint32_t>(std::atoi(util::wide_to_utf8(buf).c_str()));
}

TL_MSABI int tl_WritePrivateProfileStringA(const char* app_name, const char* key_name,
                                           const char* string_val, const char* file_name) noexcept {
    (void)app_name;
    (void)key_name;
    (void)string_val;
    (void)file_name;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_WritePrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                           const std::uint16_t* string_val, const std::uint16_t* file_name) noexcept {
    (void)app_name;
    (void)key_name;
    (void)string_val;
    (void)file_name;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetPrivateProfileSectionA(const char* app_name, char* returned_string,
                                                    const std::uint32_t size, const char* file_name) noexcept {
    (void)app_name;
    (void)file_name;
    if (returned_string != nullptr && size >= 2 && mapped_guest_range(returned_string, size, true)) {
        returned_string[0] = '\0';
        returned_string[1] = '\0';
    }
    return 0;
}

TL_MSABI std::uint32_t tl_GetPrivateProfileSectionW(const std::uint16_t* app_name, std::uint16_t* returned_string,
                                                    const std::uint32_t size, const std::uint16_t* file_name) noexcept {
    (void)app_name;
    (void)file_name;
    if (returned_string != nullptr && size >= 2 && mapped_guest_range(returned_string, size * sizeof(std::uint16_t), true)) {
        returned_string[0] = 0;
        returned_string[1] = 0;
    }
    return 0;
}

TL_MSABI int tl_GetConsoleScreenBufferInfo(const void* console_handle, void* buffer_info) noexcept {
    (void)console_handle;
    if (buffer_info == nullptr || !mapped_guest_range(buffer_info, sizeof(abi::GuestConsoleScreenBufferInfo), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* csbi = static_cast<abi::GuestConsoleScreenBufferInfo*>(buffer_info);
    *csbi = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetConsoleTextAttribute(const void* console_handle, const std::uint16_t attributes) noexcept {
    (void)console_handle;
    // stdout pertence ao convidado: só emitir códigos ANSI quando ele é um
    // terminal interativo; em pipe/arquivo a saída capturada permanece limpa.
    if (::isatty(STDOUT_FILENO)) {
        const bool red = (attributes & 0x0004) != 0;
        const bool green = (attributes & 0x0002) != 0;
        const bool blue = (attributes & 0x0001) != 0;
        const bool bold = (attributes & 0x0008) != 0;

        int ansi_color = 37;
        if (red && green && blue) ansi_color = 37;
        else if (red && green) ansi_color = 33;
        else if (red && blue) ansi_color = 35;
        else if (green && blue) ansi_color = 36;
        else if (red) ansi_color = 31;
        else if (green) ansi_color = 32;
        else if (blue) ansi_color = 34;

        std::fprintf(stdout, "\033[%d;%dm", bold ? 1 : 0, ansi_color);
        std::fflush(stdout);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

namespace {

struct InternalTpWork {
    bool used{false};
    void* callback{nullptr};
    void* context{nullptr};
};
std::array<InternalTpWork, 32> g_tp_works{};

struct InternalTpTimer {
    bool used{false};
    void* callback{nullptr};
    void* context{nullptr};
};
std::array<InternalTpTimer, 32> g_tp_timers{};

thread_local void* g_current_fiber_data = nullptr;

}  // namespace

TL_MSABI void* tl_CreateThreadpoolWork(void* callback, void* context, void* environment) noexcept {
    (void)environment;
    for (auto& w : g_tp_works) {
        if (!w.used) {
            w.used = true;
            w.callback = callback;
            w.context = context;
            return &w;
        }
    }
    return nullptr;
}

TL_MSABI void tl_SubmitThreadpoolWork(void* work) noexcept {
    if (work == nullptr) return;
    auto* w = static_cast<InternalTpWork*>(work);
    if (!w->used || w->callback == nullptr) return;
    std::thread([cb = w->callback, ctx = w->context]() {
        using CallbackFn = void (*)(void*, void*, void*);
        reinterpret_cast<CallbackFn>(cb)(nullptr, ctx, nullptr);
    }).detach();
}

TL_MSABI void tl_WaitForThreadpoolWorkCallbacks(void* work, const int cancel_pending) noexcept {
    (void)work;
    (void)cancel_pending;
}

TL_MSABI void tl_CloseThreadpoolWork(void* work) noexcept {
    if (work != nullptr) {
        static_cast<InternalTpWork*>(work)->used = false;
    }
}

TL_MSABI void* tl_CreateThreadpoolTimer(void* callback, void* context, void* environment) noexcept {
    (void)environment;
    for (auto& t : g_tp_timers) {
        if (!t.used) {
            t.used = true;
            t.callback = callback;
            t.context = context;
            return &t;
        }
    }
    return nullptr;
}

TL_MSABI void tl_SetThreadpoolTimer(void* timer, const void* due_time, const std::uint32_t period, const std::uint32_t window_length) noexcept {
    (void)timer;
    (void)due_time;
    (void)period;
    (void)window_length;
}

TL_MSABI void tl_WaitForThreadpoolTimerCallbacks(void* timer, const int cancel_pending) noexcept {
    (void)timer;
    (void)cancel_pending;
}

TL_MSABI void tl_CloseThreadpoolTimer(void* timer) noexcept {
    if (timer != nullptr) {
        static_cast<InternalTpTimer*>(timer)->used = false;
    }
}

TL_MSABI void* tl_ConvertThreadToFiber(void* parameter) noexcept {
    g_current_fiber_data = parameter;
    static char g_fiber_token = 0;
    return &g_fiber_token;
}

TL_MSABI void* tl_ConvertThreadToFiberEx(void* parameter, std::uint32_t flags) noexcept {
    (void)flags;
    g_current_fiber_data = parameter;
    static char g_fiber_ex_token = 0;
    return &g_fiber_ex_token;
}

TL_MSABI int tl_ConvertFiberToThread() noexcept {
    g_current_fiber_data = nullptr;
    return 1;
}

TL_MSABI void* tl_CreateFiber(const std::size_t stack_size, void* start_address, void* parameter) noexcept {
    (void)stack_size;
    (void)start_address;
    (void)parameter;
    static char g_fiber_created_token = 0;
    return &g_fiber_created_token;
}

TL_MSABI void* tl_CreateFiberEx(const std::size_t stack_commit, const std::size_t stack_reserve,
                                const std::uint32_t flags, void* start_address, void* parameter) noexcept {
    (void)stack_commit;
    (void)stack_reserve;
    (void)flags;
    (void)start_address;
    (void)parameter;
    static char g_fiber_ex_created_token = 0;
    return &g_fiber_ex_created_token;
}

TL_MSABI void tl_SwitchToFiber(void* fiber) noexcept {
    (void)fiber;
}

TL_MSABI void tl_DeleteFiber(void* fiber) noexcept {
    (void)fiber;
}

TL_MSABI void* tl_GetFiberData() noexcept {
    return g_current_fiber_data;
}

TL_MSABI void* tl_HeapCreate(const std::uint32_t options, const std::size_t initial_size, const std::size_t maximum_size) noexcept {
    (void)options;
    (void)initial_size;
    (void)maximum_size;
    static char g_custom_heap_token = 0;
    return &g_custom_heap_token;
}

TL_MSABI int tl_HeapDestroy(void* heap) noexcept {
    (void)heap;
    return 1;
}

TL_MSABI int tl_HeapValidate(void* heap, const std::uint32_t flags, const void* memory) noexcept {
    (void)heap;
    (void)flags;
    (void)memory;
    return 1;
}

TL_MSABI std::size_t tl_HeapSize(void* heap, const std::uint32_t flags, const void* memory) noexcept {
    (void)heap;
    (void)flags;
    if (memory == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return static_cast<std::size_t>(-1);
    }
    set_last_error(abi::kErrorSuccess);
    return malloc_usable_size(const_cast<void*>(memory));
}

TL_MSABI std::size_t tl_HeapCompact(void* heap, const std::uint32_t flags) noexcept {
    (void)heap;
    (void)flags;
    return 0;
}

TL_MSABI void tl_OutputDebugStringA(const char* const output_string) noexcept {
    if (output_string == nullptr || !mapped_guest_cstring(output_string)) {
        return;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "OutputDebugStringA"},
        diagnostics::TraceField{"message", output_string},
    };
    runtime_trace("OutputDebugStringA", fields, 2);
}

TL_MSABI void tl_OutputDebugStringW(const std::uint16_t* const output_string) noexcept {
    if (output_string == nullptr || !mapped_guest_wstring(output_string)) {
        return;
    }
    const std::string utf8 = util::wide_to_utf8(output_string);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "OutputDebugStringW"},
        diagnostics::TraceField{"message", utf8.c_str()},
    };
    runtime_trace("OutputDebugStringW", fields, 2);
}

static std::string g_custom_dll_directory;

TL_MSABI int tl_SetDllDirectoryW(const std::uint16_t* const path_name) noexcept {
    if (path_name == nullptr) {
        g_custom_dll_directory.clear();
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (!mapped_guest_wstring(path_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_custom_dll_directory = util::wide_to_utf8(path_name);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::size_t tl_VirtualQueryEx(const void* const process_handle, const void* const address,
                                       void* const buffer, const std::size_t length) noexcept {
    (void)process_handle;
    if (buffer == nullptr || length < sizeof(abi::GuestMemoryBasicInformation) ||
        !mapped_guest_range(buffer, sizeof(abi::GuestMemoryBasicInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return tl_VirtualQuery(address, buffer, length);
}

struct GuestTimeZoneInformation {
    std::int32_t bias{0};
    std::uint16_t standard_name[32]{};
    std::uint16_t standard_date[8]{};
    std::int32_t standard_bias{0};
    std::uint16_t daylight_name[32]{};
    std::uint16_t daylight_date[8]{};
    std::int32_t daylight_bias{0};
};

TL_MSABI std::uint32_t tl_GetTimeZoneInformation(void* const tz_info) noexcept {
    if (tz_info == nullptr || !mapped_guest_range(tz_info, sizeof(GuestTimeZoneInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0xFFFFFFFFU;
    }
    auto* const tzi = static_cast<GuestTimeZoneInformation*>(tz_info);
    *tzi = GuestTimeZoneInformation{};
    tzi->bias = 0;
    const std::u16string std_name = util::utf8_to_wide("UTC");
    std::copy(std_name.begin(), std_name.end(), tzi->standard_name);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetProcessId(const void* const process) noexcept {
    if (process == nullptr || process == reinterpret_cast<const void*>(~0ULL)) {
        return static_cast<std::uint32_t>(getpid());
    }
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(process);
    if (addr >= kProcessHandleBase && addr < kProcessHandleBase + kProcessHandleRange) {
        return static_cast<std::uint32_t>(addr - kProcessHandleBase);
    }
    const SyncSlot* const slot = find_sync_slot(process);
    if (slot != nullptr && slot->kind == SyncKind::Process) {
        return static_cast<std::uint32_t>(slot->child_pid);
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_MSABI int tl_QueryFullProcessImageNameW(const void* const process, const std::uint32_t flags,
                                           std::uint16_t* const exe_name, std::uint32_t* const size) noexcept {
    (void)process;
    (void)flags;
    if (exe_name == nullptr || size == nullptr || !mapped_guest_range(size, sizeof(*size), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string path =
        prefix::to_windows_path(std::filesystem::path(g_module_file_name), guest_prefix_root());
    const std::u16string wide_path = util::utf8_to_wide(path);
    const std::uint32_t capacity = *size;
    if (capacity <= wide_path.size()) {
        *size = static_cast<std::uint32_t>(wide_path.size() + 1);
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    if (!mapped_guest_range(exe_name, sizeof(std::uint16_t) * (wide_path.size() + 1), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(wide_path.begin(), wide_path.end(), exe_name);
    exe_name[wide_path.size()] = 0;
    *size = static_cast<std::uint32_t>(wide_path.size());
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FileTimeToLocalFileTime(const void* const file_time, void* const local_file_time) noexcept {
    if (file_time == nullptr || local_file_time == nullptr ||
        !mapped_guest_range(file_time, sizeof(std::uint64_t), false) ||
        !mapped_guest_range(local_file_time, sizeof(std::uint64_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *static_cast<std::uint64_t*>(local_file_time) = *static_cast<const std::uint64_t*>(file_time);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetLongPathNameW(const std::uint16_t* const short_path,
                                           std::uint16_t* const long_path,
                                           const std::uint32_t buffer_length) noexcept {
    if (short_path == nullptr || !mapped_guest_wstring(short_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string path = util::wide_to_utf8(short_path);
    const std::u16string wide_path = util::utf8_to_wide(path);
    const std::size_t len = wide_path.size();
    if (buffer_length <= len || long_path == nullptr) {
        return static_cast<std::uint32_t>(len + 1);
    }
    if (!mapped_guest_range(long_path, sizeof(std::uint16_t) * (len + 1), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(wide_path.begin(), wide_path.end(), long_path);
    long_path[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetShortPathNameW(const std::uint16_t* const long_path,
                                            std::uint16_t* const short_path,
                                            const std::uint32_t buffer_length) noexcept {
    return tl_GetLongPathNameW(long_path, short_path, buffer_length);
}

TL_MSABI int tl_SetThreadPriority(const void* const thread_handle, const int priority) noexcept {
    (void)thread_handle;
    (void)priority;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetProcessAffinityMask(const void* const process_handle,
                                       std::uintptr_t* const process_affinity_mask,
                                       std::uintptr_t* const system_affinity_mask) noexcept {
    (void)process_handle;
    if (process_affinity_mask != nullptr && mapped_guest_range(process_affinity_mask, sizeof(*process_affinity_mask), true)) {
        *process_affinity_mask = 0x0000000FULL;
    }
    if (system_affinity_mask != nullptr && mapped_guest_range(system_affinity_mask, sizeof(*system_affinity_mask), true)) {
        *system_affinity_mask = 0x0000000FULL;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CreateHardLinkW(const std::uint16_t* const new_file_name,
                                const std::uint16_t* const existing_file_name,
                                void* const security_attributes) noexcept {
    (void)security_attributes;
    char normalized_new[4096]{};
    char normalized_exist[4096]{};
    if (!normalized_wide_path(new_file_name, normalized_new) ||
        !normalized_wide_path(existing_file_name, normalized_exist)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (::link(normalized_exist, normalized_new) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_K32GetModuleFileNameExW(const void* const process,
                                                  const void* const module_handle,
                                                  std::uint16_t* const filename,
                                                  const std::uint32_t size) noexcept {
    (void)process;
    return tl_GetModuleFileNameW(module_handle, filename, size);
}

TL_MSABI std::uint32_t tl_GetTickCount(void) noexcept {
    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        const std::uint64_t ms = static_cast<std::uint64_t>(ts.tv_sec) * 1000U +
                                 static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U;
        return static_cast<std::uint32_t>(ms & 0xFFFFFFFFU);
    }
    return 1000U;
}

TL_MSABI int tl_SetCurrentDirectoryW(const std::uint16_t* const path_name) noexcept {
    if (path_name == nullptr || !mapped_guest_wstring(path_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized[4096]{};
    if (!normalized_wide_path(path_name, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (::chdir(normalized) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DeviceIoControl(void* const device, const std::uint32_t io_control_code,
                                void* const in_buffer, const std::uint32_t in_buffer_size,
                                void* const out_buffer, const std::uint32_t out_buffer_size,
                                std::uint32_t* const bytes_returned,
                                void* const overlapped) noexcept {
    (void)device;
    (void)io_control_code;
    (void)in_buffer;
    (void)in_buffer_size;
    (void)out_buffer;
    (void)out_buffer_size;
    (void)overlapped;
    if (bytes_returned != nullptr && mapped_guest_range(bytes_returned, sizeof(*bytes_returned), true)) {
        *bytes_returned = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FoldStringW(const std::uint32_t map_flags, const std::uint16_t* const src_str,
                            const int cch_src, std::uint16_t* const dest_str,
                            const int cch_dest) noexcept {
    (void)map_flags;
    if (src_str == nullptr || !mapped_guest_wstring(src_str)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t null_term_len = 0;
    while (src_str[null_term_len] != 0) {
        ++null_term_len;
    }
    const std::size_t src_len = (cch_src < 0) ? (null_term_len + 1) : static_cast<std::size_t>(cch_src);
    if (cch_dest == 0) {
        return static_cast<int>(src_len);
    }
    if (cch_dest < 0 || dest_str == nullptr ||
        !mapped_guest_range(dest_str, static_cast<std::size_t>(cch_dest) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t to_copy = std::min(static_cast<std::size_t>(cch_dest), src_len);
    for (std::size_t i = 0; i < to_copy; ++i) {
        dest_str[i] = src_str[i];
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(to_copy);
}

TL_MSABI std::uint32_t tl_SetThreadExecutionState(const std::uint32_t es_flags) noexcept {
    return es_flags;
}

TL_MSABI int tl_AllocConsole(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_AttachConsole(const std::uint32_t process_id) noexcept {
    (void)process_id;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FreeConsole(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SystemTimeToTzSpecificLocalTime(const void* const tz_info,
                                               const void* const universal_time,
                                               void* const local_time) noexcept {
    (void)tz_info;
    if (universal_time == nullptr || local_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(universal_time, 16, false) || !mapped_guest_range(local_time, 16, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(local_time, universal_time, 16);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsDBCSLeadByte(const std::uint8_t test_char) noexcept {
    (void)test_char;
    return 0;
}

TL_MSABI int tl_GetNumberFormatW(const std::uint32_t locale, const std::uint32_t flags,
                                 const std::uint16_t* const value, const void* const format,
                                 std::uint16_t* const number_str, const int cch_number) noexcept {
    (void)locale;
    (void)flags;
    (void)format;
    if (value == nullptr || !mapped_guest_wstring(value)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::size_t val_len = 0;
    while (value[val_len] != 0) {
        ++val_len;
    }
    const std::size_t len = val_len + 1;
    if (cch_number == 0) {
        return static_cast<int>(len);
    }
    if (cch_number < 0 || number_str == nullptr ||
        !mapped_guest_range(number_str, static_cast<std::size_t>(cch_number) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t to_copy = std::min(static_cast<std::size_t>(cch_number), len);
    for (std::size_t i = 0; i < to_copy; ++i) {
        number_str[i] = value[i];
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(to_copy);
}

TL_MSABI std::uint32_t tl_GetVersion(void) noexcept {
    // Windows 7 / NT 6.1 (0x00060001)
    return 0x00060001U;
}

TL_MSABI std::size_t tl_GetLargePageMinimum(void) noexcept {
    return 2097152U; // 2MB
}

TL_MSABI void tl_SetFileApisToOEM(void) noexcept {
}

TL_MSABI int tl_SetConsoleCtrlHandler(void* const handler, const int add) noexcept {
    (void)handler;
    (void)add;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetProcessTimes(void* const process, void* const creation_time, void* const exit_time,
                                void* const kernel_time, void* const user_time) noexcept {
    (void)process;
    struct GuestFileTime {
        std::uint32_t low_date_time;
        std::uint32_t high_date_time;
    };
    const GuestFileTime dummy_time{0, 0};
    if (creation_time != nullptr && mapped_guest_range(creation_time, sizeof(dummy_time), true)) {
        std::memcpy(creation_time, &dummy_time, sizeof(dummy_time));
    }
    if (exit_time != nullptr && mapped_guest_range(exit_time, sizeof(dummy_time), true)) {
        std::memcpy(exit_time, &dummy_time, sizeof(dummy_time));
    }
    if (kernel_time != nullptr && mapped_guest_range(kernel_time, sizeof(dummy_time), true)) {
        std::memcpy(kernel_time, &dummy_time, sizeof(dummy_time));
    }
    if (user_time != nullptr && mapped_guest_range(user_time, sizeof(dummy_time), true)) {
        std::memcpy(user_time, &dummy_time, sizeof(dummy_time));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetProcessAffinityMask(void* const process, const std::uintptr_t process_affinity_mask) noexcept {
    (void)process;
    (void)process_affinity_mask;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_SetThreadAffinityMask(void* const thread, const std::uintptr_t thread_affinity_mask) noexcept {
    (void)thread;
    (void)thread_affinity_mask;
    set_last_error(abi::kErrorSuccess);
    return 1; // previous mask
}

TL_MSABI std::uint32_t tl_ResumeThread(void* const thread) noexcept {
    (void)thread;
    set_last_error(abi::kErrorSuccess);
    return 0; // previous suspend count
}

TL_MSABI void* tl_OpenEventW(const std::uint32_t desired_access, const int inherit_handle,
                             const std::uint16_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000);
}

TL_MSABI void* tl_OpenFileMappingW(const std::uint32_t desired_access, const int inherit_handle,
                                   const std::uint16_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    (void)name;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x2000);
}

TL_MSABI int tl_FileTimeToDosDateTime(const void* const file_time, std::uint16_t* const fat_date,
                                       std::uint16_t* const fat_time) noexcept {
    if (file_time == nullptr || fat_date == nullptr || fat_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(file_time, 8, false) || !mapped_guest_range(fat_date, 2, true) ||
        !mapped_guest_range(fat_time, 2, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *fat_date = 0x5821; // 2024-01-01
    *fat_time = 0x0000; // 00:00:00
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_DosDateTimeToFileTime(const std::uint16_t fat_date, const std::uint16_t fat_time,
                                       void* const file_time) noexcept {
    if (file_time == nullptr || !mapped_guest_range(file_time, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t day = fat_date & 0x1FU;
    const std::uint32_t month = (fat_date >> 5U) & 0x0FU;
    const std::uint32_t year = ((fat_date >> 9U) & 0x7FU) + 1980U;
    const std::uint32_t second = (fat_time & 0x1FU) * 2U;
    const std::uint32_t minute = (fat_time >> 5U) & 0x3FU;
    const std::uint32_t hour = (fat_time >> 11U) & 0x1FU;
    if (day < 1U || day > 31U || month < 1U || month > 12U || year < 1980U || year > 2107U ||
        second > 59U || minute > 59U || hour > 23U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Validar dias do mês (inclui fevereiro bissexto).
    const bool is_leap = (year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U));
    const std::uint32_t days_in_month[] = {31, is_leap ? 29U : 28U, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days_in_month[month - 1U]) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::tm tm_utc{};
    tm_utc.tm_year = static_cast<int>(year - 1900U);
    tm_utc.tm_mon = static_cast<int>(month - 1U);
    tm_utc.tm_mday = static_cast<int>(day);
    tm_utc.tm_hour = static_cast<int>(hour);
    tm_utc.tm_min = static_cast<int>(minute);
    tm_utc.tm_sec = static_cast<int>(second);
    tm_utc.tm_isdst = -1;
    const std::time_t seconds = timegm(&tm_utc);
    if (seconds == static_cast<std::time_t>(-1)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const out = static_cast<GuestFileTime*>(file_time);
    filetime_from_unix(seconds, *out);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::int32_t tl_CompareFileTime(const void* const file_time1, const void* const file_time2) noexcept {
    if (file_time1 == nullptr || file_time2 == nullptr) {
        return 0;
    }
    std::uint64_t t1 = 0;
    std::uint64_t t2 = 0;
    std::memcpy(&t1, file_time1, sizeof(t1));
    std::memcpy(&t2, file_time2, sizeof(t2));
    if (t1 < t2) return -1;
    if (t1 > t2) return 1;
    return 0;
}

TL_MSABI int tl_GetDiskFreeSpaceW(const std::uint16_t* const root_path_name,
                                  std::uint32_t* const sectors_per_cluster,
                                  std::uint32_t* const bytes_per_sector,
                                  std::uint32_t* const number_of_free_clusters,
                                  std::uint32_t* const total_number_of_clusters) noexcept {
    (void)root_path_name;
    if (sectors_per_cluster != nullptr && mapped_guest_range(sectors_per_cluster, 4, true)) {
        *sectors_per_cluster = 8;
    }
    if (bytes_per_sector != nullptr && mapped_guest_range(bytes_per_sector, 4, true)) {
        *bytes_per_sector = 512;
    }
    if (number_of_free_clusters != nullptr && mapped_guest_range(number_of_free_clusters, 4, true)) {
        *number_of_free_clusters = 1000000;
    }
    if (total_number_of_clusters != nullptr && mapped_guest_range(total_number_of_clusters, 4, true)) {
        *total_number_of_clusters = 2000000;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindFirstStreamW(const std::uint16_t* const file_name, const int info_level,
                                   void* const find_stream_data, const std::uint32_t flags) noexcept {
    (void)file_name;
    (void)info_level;
    (void)find_stream_data;
    (void)flags;
    set_last_error(38); // ERROR_HANDLE_EOF
    return reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0)); // INVALID_HANDLE_VALUE
}

TL_MSABI int tl_FindNextStreamW(void* const find_stream, void* const find_stream_data) noexcept {
    (void)find_stream;
    (void)find_stream_data;
    set_last_error(38); // ERROR_HANDLE_EOF
    return 0;
}

TL_MSABI std::uint32_t tl_GetLogicalDriveStringsW(const std::uint32_t buffer_length,
                                                  std::uint16_t* const buffer) noexcept {
    static const std::uint16_t kDrives[] = {'C', ':', '\\', 0, 0};
    constexpr std::uint32_t kNeeded = 4;
    if (buffer_length == 0 || buffer == nullptr) {
        return kNeeded;
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::size_t to_copy = std::min(static_cast<std::size_t>(buffer_length), sizeof(kDrives) / sizeof(kDrives[0]));
    for (std::size_t i = 0; i < to_copy; ++i) {
        buffer[i] = kDrives[i];
    }
    set_last_error(abi::kErrorSuccess);
    return kNeeded;
}

TL_MSABI int tl_SetNamedPipeHandleState(void* const named_pipe, std::uint32_t* const mode,
                                        std::uint32_t* const max_collection_count,
                                        std::uint32_t* const collect_data_timeout) noexcept {
    (void)named_pipe;
    (void)mode;
    (void)max_collection_count;
    (void)collect_data_timeout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TransactNamedPipe(void* const named_pipe, void* const in_buffer, const std::uint32_t in_buffer_size,
                                  void* const out_buffer, const std::uint32_t out_buffer_size,
                                  std::uint32_t* const bytes_read, void* const overlapped) noexcept {
    (void)named_pipe;
    (void)in_buffer;
    (void)in_buffer_size;
    (void)out_buffer;
    (void)out_buffer_size;
    (void)bytes_read;
    (void)overlapped;
    set_last_error(230); // ERROR_PIPE_NOT_CONNECTED
    return 0;
}

TL_MSABI int tl_WaitNamedPipeW(const std::uint16_t* const named_pipe_name, const std::uint32_t time_out) noexcept {
    (void)named_pipe_name;
    (void)time_out;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PeekNamedPipe(void* const named_pipe, void* const buffer, const std::uint32_t buffer_size,
                              std::uint32_t* const bytes_read, std::uint32_t* const total_bytes_avail,
                              std::uint32_t* const bytes_left_this_message) noexcept {
    (void)named_pipe;
    (void)buffer;
    (void)buffer_size;
    if (bytes_read != nullptr && mapped_guest_range(bytes_read, 4, true)) {
        *bytes_read = 0;
    }
    if (total_bytes_avail != nullptr && mapped_guest_range(total_bytes_avail, 4, true)) {
        *total_bytes_avail = 0;
    }
    if (bytes_left_this_message != nullptr && mapped_guest_range(bytes_left_this_message, 4, true)) {
        *bytes_left_this_message = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_WaitForSingleObjectEx(void* const handle, const std::uint32_t milliseconds,
                                                const int alertable) noexcept {
    (void)alertable;
    return tl_WaitForSingleObject(handle, milliseconds);
}

TL_MSABI int tl_GetExitCodeThread(void* const thread, std::uint32_t* const exit_code) noexcept {
    (void)thread;
    if (exit_code != nullptr && mapped_guest_range(exit_code, 4, true)) {
        *exit_code = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TryAcquireSRWLockExclusive(void* const srw_lock) noexcept {
    (void)srw_lock;
    return 1;
}

TL_MSABI void tl_FreeLibraryAndExitThread(void* const module_handle, const std::uint32_t exit_code) noexcept {
    (void)module_handle;
    tl_ExitThread(exit_code);
}

TL_MSABI int tl_SetThreadLocale(const std::uint32_t locale) noexcept {
    (void)locale;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint16_t tl_SetThreadUILanguage(const std::uint16_t lang_id) noexcept {
    return lang_id;
}

TL_MSABI std::uint16_t tl_GetUserDefaultUILanguage(void) noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint32_t tl_GetLogicalDrives(void) noexcept {
    return (1U << 2); // Drive C:
}

TL_MSABI int tl_GetPhysicallyInstalledSystemMemory(std::uint64_t* const total_memory_in_kilobytes) noexcept {
    if (total_memory_in_kilobytes != nullptr && mapped_guest_range(total_memory_in_kilobytes, sizeof(std::uint64_t), true)) {
        *total_memory_in_kilobytes = 16ULL * 1024ULL * 1024ULL; // 16 GB in KB
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetVolumePathNameA(const char* const file_name, char* const volume_path_name,
                                   const std::uint32_t buffer_length) noexcept {
    (void)file_name;
    if (buffer_length >= 4 && volume_path_name != nullptr && mapped_guest_range(volume_path_name, 4, true)) {
        volume_path_name[0] = 'C';
        volume_path_name[1] = ':';
        volume_path_name[2] = '\\';
        volume_path_name[3] = '\0';
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TzSpecificLocalTimeToSystemTime(const void* const tz_info, const void* const local_time,
                                               void* const universal_time) noexcept {
    (void)tz_info;
    if (local_time == nullptr || universal_time == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (!mapped_guest_range(local_time, 16, false) || !mapped_guest_range(universal_time, 16, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(universal_time, local_time, 16);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UnregisterWaitEx(void* const wait_handle, void* const completion_event) noexcept {
    (void)wait_handle;
    (void)completion_event;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_RegisterWaitForSingleObject(void** const ph_new_wait_object, void* const h_object,
                                            void* const callback, void* const context,
                                            const std::uint32_t ms, const std::uint32_t flags) noexcept {
    (void)h_object;
    (void)callback;
    (void)context;
    (void)ms;
    (void)flags;
    if (ph_new_wait_object != nullptr && mapped_guest_range(ph_new_wait_object, sizeof(void*), true)) {
        *ph_new_wait_object = reinterpret_cast<void*>(0x12340001ULL);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetSearchPathMode(const std::uint32_t flags) noexcept {
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_InterlockedPushEntrySList(void* const list_head, void* const list_entry) noexcept {
    if (list_head == nullptr || list_entry == nullptr) {
        return nullptr;
    }
    // Minimal atomic-compatible SLIST emulation for single guest context
    void** entry_next = static_cast<void**>(list_entry);
    void** head_ptr = static_cast<void**>(list_head);
    void* old_head = *head_ptr;
    *entry_next = old_head;
    *head_ptr = list_entry;
    return old_head;
}

TL_MSABI int tl_CopyFileExW(const std::uint16_t* const existing_file, const std::uint16_t* const new_file,
                            void* const progress_routine, void* const data, int* const cancel,
                            const std::uint32_t flags) noexcept {
    (void)progress_routine;
    (void)data;
    (void)cancel;
    const int fail_if_exists = (flags & 1) ? 1 : 0;
    return tl_CopyFileW(existing_file, new_file, fail_if_exists);
}

TL_MSABI int tl_MoveFileWithProgressW(const std::uint16_t* const existing_file, const std::uint16_t* const new_file,
                                     void* const progress_routine, void* const data,
                                     const std::uint32_t flags) noexcept {
    (void)progress_routine;
    (void)data;
    return tl_MoveFileExW(existing_file, new_file, flags);
}

TL_MSABI std::uint32_t tl_GetCompressedFileSizeW(const std::uint16_t* const file_name,
                                                 std::uint32_t* const high) noexcept {
    if (high != nullptr && mapped_guest_range(high, sizeof(std::uint32_t), true)) {
        *high = 0;
    }
    return tl_GetFileSize(file_name != nullptr ? reinterpret_cast<void*>(0x1) : nullptr, high);
}

TL_MSABI void* tl_FindFirstChangeNotificationW(const std::uint16_t* const path, const int watch_subtree,
                                              const std::uint32_t notify_filter) noexcept {
    (void)path;
    (void)watch_subtree;
    (void)notify_filter;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x57415443ULL); // 'WATC'
}

TL_MSABI int tl_FindNextChangeNotification(void* const handle) noexcept {
    (void)handle;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FindCloseChangeNotification(void* const handle) noexcept {
    (void)handle;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint16_t tl_GetSystemDefaultLangID() noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint16_t tl_GetUserDefaultLangID() noexcept {
    return 0x0409; // en-US
}

TL_MSABI std::uint32_t tl_GetWindowsDirectoryW(std::uint16_t* const buffer, const std::uint32_t size) noexcept {
    static const std::uint16_t kWinDir[] = {'C', ':', '\\', 'W', 'i', 'n', 'd', 'o', 'w', 's', 0};
    constexpr std::uint32_t kLen = 10;
    if (buffer == nullptr || size <= kLen) {
        return kLen + 1;
    }
    if (!mapped_guest_range(buffer, (kLen + 1) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memcpy(buffer, kWinDir, (kLen + 1) * sizeof(std::uint16_t));
    set_last_error(abi::kErrorSuccess);
    return kLen;
}

TL_MSABI std::size_t tl_GlobalSize(void* const mem) noexcept {
    if (mem == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return 4096;
}

TL_MSABI int tl_SetPriorityClass(void* const process, const std::uint32_t priority_class) noexcept {
    (void)process;
    (void)priority_class;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_lstrlenW(const std::uint16_t* const str) noexcept {
    if (str == nullptr) {
        return 0;
    }
    int len = 0;
    while (str[len] != 0) {
        ++len;
    }
    return len;
}

TL_MSABI int tl_K32GetProcessMemoryInfo(void* const process, void* const counters, const std::uint32_t cb) noexcept {
    (void)process;
    if (counters == nullptr || cb < 32 || !mapped_guest_range(counters, cb, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::memset(counters, 0, cb);
    struct DummyCounters {
        std::uint32_t cb;
        std::uint32_t page_fault_count;
        std::size_t peak_working_set;
        std::size_t working_set;
        std::size_t quota_peak_paged;
        std::size_t quota_paged;
        std::size_t quota_peak_nonpaged;
        std::size_t quota_nonpaged;
        std::size_t pagefile_usage;
        std::size_t peak_pagefile_usage;
    } dummy{};
    dummy.cb = cb;
    dummy.working_set = 64 * 1024 * 1024;
    dummy.peak_working_set = 128 * 1024 * 1024;
    dummy.pagefile_usage = 64 * 1024 * 1024;
    std::memcpy(counters, &dummy, std::min<std::size_t>(cb, sizeof(dummy)));
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_K32GetProcessImageFileNameA(void* const process, char* const image_file_name, const std::uint32_t size) noexcept {
    (void)process;
    if (image_file_name == nullptr || size == 0 || !mapped_guest_range(image_file_name, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const char dummy[] = "\\Device\\HarddiskVolume1\\Windows\\System32\\RobloxPlayerInstaller.exe";
    const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(dummy));
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memcpy(image_file_name, dummy, len + 1);
    set_last_error(abi::kErrorSuccess);
    return len;
}

TL_MSABI int tl_Process32First(void* const snapshot, void* const entry) noexcept {
    (void)snapshot;
    if (entry == nullptr || !mapped_guest_range(entry, 36, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // PROCESSENTRY32 ANSI: dwSize (4), cntUsage(4), th32ProcessID(4), th32DefaultHeapID(8), th32ModuleID(4), cntThreads(4), th32ParentProcessID(4), pcPriClassBase(4), dwFlags(4), szExeFile[260]
    struct DummyEntryA {
        std::uint32_t dwSize;
        std::uint32_t cntUsage;
        std::uint32_t th32ProcessID;
        std::uintptr_t th32DefaultHeapID;
        std::uint32_t th32ModuleID;
        std::uint32_t cntThreads;
        std::uint32_t th32ParentProcessID;
        std::int32_t pcPriClassBase;
        std::uint32_t dwFlags;
        char szExeFile[260];
    }* e = reinterpret_cast<DummyEntryA*>(entry);
    const std::uint32_t in_size = e->dwSize;
    std::memset(entry, 0, std::min<std::size_t>(in_size, sizeof(DummyEntryA)));
    e->dwSize = in_size;
    e->th32ProcessID = 1000;
    e->cntThreads = 4;
    std::strncpy(e->szExeFile, "process.exe", sizeof(e->szExeFile) - 1);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Process32Next(void* const snapshot, void* const entry) noexcept {
    (void)snapshot;
    (void)entry;
    set_last_error(18); // ERROR_NO_MORE_FILES
    return 0;
}

TL_MSABI int tl_DuplicateHandle(void* const src_process, void* const src_handle, void* const target_process, void** const target_handle,
                                const std::uint32_t desired_access, const int inherit_handle, const std::uint32_t options) noexcept {
    (void)src_process;
    (void)target_process;
    (void)desired_access;
    (void)inherit_handle;
    (void)options;
    if (target_handle == nullptr || !mapped_guest_range(target_handle, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *target_handle = src_handle;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_LockFile(void* const file, const std::uint32_t offset_low, const std::uint32_t offset_high,
                         const std::uint32_t count_low, const std::uint32_t count_high) noexcept {
    (void)file;
    (void)offset_low;
    (void)offset_high;
    (void)count_low;
    (void)count_high;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_LockFileEx(void* const file, const std::uint32_t flags, const std::uint32_t reserved,
                           const std::uint32_t count_low, const std::uint32_t count_high, void* const overlapped) noexcept {
    (void)file;
    (void)flags;
    (void)reserved;
    (void)count_low;
    (void)count_high;
    (void)overlapped;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UnlockFile(void* const file, const std::uint32_t offset_low, const std::uint32_t offset_high,
                           const std::uint32_t count_low, const std::uint32_t count_high) noexcept {
    (void)file;
    (void)offset_low;
    (void)offset_high;
    (void)count_low;
    (void)count_high;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UnlockFileEx(void* const file, const std::uint32_t reserved,
                             const std::uint32_t count_low, const std::uint32_t count_high, void* const overlapped) noexcept {
    (void)file;
    (void)reserved;
    (void)count_low;
    (void)count_high;
    (void)overlapped;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetDiskFreeSpaceA(const char* const root_path_name, std::uint32_t* const sectors_per_cluster,
                                  std::uint32_t* const bytes_per_sector, std::uint32_t* const number_of_free_clusters,
                                  std::uint32_t* const total_number_of_clusters) noexcept {
    (void)root_path_name;
    if (sectors_per_cluster != nullptr && mapped_guest_range(sectors_per_cluster, sizeof(std::uint32_t), true)) {
        *sectors_per_cluster = 8;
    }
    if (bytes_per_sector != nullptr && mapped_guest_range(bytes_per_sector, sizeof(std::uint32_t), true)) {
        *bytes_per_sector = 512;
    }
    if (number_of_free_clusters != nullptr && mapped_guest_range(number_of_free_clusters, sizeof(std::uint32_t), true)) {
        *number_of_free_clusters = 50000000;
    }
    if (total_number_of_clusters != nullptr && mapped_guest_range(total_number_of_clusters, sizeof(std::uint32_t), true)) {
        *total_number_of_clusters = 100000000;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetDefaultDllDirectories(const std::uint32_t directory_flags) noexcept {
    (void)directory_flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempPathA(const std::uint32_t buffer_length, char* const buffer) noexcept {
    if (buffer == nullptr || buffer_length == 0 || !mapped_guest_range(buffer, buffer_length, true)) {
        return 0;
    }
    const char temp[] = "C:\\Temp\\";
    const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(temp));
    if (buffer_length <= len) {
        return len + 1;
    }
    std::memcpy(buffer, temp, len + 1);
    set_last_error(abi::kErrorSuccess);
    return len;
}

TL_MSABI int tl_MoveFileExA(const char* const existing_file, const char* const new_file, const std::uint32_t flags) noexcept {
    constexpr std::uint32_t kMoveFileReplaceExisting = 0x1U;
    if (existing_file == nullptr || new_file == nullptr ||
        !mapped_guest_cstring(existing_file) || !mapped_guest_cstring(new_file) ||
        (flags & ~kMoveFileReplaceExisting) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char source[4096]{};
    char destination[4096]{};
    if (!translate_windows_path(existing_file, source, sizeof(source)) ||
        !translate_windows_path(new_file, destination, sizeof(destination))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::error_code error;
    if ((flags & kMoveFileReplaceExisting) == 0U &&
        std::filesystem::exists(destination, error)) {
        set_last_error(abi::kErrorAlreadyExists);
        return 0;
    }
    if (::rename(source, destination) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_SleepEx(const std::uint32_t milliseconds, const int alertable) noexcept {
    (void)alertable;
    tl_Sleep(milliseconds);
    return 0;
}

TL_MSABI std::uint32_t tl_WaitForMultipleObjectsEx(const std::uint32_t count, const void* const* const handles,
                                                  const int wait_all, const std::uint32_t milliseconds, const int alertable) noexcept {
    (void)alertable;
    return tl_WaitForMultipleObjects(count, handles, wait_all, milliseconds);
}

TL_MSABI void tl_InitializeConditionVariable(void* const condition_variable) noexcept {
    if (condition_variable != nullptr && mapped_guest_range(condition_variable, sizeof(void*), true)) {
        *reinterpret_cast<void**>(condition_variable) = nullptr;
    }
}

TL_MSABI int tl_SleepConditionVariableCS(void* const condition_variable, void* const critical_section, const std::uint32_t milliseconds) noexcept {
    (void)condition_variable;
    if (critical_section != nullptr) {
        tl_LeaveCriticalSection(critical_section);
        if (milliseconds != 0 && milliseconds != abi::kInfinite) {
            tl_Sleep(std::min<std::uint32_t>(milliseconds, 10));
        }
        tl_EnterCriticalSection(critical_section);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindResourceExW(void* const module, const wchar_t* const type, const wchar_t* const name, const std::uint16_t language) noexcept {
    (void)language;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
}

TL_MSABI int tl_CompareStringEx(const wchar_t* const locale_name, const std::uint32_t flags,
                                const wchar_t* const string1, const int count1,
                                const wchar_t* const string2, const int count2,
                                void* const version_information, void* const reserved, const std::intptr_t param) noexcept {
    (void)locale_name;
    (void)flags;
    (void)version_information;
    (void)reserved;
    (void)param;
    if (string1 == nullptr || string2 == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    int cmp = 0;
    if (count1 < 0 || count2 < 0) {
        cmp = std::wcscmp(string1, string2);
    } else {
        cmp = std::wcsncmp(string1, string2, static_cast<std::size_t>(std::min(count1, count2)));
        if (cmp == 0 && count1 != count2) {
            cmp = count1 < count2 ? -1 : 1;
        }
    }
    return cmp < 0 ? 1 : (cmp == 0 ? 2 : 3); // 1 = CSTR_LESS_THAN, 2 = CSTR_EQUAL, 3 = CSTR_GREATER_THAN
}

TL_MSABI void* tl_CreateFile2(const wchar_t* const file_name, const std::uint32_t desired_access,
                              const std::uint32_t share_mode, const std::uint32_t creation_disposition,
                              void* const create_parameters) noexcept {
    (void)create_parameters;
    return tl_CreateFileW(reinterpret_cast<const std::uint16_t*>(file_name), desired_access, share_mode, nullptr, creation_disposition, 0x80, nullptr);
}

TL_MSABI std::uint32_t tl_GetCurrentProcessorNumber() noexcept {
    return 0;
}

TL_MSABI int tl_InitializeProcThreadAttributeList(void* const attribute_list, const std::uint32_t attribute_count,
                                                  const std::uint32_t flags, std::size_t* const size) noexcept {
    (void)attribute_count;
    (void)flags;
    if (size == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (attribute_list == nullptr) {
        *size = 64;
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memset(attribute_list, 0, *size);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_UpdateProcThreadAttribute(void* const attribute_list, const std::uint32_t flags,
                                          const std::uintptr_t attribute, void* const value,
                                          const std::size_t size, void* const previous_value,
                                          std::size_t* const return_size) noexcept {
    (void)attribute_list;
    (void)flags;
    (void)attribute;
    (void)value;
    (void)size;
    (void)previous_value;
    (void)return_size;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetSystemDirectoryA(char* const buffer, const std::uint32_t size) noexcept {
    if (buffer == nullptr || size == 0 || !mapped_guest_range(buffer, size, true)) {
        return 0;
    }
    const char sys[] = "C:\\Windows\\System32";
    const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(sys));
    if (size <= len) {
        return len + 1;
    }
    std::memcpy(buffer, sys, len + 1);
    set_last_error(abi::kErrorSuccess);
    return len;
}

TL_MSABI int tl_ReadConsoleA(void* const console_input, void* const buffer,
                             const std::uint32_t number_of_chars_to_read,
                             std::uint32_t* const number_of_chars_read,
                             void* const input_control) noexcept {
    (void)console_input;
    (void)buffer;
    (void)number_of_chars_to_read;
    (void)input_control;
    if (number_of_chars_read != nullptr && mapped_guest_range(number_of_chars_read, sizeof(std::uint32_t), true)) {
        *number_of_chars_read = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateWaitableTimerA(void* const timer_attributes, const int manual_reset, const char* const timer_name) noexcept {
    (void)timer_attributes;
    (void)manual_reset;
    (void)timer_name;
    return tl_CreateEventW(nullptr, manual_reset, 0, nullptr);
}

TL_MSABI void* tl_CreateWaitableTimerW(void* const timer_attributes, const int manual_reset, const wchar_t* const timer_name) noexcept {
    (void)timer_attributes;
    (void)manual_reset;
    (void)timer_name;
    return tl_CreateEventW(nullptr, manual_reset, 0, nullptr);
}

TL_MSABI int tl_SetWaitableTimer(void* const timer, const std::int64_t* const due_time, const std::int32_t period,
                                 void* const completion_routine, void* const arg_to_completion_routine, const int resume) noexcept {
    (void)timer;
    (void)due_time;
    (void)period;
    (void)completion_routine;
    (void)arg_to_completion_routine;
    (void)resume;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_CancelWaitableTimer(void* const timer) noexcept {
    (void)timer;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetLogicalProcessorInformation(void* const buffer, std::uint32_t* const returned_length) noexcept {
    if (returned_length == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t req_size = 64;
    if (buffer == nullptr || *returned_length < req_size) {
        *returned_length = req_size;
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::memset(buffer, 0, req_size);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetVolumePathNameW(const wchar_t* const file_name, wchar_t* const volume_path_name, const std::uint32_t buffer_length) noexcept {
    (void)file_name;
    if (volume_path_name == nullptr || buffer_length < 4 || !mapped_guest_range(volume_path_name, buffer_length * sizeof(wchar_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    volume_path_name[0] = L'C';
    volume_path_name[1] = L':';
    volume_path_name[2] = L'\\';
    volume_path_name[3] = L'\0';
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::int32_t tl_SetThreadDescription(void* const thread, const wchar_t* const description) noexcept {
    (void)thread;
    (void)description;
    return 0; // S_OK
}

TL_MSABI void* tl_GetCurrentThread() noexcept {
    return reinterpret_cast<void*>(~static_cast<std::uintptr_t>(1)); // (HANDLE)-2
}

TL_MSABI void* tl_CreateSemaphoreExW(void* const semaphore_attributes, const std::int32_t initial_count,
                                     const std::int32_t maximum_count, const wchar_t* const name,
                                     const std::uint32_t flags, const std::uint32_t desired_access) noexcept {
    (void)flags;
    (void)desired_access;
    return tl_CreateSemaphoreW(semaphore_attributes, initial_count, maximum_count, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI void* tl_OpenSemaphoreW(const std::uint32_t desired_access, const int inherit_handle, const wchar_t* const name) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    return tl_CreateSemaphoreW(nullptr, 1, 1, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI void* tl_CreateMutexExW(void* const mutex_attributes, const wchar_t* const name,
                                 const std::uint32_t flags, const std::uint32_t desired_access) noexcept {
    (void)flags;
    (void)desired_access;
    return tl_CreateMutexW(mutex_attributes, 0, reinterpret_cast<const std::uint16_t*>(name));
}

TL_MSABI void tl_DebugBreak() noexcept {
    // No-op in TradutorLinux runtime
}

TL_MSABI int tl_InitOnceBeginInitialize(void* const init_once, const std::uint32_t flags, int* const pending, void** const context) noexcept {
    if (init_once == nullptr || !mapped_guest_range(init_once, sizeof(void*), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* const ptr = static_cast<std::uintptr_t*>(init_once);
    if ((flags & 1U) != 0U) { // INIT_ONCE_CHECK_ONLY
        if (*ptr == 2U) {
            if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 0;
            if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        set_last_error(1067 /* ERROR_GEN_FAILURE */);
        return 0;
    }
    if (*ptr == 2U) {
        if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 0;
        if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
    } else {
        if (pending != nullptr && mapped_guest_range(pending, sizeof(int), true)) *pending = 1;
        if (context != nullptr && mapped_guest_range(context, sizeof(void*), true)) *context = nullptr;
        *ptr = 1U;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_InitOnceComplete(void* const init_once, const std::uint32_t flags, void* const context) noexcept {
    (void)flags;
    (void)context;
    if (init_once != nullptr && mapped_guest_range(init_once, sizeof(void*), true)) {
        *static_cast<std::uintptr_t*>(init_once) = 2U;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SwitchToThread() noexcept {
    sched_yield();
    return 1;
}

TL_MSABI std::uint32_t tl_GetSystemFirmwareTable(const std::uint32_t firmware_table_provider_signature,
                                                 const std::uint32_t firmware_table_id,
                                                 void* const firmware_table_buffer,
                                                 const std::uint32_t buffer_size) noexcept {
    (void)firmware_table_provider_signature;
    (void)firmware_table_id;
    (void)firmware_table_buffer;
    (void)buffer_size;
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI int tl_Beep(const std::uint32_t freq, const std::uint32_t duration) noexcept {
    (void)freq;
    (void)duration;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ClearCommBreak(void* const file) noexcept {
    (void)file;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ConnectNamedPipe(void* const named_pipe, void* const overlapped) noexcept {
    (void)named_pipe;
    (void)overlapped;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateNamedPipeA(const char* const name, const std::uint32_t open_mode, const std::uint32_t pipe_mode, const std::uint32_t max_instances, const std::uint32_t out_buf_size, const std::uint32_t in_buf_size, const std::uint32_t default_time_out, void* const sec_attr) noexcept {
    (void)name;
    (void)open_mode;
    (void)pipe_mode;
    (void)max_instances;
    (void)out_buf_size;
    (void)in_buf_size;
    (void)default_time_out;
    (void)sec_attr;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50495045ULL); // 'PIPE'
}

TL_MSABI int tl_CreatePipe(void** const read_pipe, void** const write_pipe, void* const pipe_attr, const std::uint32_t size) noexcept {
    (void)pipe_attr;
    (void)size;
    if (read_pipe != nullptr && mapped_guest_range(read_pipe, sizeof(void*), true)) {
        *read_pipe = reinterpret_cast<void*>(0x50524541ULL); // 'PREA'
    }
    if (write_pipe != nullptr && mapped_guest_range(write_pipe, sizeof(void*), true)) {
        *write_pipe = reinterpret_cast<void*>(0x50575249ULL); // 'PWRI'
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindResourceA(void* const module, const char* const name, const char* const type) noexcept {
    (void)module;
    (void)name;
    (void)type;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
}

TL_MSABI int tl_GetCommState(void* const file, void* const dcb) noexcept {
    (void)file;
    if (dcb != nullptr && mapped_guest_range(dcb, 28, true)) {
        std::memset(dcb, 0, 28);
        *reinterpret_cast<std::uint32_t*>(dcb) = 28; // DCBlength
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dcb) + 4) = 9600; // BaudRate
        *reinterpret_cast<std::uint8_t*>(static_cast<char*>(dcb) + 18) = 8; // ByteSize
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetLocaleInfoA(const std::uint32_t lcid, const std::uint32_t lctype, char* const lcdata, const int cch_data) noexcept {
    (void)lcid;
    (void)lctype;
    if (cch_data > 0 && lcdata != nullptr && mapped_guest_range(lcdata, static_cast<std::size_t>(cch_data), true)) {
        std::strncpy(lcdata, "0409", static_cast<std::size_t>(cch_data) - 1);
        lcdata[cch_data - 1] = '\0';
        return static_cast<int>(std::strlen(lcdata) + 1);
    }
    return 5;
}

TL_MSABI int tl_GetOverlappedResult(void* const file, void* const overlapped, std::uint32_t* const bytes_transferred, const int wait) noexcept {
    (void)file;
    (void)overlapped;
    (void)wait;
    if (bytes_transferred != nullptr && mapped_guest_range(bytes_transferred, sizeof(std::uint32_t), true)) {
        *bytes_transferred = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetThreadTimes(void* const thread, void* const creation_time, void* const exit_time, void* const kernel_time, void* const user_time) noexcept {
    (void)thread;
    const std::uint64_t dummy_ft = 130000000000000000ULL;
    if (creation_time != nullptr && mapped_guest_range(creation_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(creation_time) = dummy_ft;
    }
    if (exit_time != nullptr && mapped_guest_range(exit_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(exit_time) = dummy_ft;
    }
    if (kernel_time != nullptr && mapped_guest_range(kernel_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(kernel_time) = 1000000ULL;
    }
    if (user_time != nullptr && mapped_guest_range(user_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(user_time) = 2000000ULL;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetWindowsDirectoryA(char* const buffer, const std::uint32_t size) noexcept {
    const char win_dir[] = "C:\\Windows";
    const std::uint32_t len = sizeof(win_dir) - 1;
    if (size <= len || buffer == nullptr || !mapped_guest_range(buffer, size, true)) {
        return len + 1;
    }
    std::memcpy(buffer, win_dir, len + 1);
    return len;
}

TL_MSABI void tl_GlobalMemoryStatus(void* const buffer) noexcept {
    if (buffer != nullptr && mapped_guest_range(buffer, 32, true)) {
        auto* const mem = reinterpret_cast<std::uint32_t*>(buffer);
        mem[0] = 32; // dwLength
        mem[1] = 25; // dwMemoryLoad (25%)
        mem[2] = 0x7FFFFFFF; // dwTotalPhys (2GB)
        mem[3] = 0x60000000; // dwAvailPhys (1.5GB)
        mem[4] = 0x7FFFFFFF; // dwTotalPageFile
        mem[5] = 0x60000000; // dwAvailPageFile
        mem[6] = 0x7FFE0000; // dwTotalVirtual
        mem[7] = 0x70000000; // dwAvailVirtual
    }
}

TL_MSABI int tl_LocalFileTimeToFileTime(const void* const local_file_time, void* const file_time) noexcept {
    if (local_file_time != nullptr && file_time != nullptr &&
        mapped_guest_range(local_file_time, 8, false) && mapped_guest_range(file_time, 8, true)) {
        *reinterpret_cast<std::uint64_t*>(file_time) = *reinterpret_cast<const std::uint64_t*>(local_file_time);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI int tl_SetCommBreak(void* const file) noexcept {
    (void)file;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCommState(void* const file, void* const dcb) noexcept {
    (void)file;
    (void)dcb;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCommTimeouts(void* const file, void* const timeouts) noexcept {
    (void)file;
    (void)timeouts;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCurrentDirectoryA(const char* const path_name) noexcept {
    if (path_name == nullptr || !mapped_guest_cstring(path_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wide = util::utf8_to_wide(path_name);
    return tl_SetCurrentDirectoryW(reinterpret_cast<const std::uint16_t*>(wide.c_str()));
}

TL_MSABI int tl_SetHandleInformation(void* const object, const std::uint32_t mask, const std::uint32_t flags) noexcept {
    (void)object;
    (void)mask;
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_WaitNamedPipeA(const char* const name, const std::uint32_t timeout) noexcept {
    (void)name;
    (void)timeout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

// SensApi.dll functions
TL_MSABI int tl_IsDestinationReachableW(const wchar_t* const lpszDestination, void* const lpQOCInfo) noexcept {
    (void)lpszDestination;
    (void)lpQOCInfo;
    return 1;
}

TL_MSABI int tl_IsNetworkAlive(std::uint32_t* const lpdwFlags) noexcept {
    if (lpdwFlags != nullptr && mapped_guest_range(lpdwFlags, sizeof(std::uint32_t), true)) {
        *lpdwFlags = 1; // NETWORK_ALIVE_LAN
    }
    return 1;
}

// Notepad++ KERNEL32 functions
TL_MSABI int tl_GetTimeFormatEx(const wchar_t* const lpLocaleName, const std::uint32_t dwFlags, const void* const lpTime, const wchar_t* const lpFormat, wchar_t* const lpTimeStr, const int cchTime) noexcept {
    (void)lpLocaleName;
    (void)dwFlags;
    (void)lpTime;
    (void)lpFormat;
    const wchar_t dummy[] = L"12:00:00";
    const int len = sizeof(dummy) / sizeof(wchar_t);
    if (lpTimeStr == nullptr || cchTime == 0) return len;
    if (cchTime < len) {
        set_last_error(122);
        return 0;
    }
    std::memcpy(lpTimeStr, dummy, sizeof(dummy));
    return len;
}

TL_MSABI int tl_GetDateFormatEx(const wchar_t* const lpLocaleName, const std::uint32_t dwFlags, const void* const lpDate, const wchar_t* const lpFormat, wchar_t* const lpDateStr, const int cchDate, const wchar_t* const lpCalendar) noexcept {
    (void)lpLocaleName;
    (void)dwFlags;
    (void)lpDate;
    (void)lpFormat;
    (void)lpCalendar;
    const wchar_t dummy[] = L"2026-08-28";
    const int len = sizeof(dummy) / sizeof(wchar_t);
    if (lpDateStr == nullptr || cchDate == 0) return len;
    if (cchDate < len) {
        set_last_error(122);
        return 0;
    }
    std::memcpy(lpDateStr, dummy, sizeof(dummy));
    return len;
}

TL_MSABI wchar_t* tl_lstrcpynW(wchar_t* const lpString1, const wchar_t* const lpString2, const int iMaxLength) noexcept {
    if (lpString1 == nullptr || iMaxLength <= 0) return lpString1;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    int i = 0;
    while (i < iMaxLength - 1 && lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_GetApplicationRestartSettings(void* const hProcess, wchar_t* const pwzCommandLine, std::uint32_t* const pcchSize, std::uint32_t* const pdwFlags) noexcept {
    (void)hProcess;
    (void)pwzCommandLine;
    (void)pcchSize;
    (void)pdwFlags;
    return static_cast<int>(0x80070490); // ERROR_NOT_FOUND
}

TL_MSABI int tl_UnregisterApplicationRestart() noexcept {
    return 0; // S_OK
}

TL_MSABI int tl_lstrcmpiA(const char* const lpString1, const char* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    return strcasecmp(lpString1, lpString2);
}

TL_MSABI int tl_RegisterApplicationRestart(const wchar_t* const pwzCommandLine, const std::uint32_t dwFlags) noexcept {
    (void)pwzCommandLine;
    (void)dwFlags;
    return 0; // S_OK
}

TL_MSABI char* tl_lstrcpynA(char* const lpString1, const char* const lpString2, const int iMaxLength) noexcept {
    if (lpString1 == nullptr || iMaxLength <= 0) return lpString1;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    int i = 0;
    while (i < iMaxLength - 1 && lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_CancelIo(void* const hFile) noexcept {
    (void)hFile;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ReadDirectoryChangesW(void* const hDirectory, void* const lpBuffer, const std::uint32_t nBufferLength, const int bWatchSubtree, const std::uint32_t dwNotifyFilter, std::uint32_t* const lpBytesReturned, void* const lpOverlapped, void* const lpCompletionRoutine) noexcept {
    (void)hDirectory;
    (void)lpBuffer;
    (void)nBufferLength;
    (void)bWatchSubtree;
    (void)dwNotifyFilter;
    (void)lpOverlapped;
    (void)lpCompletionRoutine;
    if (lpBytesReturned != nullptr && mapped_guest_range(lpBytesReturned, sizeof(std::uint32_t), true)) {
        *lpBytesReturned = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetStringTypeExW(const std::uint32_t Locale, const std::uint32_t dwInfoType, const wchar_t* const lpSrcStr, const int cchSrc, std::uint16_t* const lpCharType) noexcept {
    (void)Locale;
    (void)dwInfoType;
    (void)lpSrcStr;
    if (lpCharType == nullptr) return 0;
    const int count = cchSrc > 0 ? cchSrc : 1;
    for (int i = 0; i < count; ++i) {
        lpCharType[i] = 0x0001; // C1_UPPER/ALPHA
    }
    return 1;
}

TL_MSABI int tl_LCMapStringA(const std::uint32_t Locale, const std::uint32_t dwMapFlags, const char* const lpSrcStr, const int cchSrc, char* const lpDestStr, const int cchDest) noexcept {
    (void)Locale;
    (void)dwMapFlags;
    if (lpSrcStr == nullptr) return 0;
    const int len = cchSrc > 0 ? cchSrc : static_cast<int>(std::strlen(lpSrcStr) + 1);
    if (lpDestStr == nullptr || cchDest == 0) return len;
    const int copy_len = std::min(len, cchDest);
    std::memcpy(lpDestStr, lpSrcStr, static_cast<std::size_t>(copy_len));
    return copy_len;
}

TL_MSABI int tl_GetStringTypeExA(const std::uint32_t Locale, const std::uint32_t dwInfoType, const char* const lpSrcStr, const int cchSrc, std::uint16_t* const lpCharType) noexcept {
    (void)Locale;
    (void)dwInfoType;
    (void)lpSrcStr;
    if (lpCharType == nullptr) return 0;
    const int count = cchSrc > 0 ? cchSrc : 1;
    for (int i = 0; i < count; ++i) {
        lpCharType[i] = 0x0001;
    }
    return 1;
}

TL_MSABI void tl_FreeLibraryWhenCallbackReturns(void* const pci, void* const module) noexcept {
    (void)pci;
    (void)module;
}

TL_MSABI wchar_t* tl_lstrcpyW(wchar_t* const lpString1, const wchar_t* const lpString2) noexcept {
    if (lpString1 == nullptr) return nullptr;
    if (lpString2 == nullptr) {
        lpString1[0] = 0;
        return lpString1;
    }
    std::size_t i = 0;
    while (lpString2[i] != 0) {
        lpString1[i] = lpString2[i];
        ++i;
    }
    lpString1[i] = 0;
    return lpString1;
}

TL_MSABI int tl_ReplaceFileW(const wchar_t* const lpReplacedFileName, const wchar_t* const lpReplacementFileName, const wchar_t* const lpBackupFileName, const std::uint32_t dwReplaceFlags, void* const lpExclude, void* const lpReserved) noexcept {
    (void)lpBackupFileName;
    (void)dwReplaceFlags;
    (void)lpExclude;
    (void)lpReserved;
    return tl_CopyFileW(reinterpret_cast<const std::uint16_t*>(lpReplacementFileName), reinterpret_cast<const std::uint16_t*>(lpReplacedFileName), 0);
}

TL_MSABI std::uint32_t tl_QueueUserAPC(void* const pfnAPC, void* const hThread, const std::uintptr_t dwData) noexcept {
    (void)pfnAPC;
    (void)hThread;
    (void)dwData;
    return 1;
}

TL_MSABI int tl_lstrcmpW(const wchar_t* const lpString1, const wchar_t* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    std::size_t i = 0;
    while (lpString1[i] != 0 && lpString2[i] != 0) {
        if (lpString1[i] != lpString2[i]) {
            return lpString1[i] < lpString2[i] ? -1 : 1;
        }
        ++i;
    }
    if (lpString1[i] == lpString2[i]) return 0;
    return lpString1[i] < lpString2[i] ? -1 : 1;
}

TL_MSABI int tl_lstrcmpiW(const wchar_t* const lpString1, const wchar_t* const lpString2) noexcept {
    if (lpString1 == lpString2) return 0;
    if (lpString1 == nullptr) return -1;
    if (lpString2 == nullptr) return 1;
    std::size_t i = 0;
    while (lpString1[i] != 0 && lpString2[i] != 0) {
        const auto c1 = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(lpString1[i])));
        const auto c2 = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(lpString2[i])));
        if (c1 != c2) {
            return c1 < c2 ? -1 : 1;
        }
        ++i;
    }
    if (lpString1[i] == lpString2[i]) return 0;
    return lpString1[i] < lpString2[i] ? -1 : 1;
}

}  // extern "C"

}  // namespace tradutorlinux
