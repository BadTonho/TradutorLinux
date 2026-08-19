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
constexpr Dword kErrorAlreadyExists = 183;
constexpr Dword kErrorInvalidParameter = 87;
constexpr Dword kErrorBrokenPipe = 109;

constexpr Dword kGenericRead = 0x80000000U;
constexpr Dword kGenericWrite = 0x40000000U;
constexpr Dword kCreateAlways = 2;
constexpr Dword kOpenExisting = 3;
constexpr Dword kMemCommit = 0x1000U;
constexpr Dword kMemReserve = 0x2000U;
constexpr Dword kMemRelease = 0x8000U;
constexpr Dword kMemImage = 0x1000000U;
constexpr Dword kMemPrivate = 0x20000U;
constexpr Dword kPageNoAccess = 0x01U;
constexpr Dword kPageReadOnly = 0x02U;
constexpr Dword kPageReadWrite = 0x04U;
constexpr Dword kPageWriteCopy = 0x08U;
constexpr Dword kPageExecute = 0x10U;
constexpr Dword kPageExecuteRead = 0x20U;
constexpr Dword kPageExecuteReadWrite = 0x40U;
constexpr Dword kPageExecuteWriteCopy = 0x80U;

constexpr Dword kErrorInsufficientBuffer = 122;
constexpr Dword kErrorInvalidAddress = 487;
constexpr Dword kErrorNoUnicodeTranslation = 1113;
constexpr Dword kErrorTooManyTlsIndexes = 4323;

// Fase 11: WaitForSingleObject.
constexpr Dword kWaitObject0 = 0;
constexpr Dword kWaitTimeout = 0x102;
constexpr Dword kWaitFailed = 0xFFFFFFFF;
constexpr Dword kInfinite = 0xFFFFFFFF;

// Code pages suportadas pela conversão de strings.
constexpr Dword kCpAcp = 0;          // CP_ACP -> CP1252 (locale C do runtime)
constexpr Dword kCp1252 = 1252;
constexpr Dword kCpUtf8 = 65001;

// Flags aceitas por MultiByteToWideChar / WideCharToMultiByte.
constexpr Dword kMbPrecomposed = 0x01U;
constexpr Dword kMbErrInvalidChars = 0x08U;
constexpr Dword kWcCompositeCheck = 0x200U;
constexpr Dword kWcNoBestFitChars = 0x400U;

constexpr Dword kStdInputHandle = 0xFFFFFFF6U;   // STD_INPUT_HANDLE (-10)
constexpr Dword kStdOutputHandle = 0xFFFFFFF5U;  // STD_OUTPUT_HANDLE (-11)
constexpr Dword kStdErrorHandle = 0xFFFFFFF4U;   // STD_ERROR_HANDLE (-12)

constexpr Uint kWmPaint = 0x000F;
constexpr Uint kWmClose = 0x0010;
constexpr Uint kWmCreate = 0x0001;
constexpr Uint kWmSize = 0x0005;
constexpr Uint kWmCommand = 0x0111;
constexpr Uint kWmSysCommand = 0x0112;
constexpr Uint kWmNotify = 0x004E;
constexpr Uint kWmSetFont = 0x0030;
constexpr Uint kWmCtlColorEdit = 0x0133;
constexpr Uint kWmCtlColorListBox = 0x0134;
constexpr Uint kWmCtlColorStatic = 0x0138;
constexpr Uint kWmTrayIcon = 0x0400 + 1;

constexpr Uint kWmDestroy = 0x0002;
constexpr Uint kWmQuit = 0x0012;
constexpr Uint kWmKeyDown = 0x0100;
constexpr Uint kWmKeyUp = 0x0101;
constexpr Uint kWmChar = 0x0102;
constexpr Uint kWmTimer = 0x0113;
constexpr Uint kWmMouseMove = 0x0200;
constexpr Uint kWmLButtonDown = 0x0201;
constexpr Uint kWmLButtonUp = 0x0202;
constexpr Wparam kMkLButton = 0x0001;  // MK_LBUTTON
constexpr int kSwShow = 1;

// Virtual keys (subconjunto suportado).
constexpr Wparam kVkBack = 0x08;
constexpr Wparam kVkTab = 0x09;
constexpr Wparam kVkReturn = 0x0D;
constexpr Wparam kVkEscape = 0x1B;
constexpr Wparam kVkSpace = 0x20;
constexpr Wparam kVkLeft = 0x25;
constexpr Wparam kVkUp = 0x26;
constexpr Wparam kVkRight = 0x27;
constexpr Wparam kVkDown = 0x28;
constexpr Wparam kVkDelete = 0x2E;

constexpr std::uint32_t kEnSetFocus = 0x0100;
constexpr std::uint32_t kEnKillFocus = 0x0200;
constexpr std::uint32_t kEnChange = 0x0300;
constexpr std::uint32_t kBnClicked = 0;
constexpr std::int32_t kNmDblClk = -3;
constexpr std::int32_t kLvnItemChanged = -101;
constexpr std::int32_t kLvnColumnClick = -108;
constexpr std::uint32_t kCbAddString = 0x0143;
constexpr std::uint32_t kCbGetCurSel = 0x0147;
constexpr std::uint32_t kCbSetCurSel = 0x014E;
constexpr std::uint32_t kLvmDeleteAllItems = 0x1009;
constexpr std::uint32_t kLvmGetItemA = 0x1005;
constexpr std::uint32_t kLvmSetItemTextA = 0x1006;
constexpr std::uint32_t kLvmInsertItemA = 0x1007;
constexpr std::uint32_t kLvmGetNextItem = 0x100C;
constexpr std::uint32_t kLvmInsertColumnA = 0x101B;
constexpr std::uint32_t kLvmGetItemTextA = 0x102D;
constexpr std::uint32_t kLvmSetExtendedListViewStyle = 0x1036;
constexpr std::uint32_t kLvmSortItemsEx = 0x1051;

// WNDCLASSA usado pelo alvo (sem o campo cbSize de WNDCLASSEXA).
struct GuestWndClassA {
    std::uint32_t style{};
    std::uint32_t padding{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const char* menu_name{};
    const char* class_name{};
};
static_assert(sizeof(GuestWndClassA) == 72);

struct GuestLvColumnA {
    std::uint32_t mask{};
    std::int32_t format{};
    std::int32_t width{};
    const char* text{};
    std::int32_t text_capacity{};
    std::int32_t subitem{};
};
static_assert(sizeof(GuestLvColumnA) == 32);

struct GuestLvItemA {
    std::uint32_t mask{};
    std::int32_t item{};
    std::int32_t subitem{};
    std::uint32_t state{};
    std::uint32_t state_mask{};
    char* text{};
    std::int32_t text_capacity{};
    std::int32_t image{};
    std::intptr_t param{};
    std::int32_t indent{};
    std::int32_t group_id{};
    std::int32_t columns{};
    const void* column_data{};
};
static_assert(sizeof(GuestLvItemA) == 72);

struct GuestNmListView {
    void* hwnd_from{};
    std::uintptr_t id_from{};
    std::int32_t code{};
    std::int32_t padding{};
    std::int32_t item{};
    std::int32_t subitem{};
    std::uint32_t new_state{};
    std::uint32_t old_state{};
    std::uint32_t changed{};
    std::int32_t point_x{};
    std::int32_t point_y{};
    std::intptr_t param{};
};
static_assert(sizeof(GuestNmListView) == 64);

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

// RECT com layout Microsoft x64 (16 bytes).
struct GuestRect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};
static_assert(sizeof(GuestRect) == 16);

// PAINTSTRUCT com layout Microsoft x64 (72 bytes).
struct GuestPaintStruct {
    void* hdc{};
    std::int32_t f_erase{};
    GuestRect rc_paint{};
    std::int32_t f_restore{};
    std::int32_t f_inc_update{};
    std::uint8_t rgb_reserved[32]{};
};
static_assert(sizeof(GuestPaintStruct) == 72);

// MEMORY_BASIC_INFORMATION com layout Microsoft x64 (48 bytes, sem o campo
// PartitionId das versões recentes do SDK). Os campos são preenchidos pela
// fronteira a partir do /proc/self/maps do hospedeiro.
struct GuestMemoryBasicInformation {
    void* base_address{};
    void* allocation_base{};
    std::uint32_t allocation_protect{};
    std::uint32_t padding1{};
    std::uintptr_t region_size{};
    std::uint32_t state{};
    std::uint32_t protect{};
    std::uint32_t type{};
    std::uint32_t padding2{};
};
static_assert(sizeof(GuestMemoryBasicInformation) == 48);

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
TL_MSABI void* tl_CreateMutexA(const void* security_attributes, int initial_owner,
                               const char* name) noexcept;
TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept;
TL_MSABI int tl_MulDiv(int number, int numerator, int denominator) noexcept;
TL_MSABI std::uint32_t tl_MessageBoxA(const void* owner, const char* text, const char* caption,
                                      std::uint32_t type) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExA(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassA(const void* wnd_class) noexcept;
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
TL_MSABI std::uintptr_t tl_SetTimer(const void* window, std::uintptr_t id, std::uint32_t elapsed_ms,
                                    const void* timer_proc) noexcept;
TL_MSABI int tl_KillTimer(const void* window, std::uintptr_t id) noexcept;
TL_MSABI void* tl_GetStockObject(int object) noexcept;
TL_MSABI void* tl_BeginPaint(const void* window, void* paint_struct) noexcept;
TL_MSABI int tl_EndPaint(const void* window, const void* paint_struct) noexcept;
TL_MSABI int tl_TextOut(const void* dc, int x, int y, const char* text, int length) noexcept;
TL_MSABI int tl_FillRect(const void* dc, const void* rect, const void* brush) noexcept;
TL_MSABI int tl_Rectangle(const void* dc, int left, int top, int right, int bottom) noexcept;
TL_MSABI void* tl_GetDC(const void* window) noexcept;
TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept;
TL_MSABI void* tl_CreateFontA(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const char* face_name) noexcept;
TL_MSABI void* tl_CreateSolidBrush(std::uint32_t color) noexcept;
TL_MSABI int tl_DeleteObject(const void* object) noexcept;
TL_MSABI std::uint32_t tl_SetBkColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI std::uint32_t tl_SetTextColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI int tl_GetClientRect(const void* window, void* rect) noexcept;
TL_MSABI int tl_GetCursorPos(void* point) noexcept;
TL_MSABI int tl_MoveWindow(const void* window, int x, int y, int width, int height,
                           int repaint) noexcept;
TL_MSABI std::intptr_t tl_SetWindowPos(const void* window, const void* insert_after, int x, int y,
                                       int width, int height, std::uint32_t flags) noexcept;
TL_MSABI int tl_SetWindowTextA(const void* window, const char* text) noexcept;
TL_MSABI int tl_GetWindowTextA(const void* window, char* text, int capacity) noexcept;
TL_MSABI int tl_EnableWindow(const void* window, int enable) noexcept;
TL_MSABI const void* tl_SetFocus(const void* window) noexcept;
TL_MSABI int tl_IsWindowVisible(const void* window) noexcept;
TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept;
TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept;
TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept;
TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                            std::intptr_t value) noexcept;
TL_MSABI int tl_SetForegroundWindow(const void* window) noexcept;
TL_MSABI int tl_SendMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                             abi::Lparam lparam) noexcept;
TL_MSABI int tl_PostMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                             abi::Lparam lparam) noexcept;
TL_MSABI void* tl_CreatePopupMenu() noexcept;
TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                            const char* text) noexcept;
TL_MSABI int tl_DestroyMenu(const void* menu) noexcept;
TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept;
TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept;
TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_GetConsoleMode(const void* handle, std::uint32_t* mode) noexcept;
TL_MSABI void tl_InitializeCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_IsDBCSLeadByteEx(std::uint32_t code_page, std::uint8_t test_char) noexcept;
TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_MultiByteToWideChar(std::uint32_t code_page, std::uint32_t flags, const char* mb_str,
                                   int mb_count, std::uint16_t* wide_str, int wide_count) noexcept;
TL_MSABI int tl_SetConsoleMode(const void* handle, std::uint32_t mode) noexcept;
TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t handler) noexcept;
TL_MSABI void tl_Sleep(std::uint32_t milliseconds) noexcept;
TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept;
TL_MSABI int tl_VirtualProtect(void* address, std::uintptr_t size, std::uint32_t new_protection,
                               std::uint32_t* old_protection) noexcept;
TL_MSABI std::uintptr_t tl_VirtualQuery(const void* address, void* memory_information,
                                        std::uintptr_t length) noexcept;
TL_MSABI int tl_WideCharToMultiByte(std::uint32_t code_page, std::uint32_t flags,
                                     const std::uint16_t* wide_str, int wide_count, char* mb_str,
                                     int mb_count, const char* default_char,
                                     int* used_default_char) noexcept;
TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept;
TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept;
TL_MSABI void* tl_GetProcAddress(void* module, const char* name) noexcept;
TL_MSABI const char* tl_GetCommandLineA() noexcept;
TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI void* tl_GetProcessHeap() noexcept;
TL_MSABI void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept;
TL_MSABI int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept;
TL_MSABI void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                               std::uintptr_t new_size) noexcept;
TL_MSABI std::uint64_t tl_GetTickCount64() noexcept;
TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept;

// Fase 10: Sistema de arquivos e utilitários.
TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept;
TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_DeleteFileA(const char* path) noexcept;
TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept;
TL_MSABI int tl_CreateDirectoryA(const char* path, const void* security_attributes) noexcept;
TL_MSABI void* tl_FindFirstFileA(const char* path, void* find_data) noexcept;
TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept;
TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindClose(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_FormatMessageW(std::uint32_t flags, const void* source,
                                         std::uint32_t message_id, std::uint32_t language_id,
                                         std::uint16_t* buffer, std::uint32_t size,
                                         const void* arguments) noexcept;
TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept;
TL_MSABI int tl_SetConsoleOutputCP(std::uint32_t code_page) noexcept;
TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           std::uint32_t unique, std::uint16_t* temp_file_name) noexcept;
TL_MSABI void* tl_LocalFree(void* memory) noexcept;

// Diretório atual e módulo.
TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(std::uint32_t buffer_length,
                                                char* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentDirectoryW(std::uint32_t buffer_length,
                                                std::uint16_t* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetModuleFileNameA(const void* module_handle, char* buffer,
                                              std::uint32_t size) noexcept;

// Fase 11: Concorrência.

TL_MSABI void* tl_CreateThread(const void* security_attributes, std::uintptr_t stack_size,
                                std::uintptr_t start_address, void* parameter,
                                std::uint32_t creation_flags, std::uint32_t* thread_id) noexcept;
TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* handle,
                                               std::uint32_t milliseconds) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept;
TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept;
TL_MSABI std::uint32_t tl_TlsAlloc() noexcept;
TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* tls_value) noexcept;
TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept;

// SHELL32.dll (Fase 10+): linha de comando no formato wide.
TL_MSABI std::uint16_t** tl_CommandLineToArgvW(const std::uint16_t* command_line,
                                               int* argument_count) noexcept;
TL_MSABI int tl_ShellNotifyIconA(std::uint32_t message, void* data) noexcept;

}  // extern "C"

// Define o caminho do módulo convidado antes da execução.
void set_guest_module_path(const char* path) noexcept;

// Executa um entry point Microsoft x64 e captura ExitProcess sem encerrar o
// processo hospedeiro. O ponteiro deve apontar para código já mapeado como
// executável e com imports resolvidos.
[[nodiscard]] GuestExecutionResult execute_guest_entry(std::uintptr_t entry_point,
                                                       std::uintptr_t stack_top) noexcept;

}  // namespace tradutorlinux
