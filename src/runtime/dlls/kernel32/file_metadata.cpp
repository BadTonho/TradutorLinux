#include "kernel32_file_internal.hpp"

#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {
using namespace file_internal;

namespace {

template <typename T>
bool read_guest_object(const void* const source, T& destination) noexcept {
    return runtime::read_guest_memory(source, &destination, sizeof(destination)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

template <typename T>
bool write_guest_object(void* const destination, const T& source) noexcept {
    return runtime::write_guest_memory(destination, &source, sizeof(source)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

}  // namespace

extern "C" {
TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept {
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0xFFFFFFFFU;
    }
    const std::uint64_t size = current_file_size(*slot);
    if (high_size != nullptr) {
        const std::uint32_t high = static_cast<std::uint32_t>(size >> 32);
        if (!write_guest_object(high_size, high)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0xFFFFFFFFU;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(size & 0xFFFFFFFFU);
}
TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept {
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
    constexpr std::uint32_t kAdvisoryOnly = abi::kFileAttributeNotContentIndexed;
    std::uint32_t effective_attributes = attributes;
    if (effective_attributes == 0) {
        effective_attributes = abi::kFileAttributeNormal;
    }
    // There is no content-indexing service in the runtime. Accept this
    // advisory Windows bit while keeping the POSIX-backed attributes strict.
    effective_attributes &= ~kAdvisoryOnly;
    if (effective_attributes == 0) {
        effective_attributes = abi::kFileAttributeNormal;
    }
    const std::uint32_t error = apply_win32_file_attributes(normalized, effective_attributes);
    set_last_error(error);
    trace_filesystem("set-attributes", error == abi::kErrorSuccess ? "success" : "failed",
                     std::to_string(attributes));
    return error == abi::kErrorSuccess ? 1 : 0;
}
TL_MSABI int tl_GetVolumeInformationA(const char*, char* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      char* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept {
    if (volume_name_buffer != nullptr && volume_name_size > 0 && mapped_guest_range(volume_name_buffer, volume_name_size, true)) {
        const char value[] = "Local Disk";
        const std::size_t length = std::min<std::size_t>(std::size(value), volume_name_size) - 1U;
        if (runtime::write_guest_memory(volume_name_buffer, value, length + 1U).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (volume_serial_number != nullptr && mapped_guest_range(volume_serial_number, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 0x12345678U;
        if (!write_guest_object(volume_serial_number, value)) return 0;
    }
    if (maximum_component_length != nullptr && mapped_guest_range(maximum_component_length, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 255;
        if (!write_guest_object(maximum_component_length, value)) return 0;
    }
    if (file_system_flags != nullptr && mapped_guest_range(file_system_flags, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 0x00000002U | 0x00000004U;
        if (!write_guest_object(file_system_flags, value)) return 0;
    }
    if (file_system_name_buffer != nullptr && file_system_name_size > 0 && mapped_guest_range(file_system_name_buffer, file_system_name_size, true)) {
        const char value[] = "NTFS";
        const std::size_t length = std::min<std::size_t>(std::size(value), file_system_name_size) - 1U;
        if (runtime::write_guest_memory(file_system_name_buffer, value, length + 1U).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
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
        if (runtime::write_guest_memory(volume_name_buffer, u16.data(), len * sizeof(char16_t)).status !=
                runtime::GuestMemoryAccessStatus::Success ||
            runtime::write_guest_memory(volume_name_buffer + len, "\0", sizeof(char16_t)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    if (volume_serial_number != nullptr && mapped_guest_range(volume_serial_number, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 0x12345678U;
        if (!write_guest_object(volume_serial_number, value)) return 0;
    }
    if (maximum_component_length != nullptr && mapped_guest_range(maximum_component_length, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 255;
        if (!write_guest_object(maximum_component_length, value)) return 0;
    }
    if (file_system_flags != nullptr && mapped_guest_range(file_system_flags, sizeof(std::uint32_t), true)) {
        const std::uint32_t value = 0x00000002U | 0x00000004U;
        if (!write_guest_object(file_system_flags, value)) return 0;
    }
    if (file_system_name_buffer != nullptr && file_system_name_size > 0 && mapped_guest_range(file_system_name_buffer, file_system_name_size * sizeof(std::uint16_t), true)) {
        const std::u16string u16 = util::utf8_to_wide("NTFS");
        const std::size_t len = std::min<std::size_t>(u16.size(), file_system_name_size - 1);
        if (runtime::write_guest_memory(file_system_name_buffer, u16.data(), len * sizeof(char16_t)).status !=
                runtime::GuestMemoryAccessStatus::Success ||
            runtime::write_guest_memory(file_system_name_buffer + len, "\0", sizeof(char16_t)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* file_size) noexcept {
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
    if (slot == nullptr || file_size == nullptr) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    const std::int64_t value = static_cast<std::int64_t>(current_file_size(*slot));
    if (!write_guest_object(file_size, value)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI std::uint32_t tl_GetCompressedFileSizeW(const std::uint16_t* const file_name,
                                                 std::uint32_t* const high) noexcept {
    return tl_GetFileSize(file_name != nullptr ? reinterpret_cast<void*>(0x1) : nullptr, high);
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
    LegacyFileAttributeData result{};
    result.attributes = stat_to_win32_attributes(normalized.c_str(), st);
    write_filetime(st.st_ctim, result.creation);
    write_filetime(st.st_atim, result.last_access);
    write_filetime(st.st_mtim, result.last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    result.size_low = static_cast<std::uint32_t>(file_size);
    result.size_high = static_cast<std::uint32_t>(file_size >> 32U);
    if (!write_guest_object(data, result)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time, void* access_time,
                            void* write_time) noexcept {
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
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
    LegacyFileTime creation{};
    LegacyFileTime access{};
    LegacyFileTime write{};
    if (creation_time != nullptr) {
        write_filetime(st.st_ctim, creation);
    }
    if (access_time != nullptr) {
        write_filetime(st.st_atim, access);
    }
    if (write_time != nullptr) {
        write_filetime(st.st_mtim, write);
    }
    if ((creation_time != nullptr && !write_guest_object(creation_time, creation)) ||
        (access_time != nullptr && !write_guest_object(access_time, access)) ||
        (write_time != nullptr && !write_guest_object(write_time, write))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept {
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
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
    LegacyFileTime access{};
    LegacyFileTime write{};
    if (access_time != nullptr && !read_guest_object(access_time, access)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (write_time != nullptr && !read_guest_object(write_time, write)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (access_time != nullptr && !filetime_to_timespec(access, times[0])) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (write_time != nullptr && !filetime_to_timespec(write, times[1])) {
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
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
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
    LegacyByHandleFileInformation result{};
    result.attributes = stat_to_win32_attributes(slot->path.c_str(), st);
    write_filetime(st.st_ctim, result.creation);
    write_filetime(st.st_atim, result.last_access);
    write_filetime(st.st_mtim, result.last_write);
    const auto file_size = static_cast<std::uint64_t>(st.st_size);
    const auto inode = static_cast<std::uint64_t>(st.st_ino);
    result.volume_serial_number = static_cast<std::uint32_t>(st.st_dev);
    result.size_low = static_cast<std::uint32_t>(file_size);
    result.size_high = static_cast<std::uint32_t>(file_size >> 32U);
    result.number_of_links = static_cast<std::uint32_t>(st.st_nlink);
    result.file_index_low = static_cast<std::uint32_t>(inode);
    result.file_index_high = static_cast<std::uint32_t>(inode >> 32U);
    if (!write_guest_object(information, result)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, int info_class,
                                             void* buffer, std::uint32_t size) noexcept {
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
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
    const LegacyBasicFileInformation result{
        filetime_ticks(st.st_ctim), filetime_ticks(st.st_atim), filetime_ticks(st.st_mtim),
        filetime_ticks(st.st_ctim), stat_to_win32_attributes(slot->path.c_str(), st), 0};
    if (!write_guest_object(buffer, result)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}
TL_MSABI int tl_SetFileInformationByHandle(const void* const handle, const int info_class,
                                           const void* const buffer,
                                           const std::uint32_t size) noexcept {
    FileSlotGuard slot_guard(handle);
    FileSlot* const slot = slot_guard.get();
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
        abi::GuestFileBasicInfo info{};
        if (!read_guest_object(buffer, info)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
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
        abi::GuestFileDispositionInfo info{};
        if (!read_guest_object(buffer, info)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        delete_file = info.delete_file != 0;
    } else if (info_class == static_cast<int>(abi::kFileDispositionInfoEx)) {
        if (size < sizeof(abi::GuestFileDispositionInfoEx) ||
            !mapped_guest_range(buffer, sizeof(abi::GuestFileDispositionInfoEx), false)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        abi::GuestFileDispositionInfoEx info{};
        if (!read_guest_object(buffer, info)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const std::uint32_t flags = info.flags;
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
}
}  // namespace tradutorlinux
