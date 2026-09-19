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
TL_ADVAPI_MSABI int tl_CryptImportKey(std::uintptr_t prov, const std::uint8_t* data,
                                      std::uint32_t data_len, std::uintptr_t pub_key,
                                      std::uint32_t flags, std::uintptr_t* key) noexcept;
TL_ADVAPI_MSABI int tl_CryptEncrypt(std::uintptr_t key, std::uintptr_t hash,
                                    int final_chunk, std::uint32_t flags,
                                    std::uint8_t* data, std::uint32_t* data_len,
                                    std::uint32_t buf_len) noexcept;

TL_ADVAPI_MSABI int tl_OpenProcessToken(const void* process, std::uint32_t desired_access,
                                        void** token) noexcept;
TL_ADVAPI_MSABI int tl_GetTokenInformation(const void* token, std::uint32_t information_class,
                                           void* information, std::uint32_t information_length,
                                           std::uint32_t* return_length) noexcept;
TL_ADVAPI_MSABI int tl_AllocateAndInitializeSid(const void* identifier_authority,
                                                std::uint8_t sub_authority_count,
                                                std::uint32_t sub_authority0,
                                                std::uint32_t sub_authority1,
                                                std::uint32_t sub_authority2,
                                                std::uint32_t sub_authority3,
                                                std::uint32_t sub_authority4,
                                                std::uint32_t sub_authority5,
                                                std::uint32_t sub_authority6,
                                                std::uint32_t sub_authority7,
                                                void** sid) noexcept;
TL_ADVAPI_MSABI void* tl_FreeSid(void* sid) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_GetLengthSid(const void* sid) noexcept;
TL_ADVAPI_MSABI int tl_CopySid(std::uint32_t destination_length, void* destination,
                               const void* source) noexcept;
TL_ADVAPI_MSABI int tl_EqualSid(const void* first, const void* second) noexcept;
TL_ADVAPI_MSABI int tl_IsValidSid(const void* sid) noexcept;
TL_ADVAPI_MSABI int tl_CreateWellKnownSid(std::uint32_t well_known_sid_type,
                                          const void* domain_sid, void* sid,
                                          std::uint32_t* sid_size) noexcept;
TL_ADVAPI_MSABI int tl_CheckTokenMembership(const void* token, const void* sid,
                                            int* is_member) noexcept;
TL_ADVAPI_MSABI void tl_BuildTrusteeWithSidW(void* trustee, void* sid) noexcept;
TL_ADVAPI_MSABI int tl_InitializeSecurityDescriptor(void* descriptor,
                                                     std::uint32_t revision) noexcept;
TL_ADVAPI_MSABI int tl_SetSecurityDescriptorDacl(void* descriptor, int dacl_present,
                                                 void* dacl, int dacl_defaulted) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_SetEntriesInAclW(std::uint32_t entry_count,
                                                  const void* entries, const void* old_acl,
                                                  void** new_acl) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_GetNamedSecurityInfoW(const std::uint16_t* object_name,
                                                       std::uint32_t object_type,
                                                       std::uint32_t security_information,
                                                       void** owner, void** group, void** dacl,
                                                       void** sacl, void** descriptor) noexcept;
TL_ADVAPI_MSABI std::uint32_t tl_SetNamedSecurityInfoW(std::uint16_t* object_name,
                                                       std::uint32_t object_type,
                                                       std::uint32_t security_information,
                                                       void* owner, void* group, void* dacl,
                                                       void* sacl,
                                                       std::uint32_t inheritance) noexcept;
TL_ADVAPI_MSABI int tl_SetFileSecurityW(const std::uint16_t* file_name,
                                        std::uint32_t security_information,
                                        const void* security_descriptor) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
