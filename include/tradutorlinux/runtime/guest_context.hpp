#pragma once

#include "tradutorlinux/runtime/teb.hpp"

#include <array>
#include <atomic>
#include <csetjmp>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace tradutorlinux::runtime {

enum class ContextSyncKind { Mutex, Event, Semaphore, Process };

// Estado pertencente a uma execução Win32. O objeto é deliberadamente
// independente do ABI convidado: as APIs exportadas continuam sendo funções
// TL_MSABI e consultam o contexto ativo por thread.
struct GuestContext {
    GuestContext() noexcept;
    GuestContext(const GuestContext&) = delete;
    GuestContext& operator=(const GuestContext&) = delete;

    std::jmp_buf exit_context{};
    bool execution_active{false};
    std::uint32_t exit_code{0};
    std::uint32_t last_error{0};

    GuestTeb* current_teb{nullptr};
    GuestPeb peb{};
    GuestProcessParameters process_parameters{};

    std::string module_file_name;
    std::filesystem::path prefix_path;

    const std::byte* image_base{nullptr};
    std::size_t image_size{0};
    std::uint32_t resource_rva{0};
    std::uint32_t resource_size{0};

    std::uint64_t tls_start_raw{0};
    std::uint64_t tls_end_raw{0};
    std::uint64_t tls_index_address{0};
    std::vector<std::uint64_t> tls_callbacks;

    std::array<char, 3> standard_handle_tokens{};
    std::array<void*, 3> standard_handles{};
    std::mutex process_context_mutex;

    struct ContextFileSlot {
        int fd{-1};
        bool used{false};
        std::uint64_t file_size{0};
        std::int64_t position{0};
        std::string path;
        bool delete_pending{false};
        bool unlinked{false};
    };
    std::array<ContextFileSlot, 256> files{};
    std::mutex files_mutex;

    struct ContextAllocationSlot {
        void* address{nullptr};
        std::size_t size{0};
        bool view{false};
    };
    struct ContextFileMappingSlot {
        bool used{false};
        int fd{-1};
        std::uint64_t size{0};
        std::uint32_t protect{0};
        std::string name;
    };
    struct ContextGlobalMemorySlot {
        bool used{false};
        void* address{nullptr};
        std::size_t size{0};
        std::uint32_t flags{0};
        std::uint32_t lock_count{0};
        bool global{false};
    };
    std::array<ContextAllocationSlot, 256> allocations{};
    std::mutex allocations_mutex;
    std::array<ContextFileMappingSlot, 64> mappings{};
    std::mutex mapping_mutex;
    std::array<ContextGlobalMemorySlot, 4096> global_memory{};
    std::mutex global_memory_mutex;

    struct ContextDibSlot {
        bool used{false};
        std::int32_t width{0};
        std::int32_t height{0};
        std::uint32_t stride{0};
        std::vector<std::byte> pixels;
    };
    std::array<ContextDibSlot, 32> dibs{};
    std::mutex dib_mutex;

    std::array<void*, 512> local_free_blocks{};
    std::mutex local_free_mutex;

    struct ContextResourceSlot {
        bool used{false};
        std::uint32_t data_rva{0};
        std::uint32_t data_size{0};
    };
    std::array<ContextResourceSlot, 256> resources{};
    std::mutex resource_mutex;

    struct ContextExport {
        std::string name;
        std::uint16_t ordinal{0};
        std::uintptr_t address{0};
        // Espelha loader::ExportSupport sem acoplar o contexto ao header do
        // loader. 0=Full, 1=Limited, 2=Stub.
        std::uint8_t support{0};
    };
    struct ContextModule {
        std::string name;
        std::vector<ContextExport> exports;
    };
    std::vector<ContextModule> modules;
    std::mutex modules_mutex;

    std::array<bool, 256> tls_indices_used{};
    std::mutex tls_mutex;
    std::atomic<std::uint32_t> next_thread_id{2};
    std::atomic<std::uintptr_t> unhandled_exception_filter{0};

    struct ContextSyncSlot {
        bool used{false};
        ContextSyncKind kind{ContextSyncKind::Event};
        std::mutex mutex;
        std::condition_variable condition;
        bool signaled{false};
        bool manual_reset{false};
        bool owner_valid{false};
        std::thread::id owner{};
        std::uint32_t recursion{0};
        std::int32_t count{0};
        std::int32_t maximum{0};
        pid_t child_pid{-1};
        int child_result_fd{-1};
        bool process_running{false};
        std::uint32_t process_exit_code{259U};
    };
    std::array<ContextSyncSlot, 256> syncs{};
    std::mutex sync_mutex;
};

// O contexto é local à thread hospedeira para que uma thread convidada nunca
// observe acidentalmente o estado de outra execução. A ativação pode ser
// aninhada e sempre restaura o contexto anterior no destrutor.
[[nodiscard]] GuestContext& default_guest_context() noexcept;
[[nodiscard]] GuestContext* current_guest_context() noexcept;
[[nodiscard]] GuestContext& guest_context() noexcept;

class GuestContextScope final {
public:
    explicit GuestContextScope(GuestContext& context) noexcept;
    GuestContextScope(const GuestContextScope&) = delete;
    GuestContextScope& operator=(const GuestContextScope&) = delete;
    ~GuestContextScope();

private:
    GuestContext* previous_{nullptr};
};

}  // namespace tradutorlinux::runtime
