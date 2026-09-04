#include "kernel32_internal.hpp"
namespace tradutorlinux {

namespace {

std::uint64_t current_file_size(const FileSlot& slot) noexcept {
    struct stat st{};
    if (::fstat(slot.fd, &st) == 0) {
        return static_cast<std::uint64_t>(st.st_size);
    }
    return 0;
}

bool normalize_wide_path(const std::uint16_t* path, std::string& result) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        return false;
    }
    result = normalized;
    return true;
}

std::int64_t filetime_ticks(const timespec& value) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    return (static_cast<std::int64_t>(value.tv_sec) + kEpochDifference) * 10000000LL +
           static_cast<std::int64_t>(value.tv_nsec) / 100LL;
}

struct LegacyFileTime {
    std::uint32_t low{};
    std::uint32_t high{};
};
static_assert(sizeof(LegacyFileTime) == 8);

void write_filetime(const timespec& source, LegacyFileTime& target) noexcept {
    const auto ticks = static_cast<std::uint64_t>(std::max<std::int64_t>(filetime_ticks(source), 0));
    target.low = static_cast<std::uint32_t>(ticks & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(ticks >> 32U);
}

bool filetime_to_timespec(const LegacyFileTime& value, timespec& result) noexcept {
    constexpr std::uint64_t kEpochDifference = 11644473600ULL;
    constexpr std::uint64_t kTicksPerSecond = 10000000ULL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(value.high) << 32U) | value.low;
    if (ticks < kEpochDifference * kTicksPerSecond) {
        return false;
    }
    const std::uint64_t unix_ticks = ticks - kEpochDifference * kTicksPerSecond;
    result.tv_sec = static_cast<time_t>(unix_ticks / kTicksPerSecond);
    result.tv_nsec = static_cast<long>((unix_ticks % kTicksPerSecond) * 100ULL);
    return true;
}

struct LegacyFileAttributeData {
    std::uint32_t attributes{};
    LegacyFileTime creation{};
    LegacyFileTime last_access{};
    LegacyFileTime last_write{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
};
static_assert(sizeof(LegacyFileAttributeData) == 36);

struct LegacyByHandleFileInformation {
    std::uint32_t attributes{};
    LegacyFileTime creation{};
    LegacyFileTime last_access{};
    LegacyFileTime last_write{};
    std::uint32_t volume_serial_number{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t number_of_links{};
    std::uint32_t file_index_high{};
    std::uint32_t file_index_low{};
};
static_assert(sizeof(LegacyByHandleFileInformation) == 52);

struct LegacyBasicFileInformation {
    std::int64_t creation_time{};
    std::int64_t last_access_time{};
    std::int64_t last_write_time{};
    std::int64_t change_time{};
    std::uint32_t attributes{};
    std::uint32_t reserved{};
};
static_assert(sizeof(LegacyBasicFileInformation) == 40);

struct LegacyFindDataW {
    std::uint32_t attributes{};
    std::uint32_t creation_low{};
    std::uint32_t creation_high{};
    std::uint32_t access_low{};
    std::uint32_t access_high{};
    std::uint32_t write_low{};
    std::uint32_t write_high{};
    std::uint32_t size_high{};
    std::uint32_t size_low{};
    std::uint32_t reserved0{};
    std::uint32_t reserved1{};
    std::uint16_t file_name[260]{};
    std::uint16_t alternate_file_name[14]{};
};
static_assert(sizeof(LegacyFindDataW) == 592);

void convert_find_data(const Win32FindDataA& source, LegacyFindDataW& target) noexcept {
    target = {};
    target.attributes = source.dw_file_attributes;
    target.creation_low = source.ft_creation_time_lo;
    target.creation_high = source.ft_creation_time_hi;
    target.access_low = source.ft_last_access_time_lo;
    target.access_high = source.ft_last_access_time_hi;
    target.write_low = source.ft_last_write_time_lo;
    target.write_high = source.ft_last_write_time_hi;
    target.size_high = source.n_file_size_high;
    target.size_low = source.n_file_size_low;
    target.reserved0 = source.dw_reserved0;
    target.reserved1 = source.dw_reserved1;
    const std::u16string name = util::utf8_to_wide(source.c_file_name);
    const std::size_t name_length = std::min(name.size(), std::size(target.file_name) - 1U);
    std::copy(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(name_length), target.file_name);
    target.file_name[name_length] = 0;
}

std::string normalize_windows_path_segments(const std::string& absolute_with_drive) {
    std::string drive;
    std::string_view rest;
    bool is_unc = false;
    if (absolute_with_drive.size() >= 2 && absolute_with_drive[0] == '\\' &&
        absolute_with_drive[1] == '\\') {
        is_unc = true;
        rest = std::string_view(absolute_with_drive).substr(2);
        drive = "\\\\";
    } else if (absolute_with_drive.size() >= 2 && absolute_with_drive[1] == ':') {
        drive = absolute_with_drive.substr(0, 2);
        rest = std::string_view(absolute_with_drive).substr(2);
    } else {
        rest = absolute_with_drive;
    }

    std::vector<std::string> stack;
    std::string current;
    for (std::size_t i = 0; i <= rest.size(); ++i) {
        const char c = i < rest.size() ? rest[i] : '\\';
        if (c == '\\' || c == '/') {
            if (current.empty() || current == ".") {
            } else if (current == "..") {
                if (!stack.empty()) stack.pop_back();
            } else {
                stack.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    std::string out = drive;
    if (is_unc) {
        for (const auto& seg : stack) {
            out += seg;
            out.push_back('\\');
        }
        if (!stack.empty()) out.pop_back();
        if (out == "\\\\") out = "\\\\";
    } else {
        if (!drive.empty()) {
            out.push_back('\\');
        } else if (!stack.empty()) {
            out.push_back('\\');
        }
        for (std::size_t i = 0; i < stack.size(); ++i) {
            out += stack[i];
            if (i + 1 < stack.size()) out.push_back('\\');
        }
        if (out.empty()) out = drive.empty() ? "\\" : drive + "\\";
    }
    return out;
}

std::string build_full_windows_path(const std::string& input_raw) {
    std::string input = input_raw;
    std::replace(input.begin(), input.end(), '/', '\\');

    char cwd_buf[4096]{};
    const char* cwd_cstr = ::getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr ? cwd_buf : ".";
    std::string win_cwd =
        prefix::to_windows_path(std::filesystem::path(cwd_cstr), guest_prefix_root());
    std::replace(win_cwd.begin(), win_cwd.end(), '/', '\\');

    if (win_cwd.size() == 2 && win_cwd[1] == ':') win_cwd += "\\";

    const bool is_unc = input.size() >= 2 && input[0] == '\\' && input[1] == '\\';
    const bool is_drive_abs =
        input.size() >= 3 && std::isalpha(static_cast<unsigned char>(input[0])) &&
        input[1] == ':' && (input[2] == '\\' || input[2] == '/');
    const bool is_rooted = !input.empty() && (input[0] == '\\' || input[0] == '/');
    const bool is_drive_relative =
        input.size() >= 2 && std::isalpha(static_cast<unsigned char>(input[0])) && input[1] == ':';

    std::string combined;
    if (is_unc || is_drive_abs) {
        combined = input;
    } else if (is_rooted) {
        std::string drive = "C:";
        if (win_cwd.size() >= 2 && win_cwd[1] == ':') {
            drive = win_cwd.substr(0, 2);
        }
        combined = drive + input;
    } else if (is_drive_relative) {
        const char drive_letter = input[0];
        const bool same_drive =
            win_cwd.size() >= 2 &&
            std::toupper(static_cast<unsigned char>(win_cwd[0])) ==
                std::toupper(static_cast<unsigned char>(drive_letter));
        const std::string base = same_drive ? win_cwd : (std::string(1, drive_letter) + ":\\");
        std::string rel = input.substr(2);
        while (!rel.empty() && (rel.front() == '\\' || rel.front() == '/')) {
            rel.erase(rel.begin());
        }
        if (base.back() == '\\') {
            combined = base + rel;
        } else {
            combined = base + "\\" + rel;
        }
    } else {
        if (win_cwd.back() == '\\') {
            combined = win_cwd + input;
        } else {
            combined = win_cwd + "\\" + input;
        }
    }

    return normalize_windows_path_segments(combined);
}

std::u16string final_windows_path(const std::string& path) {
    return util::utf8_to_wide(
        prefix::to_windows_path(std::filesystem::path(path), guest_prefix_root()));
}

} // namespace

extern "C" {

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
        return kInvalidHandleValue;
    }
    char normalized[4096]{};
    if (!translate_windows_path(file_name, normalized, sizeof(normalized))) {
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
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
            return kInvalidHandleValue;
    }
    const int fd = ::open(normalized, flags, 0644);
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return kInvalidHandleValue;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    auto it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& s) { return !s.used; });
    if (it == g_files.end()) {
        ::close(fd);
        set_last_error(abi::kErrorNotEnoughMemory);
        return kInvalidHandleValue;
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
        return kInvalidHandleValue;
    }
    (void)share_mode;
    (void)security_attributes;
    (void)flags_and_attributes;
    (void)template_file;
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
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
            return kInvalidHandleValue;
    }
    const int fd = ::open(normalized, flags, 0644);
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return kInvalidHandleValue;
    }
    std::lock_guard<std::mutex> lock(g_files_mutex);
    auto it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& s) { return !s.used; });
    if (it == g_files.end()) {
        ::close(fd);
        set_last_error(abi::kErrorNotEnoughMemory);
        return kInvalidHandleValue;
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
    if (handle == nullptr || handle == kInvalidHandleValue) {
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
        bool do_cleanup = false;
        {
            std::lock_guard<std::mutex> lock(g_threads_mutex);
            slot->handle_closed = true;
            if (slot->finished && !slot->joined) {
                slot->joined = true;
                do_cleanup = true;
            }
        }
        if (do_cleanup) {
            if (slot->host_thread.joinable()) {
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
            slot->handle_closed = false;
            slot->exit_code = 0;
            runtime::invalidate_memory_map_cache();
        }
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

TL_MSABI int tl_AreFileApisANSI() noexcept {
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

TL_MSABI void tl_SetFileApisToOEM(void) noexcept {
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

TL_MSABI std::uint32_t tl_GetLogicalDrives(void) noexcept {
    return (1U << 2); // Drive C:
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

TL_MSABI int tl_SetSearchPathMode(const std::uint32_t flags) noexcept {
    (void)flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
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

TL_MSABI std::uint32_t tl_GetTempPathA(const std::uint32_t buffer_length, char* const buffer) noexcept {
    if (buffer == nullptr || buffer_length == 0 || !mapped_guest_range(buffer, buffer_length, true)) {
        return 0;
    }
    const char temp[] = "C:\\windows\\temp\\";
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
    if (existing_file[0] == '/' && new_file[0] == '/') {
        std::strncpy(source, existing_file, sizeof(source) - 1);
        std::strncpy(destination, new_file, sizeof(destination) - 1);
    } else if (!translate_windows_path(existing_file, source, sizeof(source)) ||
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
    runtime::security::rename_path(std::filesystem::path{source}, std::filesystem::path{destination});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateFile2(const wchar_t* const file_name, const std::uint32_t desired_access,
                              const std::uint32_t share_mode, const std::uint32_t creation_disposition,
                              void* const create_parameters) noexcept {
    (void)create_parameters;
    return tl_CreateFileW(reinterpret_cast<const std::uint16_t*>(file_name), desired_access, share_mode, nullptr, creation_disposition, 0x80, nullptr);
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

TL_MSABI int tl_ReplaceFileW(const wchar_t* const lpReplacedFileName, const wchar_t* const lpReplacementFileName, const wchar_t* const lpBackupFileName, const std::uint32_t dwReplaceFlags, void* const lpExclude, void* const lpReserved) noexcept {
    (void)lpBackupFileName;
    (void)dwReplaceFlags;
    (void)lpExclude;
    (void)lpReserved;
    return tl_CopyFileW(reinterpret_cast<const std::uint16_t*>(lpReplacementFileName), reinterpret_cast<const std::uint16_t*>(lpReplacedFileName), 0);
}

TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept {
    return tl_MoveFileExA(from, to, 0);
}

TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept {
    if (!mapped_guest_wstring(path) || find_data == nullptr ||
        !mapped_guest_range(find_data, sizeof(LegacyFindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    std::string utf8_path;
    if (!wide_path_to_string(path, utf8_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    Win32FindDataA ansi{};
    void* const handle = tl_FindFirstFileA(utf8_path.c_str(), &ansi);
    if (handle == reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max())) {
        return handle;
    }
    convert_find_data(ansi, *static_cast<LegacyFindDataW*>(find_data));
    return handle;
}

TL_MSABI void* tl_FindFirstFileExW(const std::uint16_t* const path, const int info_level,
                                   void* const find_data, const int search_operation,
                                   const void* const search_filter,
                                   const std::uint32_t additional_flags) noexcept {
    if ((info_level != static_cast<int>(abi::kFindExInfoStandard) &&
         info_level != static_cast<int>(abi::kFindExInfoBasic)) ||
        search_operation != static_cast<int>(abi::kFindExSearchNameMatch) ||
        search_filter != nullptr ||
        (additional_flags & ~abi::kFindFirstExLargeFetch) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("find-first-ex", "failed", "unsupported-parameters");
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    void* const handle = tl_FindFirstFileW(path, find_data);
    trace_filesystem("find-first-ex",
                     handle == reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max())
                         ? "failed"
                         : "success",
                     info_level == static_cast<int>(abi::kFindExInfoBasic) ? "basic" : "standard");
    return handle;
}

TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept {
    if (find_data == nullptr || !mapped_guest_range(find_data, sizeof(LegacyFindDataW), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    Win32FindDataA ansi{};
    if (tl_FindNextFileA(handle, &ansi) == 0) {
        return 0;
    }
    convert_find_data(ansi, *static_cast<LegacyFindDataW*>(find_data));
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           std::uint32_t unique,
                                           std::uint16_t* temp_file_name) noexcept {
    constexpr std::size_t kMaxTempPath = 260;
    if (!mapped_guest_wstring(path_name) || !mapped_guest_wstring(prefix_string) ||
        path_name == nullptr || prefix_string == nullptr || temp_file_name == nullptr ||
        !mapped_guest_range(temp_file_name, kMaxTempPath * sizeof(*temp_file_name), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string directory;
    if (!normalize_wide_path(path_name, directory)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string prefix = util::wide_to_utf8(prefix_string).substr(0, 3);
    std::string pattern = directory;
    if (!pattern.empty() && pattern.back() != '/') {
        pattern.push_back('/');
    }
    pattern += prefix;
    pattern += "XXXXXX";
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    const int fd = ::mkstemp(mutable_pattern.data());
    if (fd < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    ::close(fd);
    const std::u16string wide_name = util::utf8_to_wide(mutable_pattern.data());
    std::uint32_t error = abi::kErrorSuccess;
    if (!copy_wide_string(wide_name, temp_file_name, kMaxTempPath, error)) {
        set_last_error(error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return unique != 0U ? unique : 1U;
}

TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr || slot->position < 0 || ::ftruncate(slot->fd, slot->position) != 0) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : errno_to_win32(errno));
        return 0;
    }
    slot->file_size = static_cast<std::uint64_t>(slot->position);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileAttributesExW(const std::uint16_t* path, int info_level, void* data) noexcept {
    if (info_level != 0 || data == nullptr || !mapped_guest_range(data, sizeof(LegacyFileAttributeData), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string normalized;
    struct stat st{};
    if (!normalize_wide_path(path, normalized) || ::stat(normalized.c_str(), &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<LegacyFileAttributeData*>(data);
    *result = {};
    result->attributes = stat_to_win32_attributes(normalized.c_str(), st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    result->size_low = static_cast<std::uint32_t>(file_size);
    result->size_high = static_cast<std::uint32_t>(file_size >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveFileExW(const std::uint16_t* from, const std::uint16_t* to,
                            std::uint32_t flags) noexcept {
    constexpr std::uint32_t kMoveFileReplaceExisting = 0x1U;
    if (from == nullptr || to == nullptr || (flags & ~kMoveFileReplaceExisting) != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string source_path;
    std::string target_path;
    if (!normalize_wide_path(from, source_path) || !normalize_wide_path(to, target_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::error_code error;
    if ((flags & kMoveFileReplaceExisting) == 0U &&
        std::filesystem::exists(target_path, error)) {
        set_last_error(abi::kErrorAlreadyExists);
        return 0;
    }
    if (::rename(source_path.c_str(), target_path.c_str()) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    runtime::security::rename_path(std::filesystem::path{source_path},
                                   std::filesystem::path{target_path});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_MoveFileW(const std::uint16_t* from, const std::uint16_t* to) noexcept {
    return tl_MoveFileExW(from, to, 0);
}

TL_MSABI int tl_CopyFileW(const std::uint16_t* from, const std::uint16_t* to,
                          int fail_if_exists) noexcept {
    std::string source_path;
    std::string target_path;
    if (!normalize_wide_path(from, source_path) || !normalize_wide_path(to, target_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int source = ::open(source_path.c_str(), O_RDONLY);
    if (source < 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    const int target_flags = O_WRONLY | O_CREAT | (fail_if_exists != 0 ? O_EXCL : O_TRUNC);
    const int target = ::open(target_path.c_str(), target_flags, 0666);
    if (target < 0) {
        const int failure = errno;
        ::close(source);
        set_last_error(errno_to_win32(failure));
        return 0;
    }
    bool success = true;
    std::array<char, 16384> buffer{};
    ssize_t count = 0;
    while ((count = ::read(source, buffer.data(), buffer.size())) > 0) {
        ssize_t written = 0;
        while (written < count) {
            const ssize_t result = ::write(target, buffer.data() + written, static_cast<std::size_t>(count - written));
            if (result <= 0) {
                success = false;
                break;
            }
            written += result;
        }
        if (!success) {
            break;
        }
    }
    if (count < 0) {
        success = false;
    }
    const int failure = errno;
    ::close(source);
    ::close(target);
    if (!success) {
        set_last_error(errno_to_win32(failure));
        return 0;
    }
    // CopyFile cria um objeto novo com descritor padrão; uma eventual DACL
    // virtual do destino substituído não pode acompanhar os bytes copiados.
    runtime::security::remove_path(std::filesystem::path{target_path});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_RemoveDirectoryW(const std::uint16_t* path) noexcept {
    std::string normalized;
    if (!normalize_wide_path(path, normalized) || ::rmdir(normalized.c_str()) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    runtime::security::remove_path(std::filesystem::path{normalized});
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetTempPathW(std::uint32_t buffer_length, std::uint16_t* buffer) noexcept {
    const std::u16string value = u"C:\\windows\\temp\\";
    const std::size_t required = value.size() + 1U;
    if (buffer == nullptr || buffer_length < required ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(required);
    }
    std::copy(value.begin(), value.end(), buffer);
    buffer[value.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(value.size());
}

TL_MSABI std::uint32_t tl_GetFullPathNameW(const std::uint16_t* path, std::uint32_t buffer_length,
                                           std::uint16_t* buffer, std::uint16_t** file_part) noexcept {
    if (path == nullptr || !mapped_guest_wstring(path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8 = util::wide_to_utf8(path);
    if (utf8.empty() && path[0] != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Caso especial: string vazia -> retorna 0 como Wine.
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string full = build_full_windows_path(utf8);
    const std::u16string wide = util::utf8_to_wide(full);
    if (file_part != nullptr && !mapped_guest_range(file_part, sizeof(*file_part), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || buffer_length == 0) {
        // Wine: com buffer nulo, retorna tamanho necessário sem escrever.
        // Retornamos wide.size() (sem terminador) para compatibilidade com teste existente,
        // mas documentamos que inclui terminador no cálculo de insuficiência.
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(wide.size() + 1);
    }
    if (buffer_length <= wide.size() ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        if (file_part != nullptr) *file_part = nullptr;
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(wide.size() + 1);
    }
    std::copy(wide.begin(), wide.end(), buffer);
    buffer[wide.size()] = 0;
    if (file_part != nullptr) {
        // Para simplicidade, recalcula via wide: encontra último '\' no buffer.
        std::size_t wide_slash = wide.find_last_of(u'\\');
        std::size_t wide_colon = wide.find_last_of(u':');
        std::size_t wpos = std::u16string::npos;
        if (wide_slash != std::u16string::npos) wpos = wide_slash;
        if (wide_colon != std::u16string::npos && wide_colon + 1 > wpos) wpos = wide_colon;
        *file_part = wpos == std::u16string::npos ? buffer : buffer + wpos + 1;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide.size());
}

TL_MSABI std::uint32_t tl_GetFullPathNameA(const char* path, std::uint32_t buffer_length, char* buffer,
                                          char** file_part) noexcept {
    if (path == nullptr || !mapped_guest_cstring(path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::u16string wpath = util::utf8_to_wide(path);
    // Reusa lógica W para garantir mesma normalização.
    const std::string full = build_full_windows_path(path);
    if (file_part != nullptr && !mapped_guest_range(file_part, sizeof(*file_part), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || buffer_length == 0) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<std::uint32_t>(full.size() + 1);
    }
    if (buffer_length <= full.size() ||
        !mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length), true)) {
        if (file_part != nullptr) *file_part = nullptr;
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(full.size() + 1);
    }
    std::memcpy(buffer, full.data(), full.size());
    buffer[full.size()] = '\0';
    if (file_part != nullptr) {
        const std::size_t slash = full.find_last_of('\\');
        const std::size_t colon = full.find_last_of(':');
        std::size_t pos = std::string::npos;
        if (slash != std::string::npos) pos = slash;
        if (colon != std::string::npos && colon + 1 > pos) pos = colon;
        *file_part = pos == std::string::npos ? buffer : buffer + pos + 1;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(full.size());
}

TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time, void* access_time,
                            void* write_time) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if ((creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(LegacyFileTime), true)) ||
        (access_time != nullptr && !mapped_guest_range(access_time, sizeof(LegacyFileTime), true)) ||
        (write_time != nullptr && !mapped_guest_range(write_time, sizeof(LegacyFileTime), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    if (creation_time != nullptr) {
        write_filetime(st.st_ctim, *static_cast<LegacyFileTime*>(creation_time));
    }
    if (access_time != nullptr) {
        write_filetime(st.st_atim, *static_cast<LegacyFileTime*>(access_time));
    }
    if (write_time != nullptr) {
        write_filetime(st.st_mtim, *static_cast<LegacyFileTime*>(write_time));
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if ((creation_time != nullptr && !mapped_guest_range(creation_time, sizeof(LegacyFileTime), false)) ||
        (access_time != nullptr && !mapped_guest_range(access_time, sizeof(LegacyFileTime), false)) ||
        (write_time != nullptr && !mapped_guest_range(write_time, sizeof(LegacyFileTime), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    timespec times[2]{st.st_atim, st.st_mtim};
    if (access_time != nullptr && !filetime_to_timespec(*static_cast<const LegacyFileTime*>(access_time), times[0])) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (write_time != nullptr && !filetime_to_timespec(*static_cast<const LegacyFileTime*>(write_time), times[1])) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (::futimens(slot->fd, times) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandle(const void* handle, void* information) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (information == nullptr || !mapped_guest_range(information, sizeof(LegacyByHandleFileInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    auto* result = static_cast<LegacyByHandleFileInformation*>(information);
    *result = {};
    result->attributes = stat_to_win32_attributes(slot->path.c_str(), st);
    write_filetime(st.st_ctim, result->creation);
    write_filetime(st.st_atim, result->last_access);
    write_filetime(st.st_mtim, result->last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    const auto inode = static_cast<std::uint64_t>(st.st_ino);
    result->volume_serial_number = static_cast<std::uint32_t>(st.st_dev);
    result->size_low = static_cast<std::uint32_t>(file_size);
    result->size_high = static_cast<std::uint32_t>(file_size >> 32U);
    result->number_of_links = static_cast<std::uint32_t>(st.st_nlink);
    result->file_index_low = static_cast<std::uint32_t>(inode);
    result->file_index_high = static_cast<std::uint32_t>(inode >> 32U);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, int info_class,
                                             void* buffer, std::uint32_t size) noexcept {
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (info_class != 0 || buffer == nullptr || size < sizeof(LegacyBasicFileInformation) ||
        !mapped_guest_range(buffer, sizeof(LegacyBasicFileInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        set_last_error(errno_to_win32(errno));
        return 0;
    }
    *static_cast<LegacyBasicFileInformation*>(buffer) = {
        filetime_ticks(st.st_ctim), filetime_ticks(st.st_atim), filetime_ticks(st.st_mtim),
        filetime_ticks(st.st_ctim), stat_to_win32_attributes(slot->path.c_str(), st), 0};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetFileInformationByHandle(const void* const handle, const int info_class,
                                           const void* const buffer,
                                           const std::uint32_t size) noexcept {
    FileSlot* const slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_filesystem("set-information", "failed", "invalid-handle");
        return 0;
    }
    if (buffer == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("set-information", "failed", "null-buffer");
        return 0;
    }

    if (info_class == static_cast<int>(abi::kFileBasicInfo)) {
        if (size < sizeof(abi::GuestFileBasicInfo) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileBasicInfo), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "short-basic-info");
            return 0;
        }
        const auto& info = *static_cast<const abi::GuestFileBasicInfo*>(buffer);
        if (info.reserved != 0) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        struct stat st{};
        if (::fstat(slot->fd, &st) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            return 0;
        }
        timespec times[2]{st.st_atim, st.st_mtim};
        const auto to_file_time = [](const std::int64_t value) noexcept {
            const std::uint64_t bits = static_cast<std::uint64_t>(value);
            return LegacyFileTime{static_cast<std::uint32_t>(bits & 0xFFFFFFFFU),
                                  static_cast<std::uint32_t>(bits >> 32U)};
        };
        if ((info.last_access_time != 0 &&
             !filetime_to_timespec(to_file_time(info.last_access_time), times[0])) ||
            (info.last_write_time != 0 &&
             !filetime_to_timespec(to_file_time(info.last_write_time), times[1]))) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "invalid-time");
            return 0;
        }
        if ((info.last_access_time != 0 || info.last_write_time != 0) &&
            ::futimens(slot->fd, times) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            return 0;
        }
        if (info.file_attributes != 0) {
            const std::uint32_t error = apply_win32_file_attributes(
                slot->path.c_str(), info.file_attributes);
            if (error != abi::kErrorSuccess) {
                set_last_error(error);
                trace_filesystem("set-information", "failed", "invalid-attributes");
                return 0;
            }
        }
        set_last_error(abi::kErrorSuccess);
        trace_filesystem("set-information", "success", "FileBasicInfo");
        return 1;
    }

    bool delete_file = false;
    bool posix_semantics = false;
    bool ignore_readonly = false;
    if (info_class == static_cast<int>(abi::kFileDispositionInfo)) {
        if (size < sizeof(abi::GuestFileDispositionInfo) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileDispositionInfo), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        delete_file = static_cast<const abi::GuestFileDispositionInfo*>(buffer)->delete_file != 0;
    } else if (info_class == static_cast<int>(abi::kFileDispositionInfoEx)) {
        if (size < sizeof(abi::GuestFileDispositionInfoEx) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileDispositionInfoEx), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::uint32_t flags =
            static_cast<const abi::GuestFileDispositionInfoEx*>(buffer)->flags;
        constexpr std::uint32_t kSupportedFlags = abi::kFileDispositionFlagDelete |
                                                   abi::kFileDispositionFlagPosixSemantics |
                                                   abi::kFileDispositionFlagOnClose |
                                                   abi::kFileDispositionFlagIgnoreReadonlyAttribute;
        if ((flags & ~kSupportedFlags) != 0 ||
            ((flags & ~abi::kFileDispositionFlagDelete) != 0 &&
             (flags & abi::kFileDispositionFlagDelete) == 0)) {
            set_last_error(abi::kErrorInvalidParameter);
            trace_filesystem("set-information", "failed", "invalid-disposition-flags");
            return 0;
        }
        delete_file = (flags & abi::kFileDispositionFlagDelete) != 0;
        posix_semantics = (flags & abi::kFileDispositionFlagPosixSemantics) != 0;
        ignore_readonly =
            (flags & abi::kFileDispositionFlagIgnoreReadonlyAttribute) != 0;
    } else {
        set_last_error(abi::kErrorInvalidParameter);
        trace_filesystem("set-information", "failed", "unsupported-class");
        return 0;
    }

    if (!delete_file) {
        slot->delete_pending = false;
        set_last_error(abi::kErrorSuccess);
        trace_filesystem("set-information", "success", "delete-cancelled");
        return 1;
    }
    struct stat st{};
    if (::fstat(slot->fd, &st) != 0) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    if (!ignore_readonly && (stat_to_win32_attributes(slot->path.c_str(), st) &
                             abi::kFileAttributeReadOnly) != 0) {
        set_last_error(abi::kErrorAccessDenied);
        trace_filesystem("set-information", "failed", "read-only");
        return 0;
    }
    if (posix_semantics) {
        if (!slot->unlinked && ::unlink(slot->path.c_str()) != 0) {
            const std::uint32_t error = errno_to_win32(errno);
            set_last_error(error);
            trace_filesystem("set-information", "failed", "posix-delete");
            return 0;
        }
        slot->unlinked = true;
        slot->delete_pending = false;
        runtime::security::remove_path(std::filesystem::path{slot->path});
        trace_filesystem("set-information", "success", "posix-delete");
    } else {
        slot->delete_pending = true;
        trace_filesystem("set-information", "success", "delete-on-close");
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle, std::uint16_t* buffer,
                                                    std::uint32_t buffer_length,
                                                    std::uint32_t flags) noexcept {
    if (flags != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    const std::u16string path = final_windows_path(slot->path);
    if (buffer == nullptr || buffer_length <= path.size()) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(path.size() + 1U);
    }
    if (!mapped_guest_range(buffer, static_cast<std::size_t>(buffer_length) * sizeof(*buffer), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::copy(path.begin(), path.end(), buffer);
    buffer[path.size()] = 0;
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(path.size());
}

}  // extern "C"
}  // namespace tradutorlinux
