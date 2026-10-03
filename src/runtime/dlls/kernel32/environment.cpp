#include "kernel32_process_internal.hpp"

namespace tradutorlinux {

extern "C" {

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

}  // extern "C"
}  // namespace tradutorlinux
