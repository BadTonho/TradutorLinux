#pragma once

#include <cerrno>
#include <cstdint>

namespace tradutorlinux::runtime {

// Códigos Win32 comuns correspondentes aos erros do sistema host
namespace win_error {
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kFileNotFound = 2;
constexpr std::uint32_t kPathNotFound = 3;
constexpr std::uint32_t kAccessDenied = 5;
constexpr std::uint32_t kInvalidHandle = 6;
constexpr std::uint32_t kNotEnoughMemory = 8;
constexpr std::uint32_t kSharingViolation = 32;
constexpr std::uint32_t kFileExists = 80;
constexpr std::uint32_t kInvalidParameter = 87;
constexpr std::uint32_t kBrokenPipe = 109;
constexpr std::uint32_t kDiskFull = 112;
constexpr std::uint32_t kInsufficientBuffer = 122;
constexpr std::uint32_t kAlreadyExists = 183;
constexpr std::uint32_t kDirectoryNotEmpty = 145;
constexpr std::uint32_t kConnectionRefused = 10061;
constexpr std::uint32_t kTimedOut = 10060;
constexpr std::uint32_t kAddressInUse = 10048;
}  // namespace win_error

// Tradução centralizada de errno Linux para código de erro Win32
[[nodiscard]] inline std::uint32_t errno_to_win32(const int error) noexcept {
    switch (error) {
        case 0:
            return win_error::kSuccess;
        case ENOENT:
            return win_error::kFileNotFound;
        case EACCES:
        case EPERM:
        case EROFS:
            return win_error::kAccessDenied;
        case ENOMEM:
            return win_error::kNotEnoughMemory;
        case EEXIST:
            return win_error::kAlreadyExists;
        case EINVAL:
            return win_error::kInvalidParameter;
        case EBADF:
            return win_error::kInvalidHandle;
        case ENOSPC:
            return win_error::kDiskFull;
        case EBUSY:
        case ETXTBSY:
            return win_error::kSharingViolation;
        case EPIPE:
            return win_error::kBrokenPipe;
        case ENOTEMPTY:
            return win_error::kDirectoryNotEmpty;
        case EISDIR:
            return win_error::kAccessDenied;
        case ENOTDIR:
            return win_error::kPathNotFound;
#ifdef ECONNREFUSED
        case ECONNREFUSED:
            return win_error::kConnectionRefused;
#endif
#ifdef ETIMEDOUT
        case ETIMEDOUT:
            return win_error::kTimedOut;
#endif
#ifdef EADDRINUSE
        case EADDRINUSE:
            return win_error::kAddressInUse;
#endif
        default:
            return win_error::kInvalidParameter;
    }
}

}  // namespace tradutorlinux::runtime
