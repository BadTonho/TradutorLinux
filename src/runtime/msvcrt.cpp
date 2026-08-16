#include "tradutorlinux/runtime/msvcrt.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/winapi.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
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
constexpr int kIoError = 0x0020;
constexpr int kIoBinary = 0x8000;

// Modos de arquivo do msvcrt.
constexpr int kO_BINARY = 0x8000;
constexpr int kO_TEXT = 0x4000;

constexpr int kFileSlotCount = 16;
constexpr int kFdModeCount = 64;

// Ponteiro para função convidada (convenção Microsoft x64).
using GuestFnPtr = void (TL_CRT_MSABI *)();
using GuestSignalFn = void (TL_CRT_MSABI *)(int);

// ---------------------------------------------------------------------------
// Estado por processo convidado. O convidado executa no processo filho; o
// estado é herdado pelo fork e não é compartilhado entre execuções.
// ---------------------------------------------------------------------------

std::vector<std::string> g_guest_arguments;

thread_local int g_crt_errno = 0;

std::array<GuestFile, kFileSlotCount> g_file_pool;
std::array<bool, kFileSlotCount> g_slot_used;
std::array<int, kFdModeCount> g_fd_modes;
bool g_std_ready = false;

char* g_empty_env[1] = {nullptr};

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
    g_file_pool[1].file = 1;
    g_file_pool[1].flag = kIoWrite;
    g_file_pool[2].file = 2;
    g_file_pool[2].flag = kIoWrite;
    g_fd_modes.fill(kO_TEXT);
}

void set_error(int error) {
    g_crt_errno = error;
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
    std::string out;
    if (text == nullptr) {
        return out;
    }
    while (char_limit > 0 && *text != 0) {
        --char_limit;
        const std::uint16_t unit = *text++;
        std::uint32_t code_point = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && text[0] != 0 && text[0] >= 0xDC00 &&
            text[0] <= 0xDFFF) {
            code_point =
                0x10000U + ((static_cast<std::uint32_t>(unit - 0xD800U) << 10) | (text[0] - 0xDC00U));
            ++text;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            continue;
        }
        if (code_point < 0x80) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point < 0x800) {
            out.push_back(static_cast<char>(0xC0U | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point < 0x10000) {
            out.push_back(static_cast<char>(0xE0U | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 12) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }
    return out;
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

void msvcrt_set_guest_command_line(std::vector<std::string> arguments) {
    g_guest_arguments = std::move(arguments);
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
    g_guest_initenv = (environ != nullptr) ? environ : g_empty_env;
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
    for (GuestFnPtr* current = reinterpret_cast<GuestFnPtr*>(start);
         current != reinterpret_cast<GuestFnPtr*>(end); ++current) {
        if (*current != nullptr) {
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

TL_CRT_MSABI void tl_exit(int exit_code) noexcept {
    trace_crt(TraceLevel::Info, "exit",
              {TraceField{"code", std::to_string(exit_code)}});
    run_atexit_handlers();
    tl_ExitProcess(static_cast<std::uint32_t>(exit_code));
}

TL_CRT_MSABI std::int64_t tl___C_specific_handler() noexcept {
    trace_crt(TraceLevel::Error, "seh-stub", {});
    tl_ExitProcess(3);
    return 0;
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
    return ::getenv(name);
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
    int mode = 0666;
    if (oflag & 0x100) {
        __builtin_ms_va_list ap;
        __builtin_ms_va_start(ap, oflag);
        mode = read_int_slot(ap);
        __builtin_ms_va_end(ap);
    }
    const int fd = ::open(path, translate_open_flags(oflag), mode);
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
    const int fd = ::open(path, host_flags, 0666);
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
    return std::malloc(size);
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

}  // namespace tradutorlinux
