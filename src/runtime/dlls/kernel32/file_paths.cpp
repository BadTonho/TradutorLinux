#include "kernel32_file_internal.hpp"

namespace tradutorlinux {
using namespace file_internal;

extern "C" {
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
TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle, std::uint16_t* buffer,
                                                    std::uint32_t buffer_length,
                                                    std::uint32_t flags) noexcept {
    if (flags != 0U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    FileSlotGuard slot_guard(handle);
    const FileSlot* const slot = slot_guard.get();
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
}
}  // namespace tradutorlinux
