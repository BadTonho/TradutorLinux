#include "kernel32_internal.hpp"
namespace tradutorlinux {

namespace {

[[nodiscard]] int standard_handle_index(const std::uint32_t standard_handle) noexcept {
    switch (standard_handle) {
        case abi::kStdInputHandle: return 0;
        case abi::kStdOutputHandle: return 1;
        case abi::kStdErrorHandle: return 2;
        default: return -1;
    }
}

[[nodiscard]] void* current_standard_handle(const std::size_t index) noexcept {
    std::lock_guard lock(g_process_context_mutex);
    return g_standard_handles[index];
}

[[nodiscard]] bool write_all(const int fd, const char* data, const std::size_t size) noexcept {
    std::size_t total = 0;
    while (total < size) {
        const ssize_t written = ::write(fd, data + total, size - total);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        total += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

extern "C" {

TL_MSABI void* tl_GetStdHandle(const std::uint32_t std_handle) noexcept {
    const int index = standard_handle_index(std_handle);
    if (index < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return current_standard_handle(static_cast<std::size_t>(index));
}

TL_MSABI int tl_SetStdHandle(const std::uint32_t std_handle, void* const handle) noexcept {
    const int index = standard_handle_index(std_handle);
    if (index < 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (handle != nullptr && handle_fd(handle) < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    {
        std::lock_guard lock(g_process_context_mutex);
        g_standard_handles[static_cast<std::size_t>(index)] = handle;
    }
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "set-std-handle", std::to_string(index));
    return 1;
}

TL_MSABI int tl_ReadConsoleW(const void* const console_input, std::uint16_t* const buffer,
                             const std::uint32_t chars_to_read,
                             std::uint32_t* const chars_read,
                             const void* const input_control) noexcept {
    if (chars_read == nullptr ||
        !mapped_guest_range(chars_read, sizeof(*chars_read), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *chars_read = 0;
    const int fd = handle_fd(console_input);
    if (input_control != nullptr || fd != STDIN_FILENO || chars_to_read > (1U << 20U) ||
        (chars_to_read != 0 &&
         !mapped_guest_range(buffer, static_cast<std::size_t>(chars_to_read) * sizeof(*buffer),
                             true))) {
        set_last_error(fd != STDIN_FILENO ? abi::kErrorInvalidHandle
                                          : abi::kErrorInvalidParameter);
        return 0;
    }
    if (chars_to_read == 0) {
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    std::vector<char> bytes(static_cast<std::size_t>(chars_to_read) * 4U);
    ssize_t byte_count = -1;
    do {
        byte_count = ::read(fd, bytes.data(), bytes.size());
    } while (byte_count < 0 && errno == EINTR);
    if (byte_count < 0) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    const std::u16string wide = util::utf8_to_wide(
        std::string_view{bytes.data(), static_cast<std::size_t>(byte_count)});
    const std::size_t copied = std::min<std::size_t>(wide.size(), chars_to_read);
    std::copy_n(wide.begin(), copied, buffer);
    *chars_read = static_cast<std::uint32_t>(copied);
    set_last_error(abi::kErrorSuccess);
    trace_process_console("console", "read-wide", std::to_string(copied));
    return 1;
}

TL_MSABI int tl_WriteConsoleW(const void* const console_output,
                              const std::uint16_t* const buffer,
                              const std::uint32_t chars_to_write,
                              std::uint32_t* const chars_written,
                              const void* const reserved) noexcept {
    if (chars_written == nullptr ||
        !mapped_guest_range(chars_written, sizeof(*chars_written), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    *chars_written = 0;
    const int fd = handle_fd(console_output);
    if (reserved != nullptr || (fd != STDOUT_FILENO && fd != STDERR_FILENO) ||
        chars_to_write > (1U << 20U) ||
        (chars_to_write != 0 &&
         !mapped_guest_range(buffer, static_cast<std::size_t>(chars_to_write) * sizeof(*buffer),
                             false))) {
        set_last_error((fd != STDOUT_FILENO && fd != STDERR_FILENO)
                           ? abi::kErrorInvalidHandle
                           : abi::kErrorInvalidParameter);
        return 0;
    }
    std::string utf8;
    utf8.reserve(chars_to_write);
    std::size_t position = 0;
    while (position < chars_to_write) {
        const std::uint32_t codepoint = util::decode_utf16(buffer, chars_to_write, position);
        char encoded[4]{};
        const std::size_t encoded_size = util::utf8_bytes_for(
            codepoint == util::kInvalidCodepoint ? static_cast<std::uint32_t>('?') : codepoint,
            encoded);
        utf8.append(encoded, encoded_size);
    }
    if (!write_all(fd, utf8.data(), utf8.size())) {
        const std::uint32_t error = errno_to_win32(errno);
        set_last_error(error);
        return 0;
    }
    *chars_written = chars_to_write;
    set_last_error(abi::kErrorSuccess);
    trace_process_console("console", "write-wide", std::to_string(chars_to_write));
    return 1;
}

TL_MSABI int tl_IsDebuggerPresent() noexcept {
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "debugger", "absent");
    return 0;
}

TL_MSABI const char* tl_GetCommandLineA() noexcept {
    if (g_guest_acmdln != nullptr) {
        return g_guest_acmdln;
    }
    // Windows nunca devolve linha de comando vazia; fora de execução de
    // convidado (ex.: testes), devolve um padrão mínimo.
    static const char kDefaultCommandLine[] = "guest.exe";
    return kDefaultCommandLine;
}

TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept {
    static std::vector<std::uint16_t> wide_cmd;
    const std::u16string u16 = util::utf8_to_wide(tl_GetCommandLineA());
    wide_cmd.assign(u16.begin(), u16.end());
    wide_cmd.push_back(0);
    return wide_cmd.data();
}

TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept {
    return abi::kCpUtf8;
}

TL_MSABI int tl_SetConsoleOutputCP(const std::uint32_t) noexcept {
    return 1;
}

TL_MSABI int tl_GetConsoleScreenBufferInfo(const void* console_handle, void* buffer_info) noexcept {
    (void)console_handle;
    if (buffer_info == nullptr || !mapped_guest_range(buffer_info, sizeof(abi::GuestConsoleScreenBufferInfo), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    auto* csbi = static_cast<abi::GuestConsoleScreenBufferInfo*>(buffer_info);
    *csbi = {};
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetConsoleTextAttribute(const void* console_handle, const std::uint16_t attributes) noexcept {
    (void)console_handle;
    // stdout pertence ao convidado: só emitir códigos ANSI quando ele é um
    // terminal interativo; em pipe/arquivo a saída capturada permanece limpa.
    if (::isatty(STDOUT_FILENO)) {
        const bool red = (attributes & 0x0004) != 0;
        const bool green = (attributes & 0x0002) != 0;
        const bool blue = (attributes & 0x0001) != 0;
        const bool bold = (attributes & 0x0008) != 0;

        int ansi_color = 37;
        if (red && green && blue) ansi_color = 37;
        else if (red && green) ansi_color = 33;
        else if (red && blue) ansi_color = 35;
        else if (green && blue) ansi_color = 36;
        else if (red) ansi_color = 31;
        else if (green) ansi_color = 32;
        else if (blue) ansi_color = 34;

        std::fprintf(stdout, "\033[%d;%dm", bold ? 1 : 0, ansi_color);
        std::fflush(stdout);
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_OutputDebugStringA(const char* const output_string) noexcept {
    if (output_string == nullptr || !mapped_guest_cstring(output_string)) {
        return;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "OutputDebugStringA"},
        diagnostics::TraceField{"message", output_string},
    };
    runtime_trace("OutputDebugStringA", fields, 2);
}

TL_MSABI void tl_OutputDebugStringW(const std::uint16_t* const output_string) noexcept {
    if (output_string == nullptr || !mapped_guest_wstring(output_string)) {
        return;
    }
    const std::string utf8 = util::wide_to_utf8(output_string);
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"symbol", "OutputDebugStringW"},
        diagnostics::TraceField{"message", utf8.c_str()},
    };
    runtime_trace("OutputDebugStringW", fields, 2);
}

TL_MSABI int tl_AllocConsole(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_AttachConsole(const std::uint32_t process_id) noexcept {
    (void)process_id;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_FreeConsole(void) noexcept {
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetConsoleCtrlHandler(void* const handler, const int add) noexcept {
    (void)handler;
    (void)add;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetNamedPipeHandleState(void* const named_pipe, std::uint32_t* const mode,
                                        std::uint32_t* const max_collection_count,
                                        std::uint32_t* const collect_data_timeout) noexcept {
    (void)named_pipe;
    (void)mode;
    (void)max_collection_count;
    (void)collect_data_timeout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TransactNamedPipe(void* const named_pipe, void* const in_buffer, const std::uint32_t in_buffer_size,
                                  void* const out_buffer, const std::uint32_t out_buffer_size,
                                  std::uint32_t* const bytes_read, void* const overlapped) noexcept {
    (void)named_pipe;
    (void)in_buffer;
    (void)in_buffer_size;
    (void)out_buffer;
    (void)out_buffer_size;
    (void)bytes_read;
    (void)overlapped;
    set_last_error(230); // ERROR_PIPE_NOT_CONNECTED
    return 0;
}

TL_MSABI int tl_WaitNamedPipeW(const std::uint16_t* const named_pipe_name, const std::uint32_t time_out) noexcept {
    (void)named_pipe_name;
    (void)time_out;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_PeekNamedPipe(void* const named_pipe, void* const buffer, const std::uint32_t buffer_size,
                              std::uint32_t* const bytes_read, std::uint32_t* const total_bytes_avail,
                              std::uint32_t* const bytes_left_this_message) noexcept {
    (void)named_pipe;
    (void)buffer;
    (void)buffer_size;
    if (bytes_read != nullptr && mapped_guest_range(bytes_read, 4, true)) {
        *bytes_read = 0;
    }
    if (total_bytes_avail != nullptr && mapped_guest_range(total_bytes_avail, 4, true)) {
        *total_bytes_avail = 0;
    }
    if (bytes_left_this_message != nullptr && mapped_guest_range(bytes_left_this_message, 4, true)) {
        *bytes_left_this_message = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ReadConsoleA(void* const console_input, void* const buffer,
                             const std::uint32_t number_of_chars_to_read,
                             std::uint32_t* const number_of_chars_read,
                             void* const input_control) noexcept {
    (void)console_input;
    (void)buffer;
    (void)number_of_chars_to_read;
    (void)input_control;
    if (number_of_chars_read != nullptr && mapped_guest_range(number_of_chars_read, sizeof(std::uint32_t), true)) {
        *number_of_chars_read = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void tl_DebugBreak() noexcept {
    // No-op in TradutorLinux runtime
}

TL_MSABI int tl_Beep(const std::uint32_t freq, const std::uint32_t duration) noexcept {
    (void)freq;
    (void)duration;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ClearCommBreak(void* const file) noexcept {
    (void)file;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_ConnectNamedPipe(void* const named_pipe, void* const overlapped) noexcept {
    (void)named_pipe;
    (void)overlapped;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_CreateNamedPipeA(const char* const name, const std::uint32_t open_mode, const std::uint32_t pipe_mode, const std::uint32_t max_instances, const std::uint32_t out_buf_size, const std::uint32_t in_buf_size, const std::uint32_t default_time_out, void* const sec_attr) noexcept {
    (void)name;
    (void)open_mode;
    (void)pipe_mode;
    (void)max_instances;
    (void)out_buf_size;
    (void)in_buf_size;
    (void)default_time_out;
    (void)sec_attr;
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(0x50495045ULL); // 'PIPE'
}

TL_MSABI int tl_CreatePipe(void** const read_pipe, void** const write_pipe, void* const pipe_attr, const std::uint32_t size) noexcept {
    (void)pipe_attr;
    (void)size;
    if (read_pipe != nullptr && mapped_guest_range(read_pipe, sizeof(void*), true)) {
        *read_pipe = reinterpret_cast<void*>(0x50524541ULL); // 'PREA'
    }
    if (write_pipe != nullptr && mapped_guest_range(write_pipe, sizeof(void*), true)) {
        *write_pipe = reinterpret_cast<void*>(0x50575249ULL); // 'PWRI'
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetCommState(void* const file, void* const dcb) noexcept {
    (void)file;
    if (dcb != nullptr && mapped_guest_range(dcb, 28, true)) {
        std::memset(dcb, 0, 28);
        *reinterpret_cast<std::uint32_t*>(dcb) = 28; // DCBlength
        *reinterpret_cast<std::uint32_t*>(static_cast<char*>(dcb) + 4) = 9600; // BaudRate
        *reinterpret_cast<std::uint8_t*>(static_cast<char*>(dcb) + 18) = 8; // ByteSize
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetOverlappedResult(void* const file, void* const overlapped, std::uint32_t* const bytes_transferred, const int wait) noexcept {
    (void)file;
    (void)overlapped;
    (void)wait;
    if (bytes_transferred != nullptr && mapped_guest_range(bytes_transferred, sizeof(std::uint32_t), true)) {
        *bytes_transferred = 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCommBreak(void* const file) noexcept {
    (void)file;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCommState(void* const file, void* const dcb) noexcept {
    (void)file;
    (void)dcb;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetCommTimeouts(void* const file, void* const timeouts) noexcept {
    (void)file;
    (void)timeouts;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_WaitNamedPipeA(const char* const name, const std::uint32_t timeout) noexcept {
    (void)name;
    (void)timeout;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetConsoleMode(const void* handle, std::uint32_t* mode) noexcept {
    if (mode == nullptr || !mapped_guest_range(mode, sizeof(*mode), true)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const int fd = handle_fd(handle);
    if (fd < 0 || ::isatty(fd) == 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    *mode = 0x3U;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetConsoleMode(const void* handle, std::uint32_t /*mode*/) noexcept {
    if (handle_fd(handle) < 0) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

}  // extern "C"
}  // namespace tradutorlinux
