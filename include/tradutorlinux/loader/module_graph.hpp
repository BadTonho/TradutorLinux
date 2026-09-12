#pragma once

#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/win32/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradutorlinux::loader {

enum class ModuleProvider {
    Profile,
    DriveC,
    Builtin,
};

struct GraphExportLookup {
    ExportLookup lookup;
    ModuleProvider provider{ModuleProvider::Builtin};
    std::size_t module_index{};
    std::string provider_name;
};

// Grafo de módulos pertencente a uma execução. A classe deliberadamente não
// conhece o shell/CLI: o chamador fornece o prefixo e, opcionalmente, o
// perfil já validado. O código de DLL continua sendo executado no processo
// filho criado pelo isolamento, enquanto os mapeamentos são herdados após o
// preparo dos imports.
class GuestModuleGraph final {
public:
    GuestModuleGraph(std::filesystem::path prefix_root,
                     std::optional<compat::Profile> profile,
                     std::filesystem::path main_path,
                     bool trace_enabled) noexcept;
    GuestModuleGraph(const GuestModuleGraph&) = delete;
    GuestModuleGraph& operator=(const GuestModuleGraph&) = delete;
    ~GuestModuleGraph();

    [[nodiscard]] ResolveResult resolve_imports(MappedImage& image,
                                                 const pe::PeInfo& info,
                                                 const std::filesystem::path& requester);
    void bind_main_image(MappedImage& image, const pe::PeInfo& info,
                         const std::filesystem::path& requester) noexcept;

    [[nodiscard]] void* load_library(std::string_view module_name) noexcept;
    [[nodiscard]] bool free_library(void* module_handle) noexcept;
    [[nodiscard]] void* get_module_handle(std::string_view module_name) noexcept;
    [[nodiscard]] void* module_handle_from_address(std::uintptr_t address) const noexcept;
    [[nodiscard]] GraphExportLookup get_proc_address(void* module_handle,
                                                      std::string_view symbol) noexcept;
    [[nodiscard]] GraphExportLookup get_proc_address(void* module_handle,
                                                      std::uint16_t ordinal) noexcept;
    [[nodiscard]] bool is_valid_module_handle(void* module_handle) const noexcept;
    [[nodiscard]] bool is_guest_executable_address(std::uintptr_t address) const noexcept;

    // Executa os callbacks dos módulos que foram realmente usados por
    // imports estáticos ou LoadLibrary. Dependências são percorridas antes do
    // módulo dependente; o detach ocorre no sentido inverso.
    [[nodiscard]] bool process_attach() noexcept;
    void process_detach() noexcept;
    void thread_attach() noexcept;
    void thread_detach() noexcept;

    // Libera somente as DLLs do grafo. A imagem principal é dona do
    // GuestProcess e permanece fora deste método.
    void reset() noexcept;

    // Definições ficam no .cpp; os tipos são públicos apenas para permitir
    // que helpers de parsing internos usem a representação opaca.
    struct LoadedModule;
    struct LookupVisit;

private:
    [[nodiscard]] ResolveResult resolve_imports_for_module(
        MappedImage& image, const pe::PeInfo& info,
        const std::filesystem::path& requester, std::size_t owner_index);
    [[nodiscard]] GraphExportLookup resolve_export(std::string_view module_name,
                                                    std::string_view symbol,
                                                    const std::filesystem::path& requester,
                                                    std::size_t owner_index);
    [[nodiscard]] GraphExportLookup resolve_export(std::string_view module_name,
                                                    std::uint16_t ordinal,
                                                    const std::filesystem::path& requester,
                                                    std::size_t owner_index);
    [[nodiscard]] GraphExportLookup resolve_export_named_internal(
        std::string_view module_name, std::string_view symbol,
        const std::filesystem::path& requester, std::size_t owner_index,
        std::vector<LookupVisit>& visits, std::size_t depth);
    [[nodiscard]] GraphExportLookup resolve_export_ordinal_internal(
        std::string_view module_name, std::uint16_t ordinal,
        const std::filesystem::path& requester, std::size_t owner_index,
        std::vector<LookupVisit>& visits, std::size_t depth);
    [[nodiscard]] std::optional<std::size_t> ensure_profile_module(std::string_view module_name,
                                                                    std::size_t owner_index);
    [[nodiscard]] std::optional<std::size_t> ensure_drive_module(std::string_view module_name,
                                                                 const std::filesystem::path& requester,
                                                                 std::size_t owner_index);
    [[nodiscard]] std::optional<std::size_t> load_pe_module(std::string module_name,
                                                            ModuleProvider provider,
                                                            std::filesystem::path source,
                                                            std::size_t owner_index);
    [[nodiscard]] std::optional<std::size_t> find_loaded(std::string_view module_name,
                                                         ModuleProvider provider) const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_module_for_handle(void* module_handle) const noexcept;
    [[nodiscard]] std::string normalize_module(std::string_view module_name) const;
    [[nodiscard]] bool attach_module(std::size_t index) noexcept;
    void detach_module(std::size_t index) noexcept;
    void discard_loaded_modules() noexcept;
    void reject_loaded_module(std::size_t index, std::string_view reason) noexcept;
    void release_dependency(std::size_t index) noexcept;
    void reject_profile_module(std::string_view module_name) noexcept;
    [[nodiscard]] bool is_guest_executable(const MappedImage& image,
                                           std::uintptr_t address) const noexcept;
    void trace_event(std::string_view event, std::string_view module,
                     std::string_view detail = {},
                     std::string_view provider = {}) const noexcept;

    std::filesystem::path prefix_root_;
    std::filesystem::path main_path_;
    std::optional<compat::Profile> profile_;
    bool trace_enabled_{false};
    std::vector<std::unique_ptr<LoadedModule>> modules_;
    std::vector<std::string> rejected_profile_modules_;
    std::vector<std::string> builtin_modules_;
    std::vector<std::uintptr_t> builtin_handles_;
    std::vector<std::uint32_t> builtin_refcounts_;
    MappedImage* main_image_{nullptr};
    const pe::PeInfo* main_info_{nullptr};
};

}  // namespace tradutorlinux::loader
