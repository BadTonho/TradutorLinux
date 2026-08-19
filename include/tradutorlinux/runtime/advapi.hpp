#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_ADVAPI_MSABI __attribute__((ms_abi))
#else
#error "TL_ADVAPI_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_ADVAPI_MSABI std::uint32_t tl_RegCloseKey(const void* key) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueA(const void* key, const char* value_name) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegDeleteValueW(const void* key,
                                                 const std::uint16_t* value_name) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegCreateKeyExA(const void* key, const char* subkey,
                                                  std::uint32_t reserved, char* class_name,
                                                  std::uint32_t options, std::uint32_t access,
                                                  const void* security_attributes, void** result,
                                                  std::uint32_t* disposition) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegCreateKeyExW(const void* key,
                                                  const std::uint16_t* subkey,
                                                  std::uint32_t reserved, std::uint16_t* class_name,
                                                  std::uint32_t options, std::uint32_t access,
                                                  const void* security_attributes, void** result,
                                                  std::uint32_t* disposition) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExA(const void* key, const char* subkey,
                                                std::uint32_t options, std::uint32_t access,
                                                void** result) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegOpenKeyExW(const void* key, const std::uint16_t* subkey,
                                                std::uint32_t options, std::uint32_t access,
                                                void** result) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExA(const void* key, const char* value_name,
                                                   std::uint32_t* reserved, std::uint32_t* type,
                                                   unsigned char* data,
                                                   std::uint32_t* data_size) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegQueryValueExW(const void* key,
                                                   const std::uint16_t* value_name,
                                                   std::uint32_t* reserved, std::uint32_t* type,
                                                   unsigned char* data,
                                                   std::uint32_t* data_size) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExA(const void* key, const char* value_name,
                                                 std::uint32_t reserved, std::uint32_t type,
                                                 const unsigned char* data,
                                                 std::uint32_t data_size) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_RegSetValueExW(const void* key,
                                                 const std::uint16_t* value_name,
                                                 std::uint32_t reserved, std::uint32_t type,
                                                 const unsigned char* data,
                                                 std::uint32_t data_size) noexcept;

TL_ADVAPI_MSABI int tl_CryptAcquireContextA(void** prov_handle, const char* container,
                                            const char* provider, std::uint32_t prov_type,
                                            std::uint32_t flags) noexcept;
TL_ADVAPI_MSABI int tl_CryptAcquireContextW(void** prov_handle, const std::uint16_t* container,
                                            const std::uint16_t* provider, std::uint32_t prov_type,
                                            std::uint32_t flags) noexcept;
TL_ADVAPI_MSABI int tl_CryptGenRandom(void* prov_handle, std::uint32_t length,
                                      std::uint8_t* buffer) noexcept;
TL_ADVAPI_MSABI int tl_CryptReleaseContext(void* prov_handle, std::uint32_t flags) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
