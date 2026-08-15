#include "tradutorlinux/loader/module.hpp"

#include "tradutorlinux/runtime/winapi.hpp"

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
