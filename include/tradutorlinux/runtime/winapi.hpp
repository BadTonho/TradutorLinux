#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_MSABI __attribute__((ms_abi))
#else
#error "TL_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

struct GuestExecutionResult {
    bool exited_explicitly{};
    std::uint32_t exit_code{};
};
namespace abi {

// Tipos mínimos do Win32 usados pelas APIs suportadas.
using Handle = void*;
using HWnd = void*;
using Bool = int;
using Dword = std::uint32_t;
using Uint = std::uint32_t;
using Wparam = std::uintptr_t;
using Lparam = std::intptr_t;
using Lresult = std::intptr_t;
using Atom = std::uint16_t;

constexpr Dword kErrorSuccess = 0;
constexpr Dword kErrorFileNotFound = 2;
constexpr Dword kErrorAccessDenied = 5;
constexpr Dword kErrorInvalidHandle = 6;
constexpr Dword kErrorNotEnoughMemory = 8;
constexpr Dword kErrorInvalidParameter = 87;

constexpr Dword kGenericRead = 0x80000000U;
constexpr Dword kGenericWrite = 0x40000000U;
constexpr Dword kCreateAlways = 2;
constexpr Dword kOpenExisting = 3;
constexpr Dword kMemCommit = 0x1000U;
constexpr Dword kMemReserve = 0x2000U;
constexpr Dword kMemRelease = 0x8000U;
constexpr Dword kPageReadOnly = 0x02U;
constexpr Dword kPageReadWrite = 0x04U;

constexpr Dword kStdInputHandle = 0xFFFFFFF6U;   // STD_INPUT_HANDLE (-10)
constexpr Dword kStdOutputHandle = 0xFFFFFFF5U;  // STD_OUTPUT_HANDLE (-11)
constexpr Dword kStdErrorHandle = 0xFFFFFFF4U;   // STD_ERROR_HANDLE (-12)

constexpr Uint kWmPaint = 0x000F;
constexpr Uint kWmClose = 0x0010;
constexpr Uint kWmCreate = 0x0001;

constexpr Uint kWmDestroy = 0x0002;
constexpr Uint kWmQuit = 0x0012;
constexpr Uint kWmLButtonDown = 0x0201;
constexpr int kSwShow = 1;

// MSG com layout Microsoft x64 (48 bytes). Campos em offsets fixos para
// leitura/escrita de memória convidada.
struct GuestMsg {
    HWnd hwnd{};
    std::uint32_t message{};
    std::uint32_t padding{};
    Wparam wparam{};
    Lparam lparam{};
    std::uint32_t time{};
    std::int32_t pt_x{};
    std::int32_t pt_y{};
};
static_assert(sizeof(GuestMsg) == 48);

// WNDCLASSEXA com layout Microsoft x64 (80 bytes).
struct GuestWndClassExA {
    std::uint32_t cb_size{};
    std::uint32_t style{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const char* menu_name{};
    const char* class_name{};
    void* icon_sm{};
};
static_assert(sizeof(GuestWndClassExA) == 80);

}  // namespace abi

// Fronteira de ABI: funções hospedeiras chamadas por código PE32+ x86-64.
// Todas usam a convenção Microsoft x64 (TL_MSABI) e não propagam exceções
// C++. Os handles retornados são tokens opacos válidos somente para as APIs
// de console suportadas nesta fase.
extern "C" {

TL_MSABI void* tl_GetStdHandle(std::uint32_t n_std_handle) noexcept;
TL_MSABI int tl_WriteFile(const void* file, const void* buffer, std::uint32_t bytes_to_write,
                          std::uint32_t* bytes_written, const void* overlapped) noexcept;
TL_MSABI int tl_ReadFile(const void* file, void* buffer, std::uint32_t bytes_to_read,
                         std::uint32_t* bytes_read, const void* overlapped) noexcept;
TL_MSABI void tl_ExitProcess(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_GetLastError() noexcept;
TL_MSABI void tl_SetLastError(std::uint32_t error) noexcept;
TL_MSABI void* tl_VirtualAlloc(const void* address, std::uintptr_t size, std::uint32_t allocation_type,
                               std::uint32_t protection) noexcept;
TL_MSABI int tl_VirtualFree(const void* address, std::uintptr_t size,
                            std::uint32_t free_type) noexcept;
TL_MSABI void* tl_CreateFileA(const char* path, std::uint32_t desired_access,
                              std::uint32_t share_mode, const void* security_attributes,
                              std::uint32_t creation_disposition, std::uint32_t flags,
                              const void* template_file) noexcept;
TL_MSABI int tl_CloseHandle(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_MessageBoxA(const void* owner, const char* text, const char* caption,
                                      std::uint32_t type) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExA(const void* wnd_class) noexcept;
TL_MSABI abi::HWnd tl_CreateWindowExA(std::uint32_t ex_style, const char* class_name,
                                      const char* window_name, std::uint32_t style, int x, int y,
                                      int width, int height, const void* parent, const void* menu,
                                      const void* instance, const void* param) noexcept;
TL_MSABI int tl_ShowWindow(const void* window, int cmd_show) noexcept;
TL_MSABI int tl_UpdateWindow(const void* window) noexcept;
TL_MSABI int tl_GetMessageA(void* msg, const void* window, std::uint32_t filter_min,
                            std::uint32_t filter_max) noexcept;
TL_MSABI int tl_TranslateMessage(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DispatchMessageA(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DefWindowProcA(const void* window, std::uint32_t message,
                                        abi::Wparam wparam, abi::Lparam lparam) noexcept;
TL_MSABI int tl_DestroyWindow(const void* window) noexcept;
TL_MSABI void tl_PostQuitMessage(int exit_code) noexcept;

}  // extern "C"

// Executa um entry point Microsoft x64 e captura ExitProcess sem encerrar o
// processo hospedeiro. O ponteiro deve apontar para código já mapeado como
// executável e com imports resolvidos.
[[nodiscard]] GuestExecutionResult execute_guest_entry(std::uintptr_t entry_point,
                                                       std::uintptr_t stack_top) noexcept;

}  // namespace tradutorlinux
