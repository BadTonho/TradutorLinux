#include "tradutorlinux/loader/module.hpp"

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
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

const OwnedModule* find_module(const std::string_view dll) {
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

bool register_module(const InternalModule& module) {
    if (find_module(module.name) != nullptr) {
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
        {"GetConsoleOutputCP", 62, reinterpret_cast<std::uintptr_t>(&tl_GetConsoleOutputCP)},
        {"GetTempFileNameW", 63, reinterpret_cast<std::uintptr_t>(&tl_GetTempFileNameW)},
        {"LocalFree", 64, reinterpret_cast<std::uintptr_t>(&tl_LocalFree)},
        {"SetConsoleOutputCP", 65, reinterpret_cast<std::uintptr_t>(&tl_SetConsoleOutputCP)},
        {"CreateMutexA", 66, reinterpret_cast<std::uintptr_t>(&tl_CreateMutexA)},
        {"GetStartupInfoA", 67, reinterpret_cast<std::uintptr_t>(&tl_GetStartupInfoA)},
        {"MulDiv", 68, reinterpret_cast<std::uintptr_t>(&tl_MulDiv)},
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
    };
    static const InternalModule kGdi32Module{"GDI32.dll", kGdi32Exports};
    register_module(kGdi32Module);
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
    };
    static const InternalModule kMsvcrtModule{"msvcrt.dll", kMsvcrtExports};
    register_module(kMsvcrtModule);
    static const ExportedFunction kShell32Exports[] = {
        {"CommandLineToArgvW", 1, reinterpret_cast<std::uintptr_t>(&tl_CommandLineToArgvW)},
        {"Shell_NotifyIconA", 2, reinterpret_cast<std::uintptr_t>(&tl_ShellNotifyIconA)},
    };
    static const InternalModule kShell32Module{"SHELL32.dll", kShell32Exports};
    register_module(kShell32Module);
    static const ExportedFunction kAdvapi32Exports[] = {
        {"RegCloseKey", 1, reinterpret_cast<std::uintptr_t>(&tl_RegCloseKey)},
        {"RegDeleteValueA", 2, reinterpret_cast<std::uintptr_t>(&tl_RegDeleteValueA)},
        {"RegOpenKeyExA", 3, reinterpret_cast<std::uintptr_t>(&tl_RegOpenKeyExA)},
        {"RegQueryValueExA", 4, reinterpret_cast<std::uintptr_t>(&tl_RegQueryValueExA)},
        {"RegSetValueExA", 5, reinterpret_cast<std::uintptr_t>(&tl_RegSetValueExA)},
    };
    static const InternalModule kAdvapi32Module{"ADVAPI32.dll", kAdvapi32Exports};
    register_module(kAdvapi32Module);
}

bool is_module_registered(const std::string_view dll) {
    return find_module(dll) != nullptr;
}

ExportLookup find_export(const ExportQuery& query) {
    ExportLookup lookup;
    const OwnedModule* module = find_module(query.dll);
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
    ExportLookup lookup;
    const OwnedModule* module = find_module(dll);
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

std::size_t registered_module_count() {
    return modules().size();
}

}  // namespace tradutorlinux::loader
