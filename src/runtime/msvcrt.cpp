#include "tradutorlinux/runtime/msvcrt.hpp"
#include "runtime_context.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/error_map.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" char** environ;

namespace tradutorlinux {
namespace {

using diagnostics::TraceComponent;
using diagnostics::TraceField;
using diagnostics::TraceLevel;
using diagnostics::write_trace;

// Flags do _iobuf convidado (mingw).
constexpr int kIoRead = 0x0001;
constexpr int kIoWrite = 0x0002;
constexpr int kIoReadWrite = 0x0004;
constexpr int kIoEof = 0x0010;
constexpr int kIoError = 0x0020;
constexpr int kIoBinary = 0x8000;

// Modos de arquivo do msvcrt.
constexpr int kO_BINARY = 0x8000;
constexpr int kO_TEXT = 0x4000;

constexpr int kFileSlotCount = 256;
constexpr int kFdModeCount = 512;

// Ponteiro para função convidada (convenção Microsoft x64).
using GuestFnPtr = void (TL_CRT_MSABI *)();
using GuestSignalFn = void (TL_CRT_MSABI *)(int);

// ---------------------------------------------------------------------------
// Estado por processo convidado. O convidado executa no processo filho; o
// estado é herdado pelo fork e não é compartilhado entre execuções.
// ---------------------------------------------------------------------------

std::vector<std::string> g_guest_arguments;
std::string g_guest_command_line;

thread_local int g_crt_errno = 0;

std::array<GuestFile, kFileSlotCount> g_file_pool;
std::array<bool, kFileSlotCount> g_slot_used;
std::array<int, kFdModeCount> g_fd_modes;
bool g_std_ready = false;

void ensure_standard_files() {
    if (g_std_ready) {
        return;
    }
    g_std_ready = true;
    g_file_pool = {};
    g_slot_used = {};
    g_slot_used[0] = g_slot_used[1] = g_slot_used[2] = true;
    g_file_pool[0].file = 0;
    g_file_pool[0].flag = kIoRead;
    g_file_pool[0].charbuf = -1;
    g_file_pool[1].file = 1;
    g_file_pool[1].flag = kIoWrite;
    g_file_pool[1].charbuf = -1;
    g_file_pool[2].file = 2;
    g_file_pool[2].flag = kIoWrite;
    g_file_pool[2].charbuf = -1;
    g_fd_modes.fill(kO_TEXT);
}

void set_error(int error) {
    g_crt_errno = error;
}

// Tradução de caminhos usados pelo CRT do convidado. O Win32 aceita barra
// invertida como separador; o host usa barra normal. Drives virtuais C:\ e caminhos
// absolutos são resolvidos dentro do prefixo do TradutorLinux.
[[nodiscard]] bool translate_guest_path(const char* guest_path, char* host_path,
                                        const std::size_t host_path_size) noexcept {
    if (guest_path == nullptr || guest_path[0] == '\0') {
        return false;
    }

    std::string_view view{guest_path};
    if ((view.size() >= 2 && (view[0] == 'C' || view[0] == 'c') && view[1] == ':') ||
        view.starts_with('\\')) {
        const std::filesystem::path resolved =
            prefix::resolve_windows_path(view, guest_prefix_root());
        const std::string s = resolved.string();
        if (s.size() + 1 > host_path_size) {
            return false;
        }
        std::memcpy(host_path, s.c_str(), s.size() + 1);
        return true;
    }

    std::size_t length = 0;
    for (; guest_path[length] != '\0'; ++length) {
        if (length + 1U >= host_path_size || guest_path[length] == ':') {
            return false;
        }
        host_path[length] = guest_path[length] == '\\' ? '/' : guest_path[length];
    }
    host_path[length] = '\0';
    return true;
}

void trace_crt(const TraceLevel level, const std::string_view event,
               const std::initializer_list<TraceField> fields = {}) {
    const std::vector<TraceField> list(fields.begin(), fields.end());
    write_trace(std::cerr, TraceComponent::Crt, level, event, list);
}

GuestFile* alloc_slot(int fd, int flag) {
    ensure_standard_files();
    for (std::size_t index = 3; index < g_file_pool.size(); ++index) {
        if (!g_slot_used[index]) {
            g_slot_used[index] = true;
            g_file_pool[index] = GuestFile{};
            g_file_pool[index].file = fd;
            g_file_pool[index].flag = flag;
            g_file_pool[index].charbuf = -1;
            return &g_file_pool[index];
        }
    }
    return nullptr;
}

bool write_all(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t written = ::write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(errno);
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

// ---------------------------------------------------------------------------
// va_list Microsoft x64: char* para slots de 8 bytes.
// ---------------------------------------------------------------------------

int read_int_slot(GuestVaList& ap) {
    int value = 0;
    std::memcpy(&value, ap, sizeof(value));
    ap += 8;
    return value;
}

std::uintptr_t read_ptr_slot(GuestVaList& ap) {
    std::uintptr_t value = 0;
    std::memcpy(&value, ap, sizeof(value));
    ap += 8;
    return value;
}

std::string utf16_to_utf8(const std::uint16_t* text, std::size_t char_limit) {
    if (text == nullptr) {
        return {};
    }
    return util::wide_to_utf8(text, char_limit);
}

// Converte UTF-8 para UTF-16, limitando a quantidade de unidades de saída
// (se out_size for 0, apenas calcula o tamanho necessário).
std::size_t utf8_to_utf16(const char* text, std::uint16_t* out, std::size_t out_units) {
    if (text == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    std::size_t written = 0;
    std::size_t index = 0;
    while (text[index] != '\0') {
        const auto byte = static_cast<unsigned char>(text[index]);
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        if (byte < 0x80) {
            code_point = byte;
            length = 1;
        } else if ((byte & 0xE0U) == 0xC0U) {
            code_point = byte & 0x1FU;
            length = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            code_point = byte & 0x0FU;
            length = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            code_point = byte & 0x07U;
            length = 4;
        } else {
            return static_cast<std::size_t>(-1);
        }
        if (length > 1) {
            for (std::size_t i = 1; i < length; ++i) {
                if (text[index + i] == '\0' || (static_cast<unsigned char>(text[index + i]) & 0xC0U) != 0x80U) {
                    return static_cast<std::size_t>(-1);
                }
                code_point = (code_point << 6) | (static_cast<unsigned char>(text[index + i]) & 0x3FU);
            }
        }
        index += length;
        const std::size_t needed_units = code_point > 0xFFFFU ? 2 : 1;
        if (out != nullptr) {
            if (written + needed_units > out_units) {
                return static_cast<std::size_t>(-1);
            }
            if (code_point > 0xFFFFU) {
                const std::uint32_t adjusted = code_point - 0x10000U;
                out[written] = static_cast<std::uint16_t>(0xD800U | (adjusted >> 10));
                out[written + 1] = static_cast<std::uint16_t>(0xDC00U | (adjusted & 0x3FFU));
            } else {
                out[written] = static_cast<std::uint16_t>(code_point);
            }
        }
        written += needed_units;
    }
    return written;
}

// ---------------------------------------------------------------------------
// Motor de formatação do printf mínimo (subset documentado em docs).
// ---------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
template <typename T>
std::string sprint(const std::string& format, T value) {
    const int needed = std::snprintf(nullptr, 0, format.c_str(), value);
    if (needed < 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    std::snprintf(out.data(), out.size() + 1, format.c_str(), value);
    return out;
}
#pragma GCC diagnostic pop

std::string build_host_format(const std::string& flags, int width, int precision,
                              const std::string& length, char conv) {
    std::string format = "%";
    format += flags;
    if (width > 0) {
        format += std::to_string(width);
    }
    if (precision >= 0) {
        format += "." + std::to_string(precision);
    }
    format += length;
    format += conv;
    return format;
}

std::string format_atom(const std::string& flags, int width, int precision, const std::string& length,
                        char conv, GuestVaList& ap, long& count) {
    int adjusted_width = width;
    std::string adjusted_flags = flags;
    if (adjusted_width < 0) {
        adjusted_width = -adjusted_width;
        if (adjusted_flags.find('-') == std::string::npos) {
            adjusted_flags.push_back('-');
        }
    }
    switch (conv) {
        case 'd':
        case 'i': {
            long long value = 0;
            if (length == "hh") {
                signed char v = 0;
                std::memcpy(&v, ap, 1);
                value = v;
            } else if (length == "h") {
                short v = 0;
                std::memcpy(&v, ap, 2);
                value = v;
            } else if (length == "ll" || length == "I64" || length == "j" || length == "t") {
                std::memcpy(&value, ap, 8);
            } else if (length == "z") {
                ssize_t v = 0;
                std::memcpy(&v, ap, sizeof(v));
                value = v;
            } else {
                int v = 0;
                std::memcpy(&v, ap, 4);
                value = v;
            }
            ap += 8;
            return sprint(build_host_format(adjusted_flags, adjusted_width, precision, "ll", 'd'),
                          static_cast<long long>(value));
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long value = 0;
            if (length == "hh") {
                unsigned char v = 0;
                std::memcpy(&v, ap, 1);
                value = v;
            } else if (length == "h") {
                unsigned short v = 0;
                std::memcpy(&v, ap, 2);
                value = v;
            } else if (length == "ll" || length == "I64" || length == "j" || length == "t") {
                std::memcpy(&value, ap, 8);
            } else if (length == "z") {
                std::size_t v = 0;
                std::memcpy(&v, ap, sizeof(v));
                value = v;
            } else {
                unsigned int v = 0;
                std::memcpy(&v, ap, 4);
                value = v;
            }
            ap += 8;
            return sprint(build_host_format(adjusted_flags, adjusted_width, precision, "ll", conv),
                          static_cast<unsigned long long>(value));
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            double value = 0.0;
            std::memcpy(&value, ap, 8);
            ap += 8;
            const std::string host_length = (length == "L") ? "L" : "";
            return sprint(build_host_format(adjusted_flags, adjusted_width, precision, host_length, conv),
                          value);
        }
        case 'c': {
            const int value = read_int_slot(ap);
            if (length == "l") {
                const std::uint16_t wide[2] = {static_cast<std::uint16_t>(value), 0};
                std::string text = utf16_to_utf8(wide, 1);
                const int needed = adjusted_width - static_cast<int>(text.size());
                if (adjusted_flags.find('-') == std::string::npos && needed > 0) {
                    text = std::string(static_cast<std::size_t>(needed), ' ') + text;
                } else if (adjusted_flags.find('-') != std::string::npos && needed > 0) {
                    text += std::string(static_cast<std::size_t>(needed), ' ');
                }
                return text;
            }
            return sprint(build_host_format(adjusted_flags, adjusted_width, precision, "", 'c'),
                          static_cast<int>(static_cast<unsigned char>(value)));
        }
        case 's': {
            if (length == "l" || length == "w") {
                const std::uint16_t* wide =
                    reinterpret_cast<const std::uint16_t*>(read_ptr_slot(ap));
                const std::string text = utf16_to_utf8(
                    wide, precision >= 0 ? static_cast<std::size_t>(precision)
                                         : std::numeric_limits<std::size_t>::max());
                const std::string format =
                    build_host_format(adjusted_flags, adjusted_width, -1, "", 's');
                return sprint(format, text.c_str());
            }
            const char* value = reinterpret_cast<const char*>(read_ptr_slot(ap));
            if (value == nullptr) {
                value = "(null)";
            }
            const std::string format =
                build_host_format(adjusted_flags, adjusted_width, precision, "", 's');
            return sprint(format, value);
        }
        case 'p': {
            const std::uintptr_t value = read_ptr_slot(ap);
            const std::string format =
                build_host_format(adjusted_flags, adjusted_width, precision, "", 'p');
            return sprint(format, reinterpret_cast<void*>(value));
        }
        case 'n': {
            const std::uintptr_t target = read_ptr_slot(ap);
            if (target != 0) {
                if (length == "ll") {
                    long long value = static_cast<long long>(count);
                    std::memcpy(reinterpret_cast<void*>(target), &value, sizeof(value));
                } else if (length == "l") {
                    long value = static_cast<long>(count);
                    std::memcpy(reinterpret_cast<void*>(target), &value, sizeof(value));
                } else {
                    int value = static_cast<int>(count);
                    std::memcpy(reinterpret_cast<void*>(target), &value, sizeof(value));
                }
            }
            return {};
        }
        case '%':
            return "%";
        default:
            return {};
    }
}

int vformat_into(GuestFile* file, const char* format, GuestVaList& ap) {
    std::string out;
    long count = 0;
    try {
        if (format == nullptr) {
            set_error(EINVAL);
            return EOF;
        }
        const char* p = format;
        while (*p != '\0') {
            if (*p != '%') {
                out.push_back(*p++);
                ++count;
                continue;
            }
            ++p;
            if (*p == '%') {
                out.push_back('%');
                ++count;
                ++p;
                continue;
            }
            std::string flags;
            while (*p != '\0' && std::strchr("-+0 #", *p) != nullptr) {
                flags.push_back(*p++);
            }
            int width = 0;
            if (*p == '*') {
                width = read_int_slot(ap);
                ++p;
            } else {
                while (*p != '\0' && std::isdigit(static_cast<unsigned char>(*p))) {
                    width = width * 10 + (*p - '0');
                    ++p;
                }
            }
            int precision = -1;
            if (*p == '.') {
                ++p;
                if (*p == '*') {
                    precision = read_int_slot(ap);
                    ++p;
                } else {
                    precision = 0;
                    while (*p != '\0' && std::isdigit(static_cast<unsigned char>(*p))) {
                        precision = precision * 10 + (*p - '0');
                        ++p;
                    }
                }
            }
            std::string length;
            for (;;) {
                if (*p == 'h' && p[1] == 'h') {
                    length = "hh";
                    p += 2;
                } else if (*p == 'l' && p[1] == 'l') {
                    length = "ll";
                    p += 2;
                } else if (*p == 'I' && p[1] == '6' && p[2] == '4') {
                    length = "I64";
                    p += 3;
                } else if (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' ||
                           *p == 'L' || *p == 'w') {
                    length.push_back(*p);
                    ++p;
                } else {
                    break;
                }
            }
            if (*p == '\0') {
                break;
            }
            const char conv = *p++;
            const std::string piece = format_atom(flags, width, precision, length, conv, ap, count);
            out += piece;
            count += static_cast<long>(piece.size());
        }
    } catch (...) {
        set_error(ENOMEM);
        return EOF;
    }
    if (!write_all(file->file, out.data(), out.size())) {
        file->flag |= kIoError;
        return EOF;
    }
    return static_cast<int>(count);
}

// ---------------------------------------------------------------------------
// Handlers de término.
// ---------------------------------------------------------------------------

std::vector<GuestFnPtr>& atexit_handlers() {
    static std::vector<GuestFnPtr> handlers;
    return handlers;
}

void run_atexit_handlers() {
    std::vector<GuestFnPtr>& handlers = atexit_handlers();
    for (auto it = handlers.rbegin(); it != handlers.rend(); ++it) {
        if (*it != nullptr) {
            (*it)();
        }
    }
    handlers.clear();
}

}  // namespace

// ---------------------------------------------------------------------------
// Dados exportados por msvcrt.dll (imports-dados __initenv, _commode, _fmode).
// ---------------------------------------------------------------------------

char** g_guest_initenv = nullptr;
int g_guest_commode = 0;
int g_guest_fmode = 0;
char* g_guest_acmdln = nullptr;
GuestFile g_guest_iob[3] = {
    {nullptr, 0, nullptr, 0x0001, 0, -1, 0, nullptr},
    {nullptr, 0, nullptr, 0x0002, 1, -1, 0, nullptr},
    {nullptr, 0, nullptr, 0x0002, 2, -1, 0, nullptr},
};

void msvcrt_set_guest_command_line(std::vector<std::string> arguments) {
    g_guest_arguments = std::move(arguments);
    g_guest_command_line.clear();
    for (const std::string& argument : g_guest_arguments) {
        if (!g_guest_command_line.empty()) {
            g_guest_command_line.push_back(' ');
        }
        g_guest_command_line += argument;
    }
    g_guest_acmdln = g_guest_command_line.empty() ? nullptr : g_guest_command_line.data();
}

const std::vector<std::string>& msvcrt_get_guest_arguments() noexcept {
    return g_guest_arguments;
}

// ---------------------------------------------------------------------------
// Startup e término.
// ---------------------------------------------------------------------------

TL_CRT_MSABI int tl___getmainargs(int* argc, char*** argv, char*** envp, int* glob,
                                  void* startup_info) noexcept {
    (void)glob;
    (void)startup_info;
    if (argv == nullptr || envp == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    const std::size_t argument_count = g_guest_arguments.empty() ? 1 : g_guest_arguments.size();
    char** argv_storage =
        static_cast<char**>(std::malloc(sizeof(char*) * (argument_count + 1)));
    if (argv_storage == nullptr) {
        set_error(ENOMEM);
        return -1;
    }
    if (g_guest_arguments.empty()) {
        argv_storage[0] = const_cast<char*>("");
    } else {
        for (std::size_t index = 0; index < g_guest_arguments.size(); ++index) {
            argv_storage[index] = const_cast<char*>(g_guest_arguments[index].c_str());
        }
    }
    argv_storage[argument_count] = nullptr;
    g_guest_initenv = runtime::guest_environment_block_a();
    if (argc != nullptr) {
        *argc = static_cast<int>(argument_count);
    }
    *argv = argv_storage;
    *envp = g_guest_initenv;
    const std::string argv0 =
        argument_count > 0 && argv_storage[0] != nullptr ? argv_storage[0] : "";
    trace_crt(TraceLevel::Info, "getmainargs",
              {TraceField{"argc", std::to_string(argument_count)},
               TraceField{"argv0", argv0}});
    return 0;
}

TL_CRT_MSABI void tl___initterm(void (**start)(void), void (**end)(void)) noexcept {
    if (start == nullptr || end == nullptr) {
        return;
    }
    std::ptrdiff_t count = end - start;
    trace_crt(TraceLevel::Info, "initterm",
              {TraceField{"count", std::to_string(count)}});
    for (GuestFnPtr* current = reinterpret_cast<GuestFnPtr*>(start);
         current != reinterpret_cast<GuestFnPtr*>(end); ++current) {
        if (*current != nullptr) {
            trace_crt(TraceLevel::Info, "initterm-call",
                      {TraceField{"address", std::to_string(reinterpret_cast<std::uintptr_t>(*current))},
                       TraceField{"index", std::to_string(static_cast<std::uintptr_t>(current - reinterpret_cast<GuestFnPtr*>(start)))}});
            (*current)();
        }
    }
}

TL_CRT_MSABI void tl___set_app_type(int app_type) noexcept {
    (void)app_type;
}

TL_CRT_MSABI void tl___setusermatherr(void (*handler)(void)) noexcept {
    (void)handler;
}

TL_CRT_MSABI void tl__amsg_exit(int error_code) noexcept {
    trace_crt(TraceLevel::Error, "amsg-exit",
              {TraceField{"code", std::to_string(error_code)}});
    tl_ExitProcess(static_cast<std::uint32_t>(error_code));
}

TL_CRT_MSABI void tl__cexit() noexcept {
    run_atexit_handlers();
}

TL_CRT_MSABI void tl_abort() noexcept {
    trace_crt(TraceLevel::Error, "abort", {});
    tl_ExitProcess(3);
}

TL_CRT_MSABI int tl_atexit(void (*handler)(void)) noexcept {
    atexit_handlers().push_back(reinterpret_cast<GuestFnPtr>(handler));
    return 0;
}

TL_CRT_MSABI int tl_atoi(const char* const str) noexcept {
    if (str == nullptr) {
        return 0;
    }
    return std::atoi(str);
}

TL_CRT_MSABI void tl_exit(int exit_code) noexcept {
    trace_crt(TraceLevel::Info, "exit",
              {TraceField{"code", std::to_string(exit_code)}});
    run_atexit_handlers();
    tl_ExitProcess(static_cast<std::uint32_t>(exit_code));
}

TL_CRT_MSABI std::int32_t tl___C_specific_handler(
    runtime::ExceptionRecordAmd64* const exception_record, void* const establisher_frame,
    runtime::ContextAmd64* const context_record,
    runtime::DispatcherContextAmd64* const dispatcher_context) noexcept {
    const std::int32_t result = runtime::c_specific_handler(
        exception_record, establisher_frame, context_record, dispatcher_context);
    trace_crt(TraceLevel::Info, "seh-handler",
              {TraceField{"disposition", std::to_string(result)}});
    return result;
}

// ---------------------------------------------------------------------------
// Erros e ambiente.
// ---------------------------------------------------------------------------

TL_CRT_MSABI int* tl__errno() noexcept {
    return &g_crt_errno;
}

TL_CRT_MSABI void tl__lock(GuestFile* file) noexcept {
    (void)file;
}

TL_CRT_MSABI void tl__unlock(GuestFile* file) noexcept {
    (void)file;
}

TL_CRT_MSABI char* tl_getenv(const char* name) noexcept {
    if (name == nullptr) {
        set_error(EINVAL);
        return nullptr;
    }
    return const_cast<char*>(runtime::guest_environment_cstring(name));
}

TL_CRT_MSABI unsigned int tl___lc_codepage_func() noexcept {
    return 1252;
}

TL_CRT_MSABI int tl___mb_cur_max_func() noexcept {
    return 1;
}

// ---------------------------------------------------------------------------
// Streams padrão e arquivos.
// ---------------------------------------------------------------------------

TL_CRT_MSABI GuestFile* tl___iob_func() noexcept {
    ensure_standard_files();
    return g_file_pool.data();
}

TL_CRT_MSABI int tl__fileno(const GuestFile* file) noexcept {
    return file != nullptr ? file->file : -1;
}

TL_CRT_MSABI int tl__isatty(int file_descriptor) noexcept {
    return ::isatty(file_descriptor);
}

TL_CRT_MSABI int tl__setmode(int file_descriptor, int mode) noexcept {
    ensure_standard_files();
    if (file_descriptor < 0 || file_descriptor >= kFdModeCount) {
        set_error(EBADF);
        return -1;
    }
    const int previous = g_fd_modes[static_cast<std::size_t>(file_descriptor)];
    g_fd_modes[static_cast<std::size_t>(file_descriptor)] = mode & (kO_TEXT | kO_BINARY);
    for (GuestFile& file : g_file_pool) {
        if (file.file == file_descriptor) {
            if (mode & kO_BINARY) {
                file.flag |= kIoBinary;
            } else {
                file.flag &= ~kIoBinary;
            }
        }
    }
    return previous;
}

int translate_open_flags(int win_flags) {
    int host_flags = 0;
    switch (win_flags & 0x3) {
        case 0:
            host_flags = O_RDONLY;
            break;
        case 1:
            host_flags = O_WRONLY;
            break;
        case 2:
            host_flags = O_RDWR;
            break;
        default:
            break;
    }
    if (win_flags & 0x100) {
        host_flags |= O_CREAT;
    }
    if (win_flags & 0x200) {
        host_flags |= O_TRUNC;
    }
    if (win_flags & 0x400) {
        host_flags |= O_APPEND;
    }
    if (win_flags & 0x800) {
        host_flags |= O_EXCL;
    }
    return host_flags;
}

TL_CRT_MSABI int tl__open(const char* path, int oflag, ...) noexcept {
    if (path == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    char normalized_path[4096]{};
    if (!translate_guest_path(path, normalized_path, sizeof(normalized_path))) {
        set_error(EINVAL);
        return -1;
    }
    int mode = 0666;
    if (oflag & 0x100) {
        __builtin_ms_va_list ap;
        __builtin_ms_va_start(ap, oflag);
        mode = read_int_slot(ap);
        __builtin_ms_va_end(ap);
    }
    const int fd = ::open(normalized_path, translate_open_flags(oflag), mode);
    if (fd < 0) {
        set_error(errno);
        return -1;
    }
    if (fd < kFdModeCount) {
        g_fd_modes[static_cast<std::size_t>(fd)] = (oflag & kO_BINARY) ? kO_BINARY : kO_TEXT;
    }
    return fd;
}

TL_CRT_MSABI GuestFile* tl__fdopen(int file_descriptor, const char* mode) noexcept {
    ensure_standard_files();
    if (mode == nullptr) {
        set_error(EINVAL);
        return nullptr;
    }
    const char* m = mode;
    int flag = 0;
    for (; *m != '\0'; ++m) {
        if (*m == 'r') {
            flag = kIoRead;
        } else if (*m == 'w' || *m == 'a') {
            flag = kIoWrite;
        } else if (*m == '+') {
            flag = kIoReadWrite;
        } else if (*m == 'b') {
            flag |= kIoBinary;
        } else if (*m != 't') {
            set_error(EINVAL);
            return nullptr;
        }
    }
    GuestFile* file = alloc_slot(file_descriptor, flag);
    if (file == nullptr) {
        set_error(ENOMEM);
    }
    return file;
}

TL_CRT_MSABI GuestFile* tl_fopen(const char* path, const char* mode) noexcept {
    ensure_standard_files();
    if (path == nullptr || mode == nullptr) {
        set_error(EINVAL);
        return nullptr;
    }
    trace_crt(TraceLevel::Info, "fopen", {TraceField{"path", path}, TraceField{"mode", mode}});
    char kind = '\0';
    bool plus = false;
    bool binary = false;
    for (const char* m = mode; *m != '\0'; ++m) {
        switch (*m) {
            case 'r':
                kind = 'r';
                break;
            case 'w':
                kind = 'w';
                break;
            case 'a':
                kind = 'a';
                break;
            case '+':
                plus = true;
                break;
            case 'b':
                binary = true;
                break;
            case 't':
                binary = false;
                break;
            default:
                set_error(EINVAL);
                return nullptr;
        }
    }
    if (kind == '\0') {
        set_error(EINVAL);
        return nullptr;
    }
    char normalized_path[4096]{};
    if (!translate_guest_path(path, normalized_path, sizeof(normalized_path))) {
        set_error(EINVAL);
        return nullptr;
    }
    int host_flags = 0;
    switch (kind) {
        case 'r':
            host_flags = O_RDONLY;
            break;
        case 'w':
            host_flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case 'a':
            host_flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
        default:
            break;
    }
    if (plus) {
        host_flags = (host_flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR |
                     (host_flags & (O_CREAT | O_TRUNC | O_APPEND));
    }
    const int fd = ::open(normalized_path, host_flags, 0666);
    if (fd < 0) {
        set_error(errno);
        return nullptr;
    }
    int flag = (kind == 'r') ? kIoRead : kIoWrite;
    if (plus) {
        flag = kIoReadWrite;
    }
    if (binary) {
        flag |= kIoBinary;
    }
    GuestFile* file = alloc_slot(fd, flag);
    if (file == nullptr) {
        ::close(fd);
        set_error(ENOMEM);
        return nullptr;
    }
    if (fd < kFdModeCount) {
        g_fd_modes[static_cast<std::size_t>(fd)] = binary ? kO_BINARY : kO_TEXT;
    }
    return file;
}

TL_CRT_MSABI int tl_fclose(GuestFile* file) noexcept {
    ensure_standard_files();
    if (file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    int result = 0;
    if (file->file >= 3 && ::close(file->file) != 0) {
        set_error(errno);
        result = EOF;
    }
    const int index = static_cast<int>(file - g_file_pool.data());
    if (index >= 0 && index < kFileSlotCount) {
        g_slot_used[static_cast<std::size_t>(index)] = false;
    }
    *file = GuestFile{};
    return result;
}

TL_CRT_MSABI int tl_getc(GuestFile* file) noexcept {
    if (file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    unsigned char byte = 0;
    for (;;) {
        const ssize_t result = ::read(file->file, &byte, 1);
        if (result == 1) {
            return byte;
        }
        if (result == 0) {
            return EOF;
        }
        if (errno != EINTR) {
            file->flag |= kIoError;
            set_error(errno);
            return EOF;
        }
    }
}

TL_CRT_MSABI int tl_putc(int character, GuestFile* file) noexcept {
    if (file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    const unsigned char byte = static_cast<unsigned char>(character);
    if (write_all(file->file, reinterpret_cast<const char*>(&byte), 1)) {
        return static_cast<int>(byte);
    }
    file->flag |= kIoError;
    return EOF;
}

TL_CRT_MSABI int tl_fputc(int character, GuestFile* file) noexcept {
    return tl_putc(character, file);
}

TL_CRT_MSABI int tl_fgetc(GuestFile* file) noexcept {
    if (file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    if (file->charbuf != -1) {
        const int character = file->charbuf;
        file->charbuf = -1;
        return character;
    }
    unsigned char byte = 0;
    for (;;) {
        const ssize_t result = ::read(file->file, &byte, 1);
        if (result == 1) {
            return byte;
        }
        if (result == 0) {
            file->flag |= kIoEof;
            trace_crt(TraceLevel::Info, "fgetc_eof", {TraceField{"fd", std::to_string(file->file)}});
            return EOF;
        }
        if (errno != EINTR) {
            file->flag |= kIoError;
            set_error(errno);
            return EOF;
        }
    }
}

TL_CRT_MSABI int tl_feof(const GuestFile* file) noexcept {
    if (file == nullptr) {
        return 0;
    }
    return (file->flag & kIoEof) != 0 ? 1 : 0;
}

TL_CRT_MSABI int tl_ungetc(int character, GuestFile* file) noexcept {
    if (file == nullptr || character == EOF) {
        return EOF;
    }
    file->charbuf = static_cast<int>(static_cast<unsigned char>(character));
    file->flag &= ~kIoError;
    return character;
}

TL_CRT_MSABI std::size_t tl_fread(void* buffer, std::size_t size, std::size_t count,
                                   GuestFile* file) noexcept {
    if (buffer == nullptr || file == nullptr) {
        set_error(EINVAL);
        return 0;
    }
    if (size == 0 || count == 0) {
        return 0;
    }
    const std::size_t total = size * count;
    std::size_t total_read = 0;
    auto* dest = static_cast<char*>(buffer);
    if (file->charbuf != -1 && total > 0) {
        dest[0] = static_cast<char>(static_cast<unsigned char>(file->charbuf));
        file->charbuf = -1;
        total_read = 1;
    }
    while (total_read < total) {
        const ssize_t result = ::read(file->file, dest + total_read, total - total_read);
        if (result > 0) {
            total_read += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0) {
            file->flag |= kIoEof;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        file->flag |= kIoError;
        set_error(errno);
        break;
    }
    trace_crt(TraceLevel::Info, "fread", {TraceField{"fd", std::to_string(file->file)},
                                          TraceField{"total", std::to_string(total)},
                                          TraceField{"read", std::to_string(total_read)}});
    return total_read / size;
}

TL_CRT_MSABI int tl_fputs(const char* text, GuestFile* file) noexcept {
    if (text == nullptr || file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    if (write_all(file->file, text, std::strlen(text))) {
        return 0;
    }
    file->flag |= kIoError;
    return EOF;
}

TL_CRT_MSABI std::size_t tl_fwrite(const void* buffer, std::size_t size, std::size_t count,
                                   GuestFile* file) noexcept {
    if (buffer == nullptr || file == nullptr) {
        set_error(EINVAL);
        return 0;
    }
    if (size == 0) {
        return count;
    }
    if (count > std::numeric_limits<std::size_t>::max() / size) {
        set_error(EINVAL);
        return 0;
    }
    const std::size_t total = size * count;
    if (write_all(file->file, static_cast<const char*>(buffer), total)) {
        return count;
    }
    file->flag |= kIoError;
    return total / size;
}

TL_CRT_MSABI int tl_ferror(const GuestFile* file) noexcept {
    return (file != nullptr && (file->flag & kIoError) != 0) ? 1 : 0;
}

TL_CRT_MSABI int tl_fflush(GuestFile* file) noexcept {
    if (file == nullptr) {
        return EOF;
    }
    return 0;
}

TL_CRT_MSABI int tl_fseek(GuestFile* file, long offset, int origin) noexcept {
    if (file == nullptr || file->file < 0) {
        set_error(EBADF);
        return -1;
    }
    if (origin < SEEK_SET || origin > SEEK_END) {
        set_error(EINVAL);
        return -1;
    }
    if (::lseek(file->file, static_cast<off_t>(offset), origin) == static_cast<off_t>(-1)) {
        set_error(errno);
        return -1;
    }
    file->charbuf = -1;
    return 0;
}

TL_CRT_MSABI long tl_ftell(GuestFile* file) noexcept {
    if (file == nullptr || file->file < 0) {
        set_error(EBADF);
        return -1L;
    }
    const off_t position = ::lseek(file->file, 0, SEEK_CUR);
    if (position == static_cast<off_t>(-1)) {
        set_error(errno);
        return -1L;
    }
    return static_cast<long>(position);
}

TL_CRT_MSABI void tl_rewind(GuestFile* file) noexcept {
    if (file == nullptr) {
        return;
    }
    if (::lseek(file->file, 0, SEEK_SET) != static_cast<off_t>(-1)) {
        file->flag &= ~kIoError;
        file->charbuf = -1;
    } else {
        set_error(errno);
    }
}

TL_CRT_MSABI void tl_perror(const char* message) noexcept {
    const char* error = std::strerror(g_crt_errno);
    std::string out;
    if (message != nullptr && *message != '\0') {
        out += message;
        out += ": ";
    }
    out += (error != nullptr) ? error : "Unknown error";
    out += '\n';
    write_all(2, out.data(), out.size());
}

// ---------------------------------------------------------------------------
// printf mínimo.
// ---------------------------------------------------------------------------

TL_CRT_MSABI int tl_vfprintf(GuestFile* file, const char* format, GuestVaList arguments) noexcept {
    ensure_standard_files();
    if (file == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    GuestVaList ap = arguments;
    return vformat_into(file, format, ap);
}

TL_CRT_MSABI int tl_fprintf(GuestFile* file, const char* format, ...) noexcept {
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, format);
    const int result = tl_vfprintf(file, format, ap);
    __builtin_ms_va_end(ap);
    return result;
}

// ---------------------------------------------------------------------------
// Memória, strings e conversões.
// ---------------------------------------------------------------------------

TL_CRT_MSABI void* tl_malloc(std::size_t size) noexcept {
    void* result = std::malloc(size);
    std::fprintf(stderr, "[tl][dbg] malloc size=%zu -> %p\n", size, result);
    return result;
}

TL_CRT_MSABI void* tl_calloc(std::size_t count, std::size_t size) noexcept {
    return std::calloc(count, size);
}

TL_CRT_MSABI void tl_free(void* pointer) noexcept {
    std::free(pointer);
}

TL_CRT_MSABI void* tl_memcpy(void* destination, const void* source, std::size_t count) noexcept {
    return std::memcpy(destination, source, count);
}

TL_CRT_MSABI void* tl_memset(void* destination, int value, std::size_t count) noexcept {
    return std::memset(destination, value, count);
}

TL_CRT_MSABI std::size_t tl_strlen(const char* text) noexcept {
    return text != nullptr ? std::strlen(text) : 0;
}

TL_CRT_MSABI int tl_strcmp(const char* left, const char* right) noexcept {
    return std::strcmp(left != nullptr ? left : "", right != nullptr ? right : "");
}

TL_CRT_MSABI int tl_strncmp(const char* left, const char* right, std::size_t count) noexcept {
    return std::strncmp(left != nullptr ? left : "", right != nullptr ? right : "", count);
}

TL_CRT_MSABI char* tl_strcpy(char* destination, const char* source) noexcept {
    return std::strcpy(destination, source != nullptr ? source : "");
}

TL_CRT_MSABI char* tl_strerror(int error_number) noexcept {
    thread_local std::array<char, 256> buffer{};
    const char* message = std::strerror(error_number);
    std::snprintf(buffer.data(), buffer.size(), "%s", message != nullptr ? message : "Unknown error");
    return buffer.data();
}

TL_CRT_MSABI std::size_t tl_wcslen(const std::uint16_t* text) noexcept {
    if (text == nullptr) {
        return 0;
    }
    std::size_t length = 0;
    while (text[length] != 0) {
        ++length;
    }
    return length;
}

TL_CRT_MSABI int tl__ismbblead(const unsigned int character) noexcept {
    (void)character;
    return 0;
}

TL_CRT_MSABI int tl_isalnum(int character) noexcept {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ? 1 : 0;
}

TL_CRT_MSABI int tl_toupper(int character) noexcept {
    return std::toupper(static_cast<unsigned char>(character));
}

TL_CRT_MSABI long tl_strtol(const char* text, char** end_pointer, int base) noexcept {
    if (text == nullptr) {
        set_error(EINVAL);
        return 0;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, base);
    set_error(errno);
    if (end_pointer != nullptr) {
        *end_pointer = end;
    }
    return value;
}

TL_CRT_MSABI unsigned long tl_strtoul(const char* text, char** end_pointer, int base) noexcept {
    if (text == nullptr) {
        set_error(EINVAL);
        return 0;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, base);
    set_error(errno);
    if (end_pointer != nullptr) {
        *end_pointer = end;
    }
    return value;
}

TL_CRT_MSABI char* tl_strncpy(char* destination, const char* source, std::size_t count) noexcept {
    if (destination == nullptr || count == 0) {
        return destination;
    }
    if (source == nullptr) {
        std::memset(destination, 0, count);
        return destination;
    }
    const std::size_t len = std::strlen(source);
    if (len < count) {
        std::memcpy(destination, source, len);
        std::memset(destination + len, 0, count - len);
    } else {
        std::memcpy(destination, source, count);
    }
    return destination;
}

TL_CRT_MSABI char* tl_strstr(const char* haystack, const char* needle) noexcept {
    if (haystack == nullptr || needle == nullptr) {
        return nullptr;
    }
    return const_cast<char*>(std::strstr(haystack, needle));
}

TL_CRT_MSABI char* tl_strcat(char* destination, const char* source) noexcept {
    if (destination == nullptr) {
        return destination;
    }
    if (source == nullptr) {
        return destination;
    }
    return std::strcat(destination, source);
}

TL_CRT_MSABI char* tl__strlwr(char* text) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    for (char* current = text; *current != '\0'; ++current) {
        *current = static_cast<char>(std::tolower(static_cast<unsigned char>(*current)));
    }
    return text;
}

TL_CRT_MSABI int tl_isspace(int character) noexcept {
    return std::isspace(static_cast<unsigned char>(character)) != 0 ? 1 : 0;
}

TL_CRT_MSABI void* tl_memmove(void* destination, const void* source, std::size_t count) noexcept {
    return std::memmove(destination, source, count);
}

TL_CRT_MSABI int tl_remove(const char* path) noexcept {
    if (path == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    if (std::remove(path) != 0) {
        set_error(errno);
        return -1;
    }
    return 0;
}

// Layout do struct _stat64 do MinGW (pack 8): os bits de tipo e de
// permissão coincidem com os do Linux (S_IFREG = 0x8000, S_IFDIR = 0x4000).
struct MingwStat64 {
    std::uint32_t st_dev;   // offset 0
    std::uint16_t st_ino;   // offset 4
    std::uint16_t st_mode;  // offset 6
    std::int16_t st_nlink;  // offset 8
    std::int16_t st_uid;    // offset 10
    std::int16_t st_gid;    // offset 12
    std::uint32_t st_rdev;  // offset 14
    std::uint64_t st_size;  // offset 24 (alinhado a 8)
    std::int64_t atime_sec; // offset 32
    std::int64_t mtime_sec; // offset 40
    std::int64_t ctime_sec; // offset 48
};

TL_CRT_MSABI int tl__stat64(const char* path, void* stat_buffer) noexcept {
    if (path == nullptr || stat_buffer == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    struct ::stat st;
    if (::stat(path, &st) != 0) {
        set_error(errno);
        trace_crt(TraceLevel::Info, "_stat64",
                  {{"path", path}, {"result", "-1"}, {"errno", std::to_string(errno)}});
        return -1;
    }
    MingwStat64* out = static_cast<MingwStat64*>(stat_buffer);
    *out = {};
    out->st_dev = static_cast<std::uint32_t>(st.st_dev);
    out->st_ino = static_cast<std::uint16_t>(st.st_ino);
    out->st_mode = static_cast<std::uint16_t>(st.st_mode);
    out->st_nlink = static_cast<std::int16_t>(st.st_nlink);
    out->st_uid = static_cast<std::int16_t>(st.st_uid);
    out->st_gid = static_cast<std::int16_t>(st.st_gid);
    out->st_rdev = static_cast<std::uint32_t>(st.st_rdev);
    out->st_size = static_cast<std::uint64_t>(st.st_size);
    out->atime_sec = static_cast<std::int64_t>(st.st_atime);
    out->mtime_sec = static_cast<std::int64_t>(st.st_mtime);
    out->ctime_sec = static_cast<std::int64_t>(st.st_ctime);
    trace_crt(TraceLevel::Info, "_stat64",
              {{"path", path}, {"result", "0"}, {"mode", std::to_string(out->st_mode)}});
    return 0;
}

// ---------------------------------------------------------------------------
// Locale (C locale, layout Mingw).
// ---------------------------------------------------------------------------

struct MingwLconv {
    char* decimal_point;
    char* thousands_sep;
    char* grouping;
    char* int_curr_symbol;
    char* currency_symbol;
    char* mon_decimal_point;
    char* mon_thousands_sep;
    char* mon_grouping;
    char* positive_sign;
    char* negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    wchar_t* int_curr_symbol_w;
    wchar_t* currency_symbol_w;
    wchar_t* mon_decimal_point_w;
    wchar_t* mon_thousands_sep_w;
    wchar_t* mon_grouping_w;
    wchar_t* positive_sign_w;
    wchar_t* negative_sign_w;
    wchar_t* decimal_point_w;
};

TL_CRT_MSABI void* tl_localeconv() noexcept {
    static char kEmpty[] = "";
    static char kDecimalPoint[] = ".";
    static MingwLconv kCLocale = {
        kDecimalPoint, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
        CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    return &kCLocale;
}

TL_CRT_MSABI std::int64_t tl__time64(std::int64_t* value) noexcept {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    if (value != nullptr) {
        *value = now;
    }
    return now;
}

TL_CRT_MSABI void* tl__localtime64(const std::int64_t* value) noexcept {
    if (value == nullptr) {
        return nullptr;
    }
    static thread_local std::tm result{};
    const std::time_t seconds = static_cast<std::time_t>(*value);
    return localtime_r(&seconds, &result) != nullptr ? &result : nullptr;
}

TL_CRT_MSABI std::size_t tl_strftime(char* buffer, const std::size_t capacity,
                                     const char* format, const void* time_value) noexcept {
    if (buffer == nullptr || format == nullptr || time_value == nullptr) {
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    const std::size_t result = std::strftime(buffer, capacity, format,
                                              static_cast<const std::tm*>(time_value));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    return result;
}

// ---------------------------------------------------------------------------
// Sinais (sem entrega; apenas registra handlers).
// ---------------------------------------------------------------------------

TL_CRT_MSABI void* tl_signal(int signal_number, void (*handler)(int)) noexcept {
    if (signal_number < 0 || signal_number >= 32) {
        set_error(EINVAL);
        return reinterpret_cast<void*>(SIG_ERR);
    }
    thread_local std::array<GuestSignalFn, 32> table{};
    GuestSignalFn* slot = &table[static_cast<std::size_t>(signal_number)];
    const GuestSignalFn previous = *slot;
    *slot = reinterpret_cast<GuestSignalFn>(handler);
    return reinterpret_cast<void*>(previous);
}

// ---------------------------------------------------------------------------
// Strings e conversões wide (Fase 10+): caminhos, formatação e locale.
// ---------------------------------------------------------------------------

TL_CRT_MSABI void* tl_realloc(void* pointer, std::size_t size) noexcept {
    const auto old_ptr = reinterpret_cast<std::uintptr_t>(pointer);
    void* result = std::realloc(pointer, size);
    std::fprintf(stderr, "[tl][dbg] realloc ptr=0x%zx size=%zu -> %p\n", old_ptr, size, result);
    return result;
}

TL_CRT_MSABI char* tl_setlocale(int category, const char* locale) noexcept {
    (void)category;
    if (locale != nullptr && locale[0] != '\0' && std::strcmp(locale, "C") != 0 &&
        std::strcmp(locale, "POSIX") != 0) {
        return nullptr;
    }
    static char kCLocale[] = "C";
    return kCLocale;
}

TL_CRT_MSABI char* tl_strchr(const char* text, int character) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    return const_cast<char*>(std::strchr(text, character));
}

TL_CRT_MSABI char* tl_strrchr(const char* text, int character) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    return const_cast<char*>(std::strrchr(text, character));
}

TL_CRT_MSABI int tl__stricmp(const char* left, const char* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return left == right ? 0 : (left == nullptr ? -1 : 1);
    }
    while (*left != '\0' && *right != '\0') {
        const int l = std::tolower(static_cast<unsigned char>(*left));
        const int r = std::tolower(static_cast<unsigned char>(*right));
        if (l != r) {
            return l - r;
        }
        ++left;
        ++right;
    }
    return std::tolower(static_cast<unsigned char>(*left)) -
           std::tolower(static_cast<unsigned char>(*right));
}

TL_CRT_MSABI char* tl__strdup(const char* text) noexcept {
    std::fprintf(stderr, "[tl][dbg] strdup src=\"%s\"\n", text != nullptr ? text : "(null)");
    if (text == nullptr) {
        return nullptr;
    }
    const std::size_t length = std::strlen(text) + 1;
    char* copy = static_cast<char*>(std::malloc(length));
    if (copy != nullptr) {
        std::memcpy(copy, text, length);
        std::fprintf(stderr, "[tl][dbg] strdup copy=%p len=%zu\n", static_cast<void*>(copy),
                     length);
    }
    return copy;
}

TL_CRT_MSABI int tl__umask(int mask) noexcept {
    return static_cast<int>(::umask(static_cast<mode_t>(mask)));
}

TL_CRT_MSABI int tl__chmod(const char* path, int mode) noexcept {
    if (path == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    if (::chmod(path, static_cast<mode_t>(mode)) != 0) {
        set_error(errno);
        return -1;
    }
    return 0;
}

TL_CRT_MSABI int tl__utime64(const char* path, const void* times) noexcept {
    if (path == nullptr || times == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    // struct _utimbuf64 do mingw: { __time64_t actime; __time64_t modtime; }
    // com pack 8, equivalente ao utimbuf do Linux em x86-64.
    struct GuestUtimbuf64 {
        std::int64_t actime;
        std::int64_t modtime;
    };
    const auto* time_buffer = static_cast<const GuestUtimbuf64*>(times);
    const struct ::timespec host_times[2] = {
        {.tv_sec = time_buffer->actime, .tv_nsec = 0},
        {.tv_sec = time_buffer->modtime, .tv_nsec = 0},
    };
    if (::utimensat(AT_FDCWD, path, host_times, 0) != 0) {
        set_error(errno);
        return -1;
    }
    return 0;
}

TL_CRT_MSABI GuestFile* tl__wfopen(const std::uint16_t* path, const std::uint16_t* mode) noexcept {
    if (path == nullptr || mode == nullptr) {
        set_error(EINVAL);
        return nullptr;
    }
    const std::string path_utf8 = utf16_to_utf8(path, std::numeric_limits<std::size_t>::max());
    const std::string mode_utf8 = utf16_to_utf8(mode, std::numeric_limits<std::size_t>::max());
    if (path_utf8.empty() || mode_utf8.empty()) {
        set_error(EINVAL);
        return nullptr;
    }
    return tl_fopen(path_utf8.c_str(), mode_utf8.c_str());
}

TL_CRT_MSABI int tl__wstat64(const std::uint16_t* path, void* stat_buffer) noexcept {
    if (path == nullptr || stat_buffer == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    const std::string path_utf8 = utf16_to_utf8(path, std::numeric_limits<std::size_t>::max());
    if (path_utf8.empty()) {
        set_error(EINVAL);
        return -1;
    }
    return tl__stat64(path_utf8.c_str(), stat_buffer);
}

TL_CRT_MSABI int tl__wrename(const std::uint16_t* old_path, const std::uint16_t* new_path) noexcept {
    if (old_path == nullptr || new_path == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    const std::string old_utf8 = utf16_to_utf8(old_path, std::numeric_limits<std::size_t>::max());
    const std::string new_utf8 = utf16_to_utf8(new_path, std::numeric_limits<std::size_t>::max());
    if (old_utf8.empty() || new_utf8.empty()) {
        set_error(EINVAL);
        return -1;
    }
    if (::rename(old_utf8.c_str(), new_utf8.c_str()) != 0) {
        set_error(errno);
        return -1;
    }
    return 0;
}

TL_CRT_MSABI int tl__wunlink(const std::uint16_t* path) noexcept {
    if (path == nullptr) {
        set_error(EINVAL);
        return -1;
    }
    const std::string path_utf8 = utf16_to_utf8(path, std::numeric_limits<std::size_t>::max());
    if (path_utf8.empty()) {
        set_error(EINVAL);
        return -1;
    }
    if (::unlink(path_utf8.c_str()) != 0) {
        set_error(errno);
        return -1;
    }
    return 0;
}

TL_CRT_MSABI std::uint16_t* tl__wcsdup(const std::uint16_t* text) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    const std::size_t length = tl_wcslen(text) + 1;
    auto* copy = static_cast<std::uint16_t*>(std::malloc(length * sizeof(std::uint16_t)));
    if (copy != nullptr) {
        std::memcpy(copy, text, length * sizeof(std::uint16_t));
    }
    return copy;
}

TL_CRT_MSABI std::uint16_t* tl_wcschr(const std::uint16_t* text, std::uint16_t character) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    while (*text != 0) {
        if (*text == character) {
            return const_cast<std::uint16_t*>(text);
        }
        ++text;
    }
    return character == 0 ? const_cast<std::uint16_t*>(text) : nullptr;
}

TL_CRT_MSABI std::uint16_t* tl_wcsrchr(const std::uint16_t* text, std::uint16_t character) noexcept {
    if (text == nullptr) {
        return nullptr;
    }
    const std::uint16_t* last = nullptr;
    while (*text != 0) {
        if (*text == character) {
            last = text;
        }
        ++text;
    }
    if (character == 0) {
        return const_cast<std::uint16_t*>(text);
    }
    return const_cast<std::uint16_t*>(last);
}

TL_CRT_MSABI std::uint16_t* tl_wcsncat(std::uint16_t* destination, const std::uint16_t* source,
                                       std::size_t count) noexcept {
    if (destination == nullptr || source == nullptr) {
        return destination;
    }
    std::uint16_t* end = destination + tl_wcslen(destination);
    std::size_t remaining = count;
    while (remaining > 0 && *source != 0) {
        *end++ = *source++;
        --remaining;
    }
    *end = 0;
    return destination;
}

TL_CRT_MSABI std::uint16_t* tl_wcsncpy(std::uint16_t* destination, const std::uint16_t* source,
                                       std::size_t count) noexcept {
    if (destination == nullptr || source == nullptr) {
        return destination;
    }
    std::size_t index = 0;
    while (index < count && source[index] != 0) {
        destination[index] = source[index];
        ++index;
    }
    while (index < count) {
        destination[index] = 0;
        ++index;
    }
    return destination;
}

TL_CRT_MSABI std::size_t tl_mbstowcs(std::uint16_t* destination, const char* source,
                                     std::size_t count) noexcept {
    std::fprintf(stderr, "[tl][dbg] mbstowcs src=\"%s\" count=%zu dst=%p\n",
                 source != nullptr ? source : "(null)", count,
                 static_cast<void*>(destination));
    if (source == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const std::size_t needed = utf8_to_utf16(source, nullptr, 0);
    if (needed == static_cast<std::size_t>(-1)) {
        set_error(EILSEQ);
        return static_cast<std::size_t>(-1);
    }
    if (destination == nullptr) {
        return needed;
    }
    if (count < needed) {
        set_error(E2BIG);
        return static_cast<std::size_t>(-1);
    }
    const std::size_t written = utf8_to_utf16(source, destination, count);
    std::fprintf(stderr, "[tl][dbg] mbstowcs result: %zu units:", written);
    for (std::size_t i = 0; i < written && i < 20; ++i) {
        std::fprintf(stderr, " %04X", destination[i]);
    }
    std::fprintf(stderr, "\n");
    return needed;
}

TL_CRT_MSABI std::size_t tl_wcstombs(char* destination, const std::uint16_t* source,
                                     std::size_t count) noexcept {
    std::fprintf(stderr, "[tl][dbg] wcstombs src16=");
    if (source != nullptr) {
        for (int i = 0; i < 8; ++i) {
            std::fprintf(stderr, " %04X", source[i]);
        }
    } else {
        std::fprintf(stderr, " (null)");
    }
    std::fprintf(stderr, " count=%zu\n", count);
    if (source == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const std::string converted = utf16_to_utf8(source, std::numeric_limits<std::size_t>::max());
    std::fprintf(stderr, "[tl][dbg] wcstombs result=\"%s\" len=%zu dst=%p\n", converted.c_str(),
                 converted.size(), static_cast<void*>(destination));
    if (destination == nullptr) {
        return converted.size();
    }
    if (converted.size() > count) {
        set_error(E2BIG);
        return static_cast<std::size_t>(-1);
    }
    std::memcpy(destination, converted.data(), converted.size());
    return converted.size();
}

// Formata e escreve um caractere wide em um arquivo (fputwc).
TL_CRT_MSABI int tl_fputwc(std::uint16_t character, GuestFile* file) noexcept {
    if (file == nullptr) {
        set_error(EINVAL);
        return 0xFFFF;
    }
    const std::uint16_t units[2] = {character, 0};
    const std::string utf8 = utf16_to_utf8(units, 1);
    if (utf8.empty()) {
        set_error(EILSEQ);
        return 0xFFFF;
    }
    if (write_all(file->file, utf8.data(), utf8.size())) {
        return static_cast<int>(character);
    }
    file->flag |= kIoError;
    return 0xFFFF;
}

// Converte o formato wide de fwprintf para o formato narrow do motor existente:
// no printf wide do msvcrt, %s/%ls são strings wide e %S/%hs são strings
// estreitas; %c/%lc são wide e %C/%hc são estreitas.
std::string wformat_to_narrow(const std::uint16_t* format) {
    std::string out;
    while (*format != 0) {
        if (*format != '%') {
            const std::uint16_t literal[2] = {*format, 0};
            out += utf16_to_utf8(literal, 1);
            ++format;
            continue;
        }
        ++format;
        out.push_back('%');
        if (*format == '%') {
            out.push_back('%');
            ++format;
            continue;
        }
        while (*format != 0 && std::strchr("-+0 #", static_cast<int>(static_cast<char>(*format))) != nullptr) {
            out.push_back(static_cast<char>(*format));
            ++format;
        }
        if (*format == '*') {
            out.push_back('*');
            ++format;
        } else {
            while (*format != 0 && *format >= '0' && *format <= '9') {
                out.push_back(static_cast<char>(*format));
                ++format;
            }
        }
        if (*format == '.') {
            out.push_back('.');
            ++format;
            if (*format == '*') {
                out.push_back('*');
                ++format;
            } else {
                while (*format != 0 && *format >= '0' && *format <= '9') {
                    out.push_back(static_cast<char>(*format));
                    ++format;
                }
            }
        }
        std::string length;
        while (*format != 0 && (*format == 'h' || *format == 'l' || *format == 'w' ||
                                *format == 'I' || *format == 'j' || *format == 'z' ||
                                *format == 't' || *format == 'L')) {
            if (*format == 'I' && format[1] == '6' && format[2] == '4') {
                length += "I64";
                format += 3;
            } else {
                length.push_back(static_cast<char>(*format));
                ++format;
            }
        }
        const char conv = static_cast<char>(*format);
        if (conv == '\0') {
            break;
        }
        ++format;
        if (conv == 's') {
            if (length == "h") {
                out += "s";
            } else {
                out += "ls";
            }
        } else if (conv == 'S') {
            out += "s";
        } else if (conv == 'c') {
            if (length == "h") {
                out += "c";
            } else {
                out += "lc";
            }
        } else if (conv == 'C') {
            out += "c";
        } else {
            out += length;
            out.push_back(conv);
        }
    }
    return out;
}

TL_CRT_MSABI int tl_fwprintf(GuestFile* file, const std::uint16_t* format, ...) noexcept {
    ensure_standard_files();
    if (file == nullptr || format == nullptr) {
        set_error(EINVAL);
        return EOF;
    }
    const std::string narrow = wformat_to_narrow(format);
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, format);
    const int result = tl_vfprintf(file, narrow.c_str(), ap);
    __builtin_ms_va_end(ap);
    return result;
}

TL_CRT_MSABI void tl__c_exit() noexcept {
}

TL_CRT_MSABI void tl__exit(int exit_code) noexcept {
    tl_exit(exit_code);
}

TL_CRT_MSABI int tl__XcptFilter(unsigned long xcpt, void* pinfo) noexcept {
    (void)xcpt;
    (void)pinfo;
    return 1; // EXCEPTION_EXECUTE_HANDLER
}

TL_CRT_MSABI void* tl___dllonexit(void (*func)(void), void** pbegin, void** pend) noexcept {
    (void)func;
    (void)pbegin;
    (void)pend;
    return reinterpret_cast<void*>(1);
}

struct BeginThreadData {
    unsigned (*start_address)(void*);
    void* arg_list;
};

static void* crt_thread_trampoline(void* arg) {
    auto* data = static_cast<BeginThreadData*>(arg);
    unsigned (*func)(void*) = data->start_address;
    void* param = data->arg_list;
    delete data;
    unsigned res = func(param);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(res));
}

TL_CRT_MSABI std::uintptr_t tl__beginthreadex(void* security, unsigned stack_size,
                                              unsigned (*start_address)(void*), void* arg_list,
                                              unsigned init_flag, unsigned* thread_id) noexcept {
    (void)security;
    (void)stack_size;
    (void)init_flag;
    if (start_address == nullptr) {
        set_error(EINVAL);
        return 0;
    }
    auto* data = new (std::nothrow) BeginThreadData{start_address, arg_list};
    if (data == nullptr) {
        set_error(ENOMEM);
        return 0;
    }
    pthread_t thread{};
    if (::pthread_create(&thread, nullptr, crt_thread_trampoline, data) != 0) {
        delete data;
        set_error(EAGAIN);
        return 0;
    }
    if (thread_id != nullptr) {
        *thread_id = static_cast<unsigned>(thread);
    }
    return static_cast<std::uintptr_t>(thread);
}

TL_CRT_MSABI int tl_memcmp(const void* ptr1, const void* ptr2, std::size_t num) noexcept {
    if (num == 0) return 0;
    if (ptr1 == nullptr || ptr2 == nullptr) return 0;
    return std::memcmp(ptr1, ptr2, num);
}

TL_CRT_MSABI int tl_wcscmp(const std::uint16_t* string1, const std::uint16_t* string2) noexcept {
    if (string1 == nullptr && string2 == nullptr) return 0;
    if (string1 == nullptr) return -1;
    if (string2 == nullptr) return 1;
    while (*string1 != 0 && *string1 == *string2) {
        ++string1;
        ++string2;
    }
    return static_cast<int>(*string1) - static_cast<int>(*string2);
}

TL_CRT_MSABI std::uint16_t* tl_wcsstr(const std::uint16_t* string, const std::uint16_t* str_char_set) noexcept {
    if (string == nullptr || str_char_set == nullptr) return nullptr;
    if (*str_char_set == 0) return const_cast<std::uint16_t*>(string);
    for (const std::uint16_t* s = string; *s != 0; ++s) {
        const std::uint16_t* s_sub = s;
        const std::uint16_t* set_sub = str_char_set;
        while (*s_sub != 0 && *set_sub != 0 && *s_sub == *set_sub) {
            ++s_sub;
            ++set_sub;
        }
        if (*set_sub == 0) {
            return const_cast<std::uint16_t*>(s);
        }
    }
    return nullptr;
}

TL_CRT_MSABI int tl___CxxFrameHandler(void* rec, void* frame, void* context, void* disp) noexcept {
    (void)rec;
    (void)frame;
    (void)context;
    (void)disp;
    return 1;
}

TL_CRT_MSABI void tl__CxxThrowException(void* pexcept, void* pthrow_info) noexcept {
    const std::uint64_t args[4] = {
        0x19930520ULL,
        reinterpret_cast<std::uint64_t>(pexcept),
        reinterpret_cast<std::uint64_t>(pthrow_info),
        reinterpret_cast<std::uint64_t>(g_guest_image_base),
    };
    tl_RaiseException(0xE06D7363U, 1U, 4U, args);
}

TL_CRT_MSABI void tl__purecall() noexcept {
    tl_abort();
}

TL_CRT_MSABI void tl_terminate() noexcept {
    tl_abort();
}

static unsigned long g_next_rand = 1;

TL_CRT_MSABI int tl_rand() noexcept {
    g_next_rand = g_next_rand * 214013L + 2531011L;
    return static_cast<int>((g_next_rand >> 16) & 0x7fff);
}

TL_CRT_MSABI void tl_srand(unsigned int seed) noexcept {
    g_next_rand = seed;
}

}  // namespace tradutorlinux
