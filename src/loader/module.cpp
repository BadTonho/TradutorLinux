#include "tradutorlinux/loader/module.hpp"

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/runtime/ws2_32.hpp"
#include "tradutorlinux/runtime/wininet.hpp"
#include "tradutorlinux/runtime/wintrust.hpp"
#include "tradutorlinux/runtime/crypt32.hpp"
#include "tradutorlinux/runtime/ole32.hpp"
#include "tradutorlinux/runtime/oleaut32.hpp"
#include "tradutorlinux/runtime/shlwapi.hpp"
#include "tradutorlinux/runtime/version.hpp"
#include "tradutorlinux/runtime/dwmapi.hpp"
#include "tradutorlinux/runtime/winmm.hpp"
#include "tradutorlinux/runtime/comctl32.hpp"
#include "tradutorlinux/runtime/comdlg32.hpp"
#include "tradutorlinux/runtime/imm32.hpp"
#include "tradutorlinux/runtime/psapi.hpp"
#include "tradutorlinux/runtime/mpr.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace tradutorlinux::loader {
namespace {

struct OwnedExport {
    std::string name;
    std::uint16_t ordinal{};
    std::uintptr_t address{};
};

struct OwnedModule {
    std::string name;
    std::vector<OwnedExport> exports;
};

std::vector<OwnedModule>& modules() {
    static std::vector<OwnedModule> instance;
    return instance;
}

std::mutex& modules_mutex() {
    static std::mutex instance;
    return instance;
}

// Requer que modules_mutex() esteja bloqueado pelo chamador.
const OwnedModule* find_module_locked(const std::string_view dll) {
    const std::vector<OwnedModule>& registry = modules();
    const auto found = std::find_if(registry.begin(), registry.end(), [&](const OwnedModule& module) {
        return util::ascii_iequals(module.name, dll);
    });
    if (found == registry.end()) {
        return nullptr;
    }
    return &*found;
}

}  // namespace

bool is_api_set_dll(const std::string_view dll) noexcept {
    if (dll.size() < 11) {
        return false;
    }
    // api-ms-win-* (11 chars) e ext-ms-win-* (11 chars), case-insensitive.
    auto starts_with_ci = [](std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
        }
        return true;
    };
    return starts_with_ci(dll, "api-ms-win-") || starts_with_ci(dll, "ext-ms-win-");
}

bool is_kernelbase_dll(const std::string_view dll) noexcept {
    return util::ascii_iequals(dll, "kernelbase.dll") ||
           util::ascii_iequals(dll, "kernelbase");
}

ExportLookup find_export_forwarded(const ExportQuery& query) {
    ExportLookup direct = find_export(query);
    if (direct.found) {
        return direct;
    }
    // KERNELBASE é o host real de grande parte do KERNEL32 em Windows 7+.
    if (is_kernelbase_dll(query.dll)) {
        ExportLookup k32 = find_export(ExportQuery{"KERNEL32.dll", query.symbol});
        if (k32.found) return k32;
    }
    if (!is_api_set_dll(query.dll)) {
        return direct;
    }
    // Ordem de tentativa espelha Wine: esgotar os provedores reais mais comuns.
    // KERNEL32/KERNELBASE cobrem file, memory, process, synch, etc.
    static constexpr std::string_view kCandidates[] = {
        "KERNEL32.dll", "USER32.dll",  "GDI32.dll",   "ADVAPI32.dll", "WS2_32.dll",
        "SHELL32.dll",  "ole32.dll",   "SHLWAPI.dll", "version.dll",  "WINMM.dll",
        "COMCTL32.dll", "COMDLG32.dll","IMM32.dll",   "PSAPI.dll",    "msvcrt.dll",
    };
    for (const auto& cand : kCandidates) {
        ExportLookup cand_lookup = find_export(ExportQuery{cand, query.symbol});
        if (cand_lookup.found) {
            return cand_lookup;
        }
    }
    return direct;
}

ExportLookup find_export_by_ordinal_forwarded(const std::string_view dll,
                                              const std::uint16_t ordinal) {
    ExportLookup direct = find_export_by_ordinal(dll, ordinal);
    if (direct.found) return direct;
    if (is_kernelbase_dll(dll)) {
        ExportLookup k32 = find_export_by_ordinal("KERNEL32.dll", ordinal);
        if (k32.found) return k32;
    }
    if (!is_api_set_dll(dll)) return direct;
    static constexpr std::string_view kCandidates[] = {
        "KERNEL32.dll", "USER32.dll",  "GDI32.dll",   "ADVAPI32.dll", "WS2_32.dll",
        "SHELL32.dll",  "ole32.dll",   "SHLWAPI.dll", "version.dll",  "WINMM.dll",
        "COMCTL32.dll", "COMDLG32.dll","IMM32.dll",   "PSAPI.dll",    "msvcrt.dll",
    };
    for (const auto& cand : kCandidates) {
        ExportLookup cand_lookup = find_export_by_ordinal(cand, ordinal);
        if (cand_lookup.found) return cand_lookup;
    }
    return direct;
}

bool is_module_registered_forwarded(const std::string_view dll) noexcept {
    if (is_module_registered(dll)) return true;
    if (is_kernelbase_dll(dll)) return is_module_registered("KERNEL32.dll");
    if (!is_api_set_dll(dll)) return false;
    // API Set só é considerado registrado se ao menos um provedor real existir.
    // Evita classificar como unknown-symbol quando registry está vazio (ex: testes com clear_modules).
    return is_module_registered("KERNEL32.dll") || is_module_registered("USER32.dll") ||
           is_module_registered("GDI32.dll") || is_module_registered("ADVAPI32.dll") ||
           is_module_registered("WS2_32.dll") || is_module_registered("SHELL32.dll") ||
           is_module_registered("ole32.dll") || is_module_registered("SHLWAPI.dll") ||
           is_module_registered("version.dll") || is_module_registered("WINMM.dll") ||
           is_module_registered("COMCTL32.dll") || is_module_registered("COMDLG32.dll") ||
           is_module_registered("IMM32.dll") || is_module_registered("PSAPI.dll") ||
           is_module_registered("msvcrt.dll");
}

bool register_module(const InternalModule& module) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    if (find_module_locked(module.name) != nullptr) {
        return false;
    }
    OwnedModule owned;
    owned.name = std::string{module.name};
    owned.exports.reserve(module.exports.size());
    for (const ExportedFunction& export_ : module.exports) {
        owned.exports.push_back(OwnedExport{
            .name = std::string{export_.name},
            .ordinal = export_.ordinal,
            .address = export_.address,
        });
    }
    modules().push_back(std::move(owned));
    return true;
}

void clear_modules() {
    std::lock_guard<std::mutex> lock(modules_mutex());
    modules().clear();
}

void register_builtin_modules() {
    static const ExportedFunction kKernel32Exports[] = {
        {"GetStdHandle", 1, reinterpret_cast<std::uintptr_t>(&tl_GetStdHandle)},
        {"WriteFile", 2, reinterpret_cast<std::uintptr_t>(&tl_WriteFile)},
        {"ReadFile", 3, reinterpret_cast<std::uintptr_t>(&tl_ReadFile)},
        {"ExitProcess", 4, reinterpret_cast<std::uintptr_t>(&tl_ExitProcess)},
        {"GetLastError", 5, reinterpret_cast<std::uintptr_t>(&tl_GetLastError)},
        {"SetLastError", 6, reinterpret_cast<std::uintptr_t>(&tl_SetLastError)},
        {"VirtualAlloc", 7, reinterpret_cast<std::uintptr_t>(&tl_VirtualAlloc)},
        {"VirtualFree", 8, reinterpret_cast<std::uintptr_t>(&tl_VirtualFree)},
        {"CreateFileA", 9, reinterpret_cast<std::uintptr_t>(&tl_CreateFileA)},
        {"CreateFileW", 69, reinterpret_cast<std::uintptr_t>(&tl_CreateFileW)},
        {"CloseHandle", 10, reinterpret_cast<std::uintptr_t>(&tl_CloseHandle)},
        {"DeleteCriticalSection", 11, reinterpret_cast<std::uintptr_t>(&tl_DeleteCriticalSection)},
        {"EnterCriticalSection", 12, reinterpret_cast<std::uintptr_t>(&tl_EnterCriticalSection)},
        {"GetConsoleMode", 13, reinterpret_cast<std::uintptr_t>(&tl_GetConsoleMode)},
        {"InitializeCriticalSection", 14, reinterpret_cast<std::uintptr_t>(&tl_InitializeCriticalSection)},
        {"IsDBCSLeadByteEx", 15, reinterpret_cast<std::uintptr_t>(&tl_IsDBCSLeadByteEx)},
        {"LeaveCriticalSection", 16, reinterpret_cast<std::uintptr_t>(&tl_LeaveCriticalSection)},
        {"MultiByteToWideChar", 17, reinterpret_cast<std::uintptr_t>(&tl_MultiByteToWideChar)},
        {"SetConsoleMode", 18, reinterpret_cast<std::uintptr_t>(&tl_SetConsoleMode)},
        {"SetUnhandledExceptionFilter", 19, reinterpret_cast<std::uintptr_t>(&tl_SetUnhandledExceptionFilter)},
        {"Sleep", 20, reinterpret_cast<std::uintptr_t>(&tl_Sleep)},
        {"TlsGetValue", 21, reinterpret_cast<std::uintptr_t>(&tl_TlsGetValue)},
        {"VirtualProtect", 22, reinterpret_cast<std::uintptr_t>(&tl_VirtualProtect)},
        {"VirtualQuery", 23, reinterpret_cast<std::uintptr_t>(&tl_VirtualQuery)},
        {"WideCharToMultiByte", 24, reinterpret_cast<std::uintptr_t>(&tl_WideCharToMultiByte)},
        {"GetModuleHandleA", 25, reinterpret_cast<std::uintptr_t>(&tl_GetModuleHandleA)},
        {"GetModuleHandleW", 26, reinterpret_cast<std::uintptr_t>(&tl_GetModuleHandleW)},
        {"GetProcAddress", 27, reinterpret_cast<std::uintptr_t>(&tl_GetProcAddress)},
        {"GetCommandLineA", 28, reinterpret_cast<std::uintptr_t>(&tl_GetCommandLineA)},
        {"GetCommandLineW", 29, reinterpret_cast<std::uintptr_t>(&tl_GetCommandLineW)},
        {"GetEnvironmentVariableA", 30, reinterpret_cast<std::uintptr_t>(&tl_GetEnvironmentVariableA)},
        {"GetEnvironmentVariableW", 31, reinterpret_cast<std::uintptr_t>(&tl_GetEnvironmentVariableW)},
        {"GetProcessHeap", 32, reinterpret_cast<std::uintptr_t>(&tl_GetProcessHeap)},
        {"HeapAlloc", 33, reinterpret_cast<std::uintptr_t>(&tl_HeapAlloc)},
        {"HeapFree", 34, reinterpret_cast<std::uintptr_t>(&tl_HeapFree)},
        {"HeapReAlloc", 35, reinterpret_cast<std::uintptr_t>(&tl_HeapReAlloc)},
        {"GetTickCount64", 36, reinterpret_cast<std::uintptr_t>(&tl_GetTickCount64)},
        {"GetSystemTimeAsFileTime", 37, reinterpret_cast<std::uintptr_t>(&tl_GetSystemTimeAsFileTime)},
        {"GetFileSize", 38, reinterpret_cast<std::uintptr_t>(&tl_GetFileSize)},
        {"SetFilePointer", 39, reinterpret_cast<std::uintptr_t>(&tl_SetFilePointer)},
        {"GetFileAttributesA", 40, reinterpret_cast<std::uintptr_t>(&tl_GetFileAttributesA)},
        {"DeleteFileA", 41, reinterpret_cast<std::uintptr_t>(&tl_DeleteFileA)},
        {"MoveFileA", 42, reinterpret_cast<std::uintptr_t>(&tl_MoveFileA)},
        {"CreateDirectoryA", 43, reinterpret_cast<std::uintptr_t>(&tl_CreateDirectoryA)},
        {"FindFirstFileA", 44, reinterpret_cast<std::uintptr_t>(&tl_FindFirstFileA)},
        {"FindNextFileA", 45, reinterpret_cast<std::uintptr_t>(&tl_FindNextFileA)},
        {"FindClose", 46, reinterpret_cast<std::uintptr_t>(&tl_FindClose)},
        {"GetCurrentDirectoryA", 47, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentDirectoryA)},
        {"GetCurrentDirectoryW", 48, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentDirectoryW)},
        {"GetModuleFileNameA", 49, reinterpret_cast<std::uintptr_t>(&tl_GetModuleFileNameA)},
        {"GetModuleFileNameW", 193, reinterpret_cast<std::uintptr_t>(&tl_GetModuleFileNameW)},
        // Fase 11: Concorrência.
        {"TlsAlloc", 50, reinterpret_cast<std::uintptr_t>(&tl_TlsAlloc)},
        {"TlsSetValue", 51, reinterpret_cast<std::uintptr_t>(&tl_TlsSetValue)},
        {"TlsFree", 52, reinterpret_cast<std::uintptr_t>(&tl_TlsFree)},
        {"CreateThread", 53, reinterpret_cast<std::uintptr_t>(&tl_CreateThread)},
        {"ExitThread", 54, reinterpret_cast<std::uintptr_t>(&tl_ExitThread)},
        {"WaitForSingleObject", 55, reinterpret_cast<std::uintptr_t>(&tl_WaitForSingleObject)},
        {"GetCurrentThreadId", 56, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentThreadId)},
        {"GetCurrentProcessId", 57, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentProcessId)},
        // Fase 10+: variantes wide do sistema de arquivos e console.
        {"GetFileAttributesW", 58, reinterpret_cast<std::uintptr_t>(&tl_GetFileAttributesW)},
        {"FindFirstFileW", 59, reinterpret_cast<std::uintptr_t>(&tl_FindFirstFileW)},
        {"FindNextFileW", 60, reinterpret_cast<std::uintptr_t>(&tl_FindNextFileW)},
        {"FormatMessageW", 61, reinterpret_cast<std::uintptr_t>(&tl_FormatMessageW)},
        {"FormatMessageA", 237, reinterpret_cast<std::uintptr_t>(&tl_FormatMessageA)},
        {"AreFileApisANSI", 238, reinterpret_cast<std::uintptr_t>(&tl_AreFileApisANSI)},
        {"GetConsoleOutputCP", 62, reinterpret_cast<std::uintptr_t>(&tl_GetConsoleOutputCP)},
        {"GetTempFileNameW", 63, reinterpret_cast<std::uintptr_t>(&tl_GetTempFileNameW)},
        {"LocalFree", 64, reinterpret_cast<std::uintptr_t>(&tl_LocalFree)},
        {"SetConsoleOutputCP", 65, reinterpret_cast<std::uintptr_t>(&tl_SetConsoleOutputCP)},
        {"CreateMutexA", 66, reinterpret_cast<std::uintptr_t>(&tl_CreateMutexA)},
        {"GetStartupInfoA", 67, reinterpret_cast<std::uintptr_t>(&tl_GetStartupInfoA)},
        {"MulDiv", 68, reinterpret_cast<std::uintptr_t>(&tl_MulDiv)},
        {"GetFileSizeEx", 70, reinterpret_cast<std::uintptr_t>(&tl_GetFileSizeEx)},
        {"SetFilePointerEx", 71, reinterpret_cast<std::uintptr_t>(&tl_SetFilePointerEx)},
        {"SetEndOfFile", 72, reinterpret_cast<std::uintptr_t>(&tl_SetEndOfFile)},
        {"FlushFileBuffers", 73, reinterpret_cast<std::uintptr_t>(&tl_FlushFileBuffers)},
        {"GetFileAttributesExW", 74, reinterpret_cast<std::uintptr_t>(&tl_GetFileAttributesExW)},
        {"DeleteFileW", 75, reinterpret_cast<std::uintptr_t>(&tl_DeleteFileW)},
        {"MoveFileW", 76, reinterpret_cast<std::uintptr_t>(&tl_MoveFileW)},
        {"MoveFileExW", 77, reinterpret_cast<std::uintptr_t>(&tl_MoveFileExW)},
        {"CopyFileW", 78, reinterpret_cast<std::uintptr_t>(&tl_CopyFileW)},
        {"CreateDirectoryW", 79, reinterpret_cast<std::uintptr_t>(&tl_CreateDirectoryW)},
        {"RemoveDirectoryW", 80, reinterpret_cast<std::uintptr_t>(&tl_RemoveDirectoryW)},
        {"GetTempPathW", 81, reinterpret_cast<std::uintptr_t>(&tl_GetTempPathW)},
        {"GetFullPathNameW", 82, reinterpret_cast<std::uintptr_t>(&tl_GetFullPathNameW)},
        {"GetFullPathNameA", 171, reinterpret_cast<std::uintptr_t>(&tl_GetFullPathNameA)},
        {"GetFileTime", 83, reinterpret_cast<std::uintptr_t>(&tl_GetFileTime)},
        {"SetFileTime", 84, reinterpret_cast<std::uintptr_t>(&tl_SetFileTime)},
        {"GetFileInformationByHandle", 85,
         reinterpret_cast<std::uintptr_t>(&tl_GetFileInformationByHandle)},
        {"GetFileInformationByHandleEx", 86,
         reinterpret_cast<std::uintptr_t>(&tl_GetFileInformationByHandleEx)},
        {"GetFinalPathNameByHandleW", 87,
         reinterpret_cast<std::uintptr_t>(&tl_GetFinalPathNameByHandleW)},
        {"FindResourceW", 88, reinterpret_cast<std::uintptr_t>(&tl_FindResourceW)},
        {"LoadResource", 89, reinterpret_cast<std::uintptr_t>(&tl_LoadResource)},
        {"LockResource", 90, reinterpret_cast<std::uintptr_t>(&tl_LockResource)},
        {"SizeofResource", 91, reinterpret_cast<std::uintptr_t>(&tl_SizeofResource)},
        {"CreateMutexW", 92, reinterpret_cast<std::uintptr_t>(&tl_CreateMutexW)},
        {"CreateEventA", 93, reinterpret_cast<std::uintptr_t>(&tl_CreateEventA)},
        {"CreateEventW", 94, reinterpret_cast<std::uintptr_t>(&tl_CreateEventW)},
        {"SetEvent", 95, reinterpret_cast<std::uintptr_t>(&tl_SetEvent)},
        {"ResetEvent", 96, reinterpret_cast<std::uintptr_t>(&tl_ResetEvent)},
        {"ReleaseMutex", 101, reinterpret_cast<std::uintptr_t>(&tl_ReleaseMutex)},
        {"CreateProcessW", 102, reinterpret_cast<std::uintptr_t>(&tl_CreateProcessW)},
        {"GetExitCodeProcess", 103,
         reinterpret_cast<std::uintptr_t>(&tl_GetExitCodeProcess)},
        {"TerminateProcess", 104, reinterpret_cast<std::uintptr_t>(&tl_TerminateProcess)},
        {"CreateSemaphoreA", 97, reinterpret_cast<std::uintptr_t>(&tl_CreateSemaphoreA)},
        {"CreateSemaphoreW", 98, reinterpret_cast<std::uintptr_t>(&tl_CreateSemaphoreW)},
        {"ReleaseSemaphore", 99, reinterpret_cast<std::uintptr_t>(&tl_ReleaseSemaphore)},
        {"WaitForMultipleObjects", 100,
         reinterpret_cast<std::uintptr_t>(&tl_WaitForMultipleObjects)},
        {"QueryPerformanceCounter", 105, reinterpret_cast<std::uintptr_t>(&tl_QueryPerformanceCounter)},
        {"QueryPerformanceFrequency", 106, reinterpret_cast<std::uintptr_t>(&tl_QueryPerformanceFrequency)},
        {"GetSystemInfo", 107, reinterpret_cast<std::uintptr_t>(&tl_GetSystemInfo)},
        {"GetNativeSystemInfo", 108, reinterpret_cast<std::uintptr_t>(&tl_GetNativeSystemInfo)},
        {"GlobalMemoryStatusEx", 109, reinterpret_cast<std::uintptr_t>(&tl_GlobalMemoryStatusEx)},
        {"CreateFileMappingA", 110, reinterpret_cast<std::uintptr_t>(&tl_CreateFileMappingA)},
        {"CreateFileMappingW", 111, reinterpret_cast<std::uintptr_t>(&tl_CreateFileMappingW)},
        {"MapViewOfFile", 112, reinterpret_cast<std::uintptr_t>(&tl_MapViewOfFile)},
        {"UnmapViewOfFile", 113, reinterpret_cast<std::uintptr_t>(&tl_UnmapViewOfFile)},
        {"FlushViewOfFile", 114, reinterpret_cast<std::uintptr_t>(&tl_FlushViewOfFile)},
        {"GetDiskFreeSpaceExA", 115, reinterpret_cast<std::uintptr_t>(&tl_GetDiskFreeSpaceExA)},
        {"GetDiskFreeSpaceExW", 116, reinterpret_cast<std::uintptr_t>(&tl_GetDiskFreeSpaceExW)},
        {"GetDriveTypeA", 117, reinterpret_cast<std::uintptr_t>(&tl_GetDriveTypeA)},
        {"GetDriveTypeW", 118, reinterpret_cast<std::uintptr_t>(&tl_GetDriveTypeW)},
        {"GetVolumeInformationA", 119, reinterpret_cast<std::uintptr_t>(&tl_GetVolumeInformationA)},
        {"GetVolumeInformationW", 120, reinterpret_cast<std::uintptr_t>(&tl_GetVolumeInformationW)},
        {"GetSystemTime", 121, reinterpret_cast<std::uintptr_t>(&tl_GetSystemTime)},
        {"GetLocalTime", 122, reinterpret_cast<std::uintptr_t>(&tl_GetLocalTime)},
        {"FileTimeToSystemTime", 123, reinterpret_cast<std::uintptr_t>(&tl_FileTimeToSystemTime)},
        {"SystemTimeToFileTime", 124, reinterpret_cast<std::uintptr_t>(&tl_SystemTimeToFileTime)},
        {"CompareStringA", 125, reinterpret_cast<std::uintptr_t>(&tl_CompareStringA)},
        {"CompareStringW", 126, reinterpret_cast<std::uintptr_t>(&tl_CompareStringW)},
        {"GetUserDefaultLCID", 127, reinterpret_cast<std::uintptr_t>(&tl_GetUserDefaultLCID)},
        {"GetSystemDefaultLCID", 128, reinterpret_cast<std::uintptr_t>(&tl_GetSystemDefaultLCID)},
        {"GetComputerNameA", 129, reinterpret_cast<std::uintptr_t>(&tl_GetComputerNameA)},
        {"GetComputerNameW", 130, reinterpret_cast<std::uintptr_t>(&tl_GetComputerNameW)},
        {"InitializeSRWLock", 131, reinterpret_cast<std::uintptr_t>(&tl_InitializeSRWLock)},
        {"AcquireSRWLockExclusive", 132, reinterpret_cast<std::uintptr_t>(&tl_AcquireSRWLockExclusive)},
        {"ReleaseSRWLockExclusive", 133, reinterpret_cast<std::uintptr_t>(&tl_ReleaseSRWLockExclusive)},
        {"AcquireSRWLockShared", 134, reinterpret_cast<std::uintptr_t>(&tl_AcquireSRWLockShared)},
        {"ReleaseSRWLockShared", 135, reinterpret_cast<std::uintptr_t>(&tl_ReleaseSRWLockShared)},
        {"SleepConditionVariableSRW", 136, reinterpret_cast<std::uintptr_t>(&tl_SleepConditionVariableSRW)},
        {"WakeConditionVariable", 137, reinterpret_cast<std::uintptr_t>(&tl_WakeConditionVariable)},
        {"WakeAllConditionVariable", 138, reinterpret_cast<std::uintptr_t>(&tl_WakeAllConditionVariable)},
        {"AddVectoredExceptionHandler", 139, reinterpret_cast<std::uintptr_t>(&tl_AddVectoredExceptionHandler)},
        {"RemoveVectoredExceptionHandler", 140, reinterpret_cast<std::uintptr_t>(&tl_RemoveVectoredExceptionHandler)},
        {"RaiseException", 141, reinterpret_cast<std::uintptr_t>(&tl_RaiseException)},
        {"GetPrivateProfileStringA", 142, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileStringA)},
        {"GetPrivateProfileStringW", 143, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileStringW)},
        {"GetPrivateProfileIntA", 144, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileIntA)},
        {"GetPrivateProfileIntW", 145, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileIntW)},
        {"WritePrivateProfileStringA", 146, reinterpret_cast<std::uintptr_t>(&tl_WritePrivateProfileStringA)},
        {"WritePrivateProfileStringW", 147, reinterpret_cast<std::uintptr_t>(&tl_WritePrivateProfileStringW)},
        {"GetPrivateProfileSectionA", 148, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileSectionA)},
        {"GetPrivateProfileSectionW", 149, reinterpret_cast<std::uintptr_t>(&tl_GetPrivateProfileSectionW)},
        {"GetConsoleScreenBufferInfo", 150, reinterpret_cast<std::uintptr_t>(&tl_GetConsoleScreenBufferInfo)},
        {"SetConsoleTextAttribute", 151, reinterpret_cast<std::uintptr_t>(&tl_SetConsoleTextAttribute)},
        {"CreateThreadpoolWork", 152, reinterpret_cast<std::uintptr_t>(&tl_CreateThreadpoolWork)},
        {"SubmitThreadpoolWork", 153, reinterpret_cast<std::uintptr_t>(&tl_SubmitThreadpoolWork)},
        {"WaitForThreadpoolWorkCallbacks", 154, reinterpret_cast<std::uintptr_t>(&tl_WaitForThreadpoolWorkCallbacks)},
        {"CloseThreadpoolWork", 155, reinterpret_cast<std::uintptr_t>(&tl_CloseThreadpoolWork)},
        {"CreateThreadpoolTimer", 156, reinterpret_cast<std::uintptr_t>(&tl_CreateThreadpoolTimer)},
        {"SetThreadpoolTimer", 157, reinterpret_cast<std::uintptr_t>(&tl_SetThreadpoolTimer)},
        {"WaitForThreadpoolTimerCallbacks", 158, reinterpret_cast<std::uintptr_t>(&tl_WaitForThreadpoolTimerCallbacks)},
        {"CloseThreadpoolTimer", 159, reinterpret_cast<std::uintptr_t>(&tl_CloseThreadpoolTimer)},
        {"ConvertThreadToFiber", 160, reinterpret_cast<std::uintptr_t>(&tl_ConvertThreadToFiber)},
        {"ConvertThreadToFiberEx", 188, reinterpret_cast<std::uintptr_t>(&tl_ConvertThreadToFiberEx)},
        {"ConvertFiberToThread", 161, reinterpret_cast<std::uintptr_t>(&tl_ConvertFiberToThread)},
        {"CreateFiber", 162, reinterpret_cast<std::uintptr_t>(&tl_CreateFiber)},
        {"CreateFiberEx", 189, reinterpret_cast<std::uintptr_t>(&tl_CreateFiberEx)},
        {"SwitchToFiber", 163, reinterpret_cast<std::uintptr_t>(&tl_SwitchToFiber)},
        {"DeleteFiber", 164, reinterpret_cast<std::uintptr_t>(&tl_DeleteFiber)},
        {"GetFiberData", 165, reinterpret_cast<std::uintptr_t>(&tl_GetFiberData)},
        {"CreateToolhelp32Snapshot", 190, reinterpret_cast<std::uintptr_t>(&tl_CreateToolhelp32Snapshot)},
        {"Process32FirstW", 191, reinterpret_cast<std::uintptr_t>(&tl_Process32FirstW)},
        {"Process32NextW", 192, reinterpret_cast<std::uintptr_t>(&tl_Process32NextW)},
        {"OpenProcess", 193, reinterpret_cast<std::uintptr_t>(&tl_OpenProcess)},
        {"HeapCreate", 166, reinterpret_cast<std::uintptr_t>(&tl_HeapCreate)},
        {"HeapDestroy", 167, reinterpret_cast<std::uintptr_t>(&tl_HeapDestroy)},
        {"HeapValidate", 168, reinterpret_cast<std::uintptr_t>(&tl_HeapValidate)},
        {"HeapSize", 169, reinterpret_cast<std::uintptr_t>(&tl_HeapSize)},
        {"HeapCompact", 170, reinterpret_cast<std::uintptr_t>(&tl_HeapCompact)},
        {"LoadLibraryA", 172, reinterpret_cast<std::uintptr_t>(&tl_LoadLibraryA)},
        {"LoadLibraryW", 173, reinterpret_cast<std::uintptr_t>(&tl_LoadLibraryW)},
        {"LoadLibraryExA", 174, reinterpret_cast<std::uintptr_t>(&tl_LoadLibraryExA)},
        {"LoadLibraryExW", 175, reinterpret_cast<std::uintptr_t>(&tl_LoadLibraryExW)},
        {"FreeLibrary", 176, reinterpret_cast<std::uintptr_t>(&tl_FreeLibrary)},
        {"GetModuleHandleExA", 177, reinterpret_cast<std::uintptr_t>(&tl_GetModuleHandleExA)},
        {"GetModuleHandleExW", 178, reinterpret_cast<std::uintptr_t>(&tl_GetModuleHandleExW)},
        {"GetVersionExA", 179, reinterpret_cast<std::uintptr_t>(&tl_GetVersionExA)},
        {"GetVersionExW", 180, reinterpret_cast<std::uintptr_t>(&tl_GetVersionExW)},
        {"VerifyVersionInfoW", 181, reinterpret_cast<std::uintptr_t>(&tl_VerifyVersionInfoW)},
        {"VerSetConditionMask", 182, reinterpret_cast<std::uintptr_t>(&tl_VerSetConditionMask)},
        {"GetUserDefaultLocaleName", 183, reinterpret_cast<std::uintptr_t>(&tl_GetUserDefaultLocaleName)},
        {"LocaleNameToLCID", 184, reinterpret_cast<std::uintptr_t>(&tl_LocaleNameToLCID)},
        {"WaitOnAddress", 185, reinterpret_cast<std::uintptr_t>(&tl_WaitOnAddress)},
        {"WakeByAddressSingle", 186, reinterpret_cast<std::uintptr_t>(&tl_WakeByAddressSingle)},
        {"WakeByAddressAll", 187, reinterpret_cast<std::uintptr_t>(&tl_WakeByAddressAll)},
        {"GetCurrentProcess", 194, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentProcess)},
        {"RtlCaptureContext", 195, reinterpret_cast<std::uintptr_t>(&tl_RtlCaptureContext)},
        {"RtlLookupFunctionEntry", 196, reinterpret_cast<std::uintptr_t>(&tl_RtlLookupFunctionEntry)},
        {"RtlVirtualUnwind", 197, reinterpret_cast<std::uintptr_t>(&tl_RtlVirtualUnwind)},
        {"RtlPcToFileHeader", 198, reinterpret_cast<std::uintptr_t>(&tl_RtlPcToFileHeader)},
        {"RtlUnwind", 199, reinterpret_cast<std::uintptr_t>(&tl_RtlUnwind)},
        {"RtlUnwindEx", 200, reinterpret_cast<std::uintptr_t>(&tl_RtlUnwindEx)},
        {"UnhandledExceptionFilter", 201, reinterpret_cast<std::uintptr_t>(&tl_UnhandledExceptionFilter)},
        {"SetEnvironmentVariableW", 202, reinterpret_cast<std::uintptr_t>(&tl_SetEnvironmentVariableW)},
        {"GetEnvironmentStringsW", 203, reinterpret_cast<std::uintptr_t>(&tl_GetEnvironmentStringsW)},
        {"FreeEnvironmentStringsW", 204, reinterpret_cast<std::uintptr_t>(&tl_FreeEnvironmentStringsW)},
        {"ExpandEnvironmentStringsW", 205, reinterpret_cast<std::uintptr_t>(&tl_ExpandEnvironmentStringsW)},
        {"FlsAlloc", 206, reinterpret_cast<std::uintptr_t>(&tl_FlsAlloc)},
        {"FlsFree", 207, reinterpret_cast<std::uintptr_t>(&tl_FlsFree)},
        {"FlsGetValue", 208, reinterpret_cast<std::uintptr_t>(&tl_FlsGetValue)},
        {"FlsSetValue", 209, reinterpret_cast<std::uintptr_t>(&tl_FlsSetValue)},
        {"GetACP", 210, reinterpret_cast<std::uintptr_t>(&tl_GetACP)},
        {"GetOEMCP", 211, reinterpret_cast<std::uintptr_t>(&tl_GetOEMCP)},
        {"GetCPInfo", 212, reinterpret_cast<std::uintptr_t>(&tl_GetCPInfo)},
        {"GetLocaleInfoW", 213, reinterpret_cast<std::uintptr_t>(&tl_GetLocaleInfoW)},
        {"LCMapStringW", 214, reinterpret_cast<std::uintptr_t>(&tl_LCMapStringW)},
        {"LCMapStringEx", 215, reinterpret_cast<std::uintptr_t>(&tl_LCMapStringEx)},
        {"GetLocaleInfoEx", 216, reinterpret_cast<std::uintptr_t>(&tl_GetLocaleInfoEx)},
        {"IsValidLocale", 217, reinterpret_cast<std::uintptr_t>(&tl_IsValidLocale)},
        {"IsValidCodePage", 218, reinterpret_cast<std::uintptr_t>(&tl_IsValidCodePage)},
        {"EnumSystemLocalesW", 219, reinterpret_cast<std::uintptr_t>(&tl_EnumSystemLocalesW)},
        {"GetStringTypeW", 220, reinterpret_cast<std::uintptr_t>(&tl_GetStringTypeW)},
        {"GetDateFormatW", 221, reinterpret_cast<std::uintptr_t>(&tl_GetDateFormatW)},
        {"GetTimeFormatW", 222, reinterpret_cast<std::uintptr_t>(&tl_GetTimeFormatW)},
        {"GetStartupInfoW", 223, reinterpret_cast<std::uintptr_t>(&tl_GetStartupInfoW)},
        {"GetSystemDirectoryW", 224, reinterpret_cast<std::uintptr_t>(&tl_GetSystemDirectoryW)},
        {"GetFileType", 225, reinterpret_cast<std::uintptr_t>(&tl_GetFileType)},
        {"SetStdHandle", 226, reinterpret_cast<std::uintptr_t>(&tl_SetStdHandle)},
        {"ReadConsoleW", 227, reinterpret_cast<std::uintptr_t>(&tl_ReadConsoleW)},
        {"WriteConsoleW", 228, reinterpret_cast<std::uintptr_t>(&tl_WriteConsoleW)},
        {"IsDebuggerPresent", 229, reinterpret_cast<std::uintptr_t>(&tl_IsDebuggerPresent)},
        {"IsProcessorFeaturePresent", 230,
         reinterpret_cast<std::uintptr_t>(&tl_IsProcessorFeaturePresent)},
        {"EncodePointer", 231, reinterpret_cast<std::uintptr_t>(&tl_EncodePointer)},
        {"DecodePointer", 232, reinterpret_cast<std::uintptr_t>(&tl_DecodePointer)},
        {"InitializeSListHead", 233,
         reinterpret_cast<std::uintptr_t>(&tl_InitializeSListHead)},
        {"FindFirstFileExW", 234, reinterpret_cast<std::uintptr_t>(&tl_FindFirstFileExW)},
        {"SetFileAttributesW", 235, reinterpret_cast<std::uintptr_t>(&tl_SetFileAttributesW)},
        {"SetFileInformationByHandle", 236,
         reinterpret_cast<std::uintptr_t>(&tl_SetFileInformationByHandle)},
        {"InitializeCriticalSectionAndSpinCount", 239,
         reinterpret_cast<std::uintptr_t>(&tl_InitializeCriticalSectionAndSpinCount)},
        {"InitializeCriticalSectionEx", 240,
         reinterpret_cast<std::uintptr_t>(&tl_InitializeCriticalSectionEx)},
        {"GlobalAlloc", 241, reinterpret_cast<std::uintptr_t>(&tl_GlobalAlloc)},
        {"GlobalLock", 242, reinterpret_cast<std::uintptr_t>(&tl_GlobalLock)},
        {"GlobalUnlock", 243, reinterpret_cast<std::uintptr_t>(&tl_GlobalUnlock)},
        {"GlobalFree", 244, reinterpret_cast<std::uintptr_t>(&tl_GlobalFree)},
        {"LocalAlloc", 245, reinterpret_cast<std::uintptr_t>(&tl_LocalAlloc)},
        {"LocalFree", 246, reinterpret_cast<std::uintptr_t>(&tl_LocalFree)},
        {"OutputDebugStringA", 247, reinterpret_cast<std::uintptr_t>(&tl_OutputDebugStringA)},
        {"OutputDebugStringW", 248, reinterpret_cast<std::uintptr_t>(&tl_OutputDebugStringW)},
        {"SetDllDirectoryW", 249, reinterpret_cast<std::uintptr_t>(&tl_SetDllDirectoryW)},
        {"VirtualQueryEx", 250, reinterpret_cast<std::uintptr_t>(&tl_VirtualQueryEx)},
        {"GetTimeZoneInformation", 251,
         reinterpret_cast<std::uintptr_t>(&tl_GetTimeZoneInformation)},
        {"GetProcessId", 252, reinterpret_cast<std::uintptr_t>(&tl_GetProcessId)},
        {"QueryFullProcessImageNameW", 253,
         reinterpret_cast<std::uintptr_t>(&tl_QueryFullProcessImageNameW)},
        {"FileTimeToLocalFileTime", 254,
         reinterpret_cast<std::uintptr_t>(&tl_FileTimeToLocalFileTime)},
        {"GetLongPathNameW", 255, reinterpret_cast<std::uintptr_t>(&tl_GetLongPathNameW)},
        {"GetShortPathNameW", 256, reinterpret_cast<std::uintptr_t>(&tl_GetShortPathNameW)},
        {"SetThreadPriority", 257, reinterpret_cast<std::uintptr_t>(&tl_SetThreadPriority)},
        {"GetProcessAffinityMask", 258,
         reinterpret_cast<std::uintptr_t>(&tl_GetProcessAffinityMask)},
        {"CreateHardLinkW", 259, reinterpret_cast<std::uintptr_t>(&tl_CreateHardLinkW)},
        {"K32GetModuleFileNameExW", 260, reinterpret_cast<std::uintptr_t>(&tl_K32GetModuleFileNameExW)},
        {"GetTickCount", 261, reinterpret_cast<std::uintptr_t>(&tl_GetTickCount)},
        {"SetCurrentDirectoryW", 262, reinterpret_cast<std::uintptr_t>(&tl_SetCurrentDirectoryW)},
        {"DeviceIoControl", 263, reinterpret_cast<std::uintptr_t>(&tl_DeviceIoControl)},
        {"FoldStringW", 264, reinterpret_cast<std::uintptr_t>(&tl_FoldStringW)},
        {"SetThreadExecutionState", 265, reinterpret_cast<std::uintptr_t>(&tl_SetThreadExecutionState)},
        {"AllocConsole", 266, reinterpret_cast<std::uintptr_t>(&tl_AllocConsole)},
        {"AttachConsole", 267, reinterpret_cast<std::uintptr_t>(&tl_AttachConsole)},
        {"FreeConsole", 268, reinterpret_cast<std::uintptr_t>(&tl_FreeConsole)},
        {"SystemTimeToTzSpecificLocalTime", 269,
         reinterpret_cast<std::uintptr_t>(&tl_SystemTimeToTzSpecificLocalTime)},
        {"IsDBCSLeadByte", 270, reinterpret_cast<std::uintptr_t>(&tl_IsDBCSLeadByte)},
        {"GetNumberFormatW", 271, reinterpret_cast<std::uintptr_t>(&tl_GetNumberFormatW)},
        {"GetVersion", 272, reinterpret_cast<std::uintptr_t>(&tl_GetVersion)},
        {"GetLargePageMinimum", 273, reinterpret_cast<std::uintptr_t>(&tl_GetLargePageMinimum)},
        {"SetFileApisToOEM", 274, reinterpret_cast<std::uintptr_t>(&tl_SetFileApisToOEM)},
        {"SetConsoleCtrlHandler", 275, reinterpret_cast<std::uintptr_t>(&tl_SetConsoleCtrlHandler)},
        {"GetProcessTimes", 276, reinterpret_cast<std::uintptr_t>(&tl_GetProcessTimes)},
        {"SetProcessAffinityMask", 277, reinterpret_cast<std::uintptr_t>(&tl_SetProcessAffinityMask)},
        {"SetThreadAffinityMask", 278, reinterpret_cast<std::uintptr_t>(&tl_SetThreadAffinityMask)},
        {"ResumeThread", 279, reinterpret_cast<std::uintptr_t>(&tl_ResumeThread)},
        {"OpenEventW", 280, reinterpret_cast<std::uintptr_t>(&tl_OpenEventW)},
        {"OpenFileMappingW", 281, reinterpret_cast<std::uintptr_t>(&tl_OpenFileMappingW)},
        {"FileTimeToDosDateTime", 282, reinterpret_cast<std::uintptr_t>(&tl_FileTimeToDosDateTime)},
        {"CompareFileTime", 283, reinterpret_cast<std::uintptr_t>(&tl_CompareFileTime)},
        {"GetDiskFreeSpaceW", 284, reinterpret_cast<std::uintptr_t>(&tl_GetDiskFreeSpaceW)},
        {"FindFirstStreamW", 285, reinterpret_cast<std::uintptr_t>(&tl_FindFirstStreamW)},
        {"FindNextStreamW", 286, reinterpret_cast<std::uintptr_t>(&tl_FindNextStreamW)},
        {"GetLogicalDriveStringsW", 287, reinterpret_cast<std::uintptr_t>(&tl_GetLogicalDriveStringsW)},
        {"SetNamedPipeHandleState", 288, reinterpret_cast<std::uintptr_t>(&tl_SetNamedPipeHandleState)},
        {"TransactNamedPipe", 289, reinterpret_cast<std::uintptr_t>(&tl_TransactNamedPipe)},
        {"WaitNamedPipeW", 290, reinterpret_cast<std::uintptr_t>(&tl_WaitNamedPipeW)},
        {"PeekNamedPipe", 291, reinterpret_cast<std::uintptr_t>(&tl_PeekNamedPipe)},
        {"WaitForSingleObjectEx", 292, reinterpret_cast<std::uintptr_t>(&tl_WaitForSingleObjectEx)},
        {"GetExitCodeThread", 293, reinterpret_cast<std::uintptr_t>(&tl_GetExitCodeThread)},
        {"TryAcquireSRWLockExclusive", 294, reinterpret_cast<std::uintptr_t>(&tl_TryAcquireSRWLockExclusive)},
        {"FreeLibraryAndExitThread", 295, reinterpret_cast<std::uintptr_t>(&tl_FreeLibraryAndExitThread)},
        {"SetThreadLocale", 296, reinterpret_cast<std::uintptr_t>(&tl_SetThreadLocale)},
        {"SetThreadUILanguage", 297, reinterpret_cast<std::uintptr_t>(&tl_SetThreadUILanguage)},
        {"GetUserDefaultUILanguage", 298, reinterpret_cast<std::uintptr_t>(&tl_GetUserDefaultUILanguage)},
        {"GetLogicalDrives", 299, reinterpret_cast<std::uintptr_t>(&tl_GetLogicalDrives)},
        {"GetPhysicallyInstalledSystemMemory", 300,
         reinterpret_cast<std::uintptr_t>(&tl_GetPhysicallyInstalledSystemMemory)},
        {"GetVolumePathNameA", 301, reinterpret_cast<std::uintptr_t>(&tl_GetVolumePathNameA)},
        {"TzSpecificLocalTimeToSystemTime", 302,
         reinterpret_cast<std::uintptr_t>(&tl_TzSpecificLocalTimeToSystemTime)},
        {"UnregisterWaitEx", 303, reinterpret_cast<std::uintptr_t>(&tl_UnregisterWaitEx)},
        {"RegisterWaitForSingleObject", 304,
         reinterpret_cast<std::uintptr_t>(&tl_RegisterWaitForSingleObject)},
        {"SetSearchPathMode", 305, reinterpret_cast<std::uintptr_t>(&tl_SetSearchPathMode)},
        {"InterlockedPushEntrySList", 306,
         reinterpret_cast<std::uintptr_t>(&tl_InterlockedPushEntrySList)},
        {"CopyFileExW", 307, reinterpret_cast<std::uintptr_t>(&tl_CopyFileExW)},
        {"MoveFileWithProgressW", 308, reinterpret_cast<std::uintptr_t>(&tl_MoveFileWithProgressW)},
        {"GetCompressedFileSizeW", 309, reinterpret_cast<std::uintptr_t>(&tl_GetCompressedFileSizeW)},
        {"FindFirstChangeNotificationW", 310,
         reinterpret_cast<std::uintptr_t>(&tl_FindFirstChangeNotificationW)},
        {"FindNextChangeNotification", 311,
         reinterpret_cast<std::uintptr_t>(&tl_FindNextChangeNotification)},
        {"FindCloseChangeNotification", 312,
         reinterpret_cast<std::uintptr_t>(&tl_FindCloseChangeNotification)},
        {"GetSystemDefaultLangID", 313, reinterpret_cast<std::uintptr_t>(&tl_GetSystemDefaultLangID)},
        {"GetUserDefaultLangID", 314, reinterpret_cast<std::uintptr_t>(&tl_GetUserDefaultLangID)},
        {"GetWindowsDirectoryW", 315, reinterpret_cast<std::uintptr_t>(&tl_GetWindowsDirectoryW)},
        {"GlobalSize", 316, reinterpret_cast<std::uintptr_t>(&tl_GlobalSize)},
        {"SetPriorityClass", 317, reinterpret_cast<std::uintptr_t>(&tl_SetPriorityClass)},
        {"lstrlenW", 318, reinterpret_cast<std::uintptr_t>(&tl_lstrlenW)},
        {"K32GetProcessMemoryInfo", 319, reinterpret_cast<std::uintptr_t>(&tl_K32GetProcessMemoryInfo)},
        {"K32GetProcessImageFileNameA", 320, reinterpret_cast<std::uintptr_t>(&tl_K32GetProcessImageFileNameA)},
        {"Process32First", 321, reinterpret_cast<std::uintptr_t>(&tl_Process32First)},
        {"Process32Next", 322, reinterpret_cast<std::uintptr_t>(&tl_Process32Next)},
        {"DuplicateHandle", 323, reinterpret_cast<std::uintptr_t>(&tl_DuplicateHandle)},
        {"LockFile", 324, reinterpret_cast<std::uintptr_t>(&tl_LockFile)},
        {"LockFileEx", 325, reinterpret_cast<std::uintptr_t>(&tl_LockFileEx)},
        {"UnlockFile", 326, reinterpret_cast<std::uintptr_t>(&tl_UnlockFile)},
        {"UnlockFileEx", 327, reinterpret_cast<std::uintptr_t>(&tl_UnlockFileEx)},
        {"GetDiskFreeSpaceA", 328, reinterpret_cast<std::uintptr_t>(&tl_GetDiskFreeSpaceA)},
        {"GetTempPathA", 329, reinterpret_cast<std::uintptr_t>(&tl_GetTempPathA)},
        {"MoveFileExA", 330, reinterpret_cast<std::uintptr_t>(&tl_MoveFileExA)},
        {"SleepEx", 331, reinterpret_cast<std::uintptr_t>(&tl_SleepEx)},
        {"WaitForMultipleObjectsEx", 332, reinterpret_cast<std::uintptr_t>(&tl_WaitForMultipleObjectsEx)},
        {"TryEnterCriticalSection", 333, reinterpret_cast<std::uintptr_t>(&tl_TryEnterCriticalSection)},
        {"InitializeConditionVariable", 334, reinterpret_cast<std::uintptr_t>(&tl_InitializeConditionVariable)},
        {"SleepConditionVariableCS", 335, reinterpret_cast<std::uintptr_t>(&tl_SleepConditionVariableCS)},
        {"FindResourceExW", 336, reinterpret_cast<std::uintptr_t>(&tl_FindResourceExW)},
        {"CompareStringEx", 337, reinterpret_cast<std::uintptr_t>(&tl_CompareStringEx)},
        {"CreateFile2", 338, reinterpret_cast<std::uintptr_t>(&tl_CreateFile2)},
        {"GetCurrentProcessorNumber", 339, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentProcessorNumber)},
        {"InitializeProcThreadAttributeList", 340, reinterpret_cast<std::uintptr_t>(&tl_InitializeProcThreadAttributeList)},
        {"UpdateProcThreadAttribute", 341, reinterpret_cast<std::uintptr_t>(&tl_UpdateProcThreadAttribute)},
        {"GetSystemDirectoryA", 342, reinterpret_cast<std::uintptr_t>(&tl_GetSystemDirectoryA)},
        {"ReadConsoleA", 343, reinterpret_cast<std::uintptr_t>(&tl_ReadConsoleA)},
        {"CreateWaitableTimerA", 344, reinterpret_cast<std::uintptr_t>(&tl_CreateWaitableTimerA)},
        {"CreateWaitableTimerW", 345, reinterpret_cast<std::uintptr_t>(&tl_CreateWaitableTimerW)},
        {"SetWaitableTimer", 346, reinterpret_cast<std::uintptr_t>(&tl_SetWaitableTimer)},
        {"CancelWaitableTimer", 347, reinterpret_cast<std::uintptr_t>(&tl_CancelWaitableTimer)},
        {"GetLogicalProcessorInformation", 348, reinterpret_cast<std::uintptr_t>(&tl_GetLogicalProcessorInformation)},
        {"GetVolumePathNameW", 349, reinterpret_cast<std::uintptr_t>(&tl_GetVolumePathNameW)},
        {"SetThreadDescription", 350, reinterpret_cast<std::uintptr_t>(&tl_SetThreadDescription)},
        {"GetCurrentThread", 351, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentThread)},
        {"CreateSemaphoreExW", 352, reinterpret_cast<std::uintptr_t>(&tl_CreateSemaphoreExW)},
        {"OpenSemaphoreW", 353, reinterpret_cast<std::uintptr_t>(&tl_OpenSemaphoreW)},
        {"CreateMutexExW", 354, reinterpret_cast<std::uintptr_t>(&tl_CreateMutexExW)},
        {"DebugBreak", 355, reinterpret_cast<std::uintptr_t>(&tl_DebugBreak)},
        {"InitOnceBeginInitialize", 356, reinterpret_cast<std::uintptr_t>(&tl_InitOnceBeginInitialize)},
        {"InitOnceComplete", 357, reinterpret_cast<std::uintptr_t>(&tl_InitOnceComplete)},
        {"SwitchToThread", 358, reinterpret_cast<std::uintptr_t>(&tl_SwitchToThread)},
        {"GetSystemFirmwareTable", 359, reinterpret_cast<std::uintptr_t>(&tl_GetSystemFirmwareTable)},
        {"__C_specific_handler", 360, reinterpret_cast<std::uintptr_t>(&tl___C_specific_handler)},
        {"Beep", 361, reinterpret_cast<std::uintptr_t>(&tl_Beep)},
        {"ClearCommBreak", 362, reinterpret_cast<std::uintptr_t>(&tl_ClearCommBreak)},
        {"ConnectNamedPipe", 363, reinterpret_cast<std::uintptr_t>(&tl_ConnectNamedPipe)},
        {"CreateNamedPipeA", 364, reinterpret_cast<std::uintptr_t>(&tl_CreateNamedPipeA)},
        {"CreatePipe", 365, reinterpret_cast<std::uintptr_t>(&tl_CreatePipe)},
        {"FindResourceA", 366, reinterpret_cast<std::uintptr_t>(&tl_FindResourceA)},
        {"GetCommState", 367, reinterpret_cast<std::uintptr_t>(&tl_GetCommState)},
        {"GetLocaleInfoA", 368, reinterpret_cast<std::uintptr_t>(&tl_GetLocaleInfoA)},
        {"GetOverlappedResult", 369, reinterpret_cast<std::uintptr_t>(&tl_GetOverlappedResult)},
        {"GetThreadTimes", 370, reinterpret_cast<std::uintptr_t>(&tl_GetThreadTimes)},
        {"GetWindowsDirectoryA", 371, reinterpret_cast<std::uintptr_t>(&tl_GetWindowsDirectoryA)},
        {"GlobalMemoryStatus", 372, reinterpret_cast<std::uintptr_t>(&tl_GlobalMemoryStatus)},
        {"LocalFileTimeToFileTime", 373, reinterpret_cast<std::uintptr_t>(&tl_LocalFileTimeToFileTime)},
        {"SetCommBreak", 374, reinterpret_cast<std::uintptr_t>(&tl_SetCommBreak)},
        {"SetCommState", 375, reinterpret_cast<std::uintptr_t>(&tl_SetCommState)},
        {"SetCommTimeouts", 376, reinterpret_cast<std::uintptr_t>(&tl_SetCommTimeouts)},
        {"SetCurrentDirectoryA", 377, reinterpret_cast<std::uintptr_t>(&tl_SetCurrentDirectoryA)},
        {"SetHandleInformation", 378, reinterpret_cast<std::uintptr_t>(&tl_SetHandleInformation)},
        {"WaitNamedPipeA", 379, reinterpret_cast<std::uintptr_t>(&tl_WaitNamedPipeA)},
        {"CreateProcessA", 380, reinterpret_cast<std::uintptr_t>(&tl_CreateProcessA)},
    };
    static const InternalModule kKernel32Module{"KERNEL32.dll", kKernel32Exports};
    register_module(kKernel32Module);
    static const ExportedFunction kUser32Exports[] = {
        {"MessageBoxA", 1, reinterpret_cast<std::uintptr_t>(&tl_MessageBoxA)},
        {"RegisterClassExA", 2, reinterpret_cast<std::uintptr_t>(&tl_RegisterClassExA)},
        {"CreateWindowExA", 3, reinterpret_cast<std::uintptr_t>(&tl_CreateWindowExA)},
        {"ShowWindow", 4, reinterpret_cast<std::uintptr_t>(&tl_ShowWindow)},
        {"UpdateWindow", 5, reinterpret_cast<std::uintptr_t>(&tl_UpdateWindow)},
        {"GetMessageA", 6, reinterpret_cast<std::uintptr_t>(&tl_GetMessageA)},
        {"TranslateMessage", 7, reinterpret_cast<std::uintptr_t>(&tl_TranslateMessage)},
        {"DispatchMessageA", 8, reinterpret_cast<std::uintptr_t>(&tl_DispatchMessageA)},
        {"DefWindowProcA", 9, reinterpret_cast<std::uintptr_t>(&tl_DefWindowProcA)},
        {"DestroyWindow", 10, reinterpret_cast<std::uintptr_t>(&tl_DestroyWindow)},
        {"PostQuitMessage", 11, reinterpret_cast<std::uintptr_t>(&tl_PostQuitMessage)},
        {"SetTimer", 12, reinterpret_cast<std::uintptr_t>(&tl_SetTimer)},
        {"KillTimer", 13, reinterpret_cast<std::uintptr_t>(&tl_KillTimer)},
        {"GetDC", 14, reinterpret_cast<std::uintptr_t>(&tl_GetDC)},
        {"ReleaseDC", 15, reinterpret_cast<std::uintptr_t>(&tl_ReleaseDC)},
        {"BeginPaint", 16, reinterpret_cast<std::uintptr_t>(&tl_BeginPaint)},
        {"EndPaint", 17, reinterpret_cast<std::uintptr_t>(&tl_EndPaint)},
        {"FillRect", 18, reinterpret_cast<std::uintptr_t>(&tl_FillRect)},
        {"RegisterClassA", 19, reinterpret_cast<std::uintptr_t>(&tl_RegisterClassA)},
        {"GetClientRect", 20, reinterpret_cast<std::uintptr_t>(&tl_GetClientRect)},
        {"GetCursorPos", 40, reinterpret_cast<std::uintptr_t>(&tl_GetCursorPos)},
        {"MoveWindow", 21, reinterpret_cast<std::uintptr_t>(&tl_MoveWindow)},
        {"SetWindowPos", 22, reinterpret_cast<std::uintptr_t>(&tl_SetWindowPos)},
        {"SetWindowTextA", 23, reinterpret_cast<std::uintptr_t>(&tl_SetWindowTextA)},
        {"GetWindowTextA", 24, reinterpret_cast<std::uintptr_t>(&tl_GetWindowTextA)},
        {"EnableWindow", 25, reinterpret_cast<std::uintptr_t>(&tl_EnableWindow)},
        {"SetFocus", 26, reinterpret_cast<std::uintptr_t>(&tl_SetFocus)},
        {"IsWindowVisible", 27, reinterpret_cast<std::uintptr_t>(&tl_IsWindowVisible)},
        {"InvalidateRect", 28, reinterpret_cast<std::uintptr_t>(&tl_InvalidateRect)},
        {"FindWindowA", 29, reinterpret_cast<std::uintptr_t>(&tl_FindWindowA)},
        {"LoadCursorA", 30, reinterpret_cast<std::uintptr_t>(&tl_LoadCursorA)},
        {"LoadIconA", 31, reinterpret_cast<std::uintptr_t>(&tl_LoadIconA)},
        {"SetClassLongPtrA", 32, reinterpret_cast<std::uintptr_t>(&tl_SetClassLongPtrA)},
        {"SetForegroundWindow", 33, reinterpret_cast<std::uintptr_t>(&tl_SetForegroundWindow)},
        {"SendMessageA", 34, reinterpret_cast<std::uintptr_t>(&tl_SendMessageA)},
        {"PostMessageA", 35, reinterpret_cast<std::uintptr_t>(&tl_PostMessageA)},
        {"CreatePopupMenu", 36, reinterpret_cast<std::uintptr_t>(&tl_CreatePopupMenu)},
        {"AppendMenuA", 37, reinterpret_cast<std::uintptr_t>(&tl_AppendMenuA)},
        {"DestroyMenu", 38, reinterpret_cast<std::uintptr_t>(&tl_DestroyMenu)},
        {"TrackPopupMenu", 39, reinterpret_cast<std::uintptr_t>(&tl_TrackPopupMenu)},
        {"GetSystemMetrics", 41, reinterpret_cast<std::uintptr_t>(&tl_GetSystemMetrics)},
        {"GetWindowLongPtrA", 42, reinterpret_cast<std::uintptr_t>(&tl_GetWindowLongPtrA)},
        {"GetWindowLongPtrW", 43, reinterpret_cast<std::uintptr_t>(&tl_GetWindowLongPtrW)},
        {"SetWindowLongPtrA", 44, reinterpret_cast<std::uintptr_t>(&tl_SetWindowLongPtrA)},
        {"SetWindowLongPtrW", 45, reinterpret_cast<std::uintptr_t>(&tl_SetWindowLongPtrW)},
        {"GetParent", 46, reinterpret_cast<std::uintptr_t>(&tl_GetParent)},
        {"SetParent", 47, reinterpret_cast<std::uintptr_t>(&tl_SetParent)},
        {"IsWindow", 48, reinterpret_cast<std::uintptr_t>(&tl_IsWindow)},
        {"MessageBoxW", 49, reinterpret_cast<std::uintptr_t>(&tl_MessageBoxW)},
        {"GetWindowDC", 50, reinterpret_cast<std::uintptr_t>(&tl_GetWindowDC)},
        {"SetCursor", 51, reinterpret_cast<std::uintptr_t>(&tl_SetCursor)},
        {"ShowCursor", 52, reinterpret_cast<std::uintptr_t>(&tl_ShowCursor)},
        {"SetCursorPos", 53, reinterpret_cast<std::uintptr_t>(&tl_SetCursorPos)},
        {"GetKeyState", 54, reinterpret_cast<std::uintptr_t>(&tl_GetKeyState)},
        {"GetAsyncKeyState", 55, reinterpret_cast<std::uintptr_t>(&tl_GetAsyncKeyState)},
        {"MsgWaitForMultipleObjects", 56, reinterpret_cast<std::uintptr_t>(&tl_MsgWaitForMultipleObjects)},
        {"MsgWaitForMultipleObjectsEx", 57, reinterpret_cast<std::uintptr_t>(&tl_MsgWaitForMultipleObjectsEx)},
        {"LoadStringA", 58, reinterpret_cast<std::uintptr_t>(&tl_LoadStringA)},
        {"LoadStringW", 59, reinterpret_cast<std::uintptr_t>(&tl_LoadStringW)},
        {"RegisterClassExW", 60, reinterpret_cast<std::uintptr_t>(&tl_RegisterClassExW)},
        {"RegisterClassW", 61, reinterpret_cast<std::uintptr_t>(&tl_RegisterClassW)},
        {"CreateWindowExW", 62, reinterpret_cast<std::uintptr_t>(&tl_CreateWindowExW)},
        {"GetMessageW", 63, reinterpret_cast<std::uintptr_t>(&tl_GetMessageW)},
        {"DispatchMessageW", 64, reinterpret_cast<std::uintptr_t>(&tl_DispatchMessageW)},
        {"DefWindowProcW", 65, reinterpret_cast<std::uintptr_t>(&tl_DefWindowProcW)},
        {"SetWindowTextW", 66, reinterpret_cast<std::uintptr_t>(&tl_SetWindowTextW)},
        {"GetWindowTextW", 67, reinterpret_cast<std::uintptr_t>(&tl_GetWindowTextW)},
        {"GetWindowTextLengthA", 68, reinterpret_cast<std::uintptr_t>(&tl_GetWindowTextLengthA)},
        {"GetWindowTextLengthW", 69, reinterpret_cast<std::uintptr_t>(&tl_GetWindowTextLengthW)},
        {"FindWindowW", 70, reinterpret_cast<std::uintptr_t>(&tl_FindWindowW)},
        {"LoadCursorW", 71, reinterpret_cast<std::uintptr_t>(&tl_LoadCursorW)},
        {"LoadIconW", 72, reinterpret_cast<std::uintptr_t>(&tl_LoadIconW)},
        {"SetClassLongPtrW", 73, reinterpret_cast<std::uintptr_t>(&tl_SetClassLongPtrW)},
        {"SendMessageW", 74, reinterpret_cast<std::uintptr_t>(&tl_SendMessageW)},
        {"PostMessageW", 75, reinterpret_cast<std::uintptr_t>(&tl_PostMessageW)},
        {"AppendMenuW", 76, reinterpret_cast<std::uintptr_t>(&tl_AppendMenuW)},
        {"DialogBoxParamW", 77, reinterpret_cast<std::uintptr_t>(&tl_DialogBoxParamW)},
        {"EndDialog", 78, reinterpret_cast<std::uintptr_t>(&tl_EndDialog)},
        {"GetDlgItem", 79, reinterpret_cast<std::uintptr_t>(&tl_GetDlgItem)},
        {"SetDlgItemTextW", 80, reinterpret_cast<std::uintptr_t>(&tl_SetDlgItemTextW)},
        {"SendDlgItemMessageW", 81, reinterpret_cast<std::uintptr_t>(&tl_SendDlgItemMessageW)},
        {"GetNextDlgTabItem", 82, reinterpret_cast<std::uintptr_t>(&tl_GetNextDlgTabItem)},
        {"IsDialogMessageW", 83, reinterpret_cast<std::uintptr_t>(&tl_IsDialogMessageW)},
        {"GetWindowRect", 84, reinterpret_cast<std::uintptr_t>(&tl_GetWindowRect)},
        {"GetWindowLongW", 85, reinterpret_cast<std::uintptr_t>(&tl_GetWindowLongW)},
        {"SetWindowLongW", 86, reinterpret_cast<std::uintptr_t>(&tl_SetWindowLongW)},
        {"CopyImage", 87, reinterpret_cast<std::uintptr_t>(&tl_CopyImage)},
        {"DestroyIcon", 88, reinterpret_cast<std::uintptr_t>(&tl_DestroyIcon)},
        {"GetDesktopWindow", 89, reinterpret_cast<std::uintptr_t>(&tl_GetDesktopWindow)},
        {"GetFocus", 90, reinterpret_cast<std::uintptr_t>(&tl_GetFocus)},
        {"SetCapture", 91, reinterpret_cast<std::uintptr_t>(&tl_SetCapture)},
        {"ReleaseCapture", 92, reinterpret_cast<std::uintptr_t>(&tl_ReleaseCapture)},
        {"GetCapture", 93, reinterpret_cast<std::uintptr_t>(&tl_GetCapture)},
        {"BringWindowToTop", 94, reinterpret_cast<std::uintptr_t>(&tl_BringWindowToTop)},
        {"GetWindow", 95, reinterpret_cast<std::uintptr_t>(&tl_GetWindow)},
        {"GetClassNameA", 96, reinterpret_cast<std::uintptr_t>(&tl_GetClassNameA)},
        {"GetClassNameW", 97, reinterpret_cast<std::uintptr_t>(&tl_GetClassNameW)},
        {"GetWindowThreadProcessId", 98,
         reinterpret_cast<std::uintptr_t>(&tl_GetWindowThreadProcessId)},
        {"CallWindowProcA", 99, reinterpret_cast<std::uintptr_t>(&tl_CallWindowProcA)},
        {"CallWindowProcW", 100, reinterpret_cast<std::uintptr_t>(&tl_CallWindowProcW)},
        {"PeekMessageA", 101, reinterpret_cast<std::uintptr_t>(&tl_PeekMessageA)},
        {"PeekMessageW", 102, reinterpret_cast<std::uintptr_t>(&tl_PeekMessageW)},
        {"RedrawWindow", 103, reinterpret_cast<std::uintptr_t>(&tl_RedrawWindow)},
        {"PtInRect", 104, reinterpret_cast<std::uintptr_t>(&tl_PtInRect)},
        {"CopyRect", 105, reinterpret_cast<std::uintptr_t>(&tl_CopyRect)},
        {"MapWindowPoints", 106, reinterpret_cast<std::uintptr_t>(&tl_MapWindowPoints)},
        {"MonitorFromWindow", 107, reinterpret_cast<std::uintptr_t>(&tl_MonitorFromWindow)},
        {"GetSysColor", 108, reinterpret_cast<std::uintptr_t>(&tl_GetSysColor)},
        {"CharUpperW", 109, reinterpret_cast<std::uintptr_t>(&tl_CharUpperW)},
        {"CharLowerW", 110, reinterpret_cast<std::uintptr_t>(&tl_CharLowerW)},
        {"DrawTextA", 111, reinterpret_cast<std::uintptr_t>(&tl_DrawTextA)},
        {"DrawTextW", 112, reinterpret_cast<std::uintptr_t>(&tl_DrawTextW)},
        {"SetUserObjectInformationW", 113,
         reinterpret_cast<std::uintptr_t>(&tl_SetUserObjectInformationW)},
        {"WaitForInputIdle", 114, reinterpret_cast<std::uintptr_t>(&tl_WaitForInputIdle)},
        {"FindWindowExW", 115, reinterpret_cast<std::uintptr_t>(&tl_FindWindowExW)},
        {"SetProcessDefaultLayout", 116,
         reinterpret_cast<std::uintptr_t>(&tl_SetProcessDefaultLayout)},
        {"OpenClipboard", 117, reinterpret_cast<std::uintptr_t>(&tl_OpenClipboard)},
        {"CloseClipboard", 118, reinterpret_cast<std::uintptr_t>(&tl_CloseClipboard)},
        {"SetClipboardData", 119, reinterpret_cast<std::uintptr_t>(&tl_SetClipboardData)},
        {"EmptyClipboard", 120, reinterpret_cast<std::uintptr_t>(&tl_EmptyClipboard)},
        {"MessageBoxExW", 121, reinterpret_cast<std::uintptr_t>(&tl_MessageBoxExW)},
        {"DrawIconEx", 122, reinterpret_cast<std::uintptr_t>(&tl_DrawIconEx)},
        {"LoadImageW", 123, reinterpret_cast<std::uintptr_t>(&tl_LoadImageW)},
        {"ClientToScreen", 124, reinterpret_cast<std::uintptr_t>(&tl_ClientToScreen)},
        {"GetMenu", 125, reinterpret_cast<std::uintptr_t>(&tl_GetMenu)},
        {"SetMenu", 126, reinterpret_cast<std::uintptr_t>(&tl_SetMenu)},
        {"GetSubMenu", 127, reinterpret_cast<std::uintptr_t>(&tl_GetSubMenu)},
        {"GetMenuItemCount", 128, reinterpret_cast<std::uintptr_t>(&tl_GetMenuItemCount)},
        {"GetMenuItemInfoW", 129, reinterpret_cast<std::uintptr_t>(&tl_GetMenuItemInfoW)},
        {"SetMenuItemInfoW", 130, reinterpret_cast<std::uintptr_t>(&tl_SetMenuItemInfoW)},
        {"InsertMenuItemW", 131, reinterpret_cast<std::uintptr_t>(&tl_InsertMenuItemW)},
        {"RemoveMenu", 132, reinterpret_cast<std::uintptr_t>(&tl_RemoveMenu)},
        {"EnableMenuItem", 133, reinterpret_cast<std::uintptr_t>(&tl_EnableMenuItem)},
        {"CheckMenuItem", 134, reinterpret_cast<std::uintptr_t>(&tl_CheckMenuItem)},
        {"CheckMenuRadioItem", 135, reinterpret_cast<std::uintptr_t>(&tl_CheckMenuRadioItem)},
        {"DrawMenuBar", 136, reinterpret_cast<std::uintptr_t>(&tl_DrawMenuBar)},
        {"TrackPopupMenuEx", 137, reinterpret_cast<std::uintptr_t>(&tl_TrackPopupMenuEx)},
        {"LoadMenuW", 138, reinterpret_cast<std::uintptr_t>(&tl_LoadMenuW)},
        {"CheckDlgButton", 139, reinterpret_cast<std::uintptr_t>(&tl_CheckDlgButton)},
        {"IsDlgButtonChecked", 140, reinterpret_cast<std::uintptr_t>(&tl_IsDlgButtonChecked)},
        {"CheckRadioButton", 141, reinterpret_cast<std::uintptr_t>(&tl_CheckRadioButton)},
        {"MapDialogRect", 142, reinterpret_cast<std::uintptr_t>(&tl_MapDialogRect)},
        {"GetDialogBaseUnits", 143, reinterpret_cast<std::uintptr_t>(&tl_GetDialogBaseUnits)},
        {"WindowFromPoint", 144, reinterpret_cast<std::uintptr_t>(&tl_WindowFromPoint)},
        {"ChildWindowFromPointEx", 145, reinterpret_cast<std::uintptr_t>(&tl_ChildWindowFromPointEx)},
        {"GetWindowPlacement", 146, reinterpret_cast<std::uintptr_t>(&tl_GetWindowPlacement)},
        {"SetWindowPlacement", 147, reinterpret_cast<std::uintptr_t>(&tl_SetWindowPlacement)},
        {"IsWindowEnabled", 148, reinterpret_cast<std::uintptr_t>(&tl_IsWindowEnabled)},
        {"IsZoomed", 149, reinterpret_cast<std::uintptr_t>(&tl_IsZoomed)},
        {"GetClassInfoW", 150, reinterpret_cast<std::uintptr_t>(&tl_GetClassInfoW)},
        {"GetMonitorInfoA", 151, reinterpret_cast<std::uintptr_t>(&tl_GetMonitorInfoA)},
        {"SystemParametersInfoW", 152, reinterpret_cast<std::uintptr_t>(&tl_SystemParametersInfoW)},
        {"LoadAcceleratorsW", 153, reinterpret_cast<std::uintptr_t>(&tl_LoadAcceleratorsW)},
        {"TranslateAcceleratorW", 154, reinterpret_cast<std::uintptr_t>(&tl_TranslateAcceleratorW)},
        {"LoadBitmapW", 155, reinterpret_cast<std::uintptr_t>(&tl_LoadBitmapW)},
        {"MapVirtualKeyW", 156, reinterpret_cast<std::uintptr_t>(&tl_MapVirtualKeyW)},
        {"RegisterClipboardFormatW", 157,
         reinterpret_cast<std::uintptr_t>(&tl_RegisterClipboardFormatW)},
        {"ScreenToClient", 158, reinterpret_cast<std::uintptr_t>(&tl_ScreenToClient)},
        {"CreateCaret", 159, reinterpret_cast<std::uintptr_t>(&tl_CreateCaret)},
        {"DestroyCaret", 160, reinterpret_cast<std::uintptr_t>(&tl_DestroyCaret)},
        {"SetCaretPos", 161, reinterpret_cast<std::uintptr_t>(&tl_SetCaretPos)},
        {"ShowCaret", 162, reinterpret_cast<std::uintptr_t>(&tl_ShowCaret)},
        {"HideCaret", 163, reinterpret_cast<std::uintptr_t>(&tl_HideCaret)},
        {"GetCaretPos", 164, reinterpret_cast<std::uintptr_t>(&tl_GetCaretPos)},
        {"SetScrollInfo", 165, reinterpret_cast<std::uintptr_t>(&tl_SetScrollInfo)},
        {"GetScrollInfo", 166, reinterpret_cast<std::uintptr_t>(&tl_GetScrollInfo)},
        {"ShowScrollBar", 167, reinterpret_cast<std::uintptr_t>(&tl_ShowScrollBar)},
        {"EnableScrollBar", 168, reinterpret_cast<std::uintptr_t>(&tl_EnableScrollBar)},
        {"SetScrollPos", 169, reinterpret_cast<std::uintptr_t>(&tl_SetScrollPos)},
        {"GetScrollPos", 170, reinterpret_cast<std::uintptr_t>(&tl_GetScrollPos)},
        {"SetScrollRange", 171, reinterpret_cast<std::uintptr_t>(&tl_SetScrollRange)},
        {"GetScrollRange", 172, reinterpret_cast<std::uintptr_t>(&tl_GetScrollRange)},
        {"SetCapture", 173, reinterpret_cast<std::uintptr_t>(&tl_SetCapture)},
        {"ReleaseCapture", 174, reinterpret_cast<std::uintptr_t>(&tl_ReleaseCapture)},
        {"GetCapture", 175, reinterpret_cast<std::uintptr_t>(&tl_GetCapture)},
        {"GetAsyncKeyState", 176, reinterpret_cast<std::uintptr_t>(&tl_GetAsyncKeyState)},
        {"GetKeyState", 177, reinterpret_cast<std::uintptr_t>(&tl_GetKeyState)},
        {"FlashWindow", 178, reinterpret_cast<std::uintptr_t>(&tl_FlashWindow)},
        {"FlashWindowEx", 179, reinterpret_cast<std::uintptr_t>(&tl_FlashWindowEx)},
        {"GetSysColor", 180, reinterpret_cast<std::uintptr_t>(&tl_GetSysColor)},
        {"SetSysColors", 181, reinterpret_cast<std::uintptr_t>(&tl_SetSysColors)},
        {"MessageBeep", 182, reinterpret_cast<std::uintptr_t>(&tl_MessageBeep)},
        {"TrackPopupMenu", 183, reinterpret_cast<std::uintptr_t>(&tl_TrackPopupMenu)},
        {"GetClipboardData", 184, reinterpret_cast<std::uintptr_t>(&tl_GetClipboardData)},
        {"IsClipboardFormatAvailable", 185, reinterpret_cast<std::uintptr_t>(&tl_IsClipboardFormatAvailable)},
        {"RegisterClipboardFormatA", 186, reinterpret_cast<std::uintptr_t>(&tl_RegisterClipboardFormatA)},
        {"CountClipboardFormats", 187, reinterpret_cast<std::uintptr_t>(&tl_CountClipboardFormats)},
        {"EnumClipboardFormats", 188, reinterpret_cast<std::uintptr_t>(&tl_EnumClipboardFormats)},
        {"GetDpiForWindow", 189, reinterpret_cast<std::uintptr_t>(&tl_GetDpiForWindow)},
        {"GetDpiForSystem", 190, reinterpret_cast<std::uintptr_t>(&tl_GetDpiForSystem)},
        {"SetProcessDpiAwarenessContext", 191, reinterpret_cast<std::uintptr_t>(&tl_SetProcessDpiAwarenessContext)},
        {"SetProcessDPIAware", 192, reinterpret_cast<std::uintptr_t>(&tl_SetProcessDPIAware)},
        {"GetSystemMetricsForDpi", 193, reinterpret_cast<std::uintptr_t>(&tl_GetSystemMetricsForDpi)},
        {"AdjustWindowRectExForDpi", 194, reinterpret_cast<std::uintptr_t>(&tl_AdjustWindowRectExForDpi)},
        {"CreateIconIndirect", 195, reinterpret_cast<std::uintptr_t>(&tl_CreateIconIndirect)},
        {"GetIconInfo", 196, reinterpret_cast<std::uintptr_t>(&tl_GetIconInfo)},
        {"GetIconInfoExW", 197, reinterpret_cast<std::uintptr_t>(&tl_GetIconInfoExW)},
        {"DrawIcon", 198, reinterpret_cast<std::uintptr_t>(&tl_DrawIcon)},
        {"CopyIcon", 199, reinterpret_cast<std::uintptr_t>(&tl_CopyIcon)},
        {"SetWindowRgn", 200, reinterpret_cast<std::uintptr_t>(&tl_SetWindowRgn)},
        {"GetWindowRgn", 201, reinterpret_cast<std::uintptr_t>(&tl_GetWindowRgn)},
        {"GetWindowRgnBox", 202, reinterpret_cast<std::uintptr_t>(&tl_GetWindowRgnBox)},
        {"DrawEdge", 203, reinterpret_cast<std::uintptr_t>(&tl_DrawEdge)},
        {"DrawFrameControl", 204, reinterpret_cast<std::uintptr_t>(&tl_DrawFrameControl)},
        {"DrawFocusRect", 205, reinterpret_cast<std::uintptr_t>(&tl_DrawFocusRect)},
        {"FillRect", 206, reinterpret_cast<std::uintptr_t>(&tl_FillRect)},
        {"FrameRect", 207, reinterpret_cast<std::uintptr_t>(&tl_FrameRect)},
        {"InvertRect", 208, reinterpret_cast<std::uintptr_t>(&tl_InvertRect)},
        {"GetUpdateRect", 209, reinterpret_cast<std::uintptr_t>(&tl_GetUpdateRect)},
        {"GetUpdateRgn", 210, reinterpret_cast<std::uintptr_t>(&tl_GetUpdateRgn)},
        {"InvalidateRgn", 211, reinterpret_cast<std::uintptr_t>(&tl_InvalidateRgn)},
        {"ValidateRgn", 212, reinterpret_cast<std::uintptr_t>(&tl_ValidateRgn)},
        {"ScrollWindow", 213, reinterpret_cast<std::uintptr_t>(&tl_ScrollWindow)},
        {"ScrollWindowEx", 214, reinterpret_cast<std::uintptr_t>(&tl_ScrollWindowEx)},
        {"RegisterHotKey", 215, reinterpret_cast<std::uintptr_t>(&tl_RegisterHotKey)},
        {"UnregisterHotKey", 216, reinterpret_cast<std::uintptr_t>(&tl_UnregisterHotKey)},
        {"GetProcessWindowStation", 217, reinterpret_cast<std::uintptr_t>(&tl_GetProcessWindowStation)},
        {"GetUserObjectInformationW", 218, reinterpret_cast<std::uintptr_t>(&tl_GetUserObjectInformationW)},
        {"GetShellWindow", 219, reinterpret_cast<std::uintptr_t>(&tl_GetShellWindow)},
        {"EnumDisplayDevicesA", 220, reinterpret_cast<std::uintptr_t>(&tl_EnumDisplayDevicesA)},
        {"CreateDialogParamA", 221, reinterpret_cast<std::uintptr_t>(&tl_CreateDialogParamA)},
        {"CreateMenu", 222, reinterpret_cast<std::uintptr_t>(&tl_CreateMenu)},
        {"DefDlgProcA", 223, reinterpret_cast<std::uintptr_t>(&tl_DefDlgProcA)},
        {"DeleteMenu", 224, reinterpret_cast<std::uintptr_t>(&tl_DeleteMenu)},
        {"DialogBoxParamA", 225, reinterpret_cast<std::uintptr_t>(&tl_DialogBoxParamA)},
        {"GetCaretBlinkTime", 226, reinterpret_cast<std::uintptr_t>(&tl_GetCaretBlinkTime)},
        {"GetClipboardOwner", 227, reinterpret_cast<std::uintptr_t>(&tl_GetClipboardOwner)},
        {"GetDoubleClickTime", 228, reinterpret_cast<std::uintptr_t>(&tl_GetDoubleClickTime)},
        {"GetForegroundWindow", 229, reinterpret_cast<std::uintptr_t>(&tl_GetForegroundWindow)},
        {"GetKeyboardLayout", 230, reinterpret_cast<std::uintptr_t>(&tl_GetKeyboardLayout)},
        {"GetKeyboardState", 231, reinterpret_cast<std::uintptr_t>(&tl_GetKeyboardState)},
        {"GetMessageTime", 232, reinterpret_cast<std::uintptr_t>(&tl_GetMessageTime)},
        {"GetQueueStatus", 233, reinterpret_cast<std::uintptr_t>(&tl_GetQueueStatus)},
        {"GetSysColorBrush", 234, reinterpret_cast<std::uintptr_t>(&tl_GetSysColorBrush)},
        {"GetSystemMenu", 235, reinterpret_cast<std::uintptr_t>(&tl_GetSystemMenu)},
        {"InsertMenuA", 236, reinterpret_cast<std::uintptr_t>(&tl_InsertMenuA)},
        {"IsDialogMessageA", 237, reinterpret_cast<std::uintptr_t>(&tl_IsDialogMessageA)},
        {"IsIconic", 238, reinterpret_cast<std::uintptr_t>(&tl_IsIconic)},
        {"LoadImageA", 239, reinterpret_cast<std::uintptr_t>(&tl_LoadImageA)},
        {"MessageBoxIndirectW", 240, reinterpret_cast<std::uintptr_t>(&tl_MessageBoxIndirectW)},
        {"OffsetRect", 241, reinterpret_cast<std::uintptr_t>(&tl_OffsetRect)},
        {"RegisterWindowMessageA", 242, reinterpret_cast<std::uintptr_t>(&tl_RegisterWindowMessageA)},
        {"SendDlgItemMessageA", 243, reinterpret_cast<std::uintptr_t>(&tl_SendDlgItemMessageA)},
        {"SetActiveWindow", 244, reinterpret_cast<std::uintptr_t>(&tl_SetActiveWindow)},
        {"SetDlgItemTextA", 245, reinterpret_cast<std::uintptr_t>(&tl_SetDlgItemTextA)},
        {"SetKeyboardState", 246, reinterpret_cast<std::uintptr_t>(&tl_SetKeyboardState)},
        {"SystemParametersInfoA", 247, reinterpret_cast<std::uintptr_t>(&tl_SystemParametersInfoA)},
        {"ToAsciiEx", 248, reinterpret_cast<std::uintptr_t>(&tl_ToAsciiEx)},
    };
    static const InternalModule kUser32Module{"USER32.dll", kUser32Exports};
    register_module(kUser32Module);
    static const ExportedFunction kGdi32Exports[] = {
        {"GetStockObject", 1, reinterpret_cast<std::uintptr_t>(&tl_GetStockObject)},
        {"TextOutA", 2, reinterpret_cast<std::uintptr_t>(&tl_TextOut)},
        {"TextOut", 3, reinterpret_cast<std::uintptr_t>(&tl_TextOut)},
        {"Rectangle", 4, reinterpret_cast<std::uintptr_t>(&tl_Rectangle)},
        {"CreateFontA", 5, reinterpret_cast<std::uintptr_t>(&tl_CreateFontA)},
        {"CreateSolidBrush", 6, reinterpret_cast<std::uintptr_t>(&tl_CreateSolidBrush)},
        {"DeleteObject", 7, reinterpret_cast<std::uintptr_t>(&tl_DeleteObject)},
        {"SetBkColor", 8, reinterpret_cast<std::uintptr_t>(&tl_SetBkColor)},
        {"SetTextColor", 9, reinterpret_cast<std::uintptr_t>(&tl_SetTextColor)},
        {"GetDeviceCaps", 10, reinterpret_cast<std::uintptr_t>(&tl_GetDeviceCaps)},
        {"CreateCompatibleDC", 11, reinterpret_cast<std::uintptr_t>(&tl_CreateCompatibleDC)},
        {"DeleteDC", 12, reinterpret_cast<std::uintptr_t>(&tl_DeleteDC)},
        {"CreateCompatibleBitmap", 13, reinterpret_cast<std::uintptr_t>(&tl_CreateCompatibleBitmap)},
        {"BitBlt", 14, reinterpret_cast<std::uintptr_t>(&tl_BitBlt)},
        {"SelectObject", 15, reinterpret_cast<std::uintptr_t>(&tl_SelectObject)},
        {"SetBkMode", 16, reinterpret_cast<std::uintptr_t>(&tl_SetBkMode)},
        {"CreateFontIndirectA", 17, reinterpret_cast<std::uintptr_t>(&tl_CreateFontIndirectA)},
        {"CreateFontIndirectW", 18, reinterpret_cast<std::uintptr_t>(&tl_CreateFontIndirectW)},
        {"CreateFontW", 19, reinterpret_cast<std::uintptr_t>(&tl_CreateFontW)},
        {"SetDCBrushColor", 20, reinterpret_cast<std::uintptr_t>(&tl_SetDCBrushColor)},
        {"SetDCPenColor", 21, reinterpret_cast<std::uintptr_t>(&tl_SetDCPenColor)},
        {"CreateBitmap", 22, reinterpret_cast<std::uintptr_t>(&tl_CreateBitmap)},
        {"StretchBlt", 23, reinterpret_cast<std::uintptr_t>(&tl_StretchBlt)},
        {"GetObjectW", 24, reinterpret_cast<std::uintptr_t>(&tl_GetObjectW)},
        {"CreateDIBSection", 25, reinterpret_cast<std::uintptr_t>(&tl_CreateDIBSection)},
        {"GetTextExtentPoint32W", 26, reinterpret_cast<std::uintptr_t>(&tl_GetTextExtentPoint32W)},
        {"StartDocW", 27, reinterpret_cast<std::uintptr_t>(&tl_StartDocW)},
        {"EndDoc", 28, reinterpret_cast<std::uintptr_t>(&tl_EndDoc)},
        {"StartPage", 29, reinterpret_cast<std::uintptr_t>(&tl_StartPage)},
        {"EndPage", 30, reinterpret_cast<std::uintptr_t>(&tl_EndPage)},
        {"AbortDoc", 31, reinterpret_cast<std::uintptr_t>(&tl_AbortDoc)},
        {"GetTextMetricsW", 32, reinterpret_cast<std::uintptr_t>(&tl_GetTextMetricsW)},
        {"GetTextMetricsA", 33, reinterpret_cast<std::uintptr_t>(&tl_GetTextMetricsA)},
        {"CreatePen", 34, reinterpret_cast<std::uintptr_t>(&tl_CreatePen)},
        {"ExtTextOutW", 35, reinterpret_cast<std::uintptr_t>(&tl_ExtTextOutW)},
        {"ExtTextOutA", 36, reinterpret_cast<std::uintptr_t>(&tl_ExtTextOutA)},
        {"MoveToEx", 37, reinterpret_cast<std::uintptr_t>(&tl_MoveToEx)},
        {"LineTo", 38, reinterpret_cast<std::uintptr_t>(&tl_LineTo)},
        {"Polyline", 39, reinterpret_cast<std::uintptr_t>(&tl_Polyline)},
        {"Polygon", 40, reinterpret_cast<std::uintptr_t>(&tl_Polygon)},
        {"CreateRectRgn", 41, reinterpret_cast<std::uintptr_t>(&tl_CreateRectRgn)},
        {"CombineRgn", 42, reinterpret_cast<std::uintptr_t>(&tl_CombineRgn)},
        {"SelectClipRgn", 43, reinterpret_cast<std::uintptr_t>(&tl_SelectClipRgn)},
        {"GetClipBox", 44, reinterpret_cast<std::uintptr_t>(&tl_GetClipBox)},
        {"GetCharWidthW", 45, reinterpret_cast<std::uintptr_t>(&tl_GetCharWidthW)},
        {"GetCharWidth32W", 46, reinterpret_cast<std::uintptr_t>(&tl_GetCharWidth32W)},
        {"GetTextExtentPoint32A", 47, reinterpret_cast<std::uintptr_t>(&tl_GetTextExtentPoint32A)},
        {"SetTextAlign", 48, reinterpret_cast<std::uintptr_t>(&tl_SetTextAlign)},
        {"GetTextAlign", 49, reinterpret_cast<std::uintptr_t>(&tl_GetTextAlign)},
        {"SetROP2", 50, reinterpret_cast<std::uintptr_t>(&tl_SetROP2)},
        {"GetSystemPaletteEntries", 51, reinterpret_cast<std::uintptr_t>(&tl_GetSystemPaletteEntries)},
        {"CreatePatternBrush", 52, reinterpret_cast<std::uintptr_t>(&tl_CreatePatternBrush)},
        {"CreateHatchBrush", 53, reinterpret_cast<std::uintptr_t>(&tl_CreateHatchBrush)},
        {"PatBlt", 54, reinterpret_cast<std::uintptr_t>(&tl_PatBlt)},
        {"MaskBlt", 55, reinterpret_cast<std::uintptr_t>(&tl_MaskBlt)},
        {"PlgBlt", 56, reinterpret_cast<std::uintptr_t>(&tl_PlgBlt)},
        {"AlphaBlend", 57, reinterpret_cast<std::uintptr_t>(&tl_AlphaBlend)},
        {"TransparentBlt", 58, reinterpret_cast<std::uintptr_t>(&tl_TransparentBlt)},
        {"EnumFontFamiliesExW", 59, reinterpret_cast<std::uintptr_t>(&tl_EnumFontFamiliesExW)},
        {"EnumFontFamiliesExA", 60, reinterpret_cast<std::uintptr_t>(&tl_EnumFontFamiliesExA)},
        {"CreatePolygonRgn", 61, reinterpret_cast<std::uintptr_t>(&tl_CreatePolygonRgn)},
        {"FrameRgn", 62, reinterpret_cast<std::uintptr_t>(&tl_FrameRgn)},
        {"FillRgn", 63, reinterpret_cast<std::uintptr_t>(&tl_FillRgn)},
        {"PaintRgn", 64, reinterpret_cast<std::uintptr_t>(&tl_PaintRgn)},
        {"InvertRgn", 65, reinterpret_cast<std::uintptr_t>(&tl_InvertRgn)},
        {"CreatePalette", 66, reinterpret_cast<std::uintptr_t>(&tl_CreatePalette)},
        {"ExcludeClipRect", 67, reinterpret_cast<std::uintptr_t>(&tl_ExcludeClipRect)},
        {"GetBkMode", 68, reinterpret_cast<std::uintptr_t>(&tl_GetBkMode)},
        {"GetCharABCWidthsFloatA", 69, reinterpret_cast<std::uintptr_t>(&tl_GetCharABCWidthsFloatA)},
        {"GetCharWidth32A", 70, reinterpret_cast<std::uintptr_t>(&tl_GetCharWidth32A)},
        {"GetCharWidthA", 71, reinterpret_cast<std::uintptr_t>(&tl_GetCharWidthA)},
        {"GetCharacterPlacementW", 72, reinterpret_cast<std::uintptr_t>(&tl_GetCharacterPlacementW)},
        {"GetCurrentObject", 73, reinterpret_cast<std::uintptr_t>(&tl_GetCurrentObject)},
        {"GetDIBits", 74, reinterpret_cast<std::uintptr_t>(&tl_GetDIBits)},
        {"GetObjectA", 75, reinterpret_cast<std::uintptr_t>(&tl_GetObjectA)},
        {"GetOutlineTextMetricsA", 76, reinterpret_cast<std::uintptr_t>(&tl_GetOutlineTextMetricsA)},
        {"GetPixel", 77, reinterpret_cast<std::uintptr_t>(&tl_GetPixel)},
        {"GetTextExtentExPointA", 78, reinterpret_cast<std::uintptr_t>(&tl_GetTextExtentExPointA)},
        {"GetTextExtentPointA", 79, reinterpret_cast<std::uintptr_t>(&tl_GetTextExtentPointA)},
        {"IntersectClipRect", 80, reinterpret_cast<std::uintptr_t>(&tl_IntersectClipRect)},
        {"RealizePalette", 81, reinterpret_cast<std::uintptr_t>(&tl_RealizePalette)},
        {"SelectPalette", 82, reinterpret_cast<std::uintptr_t>(&tl_SelectPalette)},
        {"SetMapMode", 83, reinterpret_cast<std::uintptr_t>(&tl_SetMapMode)},
        {"SetPaletteEntries", 84, reinterpret_cast<std::uintptr_t>(&tl_SetPaletteEntries)},
        {"SetPixel", 85, reinterpret_cast<std::uintptr_t>(&tl_SetPixel)},
        {"TranslateCharsetInfo", 86, reinterpret_cast<std::uintptr_t>(&tl_TranslateCharsetInfo)},
        {"UnrealizeObject", 87, reinterpret_cast<std::uintptr_t>(&tl_UnrealizeObject)},
        {"UpdateColors", 88, reinterpret_cast<std::uintptr_t>(&tl_UpdateColors)},
    };
    static const InternalModule kGdi32Module{"GDI32.dll", kGdi32Exports};
    register_module(kGdi32Module);
    static const ExportedFunction kWs2_32Exports[] = {
        {"accept", 1, reinterpret_cast<std::uintptr_t>(&tl_accept)},
        {"bind", 2, reinterpret_cast<std::uintptr_t>(&tl_bind)},
        {"closesocket", 3, reinterpret_cast<std::uintptr_t>(&tl_closesocket)},
        {"connect", 4, reinterpret_cast<std::uintptr_t>(&tl_connect)},
        {"getpeername", 5, reinterpret_cast<std::uintptr_t>(&tl_getpeername)},
        {"getsockname", 6, reinterpret_cast<std::uintptr_t>(&tl_getsockname)},
        {"getsockopt", 7, reinterpret_cast<std::uintptr_t>(&tl_getsockopt)},
        {"htonl", 8, reinterpret_cast<std::uintptr_t>(&tl_htonl)},
        {"htons", 9, reinterpret_cast<std::uintptr_t>(&tl_htons)},
        {"inet_addr", 10, reinterpret_cast<std::uintptr_t>(&tl_inet_addr)},
        {"inet_ntoa", 11, reinterpret_cast<std::uintptr_t>(&tl_inet_ntoa)},
        {"ioctlsocket", 12, reinterpret_cast<std::uintptr_t>(&tl_ioctlsocket)},
        {"listen", 13, reinterpret_cast<std::uintptr_t>(&tl_listen)},
        {"ntohl", 14, reinterpret_cast<std::uintptr_t>(&tl_ntohl)},
        {"ntohs", 15, reinterpret_cast<std::uintptr_t>(&tl_ntohs)},
        {"recv", 16, reinterpret_cast<std::uintptr_t>(&tl_recv)},
        {"recvfrom", 17, reinterpret_cast<std::uintptr_t>(&tl_recvfrom)},
        {"select", 18, reinterpret_cast<std::uintptr_t>(&tl_select)},
        {"send", 19, reinterpret_cast<std::uintptr_t>(&tl_send)},
        {"sendto", 20, reinterpret_cast<std::uintptr_t>(&tl_sendto)},
        {"setsockopt", 21, reinterpret_cast<std::uintptr_t>(&tl_setsockopt)},
        {"shutdown", 22, reinterpret_cast<std::uintptr_t>(&tl_shutdown)},
        {"socket", 23, reinterpret_cast<std::uintptr_t>(&tl_socket)},
        {"gethostbyaddr", 51, reinterpret_cast<std::uintptr_t>(&tl_gethostbyaddr)},
        {"gethostbyname", 52, reinterpret_cast<std::uintptr_t>(&tl_gethostbyname)},
        {"gethostname", 57, reinterpret_cast<std::uintptr_t>(&tl_gethostname)},
        {"getservbyport", 56, reinterpret_cast<std::uintptr_t>(&tl_getservbyname)},
        {"getservbyname", 55, reinterpret_cast<std::uintptr_t>(&tl_getservbyname)},
        {"WSAGetLastError", 111, reinterpret_cast<std::uintptr_t>(&tl_WSAGetLastError)},
        {"WSASetLastError", 112, reinterpret_cast<std::uintptr_t>(&tl_WSASetLastError)},
        {"WSAAsyncSelect", 115, reinterpret_cast<std::uintptr_t>(&tl_WSAAsyncSelect)},
        {"WSAAsyncGetHostByName", 116, reinterpret_cast<std::uintptr_t>(&tl_gethostbyname)},
        {"__WSAFDIsSet", 151, reinterpret_cast<std::uintptr_t>(&tl___WSAFDIsSet)},
        {"WSAStartup", 1001, reinterpret_cast<std::uintptr_t>(&tl_WSAStartup)},
        {"WSACleanup", 1002, reinterpret_cast<std::uintptr_t>(&tl_WSACleanup)},
        {"getaddrinfo", 1003, reinterpret_cast<std::uintptr_t>(&tl_getaddrinfo)},
        {"freeaddrinfo", 1004, reinterpret_cast<std::uintptr_t>(&tl_freeaddrinfo)},
        {"WSAPoll", 1005, reinterpret_cast<std::uintptr_t>(&tl_WSAPoll)},
        {"inet_ntop", 1006, reinterpret_cast<std::uintptr_t>(&tl_inet_ntop)},
        {"inet_pton", 1007, reinterpret_cast<std::uintptr_t>(&tl_inet_pton)},
        {"WSAEventSelect", 1008, reinterpret_cast<std::uintptr_t>(&tl_WSAEventSelect)},
        {"WSACreateEvent", 1009, reinterpret_cast<std::uintptr_t>(&tl_WSACreateEvent)},
        {"WSACloseEvent", 1010, reinterpret_cast<std::uintptr_t>(&tl_WSACloseEvent)},
        {"WSASetEvent", 1011, reinterpret_cast<std::uintptr_t>(&tl_WSASetEvent)},
        {"WSAResetEvent", 1012, reinterpret_cast<std::uintptr_t>(&tl_WSAResetEvent)},
        {"WSAWaitForMultipleEvents", 1013, reinterpret_cast<std::uintptr_t>(&tl_WSAWaitForMultipleEvents)},
        {"WSAEnumNetworkEvents", 1014, reinterpret_cast<std::uintptr_t>(&tl_WSAEnumNetworkEvents)},
        {"WSAIoctl", 1015, reinterpret_cast<std::uintptr_t>(&tl_WSAIoctl)},
        {"getnameinfo", 1016, reinterpret_cast<std::uintptr_t>(&tl_getnameinfo)},
        {"WSASocketA", 1017, reinterpret_cast<std::uintptr_t>(&tl_WSASocketA)},
    };
    static const InternalModule kWs2_32Module{"WS2_32.dll", kWs2_32Exports};
    register_module(kWs2_32Module);
    static const ExportedFunction kWininetExports[] = {
        {"InternetReadFile", 1, reinterpret_cast<std::uintptr_t>(&tl_InternetReadFile)},
        {"InternetCrackUrlW", 2, reinterpret_cast<std::uintptr_t>(&tl_InternetCrackUrlW)},
        {"InternetCloseHandle", 3, reinterpret_cast<std::uintptr_t>(&tl_InternetCloseHandle)},
        {"InternetConnectW", 4, reinterpret_cast<std::uintptr_t>(&tl_InternetConnectW)},
        {"InternetQueryDataAvailable", 5, reinterpret_cast<std::uintptr_t>(&tl_InternetQueryDataAvailable)},
        {"InternetSetOptionW", 6, reinterpret_cast<std::uintptr_t>(&tl_InternetSetOptionW)},
        {"HttpOpenRequestW", 7, reinterpret_cast<std::uintptr_t>(&tl_HttpOpenRequestW)},
        {"HttpAddRequestHeadersW", 8, reinterpret_cast<std::uintptr_t>(&tl_HttpAddRequestHeadersW)},
        {"HttpSendRequestW", 9, reinterpret_cast<std::uintptr_t>(&tl_HttpSendRequestW)},
        {"HttpQueryInfoW", 10, reinterpret_cast<std::uintptr_t>(&tl_HttpQueryInfoW)},
        {"InternetOpenW", 11, reinterpret_cast<std::uintptr_t>(&tl_InternetOpenW)},
    };
    static const InternalModule kWininetModule{"WININET.dll", kWininetExports};
    register_module(kWininetModule);
    static const ExportedFunction kMsvcrtExports[] = {
        {"__C_specific_handler", 1, reinterpret_cast<std::uintptr_t>(&tl___C_specific_handler)},
        {"__getmainargs", 2, reinterpret_cast<std::uintptr_t>(&tl___getmainargs)},
        {"__iob_func", 3, reinterpret_cast<std::uintptr_t>(&tl___iob_func)},
        {"___lc_codepage_func", 4, reinterpret_cast<std::uintptr_t>(&tl___lc_codepage_func)},
        {"___mb_cur_max_func", 5, reinterpret_cast<std::uintptr_t>(&tl___mb_cur_max_func)},
        {"__set_app_type", 6, reinterpret_cast<std::uintptr_t>(&tl___set_app_type)},
        {"__setusermatherr", 7, reinterpret_cast<std::uintptr_t>(&tl___setusermatherr)},
        {"_amsg_exit", 8, reinterpret_cast<std::uintptr_t>(&tl__amsg_exit)},
        {"_cexit", 9, reinterpret_cast<std::uintptr_t>(&tl__cexit)},
        {"_errno", 10, reinterpret_cast<std::uintptr_t>(&tl__errno)},
        {"_fdopen", 11, reinterpret_cast<std::uintptr_t>(&tl__fdopen)},
        {"_fileno", 12, reinterpret_cast<std::uintptr_t>(&tl__fileno)},
        {"_initterm", 13, reinterpret_cast<std::uintptr_t>(&tl___initterm)},
        {"_isatty", 14, reinterpret_cast<std::uintptr_t>(&tl__isatty)},
        {"_lock", 15, reinterpret_cast<std::uintptr_t>(&tl__lock)},
        {"_open", 16, reinterpret_cast<std::uintptr_t>(&tl__open)},
        {"_setmode", 17, reinterpret_cast<std::uintptr_t>(&tl__setmode)},
        {"_unlock", 18, reinterpret_cast<std::uintptr_t>(&tl__unlock)},
        {"abort", 19, reinterpret_cast<std::uintptr_t>(&tl_abort)},
        {"atexit", 20, reinterpret_cast<std::uintptr_t>(&tl_atexit)},
        {"calloc", 21, reinterpret_cast<std::uintptr_t>(&tl_calloc)},
        {"exit", 22, reinterpret_cast<std::uintptr_t>(&tl_exit)},
        {"fclose", 23, reinterpret_cast<std::uintptr_t>(&tl_fclose)},
        {"ferror", 24, reinterpret_cast<std::uintptr_t>(&tl_ferror)},
        {"fflush", 25, reinterpret_cast<std::uintptr_t>(&tl_fflush)},
        {"fopen", 26, reinterpret_cast<std::uintptr_t>(&tl_fopen)},
        {"fprintf", 27, reinterpret_cast<std::uintptr_t>(&tl_fprintf)},
        {"fputc", 28, reinterpret_cast<std::uintptr_t>(&tl_fputc)},
        {"fputs", 29, reinterpret_cast<std::uintptr_t>(&tl_fputs)},
        {"free", 30, reinterpret_cast<std::uintptr_t>(&tl_free)},
        {"fseek", 31, reinterpret_cast<std::uintptr_t>(&tl_fseek)},
        {"ftell", 32, reinterpret_cast<std::uintptr_t>(&tl_ftell)},
        {"fwrite", 33, reinterpret_cast<std::uintptr_t>(&tl_fwrite)},
        {"getc", 34, reinterpret_cast<std::uintptr_t>(&tl_getc)},
        {"getenv", 35, reinterpret_cast<std::uintptr_t>(&tl_getenv)},
        {"isalnum", 36, reinterpret_cast<std::uintptr_t>(&tl_isalnum)},
        {"localeconv", 37, reinterpret_cast<std::uintptr_t>(&tl_localeconv)},
        {"malloc", 38, reinterpret_cast<std::uintptr_t>(&tl_malloc)},
        {"memcpy", 39, reinterpret_cast<std::uintptr_t>(&tl_memcpy)},
        {"memset", 40, reinterpret_cast<std::uintptr_t>(&tl_memset)},
        {"perror", 41, reinterpret_cast<std::uintptr_t>(&tl_perror)},
        {"putc", 42, reinterpret_cast<std::uintptr_t>(&tl_putc)},
        {"rewind", 43, reinterpret_cast<std::uintptr_t>(&tl_rewind)},
        {"signal", 44, reinterpret_cast<std::uintptr_t>(&tl_signal)},
        {"strcmp", 45, reinterpret_cast<std::uintptr_t>(&tl_strcmp)},
        {"strcpy", 46, reinterpret_cast<std::uintptr_t>(&tl_strcpy)},
        {"strerror", 47, reinterpret_cast<std::uintptr_t>(&tl_strerror)},
        {"strlen", 48, reinterpret_cast<std::uintptr_t>(&tl_strlen)},
        {"strncmp", 49, reinterpret_cast<std::uintptr_t>(&tl_strncmp)},
        {"strtol", 50, reinterpret_cast<std::uintptr_t>(&tl_strtol)},
        {"strtoul", 51, reinterpret_cast<std::uintptr_t>(&tl_strtoul)},
        {"toupper", 52, reinterpret_cast<std::uintptr_t>(&tl_toupper)},
        {"vfprintf", 53, reinterpret_cast<std::uintptr_t>(&tl_vfprintf)},
        {"wcslen", 54, reinterpret_cast<std::uintptr_t>(&tl_wcslen)},
        {"__initenv", 55, reinterpret_cast<std::uintptr_t>(&g_guest_initenv)},
        {"_commode", 56, reinterpret_cast<std::uintptr_t>(&g_guest_commode)},
        {"_fmode", 57, reinterpret_cast<std::uintptr_t>(&g_guest_fmode)},
        {"fgetc", 58, reinterpret_cast<std::uintptr_t>(&tl_fgetc)},
        {"fread", 59, reinterpret_cast<std::uintptr_t>(&tl_fread)},
        {"ungetc", 60, reinterpret_cast<std::uintptr_t>(&tl_ungetc)},
        {"strncpy", 61, reinterpret_cast<std::uintptr_t>(&tl_strncpy)},
        {"strstr", 62, reinterpret_cast<std::uintptr_t>(&tl_strstr)},
        {"isspace", 63, reinterpret_cast<std::uintptr_t>(&tl_isspace)},
        {"strcat", 64, reinterpret_cast<std::uintptr_t>(&tl_strcat)},
        {"memmove", 65, reinterpret_cast<std::uintptr_t>(&tl_memmove)},
        {"remove", 66, reinterpret_cast<std::uintptr_t>(&tl_remove)},
        {"_stat64", 67, reinterpret_cast<std::uintptr_t>(&tl__stat64)},
        {"_onexit", 68, reinterpret_cast<std::uintptr_t>(&tl_atexit)},
        // Fase 10+: strings wide, locale, arquivos wide e formatação wide.
        {"realloc", 69, reinterpret_cast<std::uintptr_t>(&tl_realloc)},
        {"setlocale", 70, reinterpret_cast<std::uintptr_t>(&tl_setlocale)},
        {"strchr", 71, reinterpret_cast<std::uintptr_t>(&tl_strchr)},
        {"strrchr", 72, reinterpret_cast<std::uintptr_t>(&tl_strrchr)},
        {"_stricmp", 73, reinterpret_cast<std::uintptr_t>(&tl__stricmp)},
        {"_strdup", 74, reinterpret_cast<std::uintptr_t>(&tl__strdup)},
        {"_umask", 75, reinterpret_cast<std::uintptr_t>(&tl__umask)},
        {"_chmod", 76, reinterpret_cast<std::uintptr_t>(&tl__chmod)},
        {"_utime64", 77, reinterpret_cast<std::uintptr_t>(&tl__utime64)},
        {"_wfopen", 78, reinterpret_cast<std::uintptr_t>(&tl__wfopen)},
        {"_wstat64", 79, reinterpret_cast<std::uintptr_t>(&tl__wstat64)},
        {"_wrename", 80, reinterpret_cast<std::uintptr_t>(&tl__wrename)},
        {"_wunlink", 81, reinterpret_cast<std::uintptr_t>(&tl__wunlink)},
        {"_wcsdup", 82, reinterpret_cast<std::uintptr_t>(&tl__wcsdup)},
        {"wcschr", 83, reinterpret_cast<std::uintptr_t>(&tl_wcschr)},
        {"wcsrchr", 84, reinterpret_cast<std::uintptr_t>(&tl_wcsrchr)},
        {"wcsncat", 85, reinterpret_cast<std::uintptr_t>(&tl_wcsncat)},
        {"wcsncpy", 86, reinterpret_cast<std::uintptr_t>(&tl_wcsncpy)},
        {"mbstowcs", 87, reinterpret_cast<std::uintptr_t>(&tl_mbstowcs)},
        {"wcstombs", 88, reinterpret_cast<std::uintptr_t>(&tl_wcstombs)},
        {"fwprintf", 89, reinterpret_cast<std::uintptr_t>(&tl_fwprintf)},
        {"fputwc", 90, reinterpret_cast<std::uintptr_t>(&tl_fputwc)},
        {"_acmdln", 91, reinterpret_cast<std::uintptr_t>(&g_guest_acmdln)},
        {"_ismbblead", 92, reinterpret_cast<std::uintptr_t>(&tl__ismbblead)},
        {"_localtime64", 93, reinterpret_cast<std::uintptr_t>(&tl__localtime64)},
        {"_time64", 94, reinterpret_cast<std::uintptr_t>(&tl__time64)},
        {"strftime", 95, reinterpret_cast<std::uintptr_t>(&tl_strftime)},
        {"_strlwr", 96, reinterpret_cast<std::uintptr_t>(&tl__strlwr)},
        {"_c_exit", 97, reinterpret_cast<std::uintptr_t>(&tl__c_exit)},
        {"_exit", 98, reinterpret_cast<std::uintptr_t>(&tl__exit)},
        {"_XcptFilter", 99, reinterpret_cast<std::uintptr_t>(&tl__XcptFilter)},
        {"__dllonexit", 100, reinterpret_cast<std::uintptr_t>(&tl___dllonexit)},
        {"_beginthreadex", 101, reinterpret_cast<std::uintptr_t>(&tl__beginthreadex)},
        {"memcmp", 102, reinterpret_cast<std::uintptr_t>(&tl_memcmp)},
        {"wcscmp", 103, reinterpret_cast<std::uintptr_t>(&tl_wcscmp)},
        {"wcsstr", 104, reinterpret_cast<std::uintptr_t>(&tl_wcsstr)},
        {"__CxxFrameHandler", 105, reinterpret_cast<std::uintptr_t>(&tl___CxxFrameHandler)},
        {"_CxxThrowException", 106, reinterpret_cast<std::uintptr_t>(&tl__CxxThrowException)},
        {"_purecall", 107, reinterpret_cast<std::uintptr_t>(&tl__purecall)},
        {"?terminate@@YAXXZ", 108, reinterpret_cast<std::uintptr_t>(&tl_terminate)},
        {"??1type_info@@UEAA@XZ", 109, reinterpret_cast<std::uintptr_t>(&tl__c_exit)},
        {"_iob", 110, reinterpret_cast<std::uintptr_t>(g_guest_iob)},
        {"rand", 111, reinterpret_cast<std::uintptr_t>(&tl_rand)},
        {"srand", 112, reinterpret_cast<std::uintptr_t>(&tl_srand)},
    };
    static const InternalModule kMsvcrtModule{"msvcrt.dll", kMsvcrtExports};
    register_module(kMsvcrtModule);
    static const ExportedFunction kShell32Exports[] = {
        {"CommandLineToArgvW", 1, reinterpret_cast<std::uintptr_t>(&tl_CommandLineToArgvW)},
        {"Shell_NotifyIconA", 2, reinterpret_cast<std::uintptr_t>(&tl_ShellNotifyIconA)},
        {"SHGetKnownFolderPath", 3, reinterpret_cast<std::uintptr_t>(&tl_SHGetKnownFolderPath)},
        {"SHGetFolderPathW", 4, reinterpret_cast<std::uintptr_t>(&tl_SHGetFolderPathW)},
        {"SHGetFolderPathAndSubDirW", 5, reinterpret_cast<std::uintptr_t>(&tl_SHGetFolderPathAndSubDirW)},
        {"ShellExecuteW", 6, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteW)},
        {"ShellExecuteExW", 7, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteExW)},
        {"SHFileOperationW", 8, reinterpret_cast<std::uintptr_t>(&tl_SHFileOperationW)},
        {"SHGetFileInfoW", 9, reinterpret_cast<std::uintptr_t>(&tl_SHGetFileInfoW)},
        {"SHGetPathFromIDListW", 10, reinterpret_cast<std::uintptr_t>(&tl_SHGetPathFromIDListW)},
        {"SHBrowseForFolderW", 11, reinterpret_cast<std::uintptr_t>(&tl_SHBrowseForFolderW)},
        {"SHGetMalloc", 12, reinterpret_cast<std::uintptr_t>(&tl_SHGetMalloc)},
        {"SHChangeNotify", 13, reinterpret_cast<std::uintptr_t>(&tl_SHChangeNotify)},
        {"ExtractIconExW", 14, reinterpret_cast<std::uintptr_t>(&tl_ExtractIconExW)},
        {"SHGetDesktopFolder", 15, reinterpret_cast<std::uintptr_t>(&tl_SHGetDesktopFolder)},
        {"SHGetSpecialFolderLocation", 16, reinterpret_cast<std::uintptr_t>(&tl_SHGetSpecialFolderLocation)},
        {"SHGetSpecialFolderPathW", 17, reinterpret_cast<std::uintptr_t>(&tl_SHGetSpecialFolderPathW)},
        {"Shell_NotifyIconW", 18, reinterpret_cast<std::uintptr_t>(&tl_ShellNotifyIconW)},
        {"ShellExecuteA", 19, reinterpret_cast<std::uintptr_t>(&tl_ShellExecuteA)},
    };
    static const InternalModule kShell32Module{"SHELL32.dll", kShell32Exports};
    register_module(kShell32Module);
    static const ExportedFunction kAdvapi32Exports[] = {
        {"RegCloseKey", 1, reinterpret_cast<std::uintptr_t>(&tl_RegCloseKey)},
        {"RegDeleteValueA", 2, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteValueA)},
        {"RegDeleteValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteValueW)},
        {"RegCreateKeyExA", 7, reinterpret_cast<std::uintptr_t>(&tl_RegCreateKeyExA)},
        {"RegCreateKeyExW", 8, reinterpret_cast<std::uintptr_t>(&tl_RegCreateKeyExW)},
        {"RegOpenKeyExA", 3, reinterpret_cast<std::uintptr_t>(&tl_RegOpenKeyExA)},
        {"RegOpenKeyExW", 9, reinterpret_cast<std::uintptr_t>(&tl_RegOpenKeyExW)},
        {"RegQueryValueExA", 4, reinterpret_cast<std::uintptr_t>(&tl_RegQueryValueExA)},
        {"RegQueryValueExW", 10, reinterpret_cast<std::uintptr_t>(&tl_RegQueryValueExW)},
        {"RegSetValueExA", 5, reinterpret_cast<std::uintptr_t>(&tl_RegSetValueExA)},
        {"RegSetValueExW", 11, reinterpret_cast<std::uintptr_t>(&tl_RegSetValueExW)},
        {"CryptAcquireContextA", 12, reinterpret_cast<std::uintptr_t>(&tl_CryptAcquireContextA)},
        {"CryptAcquireContextW", 13, reinterpret_cast<std::uintptr_t>(&tl_CryptAcquireContextW)},
        {"CryptGenRandom", 14, reinterpret_cast<std::uintptr_t>(&tl_CryptGenRandom)},
        {"CryptReleaseContext", 15, reinterpret_cast<std::uintptr_t>(&tl_CryptReleaseContext)},
        {"OpenProcessToken", 16, reinterpret_cast<std::uintptr_t>(&tl_OpenProcessToken)},
        {"GetTokenInformation", 17, reinterpret_cast<std::uintptr_t>(&tl_GetTokenInformation)},
        {"AllocateAndInitializeSid", 18, reinterpret_cast<std::uintptr_t>(&tl_AllocateAndInitializeSid)},
        {"FreeSid", 19, reinterpret_cast<std::uintptr_t>(&tl_FreeSid)},
        {"GetLengthSid", 20, reinterpret_cast<std::uintptr_t>(&tl_GetLengthSid)},
        {"CopySid", 21, reinterpret_cast<std::uintptr_t>(&tl_CopySid)},
        {"EqualSid", 22, reinterpret_cast<std::uintptr_t>(&tl_EqualSid)},
        {"IsValidSid", 23, reinterpret_cast<std::uintptr_t>(&tl_IsValidSid)},
        {"CreateWellKnownSid", 24, reinterpret_cast<std::uintptr_t>(&tl_CreateWellKnownSid)},
        {"CheckTokenMembership", 25, reinterpret_cast<std::uintptr_t>(&tl_CheckTokenMembership)},
        {"BuildTrusteeWithSidW", 26, reinterpret_cast<std::uintptr_t>(&tl_BuildTrusteeWithSidW)},
        {"InitializeSecurityDescriptor", 27, reinterpret_cast<std::uintptr_t>(&tl_InitializeSecurityDescriptor)},
        {"SetSecurityDescriptorDacl", 28, reinterpret_cast<std::uintptr_t>(&tl_SetSecurityDescriptorDacl)},
        {"SetEntriesInAclW", 29, reinterpret_cast<std::uintptr_t>(&tl_SetEntriesInAclW)},
        {"GetNamedSecurityInfoW", 30, reinterpret_cast<std::uintptr_t>(&tl_GetNamedSecurityInfoW)},
        {"SetNamedSecurityInfoW", 31, reinterpret_cast<std::uintptr_t>(&tl_SetNamedSecurityInfoW)},
        {"SetFileSecurityW", 32, reinterpret_cast<std::uintptr_t>(&tl_SetFileSecurityW)},
        {"LookupPrivilegeValueW", 33, reinterpret_cast<std::uintptr_t>(&tl_LookupPrivilegeValueW)},
        {"AdjustTokenPrivileges", 34, reinterpret_cast<std::uintptr_t>(&tl_AdjustTokenPrivileges)},
        {"GetFileSecurityW", 35, reinterpret_cast<std::uintptr_t>(&tl_GetFileSecurityW)},
        {"RegDeleteTreeW", 36, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteTreeW)},
        {"RegEnumValueW", 37, reinterpret_cast<std::uintptr_t>(&tl_RegEnumValueW)},
        {"RegEnumKeyExW", 38, reinterpret_cast<std::uintptr_t>(&tl_RegEnumKeyExW)},
        {"RegDeleteKeyExW", 39, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyExW)},
        {"RegDeleteKeyW", 40, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyW)},
        {"GetUserNameW", 41, reinterpret_cast<std::uintptr_t>(&tl_GetUserNameW)},
        {"LookupAccountNameW", 42, reinterpret_cast<std::uintptr_t>(&tl_LookupAccountNameW)},
        {"LsaOpenPolicy", 43, reinterpret_cast<std::uintptr_t>(&tl_LsaOpenPolicy)},
        {"LsaClose", 44, reinterpret_cast<std::uintptr_t>(&tl_LsaClose)},
        {"LsaAddAccountRights", 45, reinterpret_cast<std::uintptr_t>(&tl_LsaAddAccountRights)},
        {"RegQueryInfoKeyA", 46, reinterpret_cast<std::uintptr_t>(&tl_RegQueryInfoKeyA)},
        {"RegQueryInfoKeyW", 47, reinterpret_cast<std::uintptr_t>(&tl_RegQueryInfoKeyW)},
        {"RegEnumKeyA", 48, reinterpret_cast<std::uintptr_t>(&tl_RegEnumKeyA)},
        {"RegEnumValueA", 49, reinterpret_cast<std::uintptr_t>(&tl_RegEnumValueA)},
        {"RegDeleteKeyA", 50, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteKeyA)},
        {"RegGetValueW", 51, reinterpret_cast<std::uintptr_t>(&tl_RegGetValueW)},
        {"RegisterEventSourceW", 52, reinterpret_cast<std::uintptr_t>(&tl_RegisterEventSourceW)},
        {"DeregisterEventSource", 53, reinterpret_cast<std::uintptr_t>(&tl_DeregisterEventSource)},
        {"ReportEventW", 54, reinterpret_cast<std::uintptr_t>(&tl_ReportEventW)},
        {"CryptCreateHash", 55, reinterpret_cast<std::uintptr_t>(&tl_CryptCreateHash)},
        {"CryptHashData", 56, reinterpret_cast<std::uintptr_t>(&tl_CryptHashData)},
        {"CryptGetHashParam", 57, reinterpret_cast<std::uintptr_t>(&tl_CryptGetHashParam)},
        {"CryptSetHashParam", 58, reinterpret_cast<std::uintptr_t>(&tl_CryptSetHashParam)},
        {"CryptDestroyHash", 59, reinterpret_cast<std::uintptr_t>(&tl_CryptDestroyHash)},
        {"CryptSignHashW", 60, reinterpret_cast<std::uintptr_t>(&tl_CryptSignHashW)},
        {"CryptDecrypt", 61, reinterpret_cast<std::uintptr_t>(&tl_CryptDecrypt)},
        {"CryptExportKey", 62, reinterpret_cast<std::uintptr_t>(&tl_CryptExportKey)},
        {"CryptGetUserKey", 63, reinterpret_cast<std::uintptr_t>(&tl_CryptGetUserKey)},
        {"CryptGetProvParam", 64, reinterpret_cast<std::uintptr_t>(&tl_CryptGetProvParam)},
        {"CryptDestroyKey", 65, reinterpret_cast<std::uintptr_t>(&tl_CryptDestroyKey)},
        {"CryptEnumProvidersW", 66, reinterpret_cast<std::uintptr_t>(&tl_CryptEnumProvidersW)},
        {"SystemFunction036", 67, reinterpret_cast<std::uintptr_t>(&tl_SystemFunction036)},
        {"GetUserNameA", 68, reinterpret_cast<std::uintptr_t>(&tl_GetUserNameA)},
        {"SetSecurityDescriptorOwner", 69, reinterpret_cast<std::uintptr_t>(&tl_SetSecurityDescriptorOwner)},
    };
    static const InternalModule kAdvapi32Module{"ADVAPI32.dll", kAdvapi32Exports};
    register_module(kAdvapi32Module);
    static const ExportedFunction kOle32Exports[] = {
        {"CoInitialize", 1, reinterpret_cast<std::uintptr_t>(&tl_CoInitialize)},
        {"CoInitializeEx", 2, reinterpret_cast<std::uintptr_t>(&tl_CoInitializeEx)},
        {"CoUninitialize", 3, reinterpret_cast<std::uintptr_t>(&tl_CoUninitialize)},
        {"CoCreateGuid", 4, reinterpret_cast<std::uintptr_t>(&tl_CoCreateGuid)},
        {"CoTaskMemAlloc", 5, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemAlloc)},
        {"CoTaskMemFree", 6, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemFree)},
        {"CoTaskMemRealloc", 7, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemRealloc)},
        {"CreateStreamOnHGlobal", 12, reinterpret_cast<std::uintptr_t>(&tl_CreateStreamOnHGlobal)},
        {"CoCreateInstance", 8, reinterpret_cast<std::uintptr_t>(&tl_CoCreateInstance)},
        {"CoGetClassObject", 9, reinterpret_cast<std::uintptr_t>(&tl_CoGetClassObject)},
        {"OleInitialize", 10, reinterpret_cast<std::uintptr_t>(&tl_OleInitialize)},
        {"OleUninitialize", 11, reinterpret_cast<std::uintptr_t>(&tl_OleUninitialize)},
        {"CLSIDFromString", 13, reinterpret_cast<std::uintptr_t>(&tl_CLSIDFromString)},
        {"RegisterDragDrop", 14, reinterpret_cast<std::uintptr_t>(&tl_RegisterDragDrop)},
        {"RevokeDragDrop", 15, reinterpret_cast<std::uintptr_t>(&tl_RevokeDragDrop)},
        {"DoDragDrop", 16, reinterpret_cast<std::uintptr_t>(&tl_DoDragDrop)},
        {"ReleaseStgMedium", 17, reinterpret_cast<std::uintptr_t>(&tl_ReleaseStgMedium)},
        {"StringFromGUID2", 18, reinterpret_cast<std::uintptr_t>(&tl_StringFromGUID2)},
    };
    static const InternalModule kOle32Module{"ole32.dll", kOle32Exports};
    register_module(kOle32Module);
    static const ExportedFunction kOleaut32Exports[] = {
        {"SysAllocString", 2, reinterpret_cast<std::uintptr_t>(&tl_SysAllocString)},
        {"SysReAllocString", 3, reinterpret_cast<std::uintptr_t>(&tl_SysReAllocString)},
        {"SysAllocStringLen", 4, reinterpret_cast<std::uintptr_t>(&tl_SysAllocStringLen)},
        {"SysReAllocStringLen", 5, reinterpret_cast<std::uintptr_t>(&tl_SysReAllocStringLen)},
        {"SysFreeString", 6, reinterpret_cast<std::uintptr_t>(&tl_SysFreeString)},
        {"SysStringLen", 7, reinterpret_cast<std::uintptr_t>(&tl_SysStringLen)},
        {"VariantInit", 8, reinterpret_cast<std::uintptr_t>(&tl_VariantInit)},
        {"VariantClear", 9, reinterpret_cast<std::uintptr_t>(&tl_VariantClear)},
        {"VariantCopy", 10, reinterpret_cast<std::uintptr_t>(&tl_VariantCopy)},
        {"VariantCopyInd", 11, reinterpret_cast<std::uintptr_t>(&tl_VariantCopyInd)},
        {"VariantChangeType", 12, reinterpret_cast<std::uintptr_t>(&tl_VariantChangeType)},
        {"SafeArrayCreate", 15, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayCreate)},
        {"SafeArrayDestroy", 16, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroy)},
        {"SafeArrayGetDim", 17, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetDim)},
        {"SafeArrayGetElemsize", 18, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetElemsize)},
        {"SafeArrayGetUBound", 19, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetUBound)},
        {"SafeArrayGetLBound", 20, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayGetLBound)},
        {"SafeArrayLock", 21, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayLock)},
        {"SafeArrayUnlock", 22, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayUnlock)},
        {"SafeArrayAccessData", 23, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayAccessData)},
        {"SafeArrayUnaccessData", 24, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayUnaccessData)},
        {"VariantChangeTypeEx", 147, reinterpret_cast<std::uintptr_t>(&tl_VariantChangeTypeEx)},
        {"SysStringByteLen", 149, reinterpret_cast<std::uintptr_t>(&tl_SysStringByteLen)},
        {"SysAllocStringByteLen", 150, reinterpret_cast<std::uintptr_t>(&tl_SysAllocStringByteLen)},
        {"SafeArrayDestroyData", 200, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroyData)},
        {"SafeArrayDestroyDescriptor", 201, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayDestroyDescriptor)},
        {"SafeArrayCreateVector", 411, reinterpret_cast<std::uintptr_t>(&tl_SafeArrayCreateVector)},
    };
    static const InternalModule kOleaut32Module{"OLEAUT32.dll", kOleaut32Exports};
    register_module(kOleaut32Module);
    static const ExportedFunction kWintrustExports[] = {
        {"WinVerifyTrust", 1, reinterpret_cast<std::uintptr_t>(&tl_WinVerifyTrust)},
        {"WTHelperProvDataFromStateData", 2,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperProvDataFromStateData)},
        {"WTHelperGetProvSignerFromChain", 3,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperGetProvSignerFromChain)},
        {"WTHelperGetProvCertFromChain", 4,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperGetProvCertFromChain)},
    };
    static const InternalModule kWintrustModule{"WINTRUST.dll", kWintrustExports};
    register_module(kWintrustModule);
    static const ExportedFunction kCrypt32Exports[] = {
        {"CertGetNameStringW", 1, reinterpret_cast<std::uintptr_t>(&tl_CertGetNameStringW)},
        {"CertDuplicateCertificateContext", 2,
         reinterpret_cast<std::uintptr_t>(&tl_CertDuplicateCertificateContext)},
        {"CertFreeCertificateContext", 3,
         reinterpret_cast<std::uintptr_t>(&tl_CertFreeCertificateContext)},
        {"CertOpenStore", 4, reinterpret_cast<std::uintptr_t>(&tl_CertOpenStore)},
        {"CertCloseStore", 5, reinterpret_cast<std::uintptr_t>(&tl_CertCloseStore)},
        {"CertEnumCertificatesInStore", 6,
         reinterpret_cast<std::uintptr_t>(&tl_CertEnumCertificatesInStore)},
        {"CertFindCertificateInStore", 7,
         reinterpret_cast<std::uintptr_t>(&tl_CertFindCertificateInStore)},
        {"CertGetCertificateContextProperty", 8,
         reinterpret_cast<std::uintptr_t>(&tl_CertGetCertificateContextProperty)},
        {"CertOpenSystemStoreA", 9, reinterpret_cast<std::uintptr_t>(&tl_CertOpenSystemStoreA)},
        {"CertOpenSystemStoreW", 10, reinterpret_cast<std::uintptr_t>(&tl_CertOpenSystemStoreW)},
        {"CertGetEnhancedKeyUsage", 11, reinterpret_cast<std::uintptr_t>(&tl_CertGetEnhancedKeyUsage)},
        {"CertGetIntendedKeyUsage", 12, reinterpret_cast<std::uintptr_t>(&tl_CertGetIntendedKeyUsage)},
    };
    static const InternalModule kCrypt32Module{"CRYPT32.dll", kCrypt32Exports};
    register_module(kCrypt32Module);
    static const ExportedFunction kShlwapiExports[] = {
        {"PathFileExistsA", 1, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsA)},
        {"PathFileExistsW", 2, reinterpret_cast<std::uintptr_t>(&tl_PathFileExistsW)},
        {"PathIsDirectoryA", 3, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryA)},
        {"PathIsDirectoryW", 4, reinterpret_cast<std::uintptr_t>(&tl_PathIsDirectoryW)},
        {"PathCombineA", 5, reinterpret_cast<std::uintptr_t>(&tl_PathCombineA)},
        {"PathCombineW", 6, reinterpret_cast<std::uintptr_t>(&tl_PathCombineW)},
        {"PathFindFileNameA", 7, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameA)},
        {"PathFindFileNameW", 8, reinterpret_cast<std::uintptr_t>(&tl_PathFindFileNameW)},
        {"PathFindExtensionA", 9, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionA)},
        {"PathFindExtensionW", 10, reinterpret_cast<std::uintptr_t>(&tl_PathFindExtensionW)},
        {"PathRemoveFileSpecA", 11, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecA)},
        {"PathRemoveFileSpecW", 12, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveFileSpecW)},
        {"PathAddBackslashA", 13, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashA)},
        {"PathAddBackslashW", 14, reinterpret_cast<std::uintptr_t>(&tl_PathAddBackslashW)},
        {"PathRemoveBackslashA", 15, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashA)},
        {"PathRemoveBackslashW", 16, reinterpret_cast<std::uintptr_t>(&tl_PathRemoveBackslashW)},
        {"StrStrIA", 17, reinterpret_cast<std::uintptr_t>(&tl_StrStrIA)},
        {"StrStrIW", 18, reinterpret_cast<std::uintptr_t>(&tl_StrStrIW)},
        {"StrCmpIA", 19, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIA)},
        {"StrCmpIW", 20, reinterpret_cast<std::uintptr_t>(&tl_StrCmpIW)},
        {"PathIsRelativeA", 21, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeA)},
        {"PathIsRelativeW", 22, reinterpret_cast<std::uintptr_t>(&tl_PathIsRelativeW)},
        {"SHAutoComplete", 23, reinterpret_cast<std::uintptr_t>(&tl_SHAutoComplete)},
        {"PathStripToRootW", 24, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"", 176, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"", 410, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
        {"", 413, reinterpret_cast<std::uintptr_t>(&tl_PathStripToRootW)},
    };
    static const InternalModule kShlwapiModule{"SHLWAPI.dll", kShlwapiExports};
    register_module(kShlwapiModule);
    static const ExportedFunction kVersionExports[] = {
        {"GetFileVersionInfoSizeA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeA)},
        {"GetFileVersionInfoSizeW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeW)},
        {"GetFileVersionInfoA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoA)},
        {"GetFileVersionInfoW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoW)},
        {"VerQueryValueA", 5, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueA)},
        {"VerQueryValueW", 6, reinterpret_cast<std::uintptr_t>(&tl_VerQueryValueW)},
        {"GetFileVersionInfoSizeExA", 7, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExA)},
        {"GetFileVersionInfoSizeExW", 8, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoSizeExW)},
        {"GetFileVersionInfoExA", 9, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExA)},
        {"GetFileVersionInfoExW", 10, reinterpret_cast<std::uintptr_t>(&tl_GetFileVersionInfoExW)},
    };
    static const InternalModule kVersionModule{"version.dll", kVersionExports};
    register_module(kVersionModule);
    static const ExportedFunction kWinmmExports[] = {
        {"timeGetTime", 1, reinterpret_cast<std::uintptr_t>(&tl_timeGetTime)},
        {"timeBeginPeriod", 2, reinterpret_cast<std::uintptr_t>(&tl_timeBeginPeriod)},
        {"timeEndPeriod", 3, reinterpret_cast<std::uintptr_t>(&tl_timeEndPeriod)},
        {"timeGetDevCaps", 4, reinterpret_cast<std::uintptr_t>(&tl_timeGetDevCaps)},
        {"PlaySoundA", 5, reinterpret_cast<std::uintptr_t>(&tl_PlaySoundA)},
        {"PlaySoundW", 6, reinterpret_cast<std::uintptr_t>(&tl_PlaySoundW)},
        {"timeSetEvent", 7, reinterpret_cast<std::uintptr_t>(&tl_timeSetEvent)},
    };
    static const InternalModule kWinmmModule{"WINMM.dll", kWinmmExports};
    register_module(kWinmmModule);
    static const ExportedFunction kGdiplusExports[] = {
        {"GdiplusStartup", 1, reinterpret_cast<std::uintptr_t>(&tl_GdiplusStartup)},
        {"GdiplusShutdown", 2, reinterpret_cast<std::uintptr_t>(&tl_GdiplusShutdown)},
        {"GdipAlloc", 3, reinterpret_cast<std::uintptr_t>(&tl_GdipAlloc)},
        {"GdipFree", 4, reinterpret_cast<std::uintptr_t>(&tl_GdipFree)},
        {"GdipCreateBitmapFromStream", 5, reinterpret_cast<std::uintptr_t>(&tl_GdipCreateBitmapFromStream)},
        {"GdipCloneImage", 6, reinterpret_cast<std::uintptr_t>(&tl_GdipCloneImage)},
        {"GdipDisposeImage", 7, reinterpret_cast<std::uintptr_t>(&tl_GdipDisposeImage)},
        {"GdipCreateHBITMAPFromBitmap", 8, reinterpret_cast<std::uintptr_t>(&tl_GdipCreateHBITMAPFromBitmap)},
    };
    static const InternalModule kGdiplusModule{"gdiplus.dll", kGdiplusExports};
    register_module(kGdiplusModule);
    static const ExportedFunction kUxThemeExports[] = {
        {"SetWindowTheme", 1, reinterpret_cast<std::uintptr_t>(&tl_SetWindowTheme)},
        {"OpenThemeData", 2, reinterpret_cast<std::uintptr_t>(&tl_OpenThemeData)},
        {"CloseThemeData", 3, reinterpret_cast<std::uintptr_t>(&tl_CloseThemeData)},
        {"DrawThemeBackground", 4, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeBackground)},
        {"DrawThemeText", 5, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeText)},
        {"DrawThemeTextEx", 6, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeTextEx)},
        {"GetThemeColor", 7, reinterpret_cast<std::uintptr_t>(&tl_GetThemeColor)},
        {"GetThemeFont", 8, reinterpret_cast<std::uintptr_t>(&tl_GetThemeFont)},
        {"GetThemeMetric", 9, reinterpret_cast<std::uintptr_t>(&tl_GetThemeMetric)},
        {"GetThemePartSize", 10, reinterpret_cast<std::uintptr_t>(&tl_GetThemePartSize)},
        {"GetThemeSysColor", 11, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColor)},
        {"GetThemeSysColorBrush", 12, reinterpret_cast<std::uintptr_t>(&tl_GetThemeSysColorBrush)},
        {"IsThemeActive", 13, reinterpret_cast<std::uintptr_t>(&tl_IsThemeActive)},
        {"IsAppThemed", 14, reinterpret_cast<std::uintptr_t>(&tl_IsAppThemed)},
        {"IsThemeBackgroundPartiallyTransparent", 15, reinterpret_cast<std::uintptr_t>(&tl_IsThemeBackgroundPartiallyTransparent)},
        {"BufferedPaintInit", 16, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintInit)},
        {"BufferedPaintUnInit", 17, reinterpret_cast<std::uintptr_t>(&tl_BufferedPaintUnInit)},
        {"BeginBufferedPaint", 18, reinterpret_cast<std::uintptr_t>(&tl_BeginBufferedPaint)},
        {"EndBufferedPaint", 19, reinterpret_cast<std::uintptr_t>(&tl_EndBufferedPaint)},
        {"DrawThemeParentBackground", 20, reinterpret_cast<std::uintptr_t>(&tl_DrawThemeParentBackground)},
    };
    static const InternalModule kUxThemeModule{"UxTheme.dll", kUxThemeExports};
    register_module(kUxThemeModule);
    static const ExportedFunction kDbghelpExports[] = {
        {"SymFromAddr", 1, reinterpret_cast<std::uintptr_t>(&tl_SymFromAddr)},
    };
    static const InternalModule kDbghelpModule{"dbghelp.dll", kDbghelpExports};
    register_module(kDbghelpModule);
    static const ExportedFunction kPowrProfExports[] = {
        {"PowerGetActiveScheme", 1, reinterpret_cast<std::uintptr_t>(&tl_PowerGetActiveScheme)},
        {"PowerSetActiveScheme", 2, reinterpret_cast<std::uintptr_t>(&tl_PowerSetActiveScheme)},
        {"CallNtPowerInformation", 3, reinterpret_cast<std::uintptr_t>(&tl_CallNtPowerInformation)},
    };
    static const InternalModule kPowrProfModule{"POWRPROF.dll", kPowrProfExports};
    register_module(kPowrProfModule);
    static const ExportedFunction kIphlpapiExports[] = {
        {"GetAdaptersInfo", 1, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersInfo)},
        {"GetAdaptersAddresses", 2, reinterpret_cast<std::uintptr_t>(&tl_GetAdaptersAddresses)},
        {"if_nametoindex", 3, reinterpret_cast<std::uintptr_t>(&tl_if_nametoindex)},
    };
    static const InternalModule kIphlpapiModule{"IPHLPAPI.DLL", kIphlpapiExports};
    register_module(kIphlpapiModule);
    static const ExportedFunction kComctl32Exports[] = {
        {"InitCommonControls", 1, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControls)},
        {"InitCommonControlsEx", 2, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControlsEx)},
        {"ImageList_Create", 3, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Create)},
        {"ImageList_Destroy", 4, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Destroy)},
        {"ImageList_Add", 5, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Add)},
        {"ImageList_AddMasked", 6, reinterpret_cast<std::uintptr_t>(&tl_ImageList_AddMasked)},
        {"ImageList_ReplaceIcon", 7, reinterpret_cast<std::uintptr_t>(&tl_ImageList_ReplaceIcon)},
        {"CreateStatusWindowW", 8, reinterpret_cast<std::uintptr_t>(&tl_CreateStatusWindowW)},
        {"CreateToolbarEx", 9, reinterpret_cast<std::uintptr_t>(&tl_CreateToolbarEx)},
        {"ImageList_GetImageCount", 10, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetImageCount)},
        {"PropertySheetW", 11, reinterpret_cast<std::uintptr_t>(&tl_PropertySheetW)},
        {"TaskDialogIndirect", 12, reinterpret_cast<std::uintptr_t>(&tl_TaskDialogIndirect)},
        {"TaskDialog", 13, reinterpret_cast<std::uintptr_t>(&tl_TaskDialog)},
        {"ImageList_Draw", 14, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Draw)},
        {"ImageList_DrawEx", 15, reinterpret_cast<std::uintptr_t>(&tl_ImageList_DrawEx)},
        {"ImageList_GetIcon", 16, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetIcon)},
        {"ImageList_Duplicate", 17, reinterpret_cast<std::uintptr_t>(&tl_ImageList_Duplicate)},
        {"ImageList_SetBkColor", 18, reinterpret_cast<std::uintptr_t>(&tl_ImageList_SetBkColor)},
        {"ImageList_GetBkColor", 19, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetBkColor)},
        {"ImageList_GetIconSize", 20, reinterpret_cast<std::uintptr_t>(&tl_ImageList_GetIconSize)},
        {"_TrackMouseEvent", 21, reinterpret_cast<std::uintptr_t>(&tl_TrackMouseEvent_alias)},
        {"SetWindowSubclass", 410, reinterpret_cast<std::uintptr_t>(&tl_SetWindowSubclass)},
        {"RemoveWindowSubclass", 412, reinterpret_cast<std::uintptr_t>(&tl_RemoveWindowSubclass)},
        {"DefSubclassProc", 413, reinterpret_cast<std::uintptr_t>(&tl_DefSubclassProc)},
        {"", 17, reinterpret_cast<std::uintptr_t>(&tl_InitCommonControls)},
        {"", 410, reinterpret_cast<std::uintptr_t>(&tl_SetWindowSubclass)},
        {"", 412, reinterpret_cast<std::uintptr_t>(&tl_RemoveWindowSubclass)},
        {"", 413, reinterpret_cast<std::uintptr_t>(&tl_DefSubclassProc)},
    };
    static const InternalModule kComctl32Module{"COMCTL32.dll", kComctl32Exports};
    register_module(kComctl32Module);
    static const ExportedFunction kComdlg32Exports[] = {
        {"GetOpenFileNameA", 1, reinterpret_cast<std::uintptr_t>(&tl_GetOpenFileNameA)},
        {"GetOpenFileNameW", 2, reinterpret_cast<std::uintptr_t>(&tl_GetOpenFileNameW)},
        {"GetSaveFileNameA", 3, reinterpret_cast<std::uintptr_t>(&tl_GetSaveFileNameA)},
        {"GetSaveFileNameW", 4, reinterpret_cast<std::uintptr_t>(&tl_GetSaveFileNameW)},
        {"ChooseColorA", 5, reinterpret_cast<std::uintptr_t>(&tl_ChooseColorA)},
        {"ChooseColorW", 6, reinterpret_cast<std::uintptr_t>(&tl_ChooseColorW)},
        {"PrintDlgW", 7, reinterpret_cast<std::uintptr_t>(&tl_PrintDlgW)},
        {"CommDlgExtendedError", 8, reinterpret_cast<std::uintptr_t>(&tl_CommDlgExtendedError)},
        {"ChooseFontA", 9, reinterpret_cast<std::uintptr_t>(&tl_ChooseFontA)},
        {"ChooseFontW", 10, reinterpret_cast<std::uintptr_t>(&tl_ChooseFontW)},
    };
    static const InternalModule kComdlg32Module{"COMDLG32.dll", kComdlg32Exports};
    register_module(kComdlg32Module);
    static const ExportedFunction kImm32Exports[] = {
        {"ImmGetContext", 1, reinterpret_cast<std::uintptr_t>(&tl_ImmGetContext)},
        {"ImmReleaseContext", 2, reinterpret_cast<std::uintptr_t>(&tl_ImmReleaseContext)},
        {"ImmSetCompositionWindow", 3, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionWindow)},
        {"ImmGetCompositionStringA", 4, reinterpret_cast<std::uintptr_t>(&tl_ImmGetCompositionStringA)},
        {"ImmGetCompositionStringW", 5, reinterpret_cast<std::uintptr_t>(&tl_ImmGetCompositionStringW)},
        {"ImmAssociateContext", 6, reinterpret_cast<std::uintptr_t>(&tl_ImmAssociateContext)},
        {"ImmGetVirtualKey", 7, reinterpret_cast<std::uintptr_t>(&tl_ImmGetVirtualKey)},
        {"ImmSetCompositionFontA", 8, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionFontA)},
        {"ImmSetCompositionFontW", 9, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionFontW)},
        {"ImmSetCandidateWindow", 10, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCandidateWindow)},
        {"ImmSetCompositionStringW", 11, reinterpret_cast<std::uintptr_t>(&tl_ImmSetCompositionStringW)},
        {"ImmEscapeW", 12, reinterpret_cast<std::uintptr_t>(&tl_ImmEscapeW)},
        {"ImmNotifyIME", 13, reinterpret_cast<std::uintptr_t>(&tl_ImmNotifyIME)},
    };
    static const InternalModule kImm32Module{"IMM32.dll", kImm32Exports};
    register_module(kImm32Module);
    static const ExportedFunction kPsapiExports[] = {
        {"EnumProcesses", 1, reinterpret_cast<std::uintptr_t>(&tl_EnumProcesses)},
        {"EnumProcessModules", 2, reinterpret_cast<std::uintptr_t>(&tl_EnumProcessModules)},
        {"EnumProcessModulesEx", 3, reinterpret_cast<std::uintptr_t>(&tl_EnumProcessModulesEx)},
        {"GetModuleBaseNameA", 4, reinterpret_cast<std::uintptr_t>(&tl_GetModuleBaseNameA)},
        {"GetModuleBaseNameW", 5, reinterpret_cast<std::uintptr_t>(&tl_GetModuleBaseNameW)},
        {"GetModuleFileNameExA", 6, reinterpret_cast<std::uintptr_t>(&tl_GetModuleFileNameExA)},
        {"GetModuleFileNameExW", 7, reinterpret_cast<std::uintptr_t>(&tl_GetModuleFileNameExW)},
        {"GetProcessMemoryInfo", 8, reinterpret_cast<std::uintptr_t>(&tl_GetProcessMemoryInfo)},
    };
    static const InternalModule kPsapiModule{"PSAPI.dll", kPsapiExports};
    register_module(kPsapiModule);
    static const ExportedFunction kMprExports[] = {
        {"WNetAddConnection2W", 1, reinterpret_cast<std::uintptr_t>(&tl_WNetAddConnection2W)},
        {"WNetOpenEnumW", 2, reinterpret_cast<std::uintptr_t>(&tl_WNetOpenEnumW)},
        {"WNetEnumResourceW", 3, reinterpret_cast<std::uintptr_t>(&tl_WNetEnumResourceW)},
        {"WNetCloseEnum", 4, reinterpret_cast<std::uintptr_t>(&tl_WNetCloseEnum)},
        {"WNetGetResourceInformationW", 5,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceInformationW)},
        {"WNetGetResourceParentW", 6,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceParentW)},
    };
    static const InternalModule kMprModule{"MPR.dll", kMprExports};
    register_module(kMprModule);
    static const ExportedFunction kDwmApiExports[] = {
        {"DwmSetWindowAttribute", 1, reinterpret_cast<std::uintptr_t>(&tl_DwmSetWindowAttribute)},
        {"DwmGetWindowAttribute", 2, reinterpret_cast<std::uintptr_t>(&tl_DwmGetWindowAttribute)},
        {"DwmIsCompositionEnabled", 3, reinterpret_cast<std::uintptr_t>(&tl_DwmIsCompositionEnabled)},
        {"DwmDefWindowProc", 4, reinterpret_cast<std::uintptr_t>(&tl_DwmDefWindowProc)},
        {"DwmExtendFrameIntoClientArea", 5, reinterpret_cast<std::uintptr_t>(&tl_DwmExtendFrameIntoClientArea)},
        {"DwmEnableBlurBehindWindow", 6, reinterpret_cast<std::uintptr_t>(&tl_DwmEnableBlurBehindWindow)},
        {"DwmFlush", 7, reinterpret_cast<std::uintptr_t>(&tl_DwmFlush)},
        {"DwmGetColorizationColor", 8, reinterpret_cast<std::uintptr_t>(&tl_DwmGetColorizationColor)},
    };
    static const InternalModule kDwmApiModule{"DWMAPI.dll", kDwmApiExports};
    register_module(kDwmApiModule);
}

bool is_module_registered(const std::string_view dll) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    return find_module_locked(dll) != nullptr;
}

ExportLookup find_export(const ExportQuery& query) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    const OwnedModule* module = find_module_locked(query.dll);
    if (module == nullptr) {
        return lookup;
    }
    const auto found = std::find_if(module->exports.begin(), module->exports.end(),
                                    [&](const OwnedExport& export_) {
                                        return export_.name == query.symbol;
                                    });
    if (found != module->exports.end()) {
        lookup.found = true;
        lookup.ordinal = found->ordinal;
        lookup.address = found->address;
    }
    return lookup;
}

ExportLookup find_export_by_ordinal(const std::string_view dll, const std::uint16_t ordinal) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    const OwnedModule* module = find_module_locked(dll);
    if (module == nullptr) {
        return lookup;
    }
    const auto found = std::find_if(module->exports.begin(), module->exports.end(),
                                    [&](const OwnedExport& export_) {
                                        return export_.ordinal == ordinal;
                                    });
    if (found != module->exports.end()) {
        lookup.found = true;
        lookup.ordinal = found->ordinal;
        lookup.address = found->address;
    }
    return lookup;
}

ExportLookup find_export_global(const std::string_view symbol) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    for (const auto& module : modules()) {
        const auto found = std::find_if(module.exports.begin(), module.exports.end(),
                                        [&](const OwnedExport& export_) {
                                            return export_.name == symbol;
                                        });
        if (found != module.exports.end()) {
            lookup.found = true;
            lookup.ordinal = found->ordinal;
            lookup.address = found->address;
            return lookup;
        }
    }
    return lookup;
}

ExportLookup find_export_by_ordinal_global(const std::uint16_t ordinal) {
    std::lock_guard<std::mutex> lock(modules_mutex());
    ExportLookup lookup;
    for (const auto& module : modules()) {
        const auto found = std::find_if(module.exports.begin(), module.exports.end(),
                                        [&](const OwnedExport& export_) {
                                            return export_.ordinal == ordinal;
                                        });
        if (found != module.exports.end()) {
            lookup.found = true;
            lookup.ordinal = found->ordinal;
            lookup.address = found->address;
            return lookup;
        }
    }
    return lookup;
}

bool is_valid_module_handle(void* handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    return value == 0x1000U;
}

std::size_t registered_module_count() {
    std::lock_guard<std::mutex> lock(modules_mutex());
    return modules().size();
}

}  // namespace tradutorlinux::loader
