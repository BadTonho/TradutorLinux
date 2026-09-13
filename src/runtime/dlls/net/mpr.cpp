#include "tradutorlinux/runtime/mpr.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "../../core/runtime_state_common.hpp"

namespace tradutorlinux {

namespace {

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

// NETRESOURCEW usa quatro DWORDs seguidos de quatro ponteiros na ABI Win64.
// A estrutura é copiada pela fronteira protegida para não exigir alinhamento
// nem desreferenciar diretamente o ponteiro fornecido pelo convidado.
struct GuestNetResourceW {
    std::uint32_t scope{};
    std::uint32_t type{};
    std::uint32_t display_type{};
    std::uint32_t usage{};
    const std::uint16_t* local_name{};
    const std::uint16_t* remote_name{};
    const std::uint16_t* comment{};
    const std::uint16_t* provider{};
};
static_assert(sizeof(GuestNetResourceW) == 48);

bool valid_optional_wstring(const std::uint16_t* const value) noexcept {
    return value == nullptr || runtime::validate_mapped_wstring(value);
}

bool valid_net_resource(const void* const value) noexcept {
    if (value == nullptr) {
        return false;
    }
    GuestNetResourceW resource{};
    if (runtime::read_guest_memory(value, &resource, sizeof(resource)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    return valid_optional_wstring(resource.local_name) &&
           valid_optional_wstring(resource.remote_name) &&
           valid_optional_wstring(resource.comment) &&
           valid_optional_wstring(resource.provider);
}

bool valid_optional_buffer(void* const buffer, const std::uint32_t size) noexcept {
    return size == 0U || (buffer != nullptr && mapped_range(buffer, size, true));
}

void invalid_parameter(const char* const symbol, const char* const detail) noexcept {
    set_last_error(abi::kErrorInvalidParameter);
    trace_guest_failure(symbol, "argument-validation", detail);
}

std::uint32_t unsupported(const char* const symbol, const char* const detail) noexcept {
    set_last_error(abi::kErrorNotSupported);
    runtime_trace(symbol, {
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"status", "unsupported"},
        diagnostics::TraceField{"mechanism", "stub"},
        diagnostics::TraceField{"detail", detail}}, 4);
    return abi::kErrorNotSupported;
}

void invalid_handle(const char* const symbol) noexcept {
    set_last_error(abi::kErrorInvalidHandle);
    runtime_trace(symbol, {
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"status", "invalid-handle"},
        diagnostics::TraceField{"mechanism", "validation"},
        diagnostics::TraceField{"detail", "handle de enumeração não pertence ao runtime"}}, 4);
}

}  // namespace

extern "C" {

TL_MPR_MSABI std::uint32_t tl_WNetAddConnection2W(const void* const net_resource,
                                                  const std::uint16_t* const password,
                                                  const std::uint16_t* const user_name,
                                                  const std::uint32_t flags) noexcept {
    if (!valid_net_resource(net_resource) || !valid_optional_wstring(password) ||
        !valid_optional_wstring(user_name)) {
        invalid_parameter("WNetAddConnection2W", "NETRESOURCEW ou credencial inválida");
        return abi::kErrorInvalidParameter;
    }
    (void)flags;
    return unsupported("WNetAddConnection2W", "conexões de rede e credenciais não implementadas");
}

TL_MPR_MSABI std::uint32_t tl_WNetOpenEnumW(const std::uint32_t scope, const std::uint32_t type,
                                            const std::uint32_t usage, const void* const net_resource,
                                            void** const enum_handle) noexcept {
    (void)scope;
    (void)type;
    (void)usage;
    if (enum_handle == nullptr) {
        invalid_parameter("WNetOpenEnumW", "saída enum_handle inválida");
        return abi::kErrorInvalidParameter;
    }
    if (net_resource != nullptr && !valid_net_resource(net_resource)) {
        invalid_parameter("WNetOpenEnumW", "NETRESOURCEW inválida");
        return abi::kErrorInvalidParameter;
    }
    const void* empty_handle = nullptr;
    if (runtime::write_guest_memory(enum_handle, &empty_handle, sizeof(empty_handle)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        invalid_parameter("WNetOpenEnumW", "enum_handle foi desmontado durante a escrita");
        return abi::kErrorInvalidParameter;
    }
    return unsupported("WNetOpenEnumW", "enumeração de provedores MPR não implementada");
}

TL_MPR_MSABI std::uint32_t tl_WNetEnumResourceW(void* const enum_handle, std::uint32_t* const count,
                                                void* const buffer, std::uint32_t* const buffer_size) noexcept {
    (void)enum_handle;
    if (count == nullptr || !mapped_range(count, sizeof(*count), true) ||
        buffer_size == nullptr || !mapped_range(buffer_size, sizeof(*buffer_size), true)) {
        invalid_parameter("WNetEnumResourceW", "count ou buffer_size inválido");
        return abi::kErrorInvalidParameter;
    }
    std::uint32_t requested_size = 0;
    if (runtime::read_guest_memory(buffer_size, &requested_size, sizeof(requested_size)).status !=
        runtime::GuestMemoryAccessStatus::Success || !valid_optional_buffer(buffer, requested_size)) {
        invalid_parameter("WNetEnumResourceW", "buffer de recursos inválido");
        return abi::kErrorInvalidParameter;
    }
    invalid_handle("WNetEnumResourceW");
    return abi::kErrorInvalidHandle;
}

TL_MPR_MSABI std::uint32_t tl_WNetCloseEnum(void* const enum_handle) noexcept {
    if (enum_handle == nullptr) {
        invalid_handle("WNetCloseEnum");
        return abi::kErrorInvalidHandle;
    }
    invalid_handle("WNetCloseEnum");
    return abi::kErrorInvalidHandle;
}

TL_MPR_MSABI std::uint32_t tl_WNetGetResourceInformationW(const void* const net_resource, void* const buffer,
                                                          std::uint32_t* const buffer_size,
                                                          std::uint16_t** const system) noexcept {
    if (!valid_net_resource(net_resource) || buffer_size == nullptr ||
        !mapped_range(buffer_size, sizeof(*buffer_size), true)) {
        invalid_parameter("WNetGetResourceInformationW", "NETRESOURCEW ou buffer_size inválido");
        return abi::kErrorInvalidParameter;
    }
    std::uint32_t requested_size = 0;
    if (runtime::read_guest_memory(buffer_size, &requested_size, sizeof(requested_size)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !valid_optional_buffer(buffer, requested_size)) {
        invalid_parameter("WNetGetResourceInformationW", "buffer ou system inválido");
        return abi::kErrorInvalidParameter;
    }
    if (system != nullptr) {
        const void* empty_system = nullptr;
        if (runtime::write_guest_memory(system, &empty_system, sizeof(empty_system)).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            invalid_parameter("WNetGetResourceInformationW", "system foi desmontado durante a escrita");
            return abi::kErrorInvalidParameter;
        }
    }
    return unsupported("WNetGetResourceInformationW", "informações de recursos MPR não implementadas");
}

TL_MPR_MSABI std::uint32_t tl_WNetGetResourceParentW(const void* const net_resource, void* const buffer,
                                                     std::uint32_t* const buffer_size) noexcept {
    if (!valid_net_resource(net_resource) || buffer_size == nullptr ||
        !mapped_range(buffer_size, sizeof(*buffer_size), true)) {
        invalid_parameter("WNetGetResourceParentW", "NETRESOURCEW ou buffer_size inválido");
        return abi::kErrorInvalidParameter;
    }
    std::uint32_t requested_size = 0;
    if (runtime::read_guest_memory(buffer_size, &requested_size, sizeof(requested_size)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        !valid_optional_buffer(buffer, requested_size)) {
        invalid_parameter("WNetGetResourceParentW", "buffer de recursos inválido");
        return abi::kErrorInvalidParameter;
    }
    return unsupported("WNetGetResourceParentW", "recurso pai MPR não implementado");
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_mpr_module() {
    static const ExportedFunction kMprExports[] = {
        {"WNetAddConnection2W", 1, reinterpret_cast<std::uintptr_t>(&tl_WNetAddConnection2W),
         ExportSupport::Stub},
        {"WNetOpenEnumW", 2, reinterpret_cast<std::uintptr_t>(&tl_WNetOpenEnumW), ExportSupport::Stub},
        {"WNetEnumResourceW", 3, reinterpret_cast<std::uintptr_t>(&tl_WNetEnumResourceW),
         ExportSupport::Stub},
        {"WNetCloseEnum", 4, reinterpret_cast<std::uintptr_t>(&tl_WNetCloseEnum), ExportSupport::Stub},
        {"WNetGetResourceInformationW", 5,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceInformationW), ExportSupport::Stub},
        {"WNetGetResourceParentW", 6,
         reinterpret_cast<std::uintptr_t>(&tl_WNetGetResourceParentW), ExportSupport::Stub},
    };
    static const InternalModule kMprModule{"MPR.dll", kMprExports};
    register_module(kMprModule);
}

}  // namespace tradutorlinux::loader
