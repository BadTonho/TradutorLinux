#include "kernel32_process_internal.hpp"

namespace tradutorlinux {

namespace {

constexpr std::uint32_t kStillActive = 259U;
constexpr std::uint32_t kChildProcessFailure = 0xC0000001U;
constexpr std::size_t kChildProcessProtocolSize = 5;

[[nodiscard]] std::uint64_t relocate_tls_va_for_child(
    const std::uint64_t value, const pe::PeInfo& info,
    const loader::MappedImage& image) noexcept {
    if (value == 0 || value < info.image_base ||
        value - info.image_base >= static_cast<std::uint64_t>(info.size_of_image)) {
        return value;
    }
    const std::uint64_t rva = value - info.image_base;
    return rva < image.size && image.base <= std::numeric_limits<std::uint64_t>::max() - rva
               ? image.base + rva
               : value;
}

[[nodiscard]] std::vector<std::uint64_t> relocated_tls_callbacks_for_child(
    const pe::PeInfo& info, const loader::MappedImage& image) {
    std::vector<std::uint64_t> callbacks;
    callbacks.reserve(info.tls_info.callback_vas.size());
    for (const std::uint64_t callback : info.tls_info.callback_vas) {
        callbacks.push_back(relocate_tls_va_for_child(callback, info, image));
    }
    return callbacks;
}

struct GuestProcessInformation {
    void* process_handle{};
    void* thread_handle{};
    std::uint32_t process_id{};
    std::uint32_t thread_id{};
};
static_assert(sizeof(GuestProcessInformation) == 24);

void write_child_process_result(const int fd, const GuestExecutionResult result) noexcept {
    const std::array<std::byte, kChildProcessProtocolSize> message{
        result.exited_explicitly ? std::byte{1} : std::byte{0},
        std::byte{static_cast<unsigned char>(result.exit_code & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 8U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 16U) & 0xFFU)},
        std::byte{static_cast<unsigned char>((result.exit_code >> 24U) & 0xFFU)},
    };
    std::size_t written = 0;
    while (written < message.size()) {
        const ssize_t count = ::write(fd, message.data() + written, message.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += static_cast<std::size_t>(count);
    }
}

bool read_guest_file_for_process(const char* path, std::vector<std::byte>& bytes) noexcept {
    bytes.clear();
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::array<std::byte, 4096> buffer{};
    std::size_t total = 0;
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        if (count == 0) {
            break;
        }
        const auto added = static_cast<std::size_t>(count);
        if (added > std::numeric_limits<std::size_t>::max() - total) {
            ::close(fd);
            return false;
        }
        total += added;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(added));
    }
    ::close(fd);
    return true;
}

[[noreturn]] void run_created_guest_child(const std::string& path, const int result_fd,
                                          const std::string& working_directory) noexcept {
    runtime::invalidate_memory_map_cache();
    if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
        ::munmap(const_cast<std::byte*>(g_guest_image_base), g_guest_image_size);
        set_guest_image_view(nullptr, 0, 0, 0);
    }
    GuestExecutionResult result{};
    if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }
    std::vector<std::byte> bytes;
    if (!read_guest_file_for_process(path.c_str(), bytes)) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    const pe::ParseResult parsed = pe::parse_pe(bytes);
    if (parsed.status != pe::ParseStatus::Success) {
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }
    loader::register_builtin_modules();
    loader::PrepareResult prepared = loader::prepare_process(parsed.info, bytes);
    if (prepared.status != loader::PrepareStatus::Success ||
        (prepared.process.image.delta != 0 && !prepared.process.image.has_relocation_directory)) {
        loader::destroy_process(prepared.process);
        result.exit_code = kChildProcessFailure;
        write_child_process_result(result_fd, result);
        ::close(result_fd);
        ::_exit(0);
    }

    loader::GuestProcess& process = prepared.process;
    set_guest_module_path(path.c_str());
    msvcrt_set_guest_command_line(std::vector<std::string>{
        prefix::to_windows_path(std::filesystem::path(path), guest_prefix_root())});
    set_guest_image_view(process.image.memory, process.image.size,
                         parsed.info.resource_directory_rva,
                         parsed.info.resource_directory_size);
    runtime::set_guest_unwind_view(process.image.memory, process.image.size,
                                   parsed.info.exception_directory_rva,
                                   process.info.runtime_functions);
    set_guest_tls_directory(
        relocate_tls_va_for_child(parsed.info.tls_info.start_address_of_raw_data,
                                  parsed.info, process.image),
        relocate_tls_va_for_child(parsed.info.tls_info.end_address_of_raw_data,
                                  parsed.info, process.image),
        relocate_tls_va_for_child(parsed.info.tls_info.address_of_index,
                                  parsed.info, process.image),
        parsed.info.tls_info.size_of_zero_fill,
        relocated_tls_callbacks_for_child(parsed.info, process.image));
    result = execute_guest_entry(process.thread.entry_point, process.thread.stack_top);
    set_guest_tls_directory(0, 0, 0, 0, {});
    runtime::clear_guest_unwind_view();
    set_guest_image_view(nullptr, 0, 0, 0);
    loader::destroy_process(process);
    write_child_process_result(result_fd, result);
    ::close(result_fd);
    ::_exit(0);
}

} // namespace

extern "C" {

TL_MSABI void tl_GetStartupInfoW(abi::GuestStartupInfoW* const startup_info) noexcept {
    if (startup_info == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    abi::GuestStartupInfoW output{};
    output.cb = sizeof(output);
    output.flags = abi::kStartfUseStdHandles;
    {
        std::lock_guard lock(g_process_context_mutex);
        output.std_input = g_standard_handles[0];
        output.std_output = g_standard_handles[1];
        output.std_error = g_standard_handles[2];
    }
    if (runtime::write_guest_memory(startup_info, &output, sizeof(output)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    set_last_error(abi::kErrorSuccess);
    trace_process_console("process-context", "startup-info", "wide");
}

TL_MSABI void tl_ExitProcess(const std::uint32_t exit_code) noexcept {
    if (!g_guest_execution_active) {
        set_last_error(abi::kErrorInvalidParameter);
        return;
    }
    {
        const void* caller = __builtin_return_address(0);
        char mechanism_buf[64];
        std::snprintf(mechanism_buf, sizeof(mechanism_buf), "guest-transfer caller=0x%lx",
                      reinterpret_cast<unsigned long>(caller));
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "ExitProcess"},
            diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
            diagnostics::TraceField{"status", "success"},
            diagnostics::TraceField{"mechanism", std::string(mechanism_buf)},
        };
        runtime_trace("ExitProcess", fields, 4);
    }
    g_guest_exit_code = exit_code;
    cleanup_current_fls_values();
    guest_longjmp(g_guest_exit_context, 1);
}

TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept {
    return static_cast<std::uint32_t>(::getpid());
}

TL_MSABI void* tl_GetCurrentProcess() noexcept {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
}

TL_MSABI void* tl_OpenProcess(std::uint32_t desired_access, int inherit_handle, std::uint32_t process_id) noexcept {
    (void)desired_access;
    (void)inherit_handle;
    if (process_id == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u", process_id);
    struct stat st;
    if (::stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    // Retorna token opaco base+pid; CloseHandle reconhecerá via range
    void* handle = reinterpret_cast<void*>(kProcessHandleBase + static_cast<std::uintptr_t>(process_id));
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept {
    if (startup_info != nullptr) {
        std::array<std::byte, 104> output{};
        const std::uint32_t size = 104;
        std::memcpy(output.data(), &size, sizeof(size));
        if (runtime::write_guest_memory(startup_info, output.data(), output.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return;
        }
    }
    set_last_error(abi::kErrorSuccess);
}

TL_MSABI int tl_CreateProcessA(const char* application_name, char* command_line,
                               const void* process_attributes, const void* thread_attributes,
                               const int inherit_handles, const std::uint32_t creation_flags,
                               const void* environment, const char* current_directory,
                               const void* startup_info, void* process_information) noexcept {
    (void)startup_info;
    (void)inherit_handles;
    (void)creation_flags;
    (void)environment;
    if (process_attributes != nullptr || thread_attributes != nullptr ||
        process_information == nullptr ||
        (application_name == nullptr && command_line == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    GuestProcessInformation empty_information{};
    if (runtime::write_guest_memory(process_information, &empty_information,
                                    sizeof(empty_information)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    char normalized_path[4096]{};
    const char* const path_source = application_name != nullptr ? application_name : command_line;
    if (!translate_windows_path(path_source, normalized_path, sizeof(normalized_path))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // Ordem de busca do CreateProcess: o diretório do aplicativo convidado
    // tem precedência sobre o diretório corrente do hospedeiro.
    if (normalized_path[0] != '/') {
        const std::string_view module_file{g_module_file_name};
        const std::size_t slash = module_file.find_last_of('/');
        if (slash != std::string_view::npos) {
            char candidate[4096]{};
            int written = std::snprintf(candidate, sizeof(candidate), "%.*s/%s",
                                        static_cast<int>(slash), module_file.data(),
                                        normalized_path);
            if (written > 0 && written < static_cast<int>(sizeof(candidate)) &&
                ::access(candidate, R_OK) == 0) {
                std::memcpy(normalized_path, candidate, static_cast<std::size_t>(written) + 1);
            } else {
                const std::string_view norm_view{normalized_path};
                const std::size_t norm_slash = norm_view.find_last_of('/');
                const std::string_view filename = (norm_slash != std::string_view::npos)
                                                      ? norm_view.substr(norm_slash + 1)
                                                      : norm_view;
                written = std::snprintf(candidate, sizeof(candidate), "%.*s/%.*s",
                                        static_cast<int>(slash), module_file.data(),
                                        static_cast<int>(filename.size()), filename.data());
                if (written > 0 && written < static_cast<int>(sizeof(candidate)) &&
                    ::access(candidate, R_OK) == 0) {
                    std::memcpy(normalized_path, candidate, static_cast<std::size_t>(written) + 1);
                }
            }
        }
    }
    if (::access(normalized_path, F_OK) != 0) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }

    std::string child_working_directory;
    if (current_directory != nullptr) {
        char normalized_directory[4096]{};
        if (!translate_windows_path(current_directory, normalized_directory,
                                    sizeof(normalized_directory))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        std::filesystem::path directory{normalized_directory};
        if (directory.is_relative()) {
            std::error_code current_path_error;
            directory = std::filesystem::current_path(current_path_error) / directory;
            if (current_path_error) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
        }
        const prefix::EnvironmentPaths paths =
            prefix::get_environment_paths(guest_prefix_root());
        std::error_code directory_error;
        if (!prefix::is_path_within(directory, paths.drive_c) ||
            !std::filesystem::is_directory(directory, directory_error) || directory_error) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        child_working_directory = directory.string();
    }
    SyncSlot* allocated = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        auto it = std::find_if(g_syncs.begin(), g_syncs.end(),
                               [](const SyncSlot& slot) { return !slot.used; });
        if (it == g_syncs.end()) {
            set_last_error(abi::kErrorNotEnoughMemory);
            return 0;
        }
        it->used = true;
        it->header = {runtime::HandleObjectType::Sync, 1};
        it->kind = SyncKind::Process;
        it->child_pid = -1;
        it->child_result_fd = -1;
        it->process_running = true;
        it->process_exit_code = kStillActive;
        it->signaled = false;
        it->manual_reset = false;
        it->owner_valid = false;
        it->count = 0;
        it->maximum = 0;
        allocated = &*it;
    }
    int result_pipe[2] = {-1, -1};
    if (::pipe(result_pipe) != 0) {
        {
            std::lock_guard<std::mutex> lock(g_sync_mutex);
            clear_sync_slot(*allocated);
        }
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(result_pipe[0]);
        ::close(result_pipe[1]);
        {
            std::lock_guard<std::mutex> lock(g_sync_mutex);
            clear_sync_slot(*allocated);
        }
        set_last_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    if (child == 0) {
        ::close(result_pipe[0]);
        run_created_guest_child(normalized_path, result_pipe[1], child_working_directory);
    }
    ::close(result_pipe[1]);
    {
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        allocated->child_pid = child;
        allocated->child_result_fd = result_pipe[0];
        // process_running já true
    }
    const GuestProcessInformation information{.process_handle = sync_slot_handle(*allocated),
                                              .thread_handle = nullptr,
                                              .process_id = static_cast<std::uint32_t>(child),
                                              .thread_id = 0};
    if (runtime::write_guest_memory(process_information, &information, sizeof(information)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        static_cast<void>(::kill(child, SIGKILL));
        static_cast<void>(::waitpid(child, nullptr, 0));
        std::lock_guard<std::mutex> lock(g_sync_mutex);
        clear_sync_slot(*allocated);
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetExitCodeProcess(const void* process, std::uint32_t* exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process || exit_code == nullptr) {
        set_last_error(slot == nullptr ? abi::kErrorInvalidHandle : abi::kErrorInvalidParameter);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, 0));
    const std::uint32_t result = slot->process_running ? kStillActive : slot->process_exit_code;
    if (!write_guest_value(exit_code, result)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_TerminateProcess(const void* process, const std::uint32_t exit_code) noexcept {
    SyncSlot* slot = find_sync_slot(process);
    if (slot == nullptr || slot->kind != SyncKind::Process) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (!slot->process_running || slot->child_pid <= 0 || ::kill(slot->child_pid, SIGKILL) != 0) {
        set_last_error(abi::kErrorAccessDenied);
        return 0;
    }
    static_cast<void>(wait_process_slot(*slot, abi::kInfinite));
    slot->process_exit_code = exit_code;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetProcessId(const void* const process) noexcept {
    if (process == nullptr || process == reinterpret_cast<const void*>(~0ULL)) {
        return static_cast<std::uint32_t>(getpid());
    }
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(process);
    if (addr >= kProcessHandleBase && addr < kProcessHandleBase + kProcessHandleRange) {
        return static_cast<std::uint32_t>(addr - kProcessHandleBase);
    }
    const SyncSlot* const slot = find_sync_slot(process);
    if (slot != nullptr && slot->kind == SyncKind::Process) {
        return static_cast<std::uint32_t>(slot->child_pid);
    }
    set_last_error(abi::kErrorInvalidHandle);
    return 0;
}

TL_MSABI int tl_QueryFullProcessImageNameW(const void* const process, const std::uint32_t flags,
                                           std::uint16_t* const exe_name, std::uint32_t* const size) noexcept {
    (void)process;
    (void)flags;
    if (exe_name == nullptr || size == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::uint32_t capacity = 0;
    if (!read_guest_value(size, capacity)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string path =
        prefix::to_windows_path(std::filesystem::path(g_module_file_name), guest_prefix_root());
    const std::u16string wide_path = util::utf8_to_wide(path);
    if (capacity <= wide_path.size()) {
        const std::uint32_t required = static_cast<std::uint32_t>(wide_path.size() + 1U);
        if (!write_guest_value(size, required)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    if (!write_guest_terminated_units(
            exe_name, reinterpret_cast<const std::uint16_t*>(wide_path.data()), wide_path.size()) ||
        !write_guest_value(size, static_cast<std::uint32_t>(wide_path.size()))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetProcessAffinityMask(const void* const process_handle,
                                       std::uintptr_t* const process_affinity_mask,
                                       std::uintptr_t* const system_affinity_mask) noexcept {
    (void)process_handle;
    const std::uintptr_t mask = 0x0000000FULL;
    if (process_affinity_mask != nullptr && !write_guest_value(process_affinity_mask, mask)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (system_affinity_mask != nullptr && !write_guest_value(system_affinity_mask, mask)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetProcessTimes(void* const process, void* const creation_time, void* const exit_time,
                                void* const kernel_time, void* const user_time) noexcept {
    (void)process;
    struct ProcessTimesGuestFileTime {
        std::uint32_t low_date_time;
        std::uint32_t high_date_time;
    };
    const ProcessTimesGuestFileTime dummy_time{0, 0};
    if (creation_time != nullptr && runtime::write_guest_memory(
                                         creation_time, &dummy_time, sizeof(dummy_time)).status !=
                                         runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (exit_time != nullptr && runtime::write_guest_memory(
                                     exit_time, &dummy_time, sizeof(dummy_time)).status !=
                                     runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (kernel_time != nullptr && runtime::write_guest_memory(
                                       kernel_time, &dummy_time, sizeof(dummy_time)).status !=
                                       runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (user_time != nullptr && runtime::write_guest_memory(
                                     user_time, &dummy_time, sizeof(dummy_time)).status !=
                                     runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetProcessAffinityMask(void* const process, const std::uintptr_t process_affinity_mask) noexcept {
    (void)process;
    (void)process_affinity_mask;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_SetPriorityClass(void* const process, const std::uint32_t priority_class) noexcept {
    (void)process;
    (void)priority_class;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_K32GetProcessMemoryInfo(void* const process, void* const counters, const std::uint32_t cb) noexcept {
    (void)process;
    if (counters == nullptr || cb < 32 || cb > 4096U) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    struct DummyCounters {
        std::uint32_t cb;
        std::uint32_t page_fault_count;
        std::size_t peak_working_set;
        std::size_t working_set;
        std::size_t quota_peak_paged;
        std::size_t quota_paged;
        std::size_t quota_peak_nonpaged;
        std::size_t quota_nonpaged;
        std::size_t pagefile_usage;
        std::size_t peak_pagefile_usage;
    } dummy{};
    dummy.cb = cb;
    dummy.working_set = 64 * 1024 * 1024;
    dummy.peak_working_set = 128 * 1024 * 1024;
    dummy.pagefile_usage = 64 * 1024 * 1024;
    std::array<std::byte, 256> zeroes{};
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(counters);
    for (std::size_t offset = 0; offset < cb;) {
        const std::size_t chunk = std::min<std::size_t>(zeroes.size(), cb - offset);
        if (offset > std::numeric_limits<std::uintptr_t>::max() - base ||
            runtime::write_guest_memory(reinterpret_cast<void*>(base + offset), zeroes.data(), chunk).status !=
                runtime::GuestMemoryAccessStatus::Success) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        offset += chunk;
    }
    if (runtime::write_guest_memory(counters, &dummy, std::min<std::size_t>(cb, sizeof(dummy))).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_GetCurrentProcessorNumber() noexcept {
    return 0;
}

TL_MSABI int tl_GetLogicalProcessorInformation(void* const buffer, std::uint32_t* const returned_length) noexcept {
    if (returned_length == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    constexpr std::uint32_t req_size = 64;
    std::uint32_t capacity = 0;
    if (!read_guest_value(returned_length, capacity)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (buffer == nullptr || capacity < req_size) {
        if (!write_guest_value(returned_length, req_size)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    const std::array<std::byte, req_size> output{};
    if (runtime::write_guest_memory(buffer, output.data(), output.size()).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_IsDestinationReachableW(const wchar_t* const lpszDestination, void* const lpQOCInfo) noexcept {
    (void)lpszDestination;
    (void)lpQOCInfo;
    return 1;
}

TL_MSABI int tl_IsNetworkAlive(std::uint32_t* const lpdwFlags) noexcept {
    if (lpdwFlags != nullptr && !write_guest_value(lpdwFlags, std::uint32_t{1})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    return 1;
}

TL_MSABI int tl_GetApplicationRestartSettings(void* const hProcess, wchar_t* const pwzCommandLine, std::uint32_t* const pcchSize, std::uint32_t* const pdwFlags) noexcept {
    (void)hProcess;
    (void)pwzCommandLine;
    (void)pcchSize;
    (void)pdwFlags;
    return static_cast<int>(0x80070490); // ERROR_NOT_FOUND
}

TL_MSABI int tl_UnregisterApplicationRestart() noexcept {
    return 0; // S_OK
}

TL_MSABI int tl_RegisterApplicationRestart(const wchar_t* const pwzCommandLine, const std::uint32_t dwFlags) noexcept {
    (void)pwzCommandLine;
    (void)dwFlags;
    return 0; // S_OK
}

TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, int inherit_handles,
                               std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept {
    std::u16string guest_application;
    std::u16string guest_command_line;
    std::u16string guest_directory;
    if ((application_name != nullptr &&
         !runtime::copy_guest_wstring(application_name, kMaxModuleStringUnits, guest_application)) ||
        (command_line != nullptr &&
         !runtime::copy_guest_wstring(command_line, kMaxModuleStringUnits, guest_command_line)) ||
        (current_directory != nullptr &&
         !runtime::copy_guest_wstring(current_directory, kMaxModuleStringUnits, guest_directory)) ||
        (application_name == nullptr && command_line == nullptr)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string application = application_name != nullptr
                                        ? util::wide_to_utf8(
                                              reinterpret_cast<const std::uint16_t*>(guest_application.data()),
                                              guest_application.size())
                                        : "";
    std::string command = command_line != nullptr
                              ? util::wide_to_utf8(
                                    reinterpret_cast<const std::uint16_t*>(guest_command_line.data()),
                                    guest_command_line.size())
                              : "";
    const std::string directory = current_directory != nullptr
                                      ? util::wide_to_utf8(
                                            reinterpret_cast<const std::uint16_t*>(guest_directory.data()),
                                            guest_directory.size())
                                      : "";
    char* command_pointer = command.empty() ? nullptr : command.data();
    const char* application_pointer = application.empty() ? nullptr : application.c_str();
    const char* directory_pointer = directory.empty() ? nullptr : directory.c_str();
    return tl_CreateProcessA(application_pointer, command_pointer, process_attributes, thread_attributes,
                             inherit_handles, creation_flags, environment, directory_pointer, startup_info,
                             process_information);
}

TL_MSABI std::int32_t tl_AppPolicyGetProcessTerminationMethod(void* token, std::uint32_t* policy) noexcept {
    (void)token;
    if (policy == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (!write_guest_value(policy, std::uint32_t{0})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::int32_t tl_AppPolicyGetShowDeveloperDiagnostic(void* token, std::uint32_t* policy) noexcept {
    (void)token;
    if (policy == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (!write_guest_value(policy, std::uint32_t{1})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::int32_t tl_AppPolicyGetThreadInitializationType(void* token, std::uint32_t* policy) noexcept {
    (void)token;
    if (policy == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (!write_guest_value(policy, std::uint32_t{0})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

TL_MSABI std::int32_t tl_AppPolicyGetWindowingModel(void* token, std::uint32_t* policy) noexcept {
    (void)token;
    if (policy == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    if (!write_guest_value(policy, std::uint32_t{2})) {
        set_last_error(abi::kErrorInvalidParameter);
        return 87;
    }
    set_last_error(abi::kErrorSuccess);
    return 0;
}

}  // extern "C"
}  // namespace tradutorlinux
