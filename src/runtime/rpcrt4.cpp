#include "tradutorlinux/runtime/rpcrt4.hpp"

#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/runtime/winapi.hpp"
#include "core/runtime_state_common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>

namespace tradutorlinux {

namespace {

constexpr std::int32_t kRpcSOk = 0;
constexpr std::int32_t kRpcSInvalidArg = 87;
constexpr std::int32_t kRpcSOutOfMemory = 14;

struct Win32Uuid {
    std::uint32_t data1{};
    std::uint16_t data2{};
    std::uint16_t data3{};
    std::uint8_t data4[8]{};
};
static_assert(sizeof(Win32Uuid) == 16);

void generate_uuid_v4(Win32Uuid& result) noexcept {
    std::ifstream urandom{"/dev/urandom", std::ios::binary};
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(&result), sizeof(result));
    } else {
        std::uint8_t* const raw = reinterpret_cast<std::uint8_t*>(&result);
        for (std::size_t i = 0; i < sizeof(Win32Uuid); ++i) {
            raw[i] = static_cast<std::uint8_t>(std::rand() & 0xFF);
        }
    }
    // UUID v4 (RFC 4122)
    result.data3 = static_cast<std::uint16_t>((result.data3 & 0x0FFFU) | 0x4000U);
    result.data4[0] = static_cast<std::uint8_t>((result.data4[0] & 0x3FU) | 0x80U);
}

}  // namespace

extern "C" {

TL_RPC_MSABI std::int32_t tl_UuidCreate(void* const uuid) noexcept {
    if (uuid == nullptr) {
        return kRpcSInvalidArg;
    }
    Win32Uuid result{};
    generate_uuid_v4(result);
    if (runtime::write_guest_memory(uuid, &result, sizeof(result)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return kRpcSInvalidArg;
    }
    return kRpcSOk;
}

TL_RPC_MSABI std::int32_t tl_UuidCreateSequential(void* const uuid) noexcept {
    return tl_UuidCreate(uuid);
}

TL_RPC_MSABI std::int32_t tl_UuidToStringA(const void* const uuid, char** const string_uuid) noexcept {
    if (uuid == nullptr || string_uuid == nullptr) {
        return kRpcSInvalidArg;
    }
    Win32Uuid parsed{};
    if (runtime::read_guest_memory(uuid, &parsed, sizeof(parsed)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return kRpcSInvalidArg;
    }
    char* const buffer = static_cast<char*>(std::malloc(37));
    if (buffer == nullptr) {
        return kRpcSOutOfMemory;
    }
    std::snprintf(buffer, 37, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  parsed.data1, parsed.data2, parsed.data3,
                  parsed.data4[0], parsed.data4[1], parsed.data4[2], parsed.data4[3],
                  parsed.data4[4], parsed.data4[5], parsed.data4[6], parsed.data4[7]);
    if (!write_guest_value(string_uuid, buffer)) {
        std::free(buffer);
        return kRpcSInvalidArg;
    }
    return kRpcSOk;
}

TL_RPC_MSABI std::int32_t tl_UuidToStringW(const void* const uuid, std::uint16_t** const string_uuid) noexcept {
    if (uuid == nullptr || string_uuid == nullptr) {
        return kRpcSInvalidArg;
    }
    Win32Uuid parsed{};
    if (runtime::read_guest_memory(uuid, &parsed, sizeof(parsed)).status !=
        runtime::GuestMemoryAccessStatus::Success) {
        return kRpcSInvalidArg;
    }
    std::uint16_t* const buffer = static_cast<std::uint16_t*>(std::malloc(37 * sizeof(std::uint16_t)));
    if (buffer == nullptr) {
        return kRpcSOutOfMemory;
    }
    char ascii[37];
    std::snprintf(ascii, sizeof(ascii), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  parsed.data1, parsed.data2, parsed.data3,
                  parsed.data4[0], parsed.data4[1], parsed.data4[2], parsed.data4[3],
                  parsed.data4[4], parsed.data4[5], parsed.data4[6], parsed.data4[7]);
    for (std::size_t i = 0; i < 37; ++i) {
        buffer[i] = static_cast<std::uint16_t>(ascii[i]);
    }
    if (!write_guest_value(string_uuid, buffer)) {
        std::free(buffer);
        return kRpcSInvalidArg;
    }
    return kRpcSOk;
}

TL_RPC_MSABI std::int32_t tl_RpcStringFreeA(char** const string_uuid) noexcept {
    if (string_uuid == nullptr) {
        return kRpcSInvalidArg;
    }
    char* target = nullptr;
    if (read_guest_value(string_uuid, target) && target != nullptr) {
        std::free(target);
        write_guest_value(string_uuid, static_cast<char*>(nullptr));
    }
    return kRpcSOk;
}

TL_RPC_MSABI std::int32_t tl_RpcStringFreeW(std::uint16_t** const string_uuid) noexcept {
    if (string_uuid == nullptr) {
        return kRpcSInvalidArg;
    }
    std::uint16_t* target = nullptr;
    if (read_guest_value(string_uuid, target) && target != nullptr) {
        std::free(target);
        write_guest_value(string_uuid, static_cast<std::uint16_t*>(nullptr));
    }
    return kRpcSOk;
}

}  // extern "C"

namespace loader {

void register_rpcrt4_module() {
    static const ExportedFunction kRpcrt4Exports[] = {
        {"UuidCreate", 1, reinterpret_cast<std::uintptr_t>(&tl_UuidCreate), ExportSupport::Full},
        {"UuidCreateSequential", 2, reinterpret_cast<std::uintptr_t>(&tl_UuidCreateSequential), ExportSupport::Full},
        {"UuidToStringA", 3, reinterpret_cast<std::uintptr_t>(&tl_UuidToStringA), ExportSupport::Full},
        {"UuidToStringW", 4, reinterpret_cast<std::uintptr_t>(&tl_UuidToStringW), ExportSupport::Full},
        {"RpcStringFreeA", 5, reinterpret_cast<std::uintptr_t>(&tl_RpcStringFreeA), ExportSupport::Full},
        {"RpcStringFreeW", 6, reinterpret_cast<std::uintptr_t>(&tl_RpcStringFreeW), ExportSupport::Full},
    };
    static const InternalModule kRpcrt4Module{"rpcrt4.dll", kRpcrt4Exports};
    register_module(kRpcrt4Module);
}

}  // namespace loader

}  // namespace tradutorlinux
