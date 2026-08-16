#include "tradutorlinux/loader/module.hpp"

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"

#include <algorithm>
#include <cctype>
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

[[nodiscard]] bool ascii_iequals(const std::string_view a, const std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(a[index])) !=
            std::tolower(static_cast<unsigned char>(b[index]))) {
            return false;
        }
    }
    return true;
}

const OwnedModule* find_module(const std::string_view dll) {
    const std::vector<OwnedModule>& registry = modules();
    const auto found = std::find_if(registry.begin(), registry.end(), [&](const OwnedModule& module) {
        return ascii_iequals(module.name, dll);
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
    };
    static const InternalModule kUser32Module{"USER32.dll", kUser32Exports};
    register_module(kUser32Module);
    static const ExportedFunction kGdi32Exports[] = {
        {"GetStockObject", 1, reinterpret_cast<std::uintptr_t>(&tl_GetStockObject)},
        {"TextOutA", 2, reinterpret_cast<std::uintptr_t>(&tl_TextOut)},
        {"TextOut", 3, reinterpret_cast<std::uintptr_t>(&tl_TextOut)},
        {"Rectangle", 4, reinterpret_cast<std::uintptr_t>(&tl_Rectangle)},
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
    };
    static const InternalModule kMsvcrtModule{"msvcrt.dll", kMsvcrtExports};
    register_module(kMsvcrtModule);
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
