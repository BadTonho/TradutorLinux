#include "tradutorlinux/runtime/winapi.hpp"
#include "runtime_context.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {

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

void consume_sync_signal(SyncSlot& slot) noexcept {
    if (slot.kind == SyncKind::Event) {
        if (!slot.manual_reset) {
            slot.signaled = false;
        }
    } else if (slot.kind == SyncKind::Semaphore) {
        --slot.count;
    } else if (slot.kind == SyncKind::Process) {
        // Process handles remain signaled
    } else if (slot.owner_valid && slot.owner == std::this_thread::get_id()) {
        ++slot.recursion;
    } else {
        slot.owner_valid = true;
        slot.owner = std::this_thread::get_id();
        slot.recursion = 1;
    }
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

[[nodiscard]] bool first_process_argument(const std::uint16_t* command_line,
                                          std::string& result) noexcept {
    if (!mapped_guest_wstring(command_line) || command_line == nullptr) {
        return false;
    }
    const std::string utf8 = util::wide_to_utf8(command_line);
    std::size_t begin = 0;
    while (begin < utf8.size() && (utf8[begin] == ' ' || utf8[begin] == '\t')) {
        ++begin;
    }
    if (begin == utf8.size()) {
        return false;
    }
    std::size_t end = begin;
    if (utf8[begin] == '"') {
        ++begin;
        end = utf8.find('"', begin);
        if (end == std::string::npos) {
            return false;
        }
        result = utf8.substr(begin, end - begin);
    } else {
        while (end < utf8.size() && utf8[end] != ' ' && utf8[end] != '\t') {
            ++end;
        }
        result = utf8.substr(begin, end - begin);
    }
    return !result.empty();
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
    g_guest_exit_code = exit_code;
    std::longjmp(g_guest_exit_context, 1);
}

TL_MSABI int tl_WriteFile(const void* const handle, const void* const buffer,
                          const std::uint32_t bytes_to_write,
                          std::uint32_t* const bytes_written,
                          void* const overlapped) noexcept {
    if (overlapped != nullptr || !mapped_guest_range(buffer, bytes_to_write, false) ||
        (bytes_written != nullptr && !mapped_guest_range(bytes_written, sizeof(*bytes_written), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0) {
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
        set_last_error(errno_to_win32(errno));
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
    if (overlapped != nullptr || !mapped_guest_range(buffer, bytes_to_read, true) ||
        (bytes_read != nullptr && !mapped_guest_range(bytes_read, sizeof(*bytes_read), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0) {
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
        set_last_error(errno_to_win32(errno));
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
        *slot = {};
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (SyncSlot* slot = find_sync_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        clear_sync_slot(*slot);
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (ThreadSlot* slot = find_thread_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        if (slot->host_thread.joinable()) {
            slot->host_thread.join();
        }
        if (slot->teb != nullptr) {
            free_guest_teb(slot->teb);
        }
        if (slot->stack != nullptr) {
            munmap(slot->stack, 0x100000U);
        }
        *slot = {};
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_MSABI void* tl_VirtualAlloc(void* const address, const std::size_t size,
                               const std::uint32_t allocation_type,
                               const std::uint32_t protect) noexcept {
    (void)allocation_type;
    (void)protect;
    if (size == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    void* result = mmap(address, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_allocations_mutex);
    auto it = std::find_if(g_allocations.begin(), g_allocations.end(),
                           [](const AllocationSlot& s) { return s.address == nullptr; });
    if (it != g_allocations.end()) {
        it->address = result;
        it->size = size;
    }
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return result;
}

TL_MSABI int tl_VirtualFree(void* const address, const std::size_t size,
                            const std::uint32_t free_type) noexcept {
    (void)free_type;
    if (address == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_allocations_mutex);
    auto it = std::find_if(g_allocations.begin(), g_allocations.end(),
                           [address](const AllocationSlot& s) { return s.address == address; });
    if (it == g_allocations.end()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    munmap(address, it->size != 0 ? it->size : size);
    *it = {};
    bump_guest_allocation_generation();
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
    if (high_size != nullptr) {
        *high_size = static_cast<std::uint32_t>(slot->file_size >> 32);
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(slot->file_size & 0xFFFFFFFFU);
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
        case kFileEnd: new_pos = static_cast<std::int64_t>(slot->file_size) + offset; break;
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
        return nullptr;
    }
    char normalized[4096]{};
    if (!translate_windows_path(file_name, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
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
        return nullptr;
    }
    auto it = std::find_if(g_find_slots.begin(), g_find_slots.end(), [](const FindSlot& s) { return !s.used; });
    if (it == g_find_slots.end()) {
        closedir(dir);
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    it->used = true;
    it->dir = dir;
    it->pattern = pattern;
    it->directory = directory;
    const void* handle = reinterpret_cast<const void*>(
        kFindHandleBase + static_cast<std::uintptr_t>(it - g_find_slots.begin()));
    if (tl_FindNextFileA(handle, find_data) == 0) {
        tl_FindClose(handle);
        return nullptr;
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
        if (slot->pattern == "*" || slot->pattern == "*.*" || slot->pattern == entry->d_name) {
            auto* data = static_cast<Win32FindDataA*>(find_data);
            std::memset(data, 0, sizeof(*data));
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
        if (thread->joined) {
            set_last_error(abi::kErrorInvalidHandle);
            return abi::kWaitFailed;
        }
        const auto predicate = [&]() { return thread->finished; };
        if (milliseconds == abi::kInfinite) {
            thread->finish_cv.wait(lock, predicate);
        } else if (!thread->finish_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate)) {
            set_last_error(abi::kErrorSuccess);
            return abi::kWaitTimeout;
        }
        thread->joined = true;
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
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_lock(&entry->mutex);
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept {
    CriticalSectionEntry* entry = find_cs_entry(critical_section);
    if (entry != nullptr) {
        pthread_mutex_unlock(&entry->mutex);
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept {
    if (critical_section == nullptr) {
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

TL_MSABI void* tl_CreateThread(const void* thread_attributes, const std::size_t stack_size,
                               const void* start_address, const void* parameter,
                               const std::uint32_t creation_flags,
                               std::uint32_t* thread_id) noexcept {
    (void)thread_attributes;
    if (start_address == nullptr || !mapped_guest_range(start_address, 1, false) ||
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
    const std::size_t real_stack_size = stack_size > 0 ? stack_size : 0x100000U;
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
    it->stack_top = stack_top;
    it->finished = false;
    it->joined = false;
    it->exit_code = 0;
    if (thread_id != nullptr) {
        *thread_id = it->thread_id;
    }
    using ThreadProc = TL_MSABI std::uint32_t (*)(const void*);
    auto proc = reinterpret_cast<ThreadProc>(const_cast<void*>(start_address));
    it->host_thread = std::thread([slot_ptr = &*it, proc, parameter, teb, stack_top]() {
        g_current_thread_id = slot_ptr->thread_id;
        set_guest_gs_base(teb);
        slot_ptr->exit_code = static_cast<int>(proc(parameter));
        set_guest_gs_base(nullptr);
        {
            std::lock_guard<std::mutex> lock(slot_ptr->join_mutex);
            slot_ptr->finished = true;
        }
        slot_ptr->finish_cv.notify_all();
    });
    set_last_error(abi::kErrorSuccess);
    return thread_slot_to_handle(*it);
}

TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept {
    (void)exit_code;
    pthread_exit(nullptr);
}

TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept {
    return g_current_thread_id;
}

TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept {
    return static_cast<std::uint32_t>(::getpid());
}

TL_MSABI const char* tl_GetCommandLineA() noexcept {
    return msvcrt_get_guest_command_line();
}

TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept {
    static std::vector<std::uint16_t> wide_cmd;
    const std::string utf8 = msvcrt_get_guest_command_line();
    const std::u16string u16 = util::utf8_to_wide(utf8);
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
        return static_cast<std::uint32_t>(len);
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
    const std::string win_cwd = prefix::translate_linux_path_to_windows(cwd);
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
    const std::string win_cwd = prefix::translate_linux_path_to_windows(cwd);
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
    if (filename == nullptr || size == 0 || !mapped_guest_range(filename, size, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string& path = g_module_file_name;
    const std::string win_path = prefix::translate_linux_path_to_windows(path);
    const std::size_t len = std::min<std::size_t>(win_path.size(), size - 1);
    std::memcpy(filename, win_path.data(), len);
    filename[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetModuleFileNameW(const void* module, std::uint16_t* filename,
                                              std::uint32_t size) noexcept {
    (void)module;
    if (filename == nullptr || size == 0 || !mapped_guest_range(filename, size * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string& path = g_module_file_name;
    const std::string win_path = prefix::translate_linux_path_to_windows(path);
    const std::u16string wide_path = util::utf8_to_wide(win_path);
    const std::size_t len = std::min<std::size_t>(wide_path.size(), size - 1);
    std::copy(wide_path.begin(), wide_path.begin() + len, filename);
    filename[len] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept {
    if (module_name == nullptr) {
        return const_cast<std::byte*>(g_guest_image_base);
    }
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x1000U);
}

TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return const_cast<std::byte*>(g_guest_image_base);
    }
    return tl_GetModuleHandleA(nullptr);
}

TL_MSABI void* tl_GetProcAddress(const void* module, const char* proc_name) noexcept {
    (void)module;
    if (proc_name == nullptr || !mapped_guest_cstring(proc_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    return loader::resolve_symbol_address(proc_name);
}

TL_MSABI std::uint32_t tl_TlsAlloc() noexcept {
    for (std::size_t i = 0; i < g_tls_indices_used.size(); ++i) {
        if (!g_tls_indices_used[i]) {
            g_tls_indices_used[i] = true;
            set_last_error(abi::kErrorSuccess);
            return static_cast<std::uint32_t>(i);
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return 0xFFFFFFFFU;
}

TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept {
    if (tls_index >= g_guest_tls_slots.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return g_guest_tls_slots[tls_index];
}

TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* value) noexcept {
    if (tls_index >= g_guest_tls_slots.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_guest_tls_slots[tls_index] = value;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept {
    if (tls_index >= g_tls_indices_used.size() || !g_tls_indices_used[tls_index]) {
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
                                          void* arguments) noexcept {
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
    auto free_it = std::find_if(g_syncs.begin(), g_syncs.end(),
                                [](const SyncSlot& slot) { return !slot.used; });
    if (free_it == g_syncs.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    int result_pipe[2] = {-1, -1};
    if (::pipe(result_pipe) != 0) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(result_pipe[0]);
        ::close(result_pipe[1]);
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (child == 0) {
        ::close(result_pipe[0]);
        run_created_guest_child(normalized_path, result_pipe[1]);
    }
    ::close(result_pipe[1]);
    SyncSlot& slot = *free_it;
    slot.used = true;
    slot.kind = SyncKind::Process;
    slot.child_pid = child;
    slot.child_result_fd = result_pipe[0];
    slot.process_running = true;
    slot.process_exit_code = kStillActive;
    auto* information = static_cast<GuestProcessInformation*>(process_information);
    *information = {.process_handle = sync_slot_handle(slot),
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

}  // extern "C"

}  // namespace tradutorlinux
