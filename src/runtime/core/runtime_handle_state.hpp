#pragma once

#include "runtime_state_common.hpp"

#include <array>
#include <cstdint>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace tradutorlinux {

using FileSlot = runtime::GuestContext::ContextFileSlot;
using FileMappingSlot = runtime::GuestContext::ContextFileMappingSlot;

constexpr std::uintptr_t kSyncHandleBase = 0x0000600000000000ULL;
using SyncKind = runtime::ContextSyncKind;
using SyncSlot = runtime::GuestContext::ContextSyncSlot;

constexpr std::uintptr_t kFindHandleBase = 0x0000800000000000ULL;
struct FindSlot {
    runtime::ObjectHeader header{runtime::HandleObjectType::Find, 1};
    bool used{false};
    DIR* dir{nullptr};
    std::string pattern;
    std::string directory;
};

extern std::array<FindSlot, 16> g_find_slots;

#define g_files (::tradutorlinux::runtime::guest_context().files)
#define g_files_mutex (::tradutorlinux::runtime::guest_context().files_mutex)
#define g_syncs (::tradutorlinux::runtime::guest_context().syncs)
#define g_sync_mutex (::tradutorlinux::runtime::guest_context().sync_mutex)

struct Win32FindDataA {
    std::uint32_t dw_file_attributes{0};
    std::uint32_t ft_creation_time_lo{0};
    std::uint32_t ft_creation_time_hi{0};
    std::uint32_t ft_last_access_time_lo{0};
    std::uint32_t ft_last_access_time_hi{0};
    std::uint32_t ft_last_write_time_lo{0};
    std::uint32_t ft_last_write_time_hi{0};
    std::uint32_t n_file_size_high{0};
    std::uint32_t n_file_size_low{0};
    std::uint32_t dw_reserved0{0};
    std::uint32_t dw_reserved1{0};
    char c_file_name[260]{};
    char c_alternate_file_name[14]{};
};

class FileSlotGuard {
public:
    explicit FileSlotGuard(const void* handle) noexcept;

    FileSlotGuard(const FileSlotGuard&) = delete;
    FileSlotGuard& operator=(const FileSlotGuard&) = delete;
    FileSlotGuard(FileSlotGuard&&) = delete;
    FileSlotGuard& operator=(FileSlotGuard&&) = delete;

    [[nodiscard]] FileSlot* get() const noexcept { return slot_; }
    [[nodiscard]] bool is_file_handle() const noexcept { return is_file_handle_; }

private:
    std::unique_lock<std::mutex> lock_;
    FileSlot* slot_{nullptr};
    bool is_file_handle_{false};
};

int handle_fd(const void* handle) noexcept;

struct ThreadSlot;
struct CriticalSectionEntry;
struct SnapshotSlot;

ThreadSlot* find_thread_slot(const void* handle) noexcept;
void* thread_slot_to_handle(ThreadSlot& slot) noexcept;

SyncSlot* find_sync_slot(const void* handle) noexcept;
SnapshotSlot* find_snapshot_slot(const void* handle) noexcept;
void* sync_slot_handle(const SyncSlot& slot) noexcept;
std::uint32_t wait_sync_slot(SyncSlot& slot, std::uint32_t milliseconds) noexcept;
std::uint32_t wait_process_slot(SyncSlot& slot, std::uint32_t milliseconds) noexcept;
void clear_sync_slot(SyncSlot& slot) noexcept;

CriticalSectionEntry* find_cs_entry(void* cs) noexcept;
CriticalSectionEntry* alloc_cs_entry(void* cs) noexcept;

FindSlot* find_slot_for_handle(const void* handle) noexcept;

std::uint32_t stat_to_win32_attributes(const char* path, const struct stat& st) noexcept;
std::uint32_t apply_win32_file_attributes(const char* path, std::uint32_t attributes) noexcept;

}  // namespace tradutorlinux
