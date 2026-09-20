#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_RPC_MSABI __attribute__((ms_abi))
#else
#error "TL_RPC_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_RPC_MSABI std::int32_t tl_UuidCreate(void* uuid) noexcept;
TL_RPC_MSABI std::int32_t tl_UuidCreateSequential(void* uuid) noexcept;
TL_RPC_MSABI std::int32_t tl_UuidToStringA(const void* uuid, char** string_uuid) noexcept;
TL_RPC_MSABI std::int32_t tl_UuidToStringW(const void* uuid, std::uint16_t** string_uuid) noexcept;
TL_RPC_MSABI std::int32_t tl_RpcStringFreeA(char** string_uuid) noexcept;
TL_RPC_MSABI std::int32_t tl_RpcStringFreeW(std::uint16_t** string_uuid) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
