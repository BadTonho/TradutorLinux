#pragma once

#include "tradutorlinux/runtime/teb.hpp"

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace tradutorlinux::runtime {

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

    struct ContextExport {
        std::string name;
        std::uint16_t ordinal{0};
        std::uintptr_t address{0};
    };
    struct ContextModule {
        std::string name;
        std::vector<ContextExport> exports;
    };
    std::vector<ContextModule> modules;
    std::mutex modules_mutex;
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
