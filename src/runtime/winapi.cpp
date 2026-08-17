#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/gui/x11.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace tradutorlinux {
namespace {

thread_local std::jmp_buf g_guest_exit_context;
thread_local bool g_guest_execution_active = false;
thread_local std::uint32_t g_guest_exit_code = 0;
thread_local std::uint32_t g_last_error = abi::kErrorSuccess;

extern "C" void tl_call_guest_on_stack(std::uintptr_t entry,
                                         std::uintptr_t stack_top) noexcept;

char kStdInputToken = 0;
char kStdOutputToken = 0;
char kStdErrorToken = 0;

// Tokens opacos para stock objects do GDI: o próprio endereço serve de handle
// e o deslocamento identifica o objeto. Stock objects não são liberados.
char kStockObjectTokens[24]{};

struct FileSlot {
    int fd{-1};
    bool used{false};
};

struct AllocationSlot {
    void* address{nullptr};
    std::size_t size{0};
};

// Uma faixa contígua do espaço de endereçamento com permissões uniformes,
// conforme /proc/self/maps.
struct GuestMapRegion {
    std::uintptr_t start{};
    std::uintptr_t end{};
    bool readable{false};
    bool writable{false};
};

std::array<FileSlot, 64> g_files{};
std::array<AllocationSlot, 64> g_allocations{};

void set_last_error(const std::uint32_t error) noexcept {
    g_last_error = error;
}

[[nodiscard]] std::uint32_t errno_to_win32(const int error) noexcept {
    switch (error) {
        case ENOENT:
            return abi::kErrorFileNotFound;
        case EACCES:
        case EPERM:
            return abi::kErrorAccessDenied;
        case ENOMEM:
            return abi::kErrorNotEnoughMemory;
        case EPIPE:
            // Escrever em um pipe sem leitor (ex.: stdout para `head -c0`) é
            // um erro controlado de I/O, não uma morte por sinal.
            return abi::kErrorBrokenPipe;
        default:
            return abi::kErrorInvalidParameter;
    }
}

std::vector<GuestMapRegion> g_guest_map_regions{};
std::size_t g_guest_map_generation = 0;
std::size_t g_guest_allocation_generation = 0;

// O convidado executa em processo filho, onde as únicas fontes de mmap/munmap
// são VirtualAlloc/VirtualFree; a geração só muda por essas duas APIs, então a
// cache permanece correta entre consultas.
void bump_guest_allocation_generation() noexcept {
    ++g_guest_allocation_generation;
}

void rebuild_guest_map_cache() noexcept {
    g_guest_map_regions.clear();
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos || dash == 0) {
            continue;
        }
        std::uintptr_t region_start{};
        std::uintptr_t region_end{};
        if (std::from_chars(line.data(), line.data() + dash, region_start, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, region_end, 16).ec !=
                std::errc{} ||
            region_start > region_end) {
            continue;
        }
        const std::string::size_type permissions_offset = space + 1;
        if (line.size() < permissions_offset + 4) {
            continue;
        }
        g_guest_map_regions.push_back(
            GuestMapRegion{region_start, region_end, line[permissions_offset] == 'r',
                           line[permissions_offset + 1] == 'w'});
    }
    g_guest_map_generation = g_guest_allocation_generation;
}

[[nodiscard]] bool region_holds(const GuestMapRegion& region, const std::uintptr_t start,
                                const std::uintptr_t end, const bool writable) noexcept {
    return start >= region.start && end <= region.end &&
           (writable ? region.writable : region.readable);
}

[[nodiscard]] bool mapped_guest_range(const void* address, const std::size_t size,
                                      const bool writable) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }
    const std::uintptr_t end = start + size;
    if (g_guest_map_generation != g_guest_allocation_generation) {
        rebuild_guest_map_cache();
    }
    for (const GuestMapRegion& region : g_guest_map_regions) {
        if (region_holds(region, start, end, writable)) {
            return true;
        }
    }
    // Consulta sem achado: reconstrói uma vez e tenta de novo, cobrindo
    // mapeamentos criados fora do runtime (ex.: alocador do processo de teste)
    // sem pagar o custo do /proc/self/maps em toda chamada.
    rebuild_guest_map_cache();
    for (const GuestMapRegion& region : g_guest_map_regions) {
        if (region_holds(region, start, end, writable)) {
            return true;
        }
    }
    return false;
}

FileSlot* find_file_slot(const void* handle) noexcept {
    const auto found = std::find_if(g_files.begin(), g_files.end(), [handle](const FileSlot& slot) {
        return slot.used && handle == &slot;
    });
    if (found != g_files.end()) {
        return &*found;
    }
    return nullptr;
}

[[nodiscard]] bool mapped_guest_cstring(const char* value) noexcept {
    if (value == nullptr) {
        return true;
    }
    constexpr std::size_t kMaxGuestString = 65535;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t index = 0; index < kMaxGuestString; ++index) {
        if (index > std::numeric_limits<std::uintptr_t>::max() - address ||
            !mapped_guest_range(reinterpret_cast<const void*>(address + index), 1, false)) { // NOLINT(performance-no-int-to-ptr)
            return false;
        }
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

void runtime_trace(const char* event, const std::array<diagnostics::TraceField, 4>& fields,
                   const std::size_t field_count) noexcept {
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, event,
                             std::span<const diagnostics::TraceField>{fields.data(), field_count});
}

void trace_guest_failure(const char* symbol, const char* operation,
                         const char* detail) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"category", std::string{
                                     diagnostics::failure_category_name(
                                         diagnostics::FailureCategory::GuestMemory)}},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"detail", detail},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Error, "api-failure", fields);
}

void trace_linux_failure(const char* symbol, const char* operation, const int error,
                         const std::uint32_t win32_error) noexcept {
    const std::array<diagnostics::TraceField, 5> fields{
        diagnostics::TraceField{"category", std::string{
                                     diagnostics::failure_category_name(
                                         diagnostics::FailureCategory::LinuxError)}},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"errno", std::to_string(error)},
        diagnostics::TraceField{"win32-error", std::to_string(win32_error)},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Error, "linux-failure", fields);
}

int handle_fd(const void* handle) noexcept {
    if (handle == &kStdInputToken) {
            return STDIN_FILENO;
    }
    if (handle == &kStdOutputToken) {
            return STDOUT_FILENO;
    }
    if (handle == &kStdErrorToken) {
            return STDERR_FILENO;
    }
    if (const FileSlot* slot = find_file_slot(handle); slot != nullptr) {
        return slot->fd;
    }
    return -1;
}

void trace_stub(const char* symbol) noexcept {
    trace_guest_failure(symbol, "argument-validation", "ponteiro ou parâmetro inválido");
}

struct ClassSlot {
    bool used{false};
    std::string name;
    std::uintptr_t wndproc{0};
};

struct GuestTimer {
    std::uintptr_t id{0};
    std::chrono::steady_clock::time_point deadline{};
    std::chrono::milliseconds interval{};
};

struct WindowSlot {
    bool used{false};
    std::uintptr_t wndproc{0};
    std::string class_name;
    gui::NativeWindow native{nullptr};
    bool mapped{false};
    abi::GuestMsg pending{};  // mensagem traduzida (ex.: WM_CHAR) aguardando GetMessageA
    bool has_pending{false};
    char last_key{'\0'};  // caractere do WM_KEYDOWN mais recente, para TranslateMessage
    bool left_button_down{false};  // estado do botão primário, para o wParam do mouse
    std::vector<GuestTimer> timers;  // timers ativos (WM_TIMER)
    int width{0};
    int height{0};
    bool painting{false};  // BeginPaint sem EndPaint correspondente
};

std::array<ClassSlot, 32> g_classes{};
std::array<WindowSlot, 16> g_windows{};
bool g_quit_requested = false;
std::uint32_t g_quit_code = 0;

using WndProc = TL_MSABI abi::Lresult (*)(abi::HWnd, std::uint32_t, abi::Wparam, abi::Lparam);

// Fronteira host -> convidado: invoca o WNDPROC do convidado pela convenção
// Microsoft x64. A chamada acontece enquanto o hospedeiro já executa sobre a
// pilha convidada (o convidado chamou a API hospedeira), então nenhum
// trampolim de pilha é necessário.
abi::Lresult call_wndproc(const std::uintptr_t wndproc, const abi::HWnd hwnd,
                          const std::uint32_t message, const abi::Wparam wparam,
                          const abi::Lparam lparam) noexcept {
    return std::bit_cast<WndProc>(wndproc)(hwnd, message, wparam, lparam);
}

ClassSlot* find_class_slot(const char* const name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(g_classes.begin(), g_classes.end(),
                                    [name](const ClassSlot& slot) {
                                        return slot.used && util::ascii_iequals(slot.name, name);
                                    });
    if (found != g_classes.end()) {
        return &*found;
    }
    return nullptr;
}

WindowSlot* find_window_slot(const void* const handle) noexcept {
    const auto found = std::find_if(g_windows.begin(), g_windows.end(),
                                    [handle](const WindowSlot& slot) {
                                        return slot.used && handle == &slot;
                                    });
    if (found != g_windows.end()) {
        return &*found;
    }
    return nullptr;
}

void write_guest_msg(void* const msg, const abi::HWnd hwnd, const std::uint32_t message,  // NOLINT(bugprone-easily-swappable-parameters)
                     const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    abi::GuestMsg* const out = static_cast<abi::GuestMsg*>(msg);
    out->hwnd = hwnd;
    out->message = message;
    out->padding = 0;
    out->wparam = wparam;
    out->lparam = lparam;
    out->time = 0;
    out->pt_x = 0;
    out->pt_y = 0;
}

// KeySyms X11 usados na fronteira (valores estáveis do protocolo X11). A
// camada gui entrega o keysym em WindowEvent; este mapeamento mantém o X11
// fora do módulo de runtime.
constexpr unsigned long kKeysymBackspace = 0xFF08;
constexpr unsigned long kKeysymTab = 0xFF09;
constexpr unsigned long kKeysymReturn = 0xFF0D;
constexpr unsigned long kKeysymEscape = 0xFF1B;
constexpr unsigned long kKeysymLeft = 0xFF51;
constexpr unsigned long kKeysymUp = 0xFF52;
constexpr unsigned long kKeysymRight = 0xFF53;
constexpr unsigned long kKeysymDown = 0xFF54;
constexpr unsigned long kKeysymDelete = 0xFFFF;

// Índice do stock object correspondente a um token opaco (o endereço dentro da
// tabela estática de tokens), ou -1 quando o ponteiro não pertence à tabela.
[[nodiscard]] int stock_object_index(const void* const token) noexcept {
    if (token == nullptr) {
        return -1;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(kStockObjectTokens);
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(token);
    if (value < base) {
        return -1;
    }
    const std::size_t offset = static_cast<std::size_t>(value - base);
    if (offset >= sizeof(kStockObjectTokens)) {
        return -1;
    }
    return static_cast<int>(offset);
}

// Região lida do /proc/self/maps: intervalo, permissões e presença de arquivo.
struct MapsRegion {
    std::uintptr_t start{0};
    std::uintptr_t end{0};
    char perms[4]{};  // r w x p
    bool has_path{false};
};

[[nodiscard]] bool find_maps_region(const void* const address, MapsRegion& region) noexcept {
    if (address == nullptr) {
        return false;
    }
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
    std::ifstream maps{"/proc/self/maps"};
    std::string line;
    while (std::getline(maps, line)) {
        const std::size_t dash = line.find('-');
        const std::size_t space = line.find(' ', dash == std::string::npos ? 0 : dash);
        if (dash == std::string::npos || space == std::string::npos || dash == 0) {
            continue;
        }
        std::uintptr_t start{0};
        std::uintptr_t end{0};
        if (std::from_chars(line.data(), line.data() + dash, start, 16).ec != std::errc{} ||
            std::from_chars(line.data() + dash + 1, line.data() + space, end, 16).ec != std::errc{} ||
            start > end || target < start || target >= end) {
            continue;
        }
        const std::size_t permissions_offset = space + 1;
        if (line.size() < permissions_offset + 4) {
            return false;
        }
        region.start = start;
        region.end = end;
        region.perms[0] = line[permissions_offset];
        region.perms[1] = line[permissions_offset + 1];
        region.perms[2] = line[permissions_offset + 2];
        region.perms[3] = line[permissions_offset + 3];
        region.has_path = line.find('/', permissions_offset + 4) != std::string::npos;
        return true;
    }
    return false;
}

[[nodiscard]] std::uint32_t win32_protection(const char perms[4]) noexcept {
    const bool readable = perms[0] == 'r';
    const bool writable = perms[1] == 'w';
    const bool executable = perms[2] == 'x';
    if (writable) {
        return executable ? abi::kPageExecuteReadWrite : abi::kPageReadWrite;
    }
    if (executable) {
        return readable ? abi::kPageExecuteRead : abi::kPageExecute;
    }
    return readable ? abi::kPageReadOnly : abi::kPageNoAccess;
}

[[nodiscard]] int host_protection(const std::uint32_t protection) noexcept {
    switch (protection) {
        case abi::kPageNoAccess:
            return PROT_NONE;
        case abi::kPageReadOnly:
            return PROT_READ;
        case abi::kPageReadWrite:
        case abi::kPageWriteCopy:
            return PROT_READ | PROT_WRITE;
        case abi::kPageExecute:
            return PROT_EXEC;
        case abi::kPageExecuteRead:
            return PROT_READ | PROT_EXEC;
        case abi::kPageExecuteReadWrite:
        case abi::kPageExecuteWriteCopy:
            return PROT_READ | PROT_WRITE | PROT_EXEC;
        default:
            return -1;
    }
}

// Handler registrado pelo convidado via SetUnhandledExceptionFilter. O runtime
// não invoca o handler (o convidado é de console e a falha é reportada pelo
// trace), mas o valor é armazenado para preservar a semântica da API.
std::uintptr_t g_unhandled_exception_filter = 0;

// Slots TLS por thread hospedeira. O convidado roda em uma única thread e o
// índice vem do __tls_index de seu módulo; nenhum slot é inicializado nesta
// fase, então TlsGetValue retorna null como no Windows para índice não usado.
thread_local std::array<void*, 64> g_guest_tls_slots{};

// Sequência inválida de entrada nas conversões de codepage (fora do intervalo
// Unicode). Utilizado como sentinela interno das conversões.
constexpr std::uint32_t kInvalidCodepoint = 0x110000U;

// CP1252: mapeamento dos bytes de controle 0x80-0x9F para Unicode. Os demais
// bytes são iguais ao Latin-1 (0xA0-0xFF -> U+00A0-U+00FF).
constexpr std::array<std::uint32_t, 32> kCp1252Control{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

[[nodiscard]] std::uint32_t cp1252_to_unicode(const std::uint8_t byte) noexcept {
    if (byte >= 0x80U && byte <= 0x9FU) {
        return kCp1252Control[static_cast<std::size_t>(byte - 0x80U)];
    }
    return static_cast<std::uint32_t>(byte);
}

[[nodiscard]] bool unicode_to_cp1252(const std::uint32_t codepoint, std::uint8_t& byte) noexcept {
    if (codepoint <= 0xFFU) {
        byte = static_cast<std::uint8_t>(codepoint);
        return true;
    }
    const auto found = std::find(kCp1252Control.begin(), kCp1252Control.end(), codepoint);
    if (found == kCp1252Control.end()) {
        return false;
    }
    byte = static_cast<std::uint8_t>(0x80U + static_cast<std::size_t>(found - kCp1252Control.begin()));
    return true;
}

// Decodifica um caractere de UTF-8 ou CP1252 a partir de `bytes`; avança `pos`.
// Retorna kInvalidCodepoint para sequência inválida (consumindo 1 byte).
[[nodiscard]] std::uint32_t decode_multibyte(const std::uint32_t code_page,
                                             const std::uint8_t* const bytes,
                                             const std::size_t length,
                                             std::size_t& pos) noexcept {
    const std::uint8_t first = bytes[pos];
    if (first < 0x80U) {
        pos += 1;
        return static_cast<std::uint32_t>(first);
    }
    if (code_page == abi::kCpAcp || code_page == abi::kCp1252) {
        pos += 1;
        return cp1252_to_unicode(first);
    }
    std::size_t needed = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0U) == 0xC0U) {
        needed = 2;
        value = static_cast<std::uint32_t>(first & 0x1FU);
    } else if ((first & 0xF0U) == 0xE0U) {
        needed = 3;
        value = static_cast<std::uint32_t>(first & 0x0FU);
    } else if ((first & 0xF8U) == 0xF0U) {
        needed = 4;
        value = static_cast<std::uint32_t>(first & 0x07U);
    } else {
        pos += 1;
        return kInvalidCodepoint;
    }
    if (pos + needed > length) {
        pos += 1;
        return kInvalidCodepoint;
    }
    for (std::size_t index = 1; index < needed; ++index) {
        const std::uint8_t continuation = bytes[pos + index];
        if ((continuation & 0xC0U) != 0x80U) {
            pos += 1;
            return kInvalidCodepoint;
        }
        value = (value << 6U) | static_cast<std::uint32_t>(continuation & 0x3FU);
    }
    pos += needed;
    const bool overlong = (needed == 2 && value < 0x80U) ||
                          (needed == 3 && value < 0x800U) ||
                          (needed == 4 && value < 0x10000U);
    if (overlong || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        return kInvalidCodepoint;
    }
    return value;
}

[[nodiscard]] std::size_t utf16_units_for(const std::uint32_t codepoint,
                                          std::uint16_t out[2]) noexcept {
    if (codepoint < 0x10000U) {
        out[0] = static_cast<std::uint16_t>(codepoint);
        return 1;
    }
    const std::uint32_t value = codepoint - 0x10000U;
    out[0] = static_cast<std::uint16_t>(0xD800U | (value >> 10U));
    out[1] = static_cast<std::uint16_t>(0xDC00U | (value & 0x3FFU));
    return 2;
}

[[nodiscard]] std::uint32_t decode_utf16(const std::uint16_t* const units,
                                         const std::size_t length,
                                         std::size_t& pos) noexcept {
    const std::uint16_t first = units[pos];
    if (first >= 0xD800U && first <= 0xDBFFU) {
        if (pos + 1 < length && units[pos + 1] >= 0xDC00U && units[pos + 1] <= 0xDFFFU) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first - 0xD800U) << 10U) |
                static_cast<std::uint32_t>(units[pos + 1] - 0xDC00U);
            pos += 2;
            return value + 0x10000U;
        }
        pos += 1;
        return kInvalidCodepoint;
    }
    if (first >= 0xDC00U && first <= 0xDFFFU) {
        pos += 1;
        return kInvalidCodepoint;
    }
    pos += 1;
    return static_cast<std::uint32_t>(first);
}

[[nodiscard]] std::size_t utf8_bytes_for(const std::uint32_t codepoint, char out[4]) noexcept {
    if (codepoint < 0x80U) {
        out[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint < 0x800U) {
        out[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
        out[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 2;
    }
    if (codepoint < 0x10000U) {
        out[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
        out[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 3;
    }
    out[0] = static_cast<char>(0xF0U | (codepoint >> 18U));
    out[1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
    out[2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
    out[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    return 4;
}

// CRITICAL_SECTION é um token opaco para a fronteira: o convidado roda em uma
// única thread, então a exclusão mútua é trivialmente satisfeita e a estrutura
// interna do convidado nunca é tocada. As funções validam apenas o ponteiro.
bool critical_section_valid(void* const critical_section) noexcept {
    if (critical_section == nullptr ||
        !mapped_guest_range(critical_section, 8, true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CriticalSection", "critical-section", "ponteiro inválido");
        return false;
    }
    set_last_error(abi::kErrorSuccess);
    return true;
}

// TEB (Thread Environment Block) mínimo Microsoft x64. O convidado mingw lê o
// endereço do próprio TEB via %gs:[0x30] e campos como StackBase (offset 0x8).
// Em Linux x86-64 o segmento GS é livre, então a fronteira aloca um TEB de uma
// página, o aponta via arch_prctl(ARCH_SET_GS) durante a execução do convidado
// e restaura o GS após o retorno.
struct GuestTeb {
    void* exception_list{nullptr};                // 0x00
    void* stack_base{nullptr};                    // 0x08
    void* stack_limit{nullptr};                   // 0x10
    void* sub_system_tib{nullptr};                // 0x18
    void* fiber_data{nullptr};                    // 0x20
    void* arbitrary_user_pointer{nullptr};        // 0x28
    void* self{nullptr};                          // 0x30
    void* environment_pointer{nullptr};           // 0x38
    std::uint64_t client_id[2]{0, 0};             // 0x40
    void* active_rpc_handle{nullptr};             // 0x50
    void* thread_local_storage_pointer{nullptr};  // 0x58
    void* peb{nullptr};                           // 0x60
    std::uint8_t reserved[0x110]{};               // até 0x178
    std::uint64_t tls_slots[64]{};                // 0x178
    std::uint8_t tail[0xC88]{};                   // até 0x1000 (tamanho da página)
};
static_assert(sizeof(GuestTeb) == 0x1000);

// Configura a base do segmento GS da thread atual (Linux x86-64). Em Linux o
// FS é usado pelo TLS do hospedeiro; GS fica livre para a fronteira de ABI.
bool set_guest_gs_base(const void* const base) noexcept {
    constexpr long kArchSetGs = 0x1001;  // ARCH_SET_GS
    return ::syscall(SYS_arch_prctl, kArchSetGs,
                     static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(base))) == 0;
}

// Virtual key do subconjunto suportado. Teclas especiais são mapeadas pelo
// keysym; letras usam a maiúscula (como VK_A), demais caracteres ASCII
// imprimíveis usam o próprio valor. O keysym de letras/dígitos já reflete o
// estado de Shift (XLookupString).
[[nodiscard]] abi::Wparam keydown_vkey(const unsigned long keysym,  // NOLINT(bugprone-easily-swappable-parameters)
                                       const char character) noexcept {
    switch (keysym) {
        case kKeysymBackspace:
            return abi::kVkBack;
        case kKeysymTab:
            return abi::kVkTab;
        case kKeysymReturn:
            return abi::kVkReturn;
        case kKeysymEscape:
            return abi::kVkEscape;
        case kKeysymLeft:
            return abi::kVkLeft;
        case kKeysymUp:
            return abi::kVkUp;
        case kKeysymRight:
            return abi::kVkRight;
        case kKeysymDown:
            return abi::kVkDown;
        case kKeysymDelete:
            return abi::kVkDelete;
        default:
            break;
    }
    const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
    if (value >= 'a' && value <= 'z') {
        return static_cast<abi::Wparam>(value - 0x20U);
    }
    return static_cast<abi::Wparam>(value);
}

}  // namespace

extern "C" {

TL_MSABI void* tl_GetStdHandle(const std::uint32_t standard_handle) noexcept {
    void* result = nullptr;
    if (standard_handle == abi::kStdInputHandle) {
        result = &kStdInputToken;
    } else if (standard_handle == abi::kStdOutputHandle) {
        result = &kStdOutputToken;
    } else if (standard_handle == abi::kStdErrorHandle) {
        result = &kStdErrorToken;
    }
    set_last_error(result != nullptr ? abi::kErrorSuccess : abi::kErrorInvalidParameter);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetStdHandle"},
        diagnostics::TraceField{"standard-handle", std::to_string(standard_handle)},
        diagnostics::TraceField{"result", result != nullptr ? "valid" : "invalid"},
        diagnostics::TraceField{"status", result != nullptr ? "success" : "failure"},
    };
    runtime_trace("GetStdHandle", fields, 4);
    return result;
}

TL_MSABI int tl_WriteFile(const void* handle, const void* const buffer,
                          const std::uint32_t bytes_to_write,
                          std::uint32_t* const bytes_written,
                          const void* overlapped) noexcept {
    if (bytes_written != nullptr && !mapped_guest_range(bytes_written, sizeof(*bytes_written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("WriteFile", "output-count", "ponteiro sem permissão de escrita");
        return 0;
    }
    if (bytes_written != nullptr) {
        *bytes_written = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (bytes_to_write != 0 && !mapped_guest_range(buffer, bytes_to_write, false)) ||
        overlapped != nullptr) {
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        trace_stub("WriteFile");
        return 0;
    }
    std::uint32_t total = 0;
    int failure_errno = EIO;
    while (total < bytes_to_write) {
        const ssize_t result = write(fd, static_cast<const std::byte*>(buffer) + total,
                                     bytes_to_write - total);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            failure_errno = errno;
        }
        if (result <= 0) {
            break;
        }
        total += static_cast<std::uint32_t>(result);
    }
    if (bytes_written != nullptr) {
        *bytes_written = total;
    }
    const std::uint32_t failure_error = errno_to_win32(failure_errno);
    set_last_error(total == bytes_to_write ? abi::kErrorSuccess : failure_error);
    if (total != bytes_to_write) {
        trace_linux_failure("WriteFile", "write", failure_errno, failure_error);
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "WriteFile"},
        diagnostics::TraceField{"bytes-requested", std::to_string(bytes_to_write)},
        diagnostics::TraceField{"bytes-written", std::to_string(total)},
        diagnostics::TraceField{"status", total == bytes_to_write ? "success" : "failure"},
    };
    runtime_trace("WriteFile", fields, 4);
    return total == bytes_to_write ? 1 : 0;
}

TL_MSABI int tl_ReadFile(const void* const handle, void* const buffer, // NOLINT(bugprone-easily-swappable-parameters)
                         const std::uint32_t bytes_to_read,
                         std::uint32_t* const bytes_read,
                         const void* const overlapped) noexcept {
    if (bytes_read != nullptr && !mapped_guest_range(bytes_read, sizeof(*bytes_read), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("ReadFile", "output-count", "ponteiro sem permissão de escrita");
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || (bytes_to_read != 0 && !mapped_guest_range(buffer, bytes_to_read, true)) ||
        overlapped != nullptr) {
        set_last_error(fd < 0 ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        trace_stub("ReadFile");
        return 0;
    }
    ssize_t result = 0;
    do {
        result = read(fd, buffer, bytes_to_read);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("ReadFile", "read", errno, failure_error);
        return 0;
    }
    if (bytes_read != nullptr) {
        *bytes_read = static_cast<std::uint32_t>(result);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ReadFile"},
        diagnostics::TraceField{"bytes-requested", std::to_string(bytes_to_read)},
        diagnostics::TraceField{"bytes-read", std::to_string(result)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("ReadFile", fields, 4);
    return 1;
}

TL_MSABI void tl_ExitProcess(const std::uint32_t exit_code) noexcept {
    g_guest_exit_code = exit_code;
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ExitProcess"},
        diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "guest-transfer"},
    };
    runtime_trace("ExitProcess", fields, 4);
    if (g_guest_execution_active) {
        std::longjmp(g_guest_exit_context, 1);
    }
}

TL_MSABI std::uint32_t tl_GetLastError() noexcept {
    return g_last_error;
}

TL_MSABI void tl_SetLastError(const std::uint32_t error) noexcept {
    set_last_error(error);
}

TL_MSABI void* tl_VirtualAlloc(const void* const address, const std::uintptr_t size,
                               const std::uint32_t allocation_type,
                               const std::uint32_t protection) noexcept {
    const std::size_t page = util::host_page_size();
    if (address != nullptr || size == 0 || allocation_type != (abi::kMemCommit | abi::kMemReserve) ||
        (protection != abi::kPageReadOnly && protection != abi::kPageReadWrite)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (size > static_cast<std::uintptr_t>(SIZE_MAX - (page - 1U))) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::size_t mapped_size =
        (static_cast<std::size_t>(size) + page - 1U) / page * page;
    const auto free_it = std::find_if(g_allocations.begin(), g_allocations.end(),
                                      [](const AllocationSlot& slot) {
                                          return slot.address == nullptr;
                                      });
    AllocationSlot* free_slot = free_it != g_allocations.end() ? &*free_it : nullptr;
    if (free_slot == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const int prot = protection == abi::kPageReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* mapped = mmap(nullptr, mapped_size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("VirtualAlloc", "mmap", errno, failure_error);
        return nullptr;
    }
    free_slot->address = mapped;
    free_slot->size = mapped_size;
    bump_guest_allocation_generation();
    set_last_error(abi::kErrorSuccess);
    return mapped;
}

TL_MSABI int tl_VirtualFree(const void* const address, const std::uintptr_t size,
                            const std::uint32_t free_type) noexcept {
    if (address == nullptr || size != 0 || free_type != abi::kMemRelease) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    for (AllocationSlot& slot : g_allocations) {
        if (slot.address == address) {
            const bool unmapped = munmap(slot.address, slot.size) == 0;
            if (!unmapped) {
                const std::uint32_t failure_error = errno_to_win32(errno);
                set_last_error(failure_error);
                trace_linux_failure("VirtualFree", "munmap", errno, failure_error);
                return 0;
            }
            slot = {};
            bump_guest_allocation_generation();
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    set_last_error(abi::kErrorInvalidParameter);
    return 0;
}

TL_MSABI void* tl_CreateFileA(const char* const path, const std::uint32_t desired_access,
                              const std::uint32_t share_mode,
                              const void* const security_attributes,
                              const std::uint32_t creation_disposition,
                              const std::uint32_t flags,
                              const void* const template_file) noexcept {
    if (!mapped_guest_cstring(path) || path == nullptr || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
        share_mode != 0 || security_attributes != nullptr || flags != 0 ||
        template_file != nullptr ||
        (desired_access & ~(abi::kGenericRead | abi::kGenericWrite)) != 0 ||
        (creation_disposition != abi::kCreateAlways &&
         creation_disposition != abi::kOpenExisting)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char normalized[4096]{};
    std::size_t length = 0;
    for (; path[length] != '\0'; ++length) {
        if (length + 1U >= sizeof(normalized) || path[length] == ':') {
            set_last_error(abi::kErrorInvalidParameter);
            return nullptr;
        }
        normalized[length] = path[length] == '\\' ? '/' : path[length];
    }
    normalized[length] = '\0';
    int open_flags = 0;
    const bool can_read = (desired_access & abi::kGenericRead) != 0;
    const bool can_write = (desired_access & abi::kGenericWrite) != 0;
    if (can_read && can_write) {
        open_flags |= O_RDWR;
    } else if (can_write) {
        open_flags |= O_WRONLY;
    } else {
        open_flags |= O_RDONLY;
    }
    if (creation_disposition == abi::kCreateAlways) {
        open_flags |= O_CREAT | O_TRUNC;
    }
    const int fd = open(normalized, open_flags, 0666);
    if (fd < 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("CreateFileA", "open", errno, failure_error);
        return nullptr;
    }
    const auto free_it = std::find_if(g_files.begin(), g_files.end(), [](const FileSlot& slot) {
        return !slot.used;
    });
    if (free_it != g_files.end()) {
        FileSlot& slot = *free_it;
        slot.fd = fd;
        slot.used = true;
        set_last_error(abi::kErrorSuccess);
        return &slot;
    }
    close(fd);
    set_last_error(abi::kErrorNotEnoughMemory);
    return nullptr;
}

TL_MSABI int tl_CloseHandle(const void* const handle) noexcept {
    FileSlot* slot = find_file_slot(handle);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("CloseHandle", "handle-validation", "handle inválido");
        return 0;
    }
    if (close(slot->fd) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("CloseHandle", "close", errno, failure_error);
        return 0;
    }
    *slot = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_MessageBoxA(const void* const, const char* const text,
                                      const char* const caption,
                                      const std::uint32_t type) noexcept {
    if (type != 0 || !mapped_guest_cstring(text) || !mapped_guest_cstring(caption)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t result = gui::message_box(text, caption);
    set_last_error(result == 0 ? abi::kErrorAccessDenied : abi::kErrorSuccess);
    return result;
}

TL_MSABI abi::Atom tl_RegisterClassExA(const void* const wnd_class) noexcept {
    if (wnd_class == nullptr ||
        !mapped_guest_range(wnd_class, sizeof(abi::GuestWndClassExA), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "estrutura WNDCLASSEXA inválida");
        return 0;
    }
    const auto* const wc = static_cast<const abi::GuestWndClassExA*>(wnd_class);
    if (wc->cb_size < sizeof(abi::GuestWndClassExA) || wc->window_proc == 0 ||
        wc->class_name == nullptr || !mapped_guest_cstring(wc->class_name) ||
        !mapped_guest_range(std::bit_cast<const void*>(wc->window_proc), 1, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("RegisterClassExA", "wnd-class", "cbSize, window_proc ou class_name inválido");
        return 0;
    }
    if (find_class_slot(wc->class_name) != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto free_it = std::find_if(g_classes.begin(), g_classes.end(),
                                      [](const ClassSlot& slot) { return !slot.used; });
    if (free_it == g_classes.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    ClassSlot& slot = *free_it;
    slot.used = true;
    slot.name = wc->class_name;
    slot.wndproc = wc->window_proc;
    const abi::Atom atom =
        static_cast<abi::Atom>(static_cast<std::size_t>(free_it - g_classes.begin()) + 1U);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "RegisterClassExA"},
        diagnostics::TraceField{"class", slot.name},
        diagnostics::TraceField{"atom", std::to_string(atom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("RegisterClassExA", fields, 4);
    return atom;
}

TL_MSABI abi::HWnd tl_CreateWindowExA(const std::uint32_t,  // NOLINT(bugprone-easily-swappable-parameters)
                                      const char* const class_name, const char* const window_name,
                                      const std::uint32_t, const int, const int, const int width,
                                      const int height, const void* const, const void* const,
                                      const void* const, const void* const) noexcept {
    if (!mapped_guest_cstring(class_name) || class_name == nullptr ||
        (window_name != nullptr && !mapped_guest_cstring(window_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "strings", "nome de classe ou janela inválido");
        return nullptr;
    }
    ClassSlot* const cls = find_class_slot(class_name);
    if (cls == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "class-lookup", "classe não registrada");
        return nullptr;
    }
    const auto free_it = std::find_if(g_windows.begin(), g_windows.end(),
                                      [](const WindowSlot& slot) { return !slot.used; });
    if (free_it == g_windows.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const char* const caption = window_name != nullptr ? window_name : cls->name.c_str();
    gui::NativeWindow native = gui::create_window(caption, width, height);
    if (native == nullptr) {
        set_last_error(abi::kErrorAccessDenied);
        trace_guest_failure("CreateWindowExA", "x11", "falha ao criar janela X11");
        return nullptr;
    }
    WindowSlot& slot = *free_it;
    slot.used = true;
    slot.wndproc = cls->wndproc;
    slot.class_name = cls->name;
    slot.native = native;
    slot.width = width;
    slot.height = height;
    const abi::Lresult create_result = call_wndproc(slot.wndproc, &slot, abi::kWmCreate, 0, 0);
    if (create_result == -1) {
        gui::destroy_window(slot.native);
        slot = {};
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("CreateWindowExA", "wm-create", "WM_CREATE rejeitou a criação");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "CreateWindowExA"},
        diagnostics::TraceField{"class", slot.class_name},
        diagnostics::TraceField{"window", caption},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("CreateWindowExA", fields, 4);
    return &slot;
}

TL_MSABI int tl_ShowWindow(const void* const window, const int cmd_show) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("ShowWindow", "handle-validation", "handle inválido");
        return 0;
    }
    const bool was_mapped = slot->mapped;
    if (cmd_show == 0) {
        if (slot->mapped && slot->native != nullptr) {
            gui::unmap_window(slot->native);
        }
        slot->mapped = false;
    } else if (slot->native != nullptr) {
        slot->mapped = gui::map_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    return was_mapped ? 1 : 0;
}

TL_MSABI int tl_UpdateWindow(const void* const window) noexcept {
    const WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("UpdateWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->wndproc != 0) {
        call_wndproc(slot->wndproc, const_cast<abi::HWnd>(window), abi::kWmPaint, 0, 0);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetMessageA(void* const msg, const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                            const std::uint32_t filter_min,  // NOLINT(bugprone-easily-swappable-parameters)
                            const std::uint32_t filter_max) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetMessageA", "output-message", "ponteiro sem permissão de escrita");
        return -1;
    }
    (void)filter_min;
    (void)filter_max;
    if (g_quit_requested) {
        g_quit_requested = false;
        write_guest_msg(msg, nullptr, abi::kWmQuit, g_quit_code, 0);
        set_last_error(abi::kErrorSuccess);
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "GetMessageA"},
            diagnostics::TraceField{"message", "WM_QUIT"},
            diagnostics::TraceField{"exit-code", std::to_string(g_quit_code)},
            diagnostics::TraceField{"result", "quit"},
        };
        runtime_trace("GetMessageA", fields, 4);
        return 0;
    }
    for (WindowSlot& slot : g_windows) {
        if (!slot.used || (window != nullptr && window != &slot)) {
            continue;
        }
        if (slot.has_pending) {
            write_guest_msg(msg, &slot, slot.pending.message, slot.pending.wparam,
                            slot.pending.lparam);
            slot.has_pending = false;
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
    }
    for (;;) {
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || slot.native == nullptr || (window != nullptr && window != &slot)) {
                continue;
            }
            const gui::WindowEvent event = gui::next_window_event(slot.native);
            if (event.type == gui::WindowEventType::Redraw) {
                write_guest_msg(msg, &slot, abi::kWmPaint, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Press) {
                slot.left_button_down = true;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonDown, abi::kMkLButton, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::Release) {
                slot.left_button_down = false;
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                write_guest_msg(msg, &slot, abi::kWmLButtonUp, 0, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::MouseMove) {
                const abi::Lparam lparam =
                    (static_cast<std::intptr_t>(event.y & 0xFFFF) << 16) |
                    static_cast<std::intptr_t>(event.x & 0xFFFF);
                const abi::Wparam wparam = slot.left_button_down ? abi::kMkLButton : 0;
                write_guest_msg(msg, &slot, abi::kWmMouseMove, wparam, lparam);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyDown) {
                slot.last_key = event.character;
                write_guest_msg(msg, &slot, abi::kWmKeyDown,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::KeyUp) {
                write_guest_msg(msg, &slot, abi::kWmKeyUp,
                                keydown_vkey(event.keysym, event.character), 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
            if (event.type == gui::WindowEventType::CloseRequested) {
                write_guest_msg(msg, &slot, abi::kWmClose, 0, 0);
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (WindowSlot& slot : g_windows) {
            if (!slot.used || (window != nullptr && window != &slot)) {
                continue;
            }
            for (GuestTimer& timer : slot.timers) {
                if (now >= timer.deadline) {
                    write_guest_msg(msg, &slot, abi::kWmTimer, timer.id, 0);
                    timer.deadline = std::chrono::steady_clock::now() + timer.interval;
                    set_last_error(abi::kErrorSuccess);
                    const std::array<diagnostics::TraceField, 4> fields{
                        diagnostics::TraceField{"symbol", "GetMessageA"},
                        diagnostics::TraceField{"message", "WM_TIMER"},
                        diagnostics::TraceField{"id", std::to_string(timer.id)},
                        diagnostics::TraceField{"status", "delivered"},
                    };
                    runtime_trace("GetMessageA", fields, 4);
                    return 1;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

TL_MSABI int tl_TranslateMessage(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("TranslateMessage", "message", "ponteiro de mensagem inválido");
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    if (message->message == abi::kWmKeyDown) {
        WindowSlot* const slot = find_window_slot(message->hwnd);
        if (slot != nullptr && slot->last_key != '\0' && !slot->has_pending) {
            slot->pending = {};
            slot->pending.message = abi::kWmChar;
            slot->pending.wparam =
                static_cast<abi::Wparam>(static_cast<unsigned char>(slot->last_key));
            slot->pending.lparam = 0;
            const abi::Wparam char_code = slot->pending.wparam;
            slot->last_key = '\0';
            slot->has_pending = true;
            set_last_error(abi::kErrorSuccess);
            const std::array<diagnostics::TraceField, 4> fields{
                diagnostics::TraceField{"symbol", "TranslateMessage"},
                diagnostics::TraceField{"message", "WM_CHAR"},
                diagnostics::TraceField{"wparam", std::to_string(char_code)},
                diagnostics::TraceField{"status", "translated"},
            };
            runtime_trace("TranslateMessage", fields, 4);
            return 1;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI abi::Lresult tl_DispatchMessageA(const void* const msg) noexcept {
    if (msg == nullptr || !mapped_guest_range(msg, sizeof(abi::GuestMsg), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("DispatchMessageA", "message", "ponteiro de mensagem inválido");
        return 0;
    }
    const auto* const message = static_cast<const abi::GuestMsg*>(msg);
    WindowSlot* const slot = find_window_slot(message->hwnd);
    if (slot == nullptr || slot->wndproc == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("DispatchMessageA", "window-lookup", "hwnd ou wndproc inválido");
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return call_wndproc(slot->wndproc, message->hwnd, message->message, message->wparam,
                        message->lparam);
}

TL_MSABI abi::Lresult tl_DefWindowProcA(const void* const window,
                                        const std::uint32_t message,  // NOLINT(bugprone-easily-swappable-parameters)
                                        const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    (void)wparam;
    (void)lparam;
    if (message == abi::kWmClose) {
        tl_DestroyWindow(window);
        return 0;
    }
    return 0;
}

TL_MSABI int tl_DestroyWindow(const void* const window) noexcept {
    WindowSlot* slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("DestroyWindow", "handle-validation", "handle inválido");
        return 0;
    }
    if (slot->native != nullptr) {
        gui::destroy_window(slot->native);
    }
    slot->native = nullptr;
    slot->mapped = false;
    const abi::HWnd hwnd = const_cast<abi::HWnd>(window);
    const std::uintptr_t wndproc = slot->wndproc;
    *slot = {};
    if (wndproc != 0) {
        call_wndproc(wndproc, hwnd, abi::kWmDestroy, 0, 0);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_PostQuitMessage(const int exit_code) noexcept {
    g_quit_code = static_cast<std::uint32_t>(exit_code);
    g_quit_requested = true;
}

TL_MSABI std::uintptr_t tl_SetTimer(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                                    const std::uintptr_t id,
                                    const std::uint32_t elapsed_ms,
                                    const void* const timer_proc) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || elapsed_ms == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("SetTimer", "argument-validation",
                            "hwnd inválido ou intervalo zero");
        return 0;
    }
    if (timer_proc != nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("SetTimer", "timer-proc",
                            "TIMERPROC ainda não suportado; use WM_TIMER");
        return 0;
    }
    const auto interval = std::chrono::milliseconds{elapsed_ms};
    GuestTimer* timer = nullptr;
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& entry) { return entry.id == id; });
    if (found != slot->timers.end()) {
        timer = &*found;
    } else {
        slot->timers.push_back(GuestTimer{});
        timer = &slot->timers.back();
        timer->id = id;
    }
    timer->interval = interval;
    timer->deadline = std::chrono::steady_clock::now() + interval;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"elapsed-ms", std::to_string(elapsed_ms)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("SetTimer", fields, 4);
    return id;
}

TL_MSABI int tl_KillTimer(const void* const window, const std::uintptr_t id) noexcept {
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("KillTimer", "handle-validation", "hwnd inválido");
        return 0;
    }
    const auto found = std::find_if(slot->timers.begin(), slot->timers.end(),
                                    [id](const GuestTimer& timer) { return timer.id == id; });
    if (found == slot->timers.end()) {
        set_last_error(abi::kErrorSuccess);
        return 0;
    }
    slot->timers.erase(found);
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "KillTimer"},
        diagnostics::TraceField{"id", std::to_string(id)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"result", "killed"},
    };
    runtime_trace("KillTimer", fields, 4);
    return 1;
}

// Tokens opacos para stock objects do GDI: o próprio endereço serve de handle
// e o deslocamento identifica o objeto. Stock objects não são liberados.
// (Definido no namespace anônimo; o deslocamento do token é o índice do objeto.)
TL_MSABI void* tl_GetStockObject(const int object) noexcept {
    if (object < 0 || object >= 24) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetStockObject", "object", "stock object fora da faixa suportada");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetStockObject"},
        diagnostics::TraceField{"object", std::to_string(object)},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "token"},
    };
    runtime_trace("GetStockObject", fields, 4);
    return kStockObjectTokens + object;
}

// Inicia a pintura de uma janela: preenche o PAINTSTRUCT convidado e devolve
// um HDC (o próprio handle de janela, que identifica o destino de desenho).
TL_MSABI void* tl_BeginPaint(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                             void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("BeginPaint", "paint-struct", "ponteiro sem permissão de escrita");
        return nullptr;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("BeginPaint", "handle-validation", "hwnd inválido");
        return nullptr;
    }
    auto* const ps = static_cast<abi::GuestPaintStruct*>(paint_struct);
    *ps = {};
    ps->hdc = const_cast<void*>(window);
    ps->f_erase = 1;
    ps->rc_paint = {0, 0, slot->width, slot->height};
    slot->painting = true;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "BeginPaint"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "painting"},
    };
    runtime_trace("BeginPaint", fields, 4);
    return ps->hdc;
}

TL_MSABI int tl_EndPaint(const void* const window,  // NOLINT(bugprone-easily-swappable-parameters)
                         const void* const paint_struct) noexcept {
    if (paint_struct == nullptr ||
        !mapped_guest_range(paint_struct, sizeof(abi::GuestPaintStruct), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("EndPaint", "paint-struct", "ponteiro inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("EndPaint", "handle-validation", "hwnd inválido");
        return 0;
    }
    slot->painting = false;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "EndPaint"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"result", "painted"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
    };
    runtime_trace("EndPaint", fields, 4);
    return 1;
}

TL_MSABI int tl_TextOut(const void* const dc, const int x, const int y,  // NOLINT(bugprone-easily-swappable-parameters)
                        const char* const text, const int length) noexcept {
    if (text == nullptr || length < 0 ||
        (length > 0 &&
         !mapped_guest_range(text, static_cast<std::size_t>(length), false))) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("TextOut", "text", "ponteiro ou comprimento inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("TextOut", "dc", "HDC inválido");
        return 0;
    }
    if (length > 0) {
        gui::draw_text_len(slot->native, text, length, x, y);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "TextOut"},
        diagnostics::TraceField{"x", std::to_string(x)},
        diagnostics::TraceField{"y", std::to_string(y)},
        diagnostics::TraceField{"length", std::to_string(length)},
    };
    runtime_trace("TextOut", fields, 4);
    return 1;
}

TL_MSABI int tl_FillRect(const void* const dc,  // NOLINT(bugprone-easily-swappable-parameters)
                         const void* const rect, const void* const brush) noexcept {
    if (rect == nullptr || !mapped_guest_range(rect, sizeof(abi::GuestRect), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "rect", "ponteiro RECT inválido");
        return 0;
    }
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("FillRect", "dc", "HDC inválido");
        return 0;
    }
    const int brush_index = stock_object_index(brush);
    if (brush_index < 0 || brush_index >= 6) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("FillRect", "brush", "brush deve ser um stock object WHITE..NULL");
        return 0;
    }
    const auto* const rc = static_cast<const abi::GuestRect*>(rect);
    const int width = rc->right - rc->left;
    const int height = rc->bottom - rc->top;
    if (width > 0 && height > 0 && brush_index != 5) {
        gui::fill_rectangle(slot->native, rc->left, rc->top, width, height, brush_index);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "FillRect"},
        diagnostics::TraceField{"brush", std::to_string(brush_index)},
        diagnostics::TraceField{"rect", std::to_string(rc->left) + "," + std::to_string(rc->top) +
                                     "-" + std::to_string(rc->right) + "," +
                                     std::to_string(rc->bottom)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("FillRect", fields, 4);
    return 1;
}

TL_MSABI int tl_Rectangle(const void* const dc,  // NOLINT(bugprone-easily-swappable-parameters)
                          const int left, const int top, const int right,
                          const int bottom) noexcept {
    WindowSlot* const slot = find_window_slot(dc);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("Rectangle", "dc", "HDC inválido");
        return 0;
    }
    const int width = right - left;
    const int height = bottom - top;
    if (width > 0 && height > 0) {
        gui::draw_rectangle(slot->native, left, top, width, height);
        gui::flush_window(slot->native);
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "Rectangle"},
        diagnostics::TraceField{"rect", std::to_string(left) + "," + std::to_string(top) + "-" +
                                     std::to_string(right) + "," + std::to_string(bottom)},
        diagnostics::TraceField{"mechanism", "contorno"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("Rectangle", fields, 4);
    return 1;
}

TL_MSABI void* tl_GetDC(const void* const window) noexcept {
    const WindowSlot* const slot = find_window_slot(window);
    if (slot == nullptr || slot->native == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        trace_guest_failure("GetDC", "handle-validation", "hwnd inválido");
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetDC"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "window-dc"},
    };
    runtime_trace("GetDC", fields, 4);
    return const_cast<void*>(window);
}

TL_MSABI int tl_ReleaseDC(const void* const window, const void* const dc) noexcept {
    if (window == nullptr || dc != window) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("ReleaseDC", "dc", "HDC não pertence à janela");
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "ReleaseDC"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"mechanism", "hdc=hwnd"},
        diagnostics::TraceField{"result", "released"},
    };
    runtime_trace("ReleaseDC", fields, 4);
    return 1;
}

TL_MSABI void tl_InitializeCriticalSection(void* const critical_section) noexcept {
    critical_section_valid(critical_section);
}

TL_MSABI void tl_DeleteCriticalSection(void* const critical_section) noexcept {
    critical_section_valid(critical_section);
}

TL_MSABI void tl_EnterCriticalSection(void* const critical_section) noexcept {
    critical_section_valid(critical_section);
}

TL_MSABI void tl_LeaveCriticalSection(void* const critical_section) noexcept {
    critical_section_valid(critical_section);
}

TL_MSABI int tl_GetConsoleMode(const void* const handle, std::uint32_t* const mode) noexcept {
    if (mode == nullptr || !mapped_guest_range(mode, sizeof(*mode), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("GetConsoleMode", "mode", "ponteiro sem permissão de escrita");
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || ::isatty(fd) == 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *mode = fd == STDIN_FILENO ? 0x3U : 0x3U;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "GetConsoleMode"},
        diagnostics::TraceField{"fd", std::to_string(fd)},
        diagnostics::TraceField{"mode", std::to_string(*mode)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("GetConsoleMode", fields, 4);
    return 1;
}

TL_MSABI int tl_SetConsoleMode(const void* const handle, const std::uint32_t mode) noexcept {
    const int fd = handle_fd(handle);
    if (fd < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetConsoleMode"},
        diagnostics::TraceField{"fd", std::to_string(fd)},
        diagnostics::TraceField{"mode", std::to_string(mode)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("SetConsoleMode", fields, 4);
    return 1;
}

TL_MSABI int tl_IsDBCSLeadByteEx(const std::uint32_t, const std::uint8_t) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(const std::uintptr_t handler) noexcept {
    const std::uintptr_t previous = g_unhandled_exception_filter;
    g_unhandled_exception_filter = handler;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "SetUnhandledExceptionFilter"},
        diagnostics::TraceField{"handler", std::to_string(handler)},
        diagnostics::TraceField{"previous", std::to_string(previous)},
        diagnostics::TraceField{"mechanism", "registrado-sem-invocacao"},
    };
    runtime_trace("SetUnhandledExceptionFilter", fields, 4);
    return previous;
}

TL_MSABI void tl_Sleep(const std::uint32_t milliseconds) noexcept {
    timespec requested{
        .tv_sec = static_cast<std::time_t>(milliseconds / 1000U),
        .tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000L),
    };
    timespec remaining{};
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        requested = remaining;
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI void* tl_TlsGetValue(const std::uint32_t tls_index) noexcept {
    if (tls_index >= g_guest_tls_slots.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return g_guest_tls_slots[tls_index];
}

TL_MSABI int tl_VirtualProtect(void* const address, const std::uintptr_t size, // NOLINT(bugprone-easily-swappable-parameters)
                               const std::uint32_t new_protection,
                               std::uint32_t* const old_protection) noexcept {
    if (old_protection == nullptr ||
        !mapped_guest_range(old_protection, sizeof(*old_protection), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("VirtualProtect", "old-protection", "ponteiro sem permissão de escrita");
        return 0;
    }
    const int prot = host_protection(new_protection);
    if (address == nullptr || size == 0 || prot < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    MapsRegion region{};
    if (!find_maps_region(address, region)) {
        set_last_error(abi::kErrorInvalidAddress);
        trace_guest_failure("VirtualProtect", "region-lookup", "endereço não mapeado");
        return 0;
    }
    *old_protection = win32_protection(region.perms);
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uintptr_t end = start + size;
    if (start < region.start || end > region.end) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    constexpr std::uintptr_t kPageSize = 0x1000U;
    const std::uintptr_t rounded_start = start / kPageSize * kPageSize;
    std::uintptr_t rounded_end = (end + kPageSize - 1U) / kPageSize * kPageSize;
    if (rounded_end > region.end) {
        rounded_end = region.end;
    }
    if (rounded_start >= rounded_end ||
        mprotect(std::bit_cast<void*>(rounded_start),
                 static_cast<std::size_t>(rounded_end - rounded_start), prot) != 0) {
        const std::uint32_t failure_error = errno_to_win32(errno);
        set_last_error(failure_error);
        trace_linux_failure("VirtualProtect", "mprotect", errno, failure_error);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "VirtualProtect"},
        diagnostics::TraceField{"address", std::to_string(start)},
        diagnostics::TraceField{"protection", std::to_string(new_protection)},
        diagnostics::TraceField{"old-protection", std::to_string(*old_protection)},
    };
    runtime_trace("VirtualProtect", fields, 4);
    return 1;
}

TL_MSABI std::uintptr_t tl_VirtualQuery(const void* const address, void* const memory_information, // NOLINT(bugprone-easily-swappable-parameters)
                                        const std::uintptr_t length) noexcept {
    if (memory_information == nullptr ||
        length < sizeof(abi::GuestMemoryBasicInformation) ||
        !mapped_guest_range(memory_information, sizeof(abi::GuestMemoryBasicInformation), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("VirtualQuery", "memory-information", "buffer de saída inválido");
        return 0;
    }
    MapsRegion region{};
    if (!find_maps_region(address, region)) {
        set_last_error(abi::kErrorInvalidAddress);
        return 0;
    }
    auto* const info = static_cast<abi::GuestMemoryBasicInformation*>(memory_information);
    info->base_address = std::bit_cast<void*>(region.start);
    info->allocation_base = std::bit_cast<void*>(region.start);
    info->allocation_protect = win32_protection(region.perms);
    info->padding1 = 0;
    info->region_size = static_cast<std::uintptr_t>(region.end - region.start);
    info->state = abi::kMemCommit;
    info->protect = win32_protection(region.perms);
    info->type = region.has_path ? abi::kMemImage : abi::kMemPrivate;
    info->padding2 = 0;
    set_last_error(abi::kErrorSuccess);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "VirtualQuery"},
        diagnostics::TraceField{"address", std::to_string(reinterpret_cast<std::uintptr_t>(address))},
        diagnostics::TraceField{"region-size", std::to_string(info->region_size)},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("VirtualQuery", fields, 4);
    return sizeof(abi::GuestMemoryBasicInformation);
}

TL_MSABI int tl_MultiByteToWideChar(const std::uint32_t code_page, const std::uint32_t flags, // NOLINT(bugprone-easily-swappable-parameters)
                                    const char* const mb_str, const int mb_count,
                                    std::uint16_t* const wide_str, const int wide_count) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpUtf8;
    if (mb_str == nullptr || mb_count == 0 || !supported_page ||
        (flags & ~(abi::kMbPrecomposed | abi::kMbErrInvalidChars)) != 0 ||
        (wide_count != 0 && wide_str == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = mb_count == -1;
    if (null_terminated && !mapped_guest_cstring(mb_str)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("MultiByteToWideChar", "source", "string convidada inválida");
        return 0;
    }
    const std::uint32_t byte_count =
        null_terminated ? static_cast<std::uint32_t>(std::strlen(mb_str))
                        : static_cast<std::uint32_t>(mb_count);
    if (!mapped_guest_range(mb_str, byte_count, false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("MultiByteToWideChar", "source", "memória convidada inválida");
        return 0;
    }
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(mb_str);
    std::size_t index = 0;
    std::size_t needed = 0;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            if ((flags & abi::kMbErrInvalidChars) != 0) {
                set_last_error(abi::kErrorNoUnicodeTranslation);
                return 0;
            }
            codepoint = 0x3FU;
        }
        std::uint16_t units[2]{};
        needed += utf16_units_for(codepoint, units);
    }
    if (null_terminated) {
        needed += 1;
    }
    if (needed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (wide_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (wide_count < 0 || static_cast<std::size_t>(wide_count) < needed ||
        !mapped_guest_range(wide_str, needed * sizeof(std::uint16_t), true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    index = 0;
    std::size_t written = 0;
    while (index < byte_count) {
        std::uint32_t codepoint = decode_multibyte(code_page, bytes, byte_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = 0x3FU;
        }
        std::uint16_t units[2]{};
        const std::size_t count = utf16_units_for(codepoint, units);
        for (std::size_t unit = 0; unit < count; ++unit) {
            wide_str[written] = units[unit];
            written += 1;
        }
    }
    if (null_terminated) {
        wide_str[written] = 0;
        written += 1;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

TL_MSABI int tl_WideCharToMultiByte(const std::uint32_t code_page, const std::uint32_t flags, // NOLINT(bugprone-easily-swappable-parameters)
                                    const std::uint16_t* const wide_str, const int wide_count,
                                    char* const mb_str, const int mb_count,
                                    const char* const default_char,
                                    int* const used_default_char) noexcept {
    const bool supported_page = code_page == abi::kCpAcp || code_page == abi::kCp1252 ||
                                code_page == abi::kCpUtf8;
    if (wide_str == nullptr || wide_count == 0 || !supported_page ||
        (flags & ~(abi::kWcCompositeCheck | abi::kWcNoBestFitChars)) != 0 ||
        (mb_count != 0 && mb_str == nullptr) ||
        (used_default_char != nullptr &&
         !mapped_guest_range(used_default_char, sizeof(*used_default_char), true))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const bool null_terminated = wide_count == -1;
    std::size_t unit_count = 0;
    if (null_terminated) {
        while (wide_str[unit_count] != 0) {
            unit_count += 1;
        }
    } else {
        unit_count = static_cast<std::size_t>(wide_count);
    }
    if (!mapped_guest_range(wide_str, unit_count * sizeof(std::uint16_t), false)) {
        set_last_error(abi::kErrorInvalidParameter);
        trace_guest_failure("WideCharToMultiByte", "source", "memória convidada inválida");
        return 0;
    }
    const bool utf8 = code_page == abi::kCpUtf8;
    std::size_t index = 0;
    std::size_t needed = 0;
    while (index < unit_count) {
        const std::uint32_t codepoint = decode_utf16(wide_str, unit_count, index);
        if (utf8) {
            char bytes[4]{};
            needed += utf8_bytes_for(codepoint > 0x10FFFFU ? 0x3FU : codepoint, bytes);
        } else {
            needed += 1;
        }
    }
    if (null_terminated) {
        needed += 1;
    }
    if (mb_str == nullptr) {
        set_last_error(abi::kErrorSuccess);
        return static_cast<int>(needed);
    }
    if (mb_count < 0 || static_cast<std::size_t>(mb_count) < needed ||
        !mapped_guest_range(mb_str, needed, true)) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    const char fallback = default_char != nullptr ? *default_char : '?';
    bool used_default = false;
    index = 0;
    std::size_t written = 0;
    while (index < unit_count) {
        std::uint32_t codepoint = decode_utf16(wide_str, unit_count, index);
        if (codepoint > 0x10FFFFU) {
            codepoint = 0x3FU;
        }
        if (utf8) {
            char bytes[4]{};
            const std::size_t count = utf8_bytes_for(codepoint, bytes);
            for (std::size_t byte_index = 0; byte_index < count; ++byte_index) {
                mb_str[written] = bytes[byte_index];
                written += 1;
            }
        } else {
            std::uint8_t byte = 0;
            if (unicode_to_cp1252(codepoint, byte)) {
                mb_str[written] = static_cast<char>(byte);
            } else {
                mb_str[written] = fallback;
                used_default = true;
            }
            written += 1;
        }
    }
    if (null_terminated) {
        mb_str[written] = '\0';
        written += 1;
    }
    if (used_default_char != nullptr) {
        *used_default_char = used_default ? 1 : 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<int>(written);
}

// ---------------------------------------------------------------------------
// GetModuleHandle / GetProcAddress (Fase 9)
// ---------------------------------------------------------------------------

// Módulos conhecidos internamente. O handle é o endereço do próprio
// registrations (token opaco); GetProcAddress consulta o registro interno.
struct KnownModule {
    const char* name;
    std::uintptr_t handle;
};

std::vector<KnownModule>& known_modules() {
    static std::vector<KnownModule> instance;
    return instance;
}

void ensure_known_modules() {
    if (!known_modules().empty()) {
        return;
    }
    known_modules().push_back({"kernel32.dll", 0x1000});
    known_modules().push_back({"user32.dll", 0x2000});
    known_modules().push_back({"gdi32.dll", 0x3000});
    known_modules().push_back({"msvcrt.dll", 0x4000});
}

void* tl_GetModuleHandleA(const char* module_name) noexcept {
    ensure_known_modules();
    if (module_name == nullptr) {
        return reinterpret_cast<void*>(0x1000);
    }
    std::string name_lower(module_name);
    for (auto& c : name_lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& mod : known_modules()) {
        if (name_lower == mod.name) {
            return reinterpret_cast<void*>(mod.handle);
        }
    }
    set_last_error(abi::kErrorFileNotFound);
    return nullptr;
}

void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return tl_GetModuleHandleA(nullptr);
    }
    std::string narrow;
    while (*module_name != 0) {
        narrow.push_back(static_cast<char>(*module_name & 0x7F));
        ++module_name;
    }
    return tl_GetModuleHandleA(narrow.c_str());
}

void* tl_GetProcAddress(void* module, const char* name) noexcept {
    (void)module;
    (void)name;
    set_last_error(abi::kErrorFileNotFound);
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetCommandLine / GetEnvironmentVariable (Fase 9)
// ---------------------------------------------------------------------------

static std::string g_command_line_string;

const char* tl_GetCommandLineA() noexcept {
    if (g_command_line_string.empty()) {
        const auto& args = msvcrt_get_guest_arguments();
        if (args.empty()) {
            g_command_line_string = "\"\"";
        } else {
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    g_command_line_string.push_back(' ');
                }
                g_command_line_string.push_back('"');
                g_command_line_string += args[i];
                g_command_line_string.push_back('"');
            }
        }
    }
    return g_command_line_string.c_str();
}

const std::uint16_t* tl_GetCommandLineW() noexcept {
    static std::vector<std::uint16_t> wide_cmdline;
    const char* narrow = tl_GetCommandLineA();
    wide_cmdline.clear();
    while (*narrow != '\0') {
        wide_cmdline.push_back(static_cast<std::uint16_t>(static_cast<unsigned char>(*narrow)));
        ++narrow;
    }
    wide_cmdline.push_back(0);
    return wide_cmdline.data();
}

extern char** environ;

std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                          std::uint32_t size) noexcept {
    if (name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const char* value = ::getenv(name);
    if (value == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    const std::size_t len = std::strlen(value);
    if (buffer == nullptr || size == 0) {
        return static_cast<std::uint32_t>(len);
    }
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return static_cast<std::uint32_t>(len);
    }
    std::memcpy(buffer, value, len);
    buffer[len] = '\0';
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                          std::uint32_t size) noexcept {
    if (name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string narrow_name;
    while (*name != 0) {
        narrow_name.push_back(static_cast<char>(*name & 0x7F));
        ++name;
    }
    const std::uint32_t result = tl_GetEnvironmentVariableA(narrow_name.c_str(), nullptr, 0);
    if (result == 0) {
        return 0;
    }
    if (buffer == nullptr || size == 0) {
        return result;
    }
    if (size <= result) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return result;
    }
    char narrow_buf[1024]{};
    tl_GetEnvironmentVariableA(narrow_name.c_str(), narrow_buf, sizeof(narrow_buf));
    for (std::uint32_t i = 0; i <= result; ++i) {
        buffer[i] = static_cast<std::uint16_t>(static_cast<unsigned char>(narrow_buf[i]));
    }
    set_last_error(abi::kErrorSuccess);
    return result;
}

// ---------------------------------------------------------------------------
// Heap (Fase 9) — wrapper sobre malloc/free do hospedeiro.
// ---------------------------------------------------------------------------

void* tl_GetProcessHeap() noexcept {
    static char g_process_heap_token = 0;
    return &g_process_heap_token;
}

void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept {
    (void)heap;
    if ((flags & 0x0008) != 0) {
        return std::calloc(1, size);
    }
    return std::malloc(size);
}

int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept {
    (void)heap;
    (void)flags;
    std::free(memory);
    return 1;
}

void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                      std::uintptr_t new_size) noexcept {
    (void)heap;
    (void)flags;
    return std::realloc(memory, new_size);
}

// ---------------------------------------------------------------------------
// Tempo (Fase 9)
// ---------------------------------------------------------------------------

std::uint64_t tl_GetTickCount64() noexcept {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
}

void tl_GetSystemTimeAsFileTime(void* file_time) noexcept {
    auto* ft = static_cast<std::uint64_t*>(file_time);
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    const auto since_epoch = duration_cast<nanoseconds>(now).count();
    const std::uint64_t ticks_100ns = static_cast<std::uint64_t>(since_epoch) / 100;
    const std::uint64_t epoch_diff = 116444736000000000ULL;
    *ft = ticks_100ns + epoch_diff;
}

}  // extern "C"

GuestExecutionResult execute_guest_entry(const std::uintptr_t entry_point, // NOLINT(bugprone-easily-swappable-parameters)
                                         const std::uintptr_t stack_top) noexcept {
    using EntryPoint = TL_MSABI void (*)();
    const auto entry = std::bit_cast<EntryPoint>(entry_point);
    if (entry == nullptr || stack_top == 0) {
        return {};
    }
    constexpr std::size_t kTebSize = sizeof(GuestTeb);
    void* const teb = mmap(nullptr, kTebSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (teb == MAP_FAILED) {
        return {};
    }
    auto* const fields = static_cast<GuestTeb*>(teb);
    fields->self = teb;
    fields->stack_base = std::bit_cast<void*>(stack_top);
    fields->stack_limit = std::bit_cast<void*>(stack_top - 0x100000U);  // kGuestStackSize
    const bool gs_configured = set_guest_gs_base(teb);
    if (!gs_configured) {
        static_cast<void>(munmap(teb, kTebSize));
        return {};
    }
    g_quit_requested = false;
    g_quit_code = 0;
    g_guest_execution_active = true;
    if (setjmp(g_guest_exit_context) == 0) {
        tl_call_guest_on_stack(std::bit_cast<std::uintptr_t>(entry), stack_top);
        g_guest_execution_active = false;
        static_cast<void>(set_guest_gs_base(nullptr));
        static_cast<void>(munmap(teb, kTebSize));
        return {};
    }
    g_guest_execution_active = false;
    static_cast<void>(set_guest_gs_base(nullptr));
    static_cast<void>(munmap(teb, kTebSize));
    return {.exited_explicitly = true, .exit_code = g_guest_exit_code};
}

}  // namespace tradutorlinux
