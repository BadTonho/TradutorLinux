#include "kernel32_file_internal.hpp"

#include "tradutorlinux/util/unicode.hpp"

#include <cstring>

namespace tradutorlinux {
using namespace file_internal;

namespace {
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

[[nodiscard]] void* find_first_file_a_impl(const char* const file_name,
                                            void* const find_data) noexcept {
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
    auto it = std::find_if(g_find_slots.begin(), g_find_slots.end(),
                           [](const FindSlot& slot) { return !slot.used; });
    if (it == g_find_slots.end()) {
        closedir(dir);
        set_last_error(abi::kErrorNotEnoughMemory);
        return kInvalidHandleValue;
    }
    it->used = true;
    it->header = {runtime::HandleObjectType::Find, 1};
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
}  // namespace

extern "C" {
TL_MSABI void* tl_FindFirstFileA(const char* file_name, void* find_data) noexcept {
    if (file_name == nullptr || find_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
    }
    return find_first_file_a_impl(file_name, find_data);
}
TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept {
    FindSlot* slot = find_slot_for_handle(handle);
    if (slot == nullptr || !slot->used || find_data == nullptr) {
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
            Win32FindDataA data{};
            std::strncpy(data.c_file_name, entry->d_name, sizeof(data.c_file_name) - 1);
            std::string full_path = slot->directory + "/" + entry->d_name;
            struct stat st{};
            if (stat(full_path.c_str(), &st) == 0) {
                data.dw_file_attributes = stat_to_win32_attributes(full_path.c_str(), st);
                data.n_file_size_low = static_cast<std::uint32_t>(st.st_size & 0xFFFFFFFFU);
                data.n_file_size_high = static_cast<std::uint32_t>(st.st_size >> 32);
                GuestFileTime creation{};
                GuestFileTime access{};
                GuestFileTime write{};
                filetime_from_unix(st.st_ctim.tv_sec, creation);
                filetime_from_unix(st.st_atim.tv_sec, access);
                filetime_from_unix(st.st_mtim.tv_sec, write);
                data.ft_creation_time_lo = creation.low;
                data.ft_creation_time_hi = creation.high;
                data.ft_last_access_time_lo = access.low;
                data.ft_last_access_time_hi = access.high;
                data.ft_last_write_time_lo = write.low;
                data.ft_last_write_time_hi = write.high;
            }
            if (runtime::write_guest_memory(find_data, &data, sizeof(data)).status !=
                runtime::GuestMemoryAccessStatus::Success) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
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
    if (slot->header.ref_count > 1) {
        --slot->header.ref_count;
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (slot->dir != nullptr) {
        closedir(slot->dir);
    }
    *slot = {};
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
TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept {
    if (path == nullptr || find_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    std::string utf8_path;
    if (!wide_path_to_string(path, utf8_path)) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    }
    Win32FindDataA ansi{};
    void* const handle = find_first_file_a_impl(utf8_path.c_str(), &ansi);
    if (handle == reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max())) {
        return handle;
    }
    LegacyFindDataW wide{};
    convert_find_data(ansi, wide);
    if (runtime::write_guest_memory(find_data, &wide, sizeof(wide)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        tl_FindClose(handle);
        set_last_error(abi::kErrorInvalidParameter);
        return kInvalidHandleValue;
    }
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
    if (find_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    Win32FindDataA ansi{};
    if (tl_FindNextFileA(handle, &ansi) == 0) {
        return 0;
    }
    LegacyFindDataW wide{};
    convert_find_data(ansi, wide);
    if (runtime::write_guest_memory(find_data, &wide, sizeof(wide)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return 1;
}
}
}  // namespace tradutorlinux
