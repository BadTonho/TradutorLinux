#include "kernel32_process_internal.hpp"

namespace tradutorlinux {

namespace {

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

} // namespace

extern "C" {

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
        // Mesmo sem processos, retorna snapshot vazio
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

}  // extern "C"
}  // namespace tradutorlinux
