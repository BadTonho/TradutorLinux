#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_PSAPI_MSABI __attribute__((ms_abi))
#else
#error "TL_PSAPI_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_PSAPI_MSABI int tl_EnumProcesses(std::uint32_t* process_ids, std::uint32_t size, std::uint32_t* bytes_returned) noexcept;
TL_PSAPI_MSABI int tl_EnumProcessModules(const void* process, void** modules, std::uint32_t size, std::uint32_t* needed) noexcept;
TL_PSAPI_MSABI int tl_EnumProcessModulesEx(const void* process, void** modules, std::uint32_t size, std::uint32_t* needed, std::uint32_t filter_flag) noexcept;
TL_PSAPI_MSABI std::uint32_t tl_GetModuleBaseNameA(const void* process, void* module, char* base_name, std::uint32_t size) noexcept;
TL_PSAPI_MSABI std::uint32_t tl_GetModuleBaseNameW(const void* process, void* module, std::uint16_t* base_name, std::uint32_t size) noexcept;
TL_PSAPI_MSABI std::uint32_t tl_GetModuleFileNameExA(const void* process, void* module, char* filename, std::uint32_t size) noexcept;
TL_PSAPI_MSABI std::uint32_t tl_GetModuleFileNameExW(const void* process, void* module, std::uint16_t* filename, std::uint32_t size) noexcept;
TL_PSAPI_MSABI int tl_GetProcessMemoryInfo(const void* process, void* counters, std::uint32_t size) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
