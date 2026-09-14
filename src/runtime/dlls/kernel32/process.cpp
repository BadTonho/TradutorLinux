#include "kernel32_process_internal.hpp"
namespace tradutorlinux {

namespace {

constexpr std::uint32_t kStillActive = 259U;
constexpr std::uint32_t kChildProcessFailure = 0xC0000001U;
constexpr std::size_t kChildProcessProtocolSize = 5;
constexpr std::size_t kMaxEnvironmentStringUnits = 32768U;
constexpr std::size_t kMaxModuleStringUnits = 4096U;

template <typename Unit>
[[nodiscard]] bool write_guest_terminated_units(void* const destination,
                                                const Unit* const source,
                                                const std::size_t length) noexcept {
    if (destination == nullptr || length > std::numeric_limits<std::size_t>::max() / sizeof(Unit)) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(destination);
    const std::size_t bytes = length * sizeof(Unit);
    if (base > std::numeric_limits<std::uintptr_t>::max() - bytes) {
        return false;
    }
    if (bytes > 0 && runtime::write_guest_memory(destination, source, bytes).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    const Unit terminator{};
    return runtime::write_guest_memory(reinterpret_cast<void*>(base + bytes), &terminator,
                                       sizeof(terminator)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

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

struct ResourceKey {
    bool named{false};
    std::uint16_t id{0};
    std::u16string name;
};

[[nodiscard]] bool resource_directory_range(const std::uint32_t relative,
                                            const std::size_t size) noexcept {
    const std::size_t image_size = g_guest_image_size;
    const std::size_t resource_base = g_guest_resource_rva;
    const std::size_t resource_size = g_guest_resource_size;
    return resource_base <= image_size && relative <= resource_size &&
           size <= resource_size - relative && relative <= image_size - resource_base &&
           size <= image_size - resource_base - relative;
}

[[nodiscard]] bool read_resource_u16(const std::uint32_t offset, std::uint16_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint16_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint16_t));
    return true;
}

[[nodiscard]] bool read_resource_u32(const std::uint32_t offset, std::uint32_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint32_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint32_t));
    return true;
}

[[nodiscard]] bool make_resource_key(const std::uint16_t* value, ResourceKey& key) noexcept {
    if (value == nullptr) {
        return false;
    }
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    if (raw <= 0xFFFFU) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw);
        return true;
    }
    if (!mapped_guest_wstring(value)) {
        return false;
    }
    key.named = true;
    for (std::size_t index = 0; value[index] != 0; ++index) {
        key.name.push_back(static_cast<char16_t>(value[index]));
    }
    return true;
}

[[nodiscard]] bool resource_entry_key(const std::uint32_t raw_name,
                                      ResourceKey& key) noexcept {
    if ((raw_name & 0x80000000U) == 0) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw_name & 0xFFFFU);
        return (raw_name & 0xFFFF0000U) == 0;
    }
    const std::uint32_t name_offset = raw_name & 0x7FFFFFFFU;
    std::uint16_t length = 0;
    if (name_offset > std::numeric_limits<std::uint32_t>::max() - 2U ||
        !read_resource_u16(name_offset, length) || length > 4096U ||
        !resource_directory_range(name_offset + 2U,
                                  static_cast<std::size_t>(length) * sizeof(std::uint16_t))) {
        return false;
    }
    key.named = true;
    key.name.clear();
    for (std::uint16_t index = 0; index < length; ++index) {
        std::uint16_t unit = 0;
        const std::uint32_t offset = name_offset + 2U +
                                     static_cast<std::uint32_t>(index) *
                                         static_cast<std::uint32_t>(sizeof(std::uint16_t));
        if (!read_resource_u16(offset, unit)) {
            return false;
        }
        key.name.push_back(static_cast<char16_t>(unit));
    }
    return true;
}

[[nodiscard]] bool resource_keys_equal(const ResourceKey& left,
                                       const ResourceKey& right) noexcept {
    if (left.named != right.named) {
        return false;
    }
    if (!left.named) {
        return left.id == right.id;
    }
    if (left.name.size() != right.name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.name.size(); ++i) {
        const auto c1 = std::tolower(static_cast<unsigned char>(left.name[i]));
        const auto c2 = std::tolower(static_cast<unsigned char>(right.name[i]));
        if (c1 != c2) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool find_resource_entry(const std::uint32_t directory_offset,
                                       const ResourceKey& wanted,
                                       std::uint32_t& child_raw) noexcept {
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (directory_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(directory_offset + 12U, named_count) ||
        !read_resource_u16(directory_offset + 14U, id_count)) {
        return false;
    }
    const std::uint32_t entry_count = static_cast<std::uint32_t>(named_count) + id_count;
    if (entry_count > 4096U ||
        !resource_directory_range(directory_offset + 16U,
                                  static_cast<std::size_t>(entry_count) * 8U)) {
        return false;
    }
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::uint32_t entry_offset = directory_offset + 16U + index * 8U;
        std::uint32_t raw_name = 0;
        std::uint32_t raw_child = 0;
        if (!read_resource_u32(entry_offset, raw_name) ||
            !read_resource_u32(entry_offset + 4U, raw_child)) {
            return false;
        }
        ResourceKey actual;
        if (!resource_entry_key(raw_name, actual)) {
            return false;
        }
        if (resource_keys_equal(actual, wanted)) {
            child_raw = raw_child;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool current_resource_module(const void* module) noexcept {
    if (module == nullptr) {
        return true;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module);
    return value == 0x1000U ||
           (g_guest_image_base != nullptr &&
            value == reinterpret_cast<std::uintptr_t>(g_guest_image_base));
}

[[nodiscard]] std::optional<ResourceSlot> resolve_resource(const void* module,
                                                           const std::uint16_t* name,
                                                           const std::uint16_t* type) noexcept {
    if (!current_resource_module(module) || g_guest_image_base == nullptr ||
        g_guest_resource_size < 16U) {
        return std::nullopt;
    }
    ResourceKey type_key;
    ResourceKey name_key;
    if (!make_resource_key(type, type_key) || !make_resource_key(name, name_key)) {
        return std::nullopt;
    }
    std::uint32_t name_dir_raw = 0;
    if (!find_resource_entry(0, type_key, name_dir_raw) || (name_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    std::uint32_t lang_dir_raw = 0;
    if (!find_resource_entry(name_dir_raw & 0x7FFFFFFFU, name_key, lang_dir_raw) ||
        (lang_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    const std::uint32_t lang_offset = lang_dir_raw & 0x7FFFFFFFU;
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (lang_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(lang_offset + 12U, named_count) ||
        !read_resource_u16(lang_offset + 14U, id_count) ||
        static_cast<std::uint32_t>(named_count) + id_count == 0U) {
        return std::nullopt;
    }
    std::uint32_t data_entry_raw = 0;
    if (!read_resource_u32(lang_offset + 16U + 4U, data_entry_raw) ||
        (data_entry_raw & 0x80000000U) != 0) {
        return std::nullopt;
    }
    std::uint32_t data_rva = 0;
    std::uint32_t data_size = 0;
    if (!read_resource_u32(data_entry_raw, data_rva) ||
        !read_resource_u32(data_entry_raw + 4U, data_size) ||
        data_rva > g_guest_image_size || data_size > g_guest_image_size - data_rva) {
        return std::nullopt;
    }
    return ResourceSlot{.used = true, .data_rva = data_rva, .data_size = data_size};
}

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

[[nodiscard]] std::string extract_module_filename(const char* input) noexcept {
    if (input == nullptr) return {};
    std::string_view view{input};
    const std::size_t pos = view.find_last_of("/\\:");
    if (pos != std::string_view::npos) {
        if (pos + 1 >= view.size()) return {};
        view = view.substr(pos + 1);
    }
    view = std::string_view{view.data(), strnlen(view.data(), view.size())};
    // Trim leading/trailing spaces (comum em LoadLibrary).
    std::size_t start = 0;
    while (start < view.size() && std::isspace(static_cast<unsigned char>(view[start]))) ++start;
    std::size_t end = view.size();
    while (end > start && std::isspace(static_cast<unsigned char>(view[end - 1]))) --end;
    return std::string{view.substr(start, end - start)};
}

[[nodiscard]] std::string normalize_module_name(const char* input) noexcept {
    std::string fname = extract_module_filename(input);
    if (fname.empty()) return {};
    std::string lower;
    lower.reserve(fname.size() + 4);
    for (const char c : fname) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (!lower.ends_with(".dll")) lower += ".dll";
    return lower;
}

[[nodiscard]] bool is_module_available(const std::string& normalized) noexcept {
    if (loader::registered_module_count() == 0) {
        loader::register_builtin_modules();
    }
    if (loader::is_module_registered(normalized)) return true;
    if (loader::is_api_set_dll(normalized) || loader::is_kernelbase_dll(normalized)) {
        return loader::is_module_registered_forwarded(normalized);
    }
    return false;
}

[[nodiscard]] bool is_valid_handle_for_free(void* handle) noexcept {
    if (handle == nullptr) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    if (value == 0x1000U) return true;
    if (g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base)) return true;
    if (runtime::guest_context().module_graph != nullptr &&
        runtime::guest_context().module_graph->is_valid_module_handle(handle)) return true;
    if (loader::is_valid_module_handle(handle)) return true;
    return false;
}

[[nodiscard]] void* load_library_normalized(const std::string& normalized) noexcept {
    if (normalized.empty()) return nullptr;
    if (runtime::guest_context().module_graph != nullptr) {
        return runtime::guest_context().module_graph->load_library(normalized);
    }
    if (!is_module_available(normalized)) return nullptr;
    return reinterpret_cast<void*>(0x1000U);
}

[[nodiscard]] void* get_module_handle_normalized(const std::string& normalized) noexcept {
    if (normalized.empty()) return nullptr;
    if (runtime::guest_context().module_graph != nullptr) {
        return runtime::guest_context().module_graph->get_module_handle(normalized);
    }
    if (!is_module_available(normalized)) return nullptr;
    return reinterpret_cast<void*>(0x1000U);
}

bool read_proc_status_field(const std::uint32_t pid, const char* field, std::string& value) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/status", pid);
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    const std::size_t field_len = std::strlen(field);
    while (std::getline(file, line)) {
        if (line.compare(0, field_len, field) == 0 && line.size() > field_len && line[field_len] == ':') {
            std::size_t pos = field_len + 1;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
            value = line.substr(pos);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
            return true;
        }
    }
    return false;
}

bool fill_process_entry(const std::uint32_t pid, abi::GuestProcessEntry32W& out) {
    std::string ppid_str, threads_str, name_str;
    std::uint32_t ppid = 0;
    std::uint32_t threads = 1;
    if (read_proc_status_field(pid, "PPid", ppid_str)) {
        try { ppid = static_cast<std::uint32_t>(std::stoul(ppid_str)); } catch (...) { ppid = 0; }
    }
    if (read_proc_status_field(pid, "Threads", threads_str)) {
        try { threads = static_cast<std::uint32_t>(std::stoul(threads_str)); } catch (...) { threads = 1; }
    }
    if (!read_proc_status_field(pid, "Name", name_str)) {
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
        std::ifstream comm(path);
        if (comm) std::getline(comm, name_str);
        if (name_str.empty()) name_str = "unknown";
    }
    const std::uint32_t saved_size = out.dwSize;
    out = {};
    out.dwSize = saved_size;
    out.cntUsage = 0;
    out.th32ProcessID = pid;
    out.th32DefaultHeapID = 0;
    out.th32ModuleID = 0;
    out.cntThreads = threads;
    out.th32ParentProcessID = ppid;
    out.pcPriClassBase = 8;
    out.dwFlags = 0;
    std::u16string wname = util::utf8_to_wide(name_str);
    for (std::size_t i = 0; i < wname.size() && i < 259; ++i) {
        out.szExeFile[i] = static_cast<std::uint16_t>(wname[i]);
    }
    out.szExeFile[std::min<std::size_t>(wname.size(), 259)] = 0;
    out.padding1 = 0;
    out.padding2 = 0;
    return true;
}

static std::string g_custom_dll_directory;

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
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "ExitProcess"},
            diagnostics::TraceField{"exit-code", std::to_string(exit_code)},
            diagnostics::TraceField{"status", "success"},
            diagnostics::TraceField{"mechanism", "guest-transfer"},
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

TL_MSABI std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                                  std::uint32_t size) noexcept {
    std::string guest_name;
    if (!runtime::copy_guest_cstring(name, kMaxEnvironmentStringUnits, guest_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::optional<std::string> value = runtime::guest_environment_value(guest_name);
    if (!value.has_value()) {
        set_last_error(abi::kErrorEnvvarNotFound);
        return 0;
    }
    const std::size_t len = value->size();
    if (buffer == nullptr || size == 0) {
        return static_cast<std::uint32_t>(len + 1);
    }
    if (size <= len) {
        set_last_error(abi::kErrorInsufficientBuffer);
        // MSDN: com buffer insuficiente, devolve o tamanho necessário
        // incluindo o terminador nulo.
        return static_cast<std::uint32_t>(len + 1);
    }
    if (!write_guest_terminated_units(buffer, value->data(), len)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(len);
}

TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept {
    std::u16string guest_name;
    if (!runtime::copy_guest_wstring(name, kMaxEnvironmentStringUnits, guest_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string narrow_name = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_name.data()), guest_name.size());
    const std::optional<std::string> value = runtime::guest_environment_value(narrow_name);
    if (!value.has_value()) {
        set_last_error(abi::kErrorEnvvarNotFound);
        return 0;
    }
    const std::u16string wide_value = util::utf8_to_wide(*value);
    const std::uint32_t result = static_cast<std::uint32_t>(wide_value.size() + 1U);
    if (buffer == nullptr || size == 0) {
        return result;
    }
    if (size <= wide_value.size()) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return result;
    }
    if (!write_guest_terminated_units(buffer, wide_value.data(), wide_value.size())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return static_cast<std::uint32_t>(wide_value.size());
}

TL_MSABI int tl_SetEnvironmentVariableW(const std::uint16_t* const name,
                                        const std::uint16_t* const value) noexcept {
    std::u16string guest_name;
    std::u16string guest_value;
    if (!runtime::copy_guest_wstring(name, kMaxEnvironmentStringUnits, guest_name) ||
        (value != nullptr &&
         !runtime::copy_guest_wstring(value, kMaxEnvironmentStringUnits, guest_value))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string narrow_name = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_name.data()), guest_name.size());
    std::optional<std::string> narrow_value;
    if (value != nullptr) {
        narrow_value = util::wide_to_utf8(
            reinterpret_cast<const std::uint16_t*>(guest_value.data()), guest_value.size());
    }
    if (!runtime::set_guest_environment_value(narrow_name, narrow_value)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "set"},
        diagnostics::TraceField{"name", narrow_name},
        diagnostics::TraceField{"action", value == nullptr ? "remove" : "define"},
        diagnostics::TraceField{"status", "success"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint16_t* tl_GetEnvironmentStringsW() noexcept {
    std::uint16_t* const block = runtime::allocate_environment_block_w();
    if (block == nullptr) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "block"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"encoding", "utf-16"},
        diagnostics::TraceField{"ownership", "runtime"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return block;
}

TL_MSABI int tl_FreeEnvironmentStringsW(std::uint16_t* const block) noexcept {
    if (!runtime::free_environment_block_w(block)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI std::uint32_t tl_ExpandEnvironmentStringsW(const std::uint16_t* const source,
                                                     std::uint16_t* const destination,
                                                     const std::uint32_t size) noexcept {
    std::u16string input;
    if (!runtime::copy_guest_wstring(source, kMaxEnvironmentStringUnits, input)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string expanded;
    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != u'%') {
            expanded.push_back(input[index++]);
            continue;
        }
        const std::size_t closing = input.find(u'%', index + 1U);
        if (closing == std::u16string::npos || closing == index + 1U) {
            expanded.push_back(input[index++]);
            continue;
        }
        const std::u16string variable = input.substr(index + 1U, closing - index - 1U);
        const std::optional<std::string> value = runtime::guest_environment_value(
            util::wide_to_utf8(reinterpret_cast<const std::uint16_t*>(variable.data()), variable.size()));
        if (value.has_value()) {
            const std::u16string replacement = util::utf8_to_wide(*value);
            expanded.append(replacement);
        } else {
            expanded.append(input, index, closing - index + 1U);
        }
        index = closing + 1U;
    }
    const std::uint32_t needed = static_cast<std::uint32_t>(expanded.size() + 1U);
    if (destination == nullptr || size == 0) {
        return needed;
    }
    if (size < needed) {
        set_last_error(abi::kErrorInsufficientBuffer);
        return needed;
    }
    if (!write_guest_terminated_units(destination, expanded.data(), expanded.size())) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", "expand"},
        diagnostics::TraceField{"status", "success"},
        diagnostics::TraceField{"required", std::to_string(needed)},
        diagnostics::TraceField{"encoding", "utf-16"},
    };
    runtime_trace("environment", fields, 4);
    set_last_error(abi::kErrorSuccess);
    return needed;
}

TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept {
    if (module_name == nullptr) {
        set_last_error(abi::kErrorSuccess);
        if (g_guest_image_base != nullptr) {
            return const_cast<std::byte*>(g_guest_image_base);
        }
        return reinterpret_cast<void*>(0x1000U);
    }
    std::string guest_module_name;
    if (!runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(guest_module_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    void* const handle = get_module_handle_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept {
    if (module_name == nullptr) {
        return tl_GetModuleHandleA(nullptr);
    }
    std::u16string guest_module_name;
    if (!runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_module_name.data()), guest_module_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    void* const handle = get_module_handle_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorFileNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI int tl_GetModuleHandleExA(std::uint32_t flags, const char* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string guest_module_name;
    bool module_name_copied = false;
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        // Se o endereço estiver dentro da imagem do convidado ou for o token de módulo, trata como handle do exe.
        if (addr == 0x1000U) {
            if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                if (!write_guest_value(module, const_cast<std::byte*>(g_guest_image_base))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (runtime::guest_context().module_graph != nullptr) {
            if (void* const handle = runtime::guest_context().module_graph->module_handle_from_address(addr);
                handle != nullptr) {
                if (!write_guest_value(module, handle)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        // Tenta interpretar module_name como string se estiver em memória de convidado e falhar o range check acima.
        // Para manter compatibilidade, se o ponteiro for uma string válida, cai no caminho normal.
        if (!runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        module_name_copied = true;
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleA(nullptr);
        if (handle == nullptr) return 0;
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    // module_name é nome (A). Validar.
    if (module_name == nullptr ||
        (!module_name_copied &&
         !runtime::copy_guest_cstring(module_name, kMaxModuleStringUnits, guest_module_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(guest_module_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    if (runtime::guest_context().module_graph != nullptr) {
        void* const handle = runtime::guest_context().module_graph->get_module_handle(normalized);
        if (handle == nullptr) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        if (!is_module_available(normalized)) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_GetModuleHandleExW(std::uint32_t flags, const std::uint16_t* module_name, void** module) noexcept {
    constexpr std::uint32_t kValidFlags = abi::kGetModuleHandleExFlagPin |
                                          abi::kGetModuleHandleExFlagUnchangedRefcount |
                                          abi::kGetModuleHandleExFlagFromAddress;
    if ((flags & ~kValidFlags) != 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (module == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::u16string guest_module_name;
    bool module_name_copied = false;
    const bool from_address = (flags & abi::kGetModuleHandleExFlagFromAddress) != 0;
    if (from_address) {
        if (module_name == nullptr) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        const auto addr = reinterpret_cast<std::uintptr_t>(module_name);
        if (addr == 0x1000U) {
            if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
                set_last_error(abi::kErrorInvalidParameter);
                return 0;
            }
            set_last_error(abi::kErrorSuccess);
            return 1;
        }
        if (g_guest_image_base != nullptr && g_guest_image_size > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(g_guest_image_base);
            if (addr >= base && addr < base + g_guest_image_size) {
                if (!write_guest_value(module, const_cast<std::byte*>(g_guest_image_base))) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (runtime::guest_context().module_graph != nullptr) {
            if (void* const handle = runtime::guest_context().module_graph->module_handle_from_address(addr);
                handle != nullptr) {
                if (!write_guest_value(module, handle)) {
                    set_last_error(abi::kErrorInvalidParameter);
                    return 0;
                }
                set_last_error(abi::kErrorSuccess);
                return 1;
            }
        }
        if (!runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        module_name_copied = true;
    }
    if (!from_address && module_name == nullptr) {
        void* handle = tl_GetModuleHandleW(nullptr);
        if (handle == nullptr) return 0;
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (module_name == nullptr ||
        (!module_name_copied &&
         !runtime::copy_guest_wstring(module_name, kMaxModuleStringUnits, guest_module_name))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (guest_module_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_module_name.data()), guest_module_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorFileNotFound);
        return 0;
    }
    if (runtime::guest_context().module_graph != nullptr) {
        void* const handle = runtime::guest_context().module_graph->get_module_handle(normalized);
        if (handle == nullptr) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, handle)) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else {
        if (!is_module_available(normalized)) {
            set_last_error(abi::kErrorFileNotFound);
            return 0;
        }
        if (!write_guest_value(module, reinterpret_cast<void*>(0x1000U))) {
            set_last_error(abi::kErrorInvalidParameter);
            return 0;
        }
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_LoadLibraryA(const char* file_name) noexcept {
    std::string guest_file_name;
    if (!runtime::copy_guest_cstring(file_name, kMaxModuleStringUnits, guest_file_name) ||
        guest_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(guest_file_name.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    void* const handle = load_library_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_LoadLibraryW(const std::uint16_t* file_name) noexcept {
    std::u16string guest_file_name;
    if (!runtime::copy_guest_wstring(file_name, kMaxModuleStringUnits, guest_file_name) ||
        guest_file_name.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string utf8 = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_file_name.data()), guest_file_name.size());
    if (utf8.empty()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const std::string normalized = normalize_module_name(utf8.c_str());
    if (normalized.empty()) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    void* const handle = load_library_normalized(normalized);
    if (handle == nullptr) {
        set_last_error(abi::kErrorModNotFound);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return handle;
}

TL_MSABI void* tl_LoadLibraryExA(const char* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryA(file_name);
}

TL_MSABI void* tl_LoadLibraryExW(const std::uint16_t* file_name, void* file, std::uint32_t flags) noexcept {
    (void)file;
    (void)flags;
    return tl_LoadLibraryW(file_name);
}

TL_MSABI int tl_FreeLibrary(void* module) noexcept {
    if (runtime::guest_context().module_graph != nullptr &&
        runtime::guest_context().module_graph->is_valid_module_handle(module)) {
        if (!runtime::guest_context().module_graph->free_library(module)) {
            set_last_error(abi::kErrorInvalidHandle);
            return 0;
        }
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    if (!is_valid_handle_for_free(module)) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_GetProcAddress(void* module, const char* proc_name) noexcept {
    if (proc_name == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto proc_addr = reinterpret_cast<std::uintptr_t>(proc_name);
    // Ordinal via MAKEINTRESOURCE (HIWORD == 0).
    if (proc_addr <= 0xFFFFU) {
        const auto ordinal = static_cast<std::uint16_t>(proc_addr & 0xFFFFU);
        if (runtime::guest_context().module_graph != nullptr) {
            if (module != nullptr && !is_valid_handle_for_free(module) &&
                (g_guest_image_base == nullptr ||
                 reinterpret_cast<std::uintptr_t>(module) !=
                     reinterpret_cast<std::uintptr_t>(g_guest_image_base))) {
                set_last_error(abi::kErrorInvalidHandle);
                return nullptr;
            }
            const loader::GraphExportLookup found =
                runtime::guest_context().module_graph->get_proc_address(module, ordinal);
            if (found.lookup.found && found.lookup.address != 0) {
                set_last_error(abi::kErrorSuccess);
                return reinterpret_cast<void*>(found.lookup.address);
            }
            set_last_error(abi::kErrorProcNotFound);
            return nullptr;
        }
        if (loader::registered_module_count() == 0) loader::register_builtin_modules();
        loader::ExportLookup found{};
        if (module != nullptr && is_valid_handle_for_free(module)) {
            // Tenta ordinal no módulo específico quando possível; para token único, busca global.
            found = loader::find_export_by_ordinal_global(ordinal);
        } else if (module == nullptr) {
            found = loader::find_export_by_ordinal_global(ordinal);
        } else {
            // Handle inválido: falha como no Windows.
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        if (found.found && found.address != 0) {
            set_last_error(abi::kErrorSuccess);
            return reinterpret_cast<void*>(found.address);
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    std::string guest_proc_name;
    if (!runtime::copy_guest_cstring(proc_name, kMaxModuleStringUnits, guest_proc_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    if (guest_proc_name.empty()) {
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    if (loader::registered_module_count() == 0) loader::register_builtin_modules();
    if (runtime::guest_context().module_graph != nullptr) {
        if (module != nullptr && !is_valid_handle_for_free(module) &&
            (g_guest_image_base == nullptr ||
             reinterpret_cast<std::uintptr_t>(module) !=
                 reinterpret_cast<std::uintptr_t>(g_guest_image_base))) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        const loader::GraphExportLookup found =
            runtime::guest_context().module_graph->get_proc_address(module, guest_proc_name.c_str());
        if (found.lookup.found && found.lookup.address != 0) {
            set_last_error(abi::kErrorSuccess);
            return reinterpret_cast<void*>(found.lookup.address);
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    // Validação de handle: se não for nullptr e não for handle válido, falha.
    if (module != nullptr && !is_valid_handle_for_free(module)) {
        // Permitir handle de imagem do convidado (recursos) como válido, mas GetProcAddress nele não tem exports.
        const auto value = reinterpret_cast<std::uintptr_t>(module);
        const bool is_image = g_guest_image_base != nullptr && value == reinterpret_cast<std::uintptr_t>(g_guest_image_base);
        if (!is_image) {
            set_last_error(abi::kErrorInvalidHandle);
            return nullptr;
        }
        set_last_error(abi::kErrorProcNotFound);
        return nullptr;
    }
    loader::ExportLookup found{};
    // Token único 0x1000 representa qualquer DLL registrada; busca global cobre apisets.
    found = loader::find_export_global(guest_proc_name.c_str());
    if (found.found && found.address != 0) {
        set_last_error(abi::kErrorSuccess);
        return reinterpret_cast<void*>(found.address);
    }
    // Se não encontrou globalmente, tenta lookup com forwarders para cobrir símbolos que só existem via API Set.
    // fallback já é global, então direto erro.
    set_last_error(abi::kErrorProcNotFound);
    return nullptr;
}

TL_MSABI void* tl_CreateToolhelp32Snapshot(std::uint32_t flags, std::uint32_t process_id) noexcept {
    (void)process_id;
    // Suporta apenas SNAPPROCESS; outros flags retornam INVALID_HANDLE_VALUE
    if ((flags & abi::kTh32csSnapProcess) == 0) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
    }
    std::vector<std::uint32_t> pids;
    DIR* dir = ::opendir("/proc");
    if (dir == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
    }
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        bool numeric = true;
        for (const char* p = name; *p; ++p) if (!std::isdigit(static_cast<unsigned char>(*p))) { numeric = false; break; }
        if (!numeric) continue;
        try {
            std::uint32_t pid = static_cast<std::uint32_t>(std::stoul(name));
            // Verifica se ainda existe e temos permissão (access)
            char path[64];
            std::snprintf(path, sizeof(path), "/proc/%u", pid);
            struct stat st;
            if (::stat(path, &st) == 0) pids.push_back(pid);
        } catch (...) {}
    }
    ::closedir(dir);
    if (pids.empty()) {
        // Mesmo sem processos, retorna snapshot vazio (Windows retornaria handle válido com zero processos?)
        // Para manter contrato, retorna handle válido com lista vazia; Process32First falhará com NO_MORE_FILES
    }
    std::sort(pids.begin(), pids.end());
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    for (auto& slot : g_snapshots) {
        if (!slot.used) {
            slot.used = true;
            slot.header = {runtime::HandleObjectType::Snapshot, 1};
            slot.pids = std::move(pids);
            slot.next_index = 0;
            slot.flags = flags;
            set_last_error(abi::kErrorSuccess);
            return static_cast<void*>(&slot);
        }
    }
    set_last_error(abi::kErrorNotEnoughMemory);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
}

TL_MSABI int tl_Process32FirstW(void* snapshot, void* entry) noexcept {
    if (snapshot == nullptr || snapshot == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) ||
        entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    SnapshotSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (auto& s : g_snapshots) if (s.used && snapshot == static_cast<void*>(&s)) { slot = &s; break; }
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    std::uint32_t dwSize = 0;
    if (!read_guest_value(entry, dwSize)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (dwSize != sizeof(abi::GuestProcessEntry32W)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    if (slot->pids.empty() || slot->next_index >= slot->pids.size()) {
        // Tenta repopular se vazio? Já vazio
        set_last_error(abi::kErrorNoMoreFiles);
        return 0;
    }
    slot->next_index = 0;
    abi::GuestProcessEntry32W output{};
    output.dwSize = dwSize;
    if (!fill_process_entry(slot->pids[0], output) ||
        runtime::write_guest_memory(entry, &output, sizeof(output)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    slot->next_index = 1;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Process32NextW(void* snapshot, void* entry) noexcept {
    if (snapshot == nullptr || snapshot == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) ||
        entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    SnapshotSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        for (auto& s : g_snapshots) if (s.used && snapshot == static_cast<void*>(&s)) { slot = &s; break; }
    }
    if (slot == nullptr) {
        set_last_error(abi::kErrorInvalidHandle);
        return 0;
    }
    std::uint32_t dwSize = 0;
    if (!read_guest_value(entry, dwSize)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (dwSize != sizeof(abi::GuestProcessEntry32W)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    if (slot->next_index >= slot->pids.size()) {
        set_last_error(abi::kErrorNoMoreFiles);
        return 0;
    }
    abi::GuestProcessEntry32W output{};
    output.dwSize = dwSize;
    if (!fill_process_entry(slot->pids[slot->next_index], output) ||
        runtime::write_guest_memory(entry, &output, sizeof(output)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    slot->next_index++;
    set_last_error(abi::kErrorSuccess);
    return 1;
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

TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept {
    const auto slot = resolve_resource(module, name, type);
    if (!slot.has_value()) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "FindResourceW"},
            diagnostics::TraceField{"status", "not-found"},
            diagnostics::TraceField{"resource-rva", std::to_string(g_guest_resource_rva)},
            diagnostics::TraceField{"resource-size", std::to_string(g_guest_resource_size)},
        };
        runtime_trace("FindResourceW", fields, 4);
        set_last_error(abi::kErrorResourceNotFound);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    auto it = std::find_if(g_resources.begin(), g_resources.end(),
                           [](const ResourceSlot& s) { return !s.used; });
    if (it == g_resources.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    *it = *slot;
    const auto index = static_cast<std::size_t>(it - g_resources.begin());
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(kResourceHandleBase + index);
}

TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(resource);
}

TL_MSABI void* tl_LockResource(const void* resource_data) noexcept {
    if (resource_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource_data);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used || g_guest_image_base == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<std::byte*>(g_guest_image_base + slot.data_rva);
}

TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return slot.data_size;
}

TL_MSABI int tl_CreateProcessA(const char* application_name, char* command_line,
                               const void* process_attributes, const void* thread_attributes,
                               const int inherit_handles, const std::uint32_t creation_flags,
                               const void* environment, const char* current_directory,
                               const void* startup_info, void* process_information) noexcept {
    (void)startup_info;
    if (process_attributes != nullptr || thread_attributes != nullptr || inherit_handles != 0 ||
        creation_flags != 0 || environment != nullptr ||
        process_information == nullptr ||
        !mapped_guest_range(process_information, sizeof(GuestProcessInformation), true) ||
        (application_name != nullptr && !mapped_guest_cstring(application_name)) ||
        (application_name == nullptr && (command_line == nullptr || !mapped_guest_cstring(command_line))) ||
        (current_directory != nullptr && !mapped_guest_cstring(current_directory))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string guest_path;
    if (application_name != nullptr) {
        guest_path = application_name;
    } else {
        guest_path = command_line;
    }
    char normalized_path[4096]{};
    if (!translate_windows_path(guest_path.c_str(), normalized_path, sizeof(normalized_path))) {
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
    auto* information = static_cast<GuestProcessInformation*>(process_information);
    *information = {.process_handle = sync_slot_handle(*allocated),
                    .thread_handle = nullptr,
                    .process_id = static_cast<std::uint32_t>(child),
                    .thread_id = 0};
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

TL_MSABI int tl_SetDllDirectoryW(const std::uint16_t* const path_name) noexcept {
    if (path_name == nullptr) {
        g_custom_dll_directory.clear();
        set_last_error(abi::kErrorSuccess);
        return 1;
    }
    std::u16string guest_path_name;
    if (!runtime::copy_guest_wstring(path_name, kMaxModuleStringUnits, guest_path_name)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    g_custom_dll_directory = util::wide_to_utf8(
        reinterpret_cast<const std::uint16_t*>(guest_path_name.data()), guest_path_name.size());
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

TL_MSABI void tl_FreeLibraryAndExitThread(void* const module_handle, const std::uint32_t exit_code) noexcept {
    (void)module_handle;
    tl_ExitThread(exit_code);
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

TL_MSABI int tl_Process32First(void* const snapshot, void* const entry) noexcept {
    (void)snapshot;
    if (entry == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    // PROCESSENTRY32 ANSI: dwSize (4), cntUsage(4), th32ProcessID(4), th32DefaultHeapID(8), th32ModuleID(4), cntThreads(4), th32ParentProcessID(4), pcPriClassBase(4), dwFlags(4), szExeFile[260]
    struct DummyEntryA {
        std::uint32_t dwSize;
        std::uint32_t cntUsage;
        std::uint32_t th32ProcessID;
        std::uintptr_t th32DefaultHeapID;
        std::uint32_t th32ModuleID;
        std::uint32_t cntThreads;
        std::uint32_t th32ParentProcessID;
        std::int32_t pcPriClassBase;
        std::uint32_t dwFlags;
        char szExeFile[260];
    } output{};
    if (!read_guest_value(entry, output.dwSize)) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::uint32_t in_size = output.dwSize;
    output = {};
    output.dwSize = in_size;
    output.th32ProcessID = 1000;
    output.cntThreads = 4;
    std::strncpy(output.szExeFile, "process.exe", sizeof(output.szExeFile) - 1);
    const std::size_t bytes_to_write = std::min<std::size_t>(in_size, sizeof(output));
    if (bytes_to_write > 0 &&
        runtime::write_guest_memory(entry, &output, bytes_to_write).status !=
            runtime::GuestMemoryAccessStatus::Success) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI int tl_Process32Next(void* const snapshot, void* const entry) noexcept {
    (void)snapshot;
    (void)entry;
    set_last_error(18); // ERROR_NO_MORE_FILES
    return 0;
}

TL_MSABI int tl_SetDefaultDllDirectories(const std::uint32_t directory_flags) noexcept {
    (void)directory_flags;
    set_last_error(abi::kErrorSuccess);
    return 1;
}

TL_MSABI void* tl_FindResourceExW(void* const module, const wchar_t* const type, const wchar_t* const name, const std::uint16_t language) noexcept {
    (void)language;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
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

TL_MSABI void* tl_FindResourceA(void* const module, const char* const name, const char* const type) noexcept {
    (void)module;
    (void)name;
    (void)type;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
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

TL_MSABI void tl_FreeLibraryWhenCallbackReturns(void* const pci, void* const module) noexcept {
    (void)pci;
    (void)module;
}

TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, int inherit_handles,
                               std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept {
    if ((current_directory != nullptr && !mapped_guest_wstring(current_directory)) ||
        (application_name != nullptr && !mapped_guest_wstring(application_name)) ||
        (application_name == nullptr && !mapped_guest_wstring(command_line))) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const std::string application = application_name != nullptr ? util::wide_to_utf8(application_name) : "";
    std::string command = command_line != nullptr ? util::wide_to_utf8(command_line) : "";
    const std::string directory =
        current_directory != nullptr ? util::wide_to_utf8(current_directory) : "";
    char* command_pointer = command.empty() ? nullptr : command.data();
    const char* application_pointer = application.empty() ? nullptr : application.c_str();
    const char* directory_pointer = directory.empty() ? nullptr : directory.c_str();
    return tl_CreateProcessA(application_pointer, command_pointer, process_attributes, thread_attributes,
                             inherit_handles, creation_flags, environment, directory_pointer, startup_info,
                             process_information);
}

}  // extern "C"
}  // namespace tradutorlinux
