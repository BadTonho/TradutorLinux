#include "kernel32_file_internal.hpp"
#include "../../core/runtime_memory_state.hpp"
#include "kernel32_memory_internal.hpp"

#include <fcntl.h>
#include <sys/mman.h>

namespace tradutorlinux {
using namespace file_internal;

extern "C" {
TL_MSABI std::uint32_t tl_GetFileType(const void* const handle) noexcept {
    FileSlotGuard slot_guard(handle);
    const int fd = slot_guard.get() != nullptr ? slot_guard.get()->fd
                                                : (slot_guard.is_file_handle() ? -1 : handle_fd(handle));
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
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
    const int fd = slot != nullptr ? slot->fd : (slot_guard.is_file_handle() ? -1 : handle_fd(handle));
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
    synchronize_file_position(slot, fd);
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
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
    const int fd = slot != nullptr ? slot->fd : (slot_guard.is_file_handle() ? -1 : handle_fd(handle));
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
    synchronize_file_position(slot, fd);
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
    it->header = {runtime::HandleObjectType::File, 1};
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
    it->header = {runtime::HandleObjectType::File, 1};
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
    {
        FileSlotGuard file_guard(handle);
        if (FileSlot* const slot = file_guard.get(); slot != nullptr) {
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
        if (file_guard.is_file_handle()) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
    }
    if (runtime::ObjectHeader* header = runtime::get_object_header(handle); header != nullptr) {
        if (header->type == runtime::HandleObjectType::Find) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        if (header->ref_count > 1) {
            --header->ref_count;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
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
    if (SnapshotSlot* slot = find_snapshot_slot(handle); slot != nullptr) {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        slot->header = {};
        slot->used = false;
        slot->pids.clear();
        slot->next_index = 0;
        slot->flags = 0;
        set_last_error(abi::kErrorSuccess);
        return 1;
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
TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept {
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
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
TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept {
    FileSlotGuard slot_guard(handle);
    const int fd = slot_guard.get() != nullptr ? slot_guard.get()->fd
                                                : (slot_guard.is_file_handle() ? -1 : handle_fd(handle));
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
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
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
    if (src_handle == nullptr || src_handle == kInvalidHandleValue) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (src_handle == &kStdInputToken || src_handle == &kStdOutputToken || src_handle == &kStdErrorToken) {
        *target_handle = src_handle;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    {
        FileSlotGuard file_guard(src_handle);
        if (FileSlot* const slot = file_guard.get(); slot != nullptr) {
            ++slot->header.ref_count;
            *target_handle = src_handle;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (file_guard.is_file_handle()) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
    }
    if (runtime::ObjectHeader* header = runtime::get_object_header(src_handle); header != nullptr) {
        ++header->ref_count;
        *target_handle = src_handle;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(src_handle);
    if (addr >= kProcessHandleBase && addr < kProcessHandleBase + kProcessHandleRange) {
        *target_handle = src_handle;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
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
TL_MSABI void* tl_CreateFile2(const wchar_t* const file_name, const std::uint32_t desired_access,
                              const std::uint32_t share_mode, const std::uint32_t creation_disposition,
                              void* const create_parameters) noexcept {
    (void)create_parameters;
    return tl_CreateFileW(reinterpret_cast<const std::uint16_t*>(file_name), desired_access, share_mode, nullptr, creation_disposition, 0x80, nullptr);
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
TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept {
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
    if (slot == nullptr || slot->position < 0 || ::ftruncate(slot->fd, slot->position) != 0) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : errno_to_win32(errno));
        return 0;
    }
    slot->file_size = static_cast<std::uint64_t>(slot->position);
    set_last_error(abi::kErrorSuccess);
    return 1;
}
}
}  // namespace tradutorlinux
