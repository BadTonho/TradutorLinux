#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
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
    return left.named == right.named &&
           (left.named ? left.name == right.name : left.id == right.id);
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
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return false;
    }
    std::istreambuf_iterator<char> iterator{stream};
    const std::istreambuf_iterator<char> end;
    for (; iterator != end; ++iterator) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iterator)));
    }
    return !stream.bad();
}

[[noreturn]] void run_created_guest_child(const std::string& path, const int result_fd) noexcept {
    // O fork copiou o cache de /proc/self/maps do pai: este processo fará
    // novos mapeamentos (imagem, pilha), então o cache precisa recomeçar.
    runtime::invalidate_memory_map_cache();
    GuestExecutionResult result{};
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
    msvcrt_set_guest_command_line(std::vector<std::string>{path});
    set_guest_image_view(process.image.memory, process.image.size,
                         parsed.info.resource_directory_rva,
                         parsed.info.resource_directory_size);
    result = execute_guest_entry(process.thread.entry_point, process.thread.stack_top);
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    write_child_process_result(result_fd, result);
    ::close(result_fd);
    ::_exit(0);
}

}  // namespace

extern "C" {

TL_MSABI void* tl_GetStdHandle(const std::uint32_t std_handle) noexcept {
    switch (std_handle) {
        case abi::kStdInputHandle:
            set_last_error(abi::kErrorSuccess);
            return &kStdInputToken;
        case abi::kStdOutputHandle:
            set_last_error(abi::kErrorSuccess);
            return &kStdOutputToken;
        case abi::kStdErrorHandle:
            set_last_error(abi::kErrorSuccess);
            return &kStdErrorToken;
        default:
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
    }
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
    const std::string utf8 = util::wide_to_utf8(path);
    return tl_CreateFileA(utf8.c_str(), desired_access, share_mode, security_attributes,
                          creation_disposition, flags_and_attributes, template_file);
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
    if (FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_files_mutex);
        ::close(slot->fd);
        slot->used = false;
        slot->fd = -1;
        slot->file_size = 0;
        slot->position = 0;
        set_last_error(abi::kErrorSuccess);
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

TL_MSABI void* tl_LocalFree(void* memory) noexcept {
    std::free(memory);
    return nullptr;
}

TL_MSABI std::uint64_t tl_GetTickCount64() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
}

TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept {
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
        if (slot->pattern == "*" || slot->pattern == "*.*" || slot->pattern == entry->d_name) {
            auto* data = static_cast<Win32FindDataA*>(find_data);
        *data = {};
            std::strncpy(data->c_file_name, entry->d_name, sizeof(data->c_file_name) - 1);
            std::string full_path = slot->directory + "/" + entry->d_name;
            struct stat st{};
            if (stat(full_path.c_str(), &st) == 0) {
                data->dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
                data->n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFFU);
                data->n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
            }
            set_last_error(abi::kErrorSuccess);
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
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
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
    if (name != nullptr && !mapped_guest_wstring(name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return tl_CreateMutexA(security_attributes, initial_owner, nullptr);
}

TL_MSABI void* tl_CreateEventA(const void* security_attributes, const int manual_reset,
                               const int initial_state, const char* name) noexcept {
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
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
    if (name != nullptr && !mapped_guest_wstring(name)) {
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
    if (security_attributes != nullptr || (name != nullptr && !mapped_guest_cstring(name)) ||
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
    tl_InitializeCriticalSection(critical_section);
    return 1;
}

TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
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
    set_last_error(abi::kErrorInvalidParameter);
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
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
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
    const std::size_t real_stack_size = stack_size > 0 ? static_cast<std::size_t>(stack_size) : 0x100000U;
    void* stack = mmap(nullptr, real_stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::uintptr_t stack_top = reinterpret_cast<std::uintptr_t>(stack) + real_stack_size;
    void* teb = allocate_guest_teb(stack_top, real_stack_size);
    if (teb == nullptr) {
        munmap(stack, real_stack_size);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->thread_id = g_next_thread_id.fetch_add(1);
    it->teb = teb;
    it->stack = static_cast<std::byte*>(stack);
    it->stack_size = real_stack_size;
    it->stack_top = stack_top;
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
    it->host_thread = std::thread([slot_ptr = &*it, proc, parameter, teb]() {
        g_current_thread_id = slot_ptr->thread_id;
        set_guest_gs_base(teb);
        std::jmp_buf exit_point{};
        t_thread_exit_context = &exit_point;
        t_thread_exit_slot = slot_ptr;
        if (setjmp(exit_point) == 0) {
            slot_ptr->exit_code = static_cast<int>(proc(parameter));
        }
        // Após longjmp, ler o slot pelo TLS (não depender de registradores).
        ThreadSlot* const finished_slot = t_thread_exit_slot;
        t_thread_exit_context = nullptr;
        t_thread_exit_slot = nullptr;
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
    const char* value = std::getenv(name);
    if (value == nullptr) {
        set_last_error(abi::kErrorEnvvarNotFound);
        return 0;
    }
    const std::size_t len = std::strlen(value);
    if (buffer == nullptr || size == 0) {
        return static_cast<std::uint32_t>(len + 1);
    }
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        // MSDN: com buffer insuficiente, devolve o tamanho necessário
        // incluindo o terminador nulo.
        return static_cast<std::uint32_t>(len + 1);
    }
    std::memcpy(buffer, value, len);
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept {
    if (name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string narrow_name = util::wide_to_utf8(name);
    const std::uint32_t result = tl_GetEnvironmentVariableA(narrow_name.c_str(), nullptr, 0);
    if (result == 0) {
        return 0;
    }
    if (buffer == nullptr || size == 0) {
        return result;
    }
    if (size <= result) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return result;
    }
    std::vector<char> narrow_buf(result + 1, 0);
    tl_GetEnvironmentVariableA(narrow_name.c_str(), narrow_buf.data(), static_cast<std::uint32_t>(narrow_buf.size()));
    const std::u16string wide_res = util::utf8_to_wide(narrow_buf.data());
    std::copy(wide_res.begin(), wide_res.end(), buffer);
    buffer[wide_res.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_res.size());
}

TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(std::uint32_t buffer_length, char* buffer) noexcept {
    char cwd[4096]{};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const std::string win_cwd = prefix::to_windows_path(cwd);
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
    const std::string win_cwd = prefix::to_windows_path(cwd);
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
    const std::string& path = g_module_file_name;
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
    if (filename == nullptr || size == 0) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(g_module_file_name.size() + 1);
    }
    if (!mapped_guest_range(filename, static_cast<std::size_t>(size) * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wide_path = util::utf8_to_wide(g_module_file_name);
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
        if (g_guest_image_base != nullptr) {
            return const_cast<std::byte*>(g_guest_image_base);
        }
        set_last_error(abi::kErrorSuccess);
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
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t top_level_filter) noexcept {
    const std::uintptr_t previous = g_unhandled_exception_filter;
    g_unhandled_exception_filter = top_level_filter;
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

TL_MSABI std::uint32_t tl_FormatMessageW(const std::uint32_t flags, const void* source,
                                          const std::uint32_t message_id, const std::uint32_t language_id,
                                          std::uint16_t* buffer, const std::uint32_t size,
                                          const void* arguments) noexcept {
    (void)source;
    (void)language_id;
    (void)arguments;
    const bool allocate = (flags & abi::kFormatMessageAllocateBuffer) != 0;
    std::string text;
    if ((flags & abi::kFormatMessageFromSystem) != 0) {
        switch (message_id) {
            case abi::kErrorSuccess: text = "The operation completed successfully."; break;
            case abi::kErrorFileNotFound: text = "The system cannot find the file specified."; break;
            case abi::kErrorAccessDenied: text = "Access is denied."; break;
            case abi::kErrorInvalidHandle: text = "The handle is invalid."; break;
            case abi::kErrorNotEnoughMemory: text = "Not enough memory resources are available."; break;
            case abi::kErrorInvalidParameter: text = "The parameter is incorrect."; break;
            case abi::kErrorInsufficientBuffer: text = "The data area passed to a system call is too small."; break;
            case abi::kErrorNoUnicodeTranslation: text = "No mapping for the Unicode character exists in the target multi-byte code page."; break;
            default: text = "Unknown error " + std::to_string(message_id) + "."; break;
        }
    } else {
        text = "Unknown error " + std::to_string(message_id) + ".";
    }
    const std::u16string wide_text = util::utf8_to_wide(text);
    const std::size_t required = wide_text.size() + 1;
    if (allocate) {
        auto* storage = static_cast<std::uint16_t*>(std::malloc(required * sizeof(std::uint16_t)));
        if (storage == nullptr) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        std::copy(wide_text.begin(), wide_text.end(), storage);
        storage[wide_text.size()] = 0;
        auto** output = reinterpret_cast<std::uint16_t**>(buffer);
        *output = storage;
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide_text.size());
    }
    if (required > size) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    std::copy(wide_text.begin(), wide_text.end(), buffer);
    buffer[wide_text.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_text.size());
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
        creation_flags != 0 || environment != nullptr || current_directory != nullptr ||
        process_information == nullptr ||
        !mapped_guest_range(process_information, sizeof(GuestProcessInformation), true) ||
        (application_name != nullptr && !mapped_guest_cstring(application_name)) ||
        (application_name == nullptr && (command_line == nullptr || !mapped_guest_cstring(command_line)))) {
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
            const int written = std::snprintf(candidate, sizeof(candidate), "%.*s/%s",
                                              static_cast<int>(slash), module_file.data(),
                                              normalized_path);
            if (written > 0 && written < static_cast<int>(sizeof(candidate)) &&
                ::access(candidate, X_OK) == 0) {
                std::memcpy(normalized_path, candidate, static_cast<std::size_t>(written) + 1);
            }
        }
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
        run_created_guest_child(normalized_path, result_pipe[1]);
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

std::vector<void*> g_veh_handlers;
std::mutex g_veh_mutex;

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
    if (handler == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_veh_mutex);
    if (first != 0) {
        g_veh_handlers.insert(g_veh_handlers.begin(), handler);
    } else {
        g_veh_handlers.push_back(handler);
    }
    set_last_error(abi::kErrorSuccess);
    return handler;
}

TL_MSABI std::uint32_t tl_RemoveVectoredExceptionHandler(void* handle) noexcept {
    if (handle == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_veh_mutex);
    auto it = std::find(g_veh_handlers.begin(), g_veh_handlers.end(), handle);
    if (it != g_veh_handlers.end()) {
        g_veh_handlers.erase(it);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI void tl_RaiseException(const std::uint32_t exception_code, const std::uint32_t exception_flags,
                                const std::uint32_t number_of_arguments, const std::uint64_t* arguments) noexcept {
    (void)exception_code;
    (void)exception_flags;
    (void)number_of_arguments;
    (void)arguments;
    const std::string detail = std::to_string(exception_code);
    trace_guest_failure("RaiseException", "code", detail.c_str());
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
    (void)memory;
    return 4096;
}

TL_MSABI std::size_t tl_HeapCompact(void* heap, const std::uint32_t flags) noexcept {
    (void)heap;
    (void)flags;
    return 0;
}

}  // extern "C"

}  // namespace tradutorlinux
