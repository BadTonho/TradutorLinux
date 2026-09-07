#pragma once

#include "tradutorlinux/win32/types.hpp"

namespace tradutorlinux {
extern "C" {

TL_MSABI void* tl_GetStdHandle(std::uint32_t n_std_handle) noexcept;
TL_MSABI int tl_SetStdHandle(std::uint32_t n_std_handle, void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetFileType(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetSystemDirectoryW(std::uint16_t* buffer,
                                              std::uint32_t size) noexcept;
TL_MSABI void tl_GetStartupInfoW(abi::GuestStartupInfoW* startup_info) noexcept;
TL_MSABI int tl_ReadConsoleW(const void* console_input, std::uint16_t* buffer,
                             std::uint32_t chars_to_read, std::uint32_t* chars_read,
                             const void* input_control) noexcept;
TL_MSABI int tl_WriteConsoleW(const void* console_output, const std::uint16_t* buffer,
                              std::uint32_t chars_to_write, std::uint32_t* chars_written,
                              const void* reserved) noexcept;
TL_MSABI int tl_IsDebuggerPresent() noexcept;
TL_MSABI int tl_IsProcessorFeaturePresent(std::uint32_t processor_feature) noexcept;
TL_MSABI void* tl_EncodePointer(void* pointer) noexcept;
TL_MSABI void* tl_DecodePointer(void* pointer) noexcept;
TL_MSABI void tl_InitializeSListHead(abi::GuestSListHeader* list_head) noexcept;
TL_MSABI int tl_WriteFile(const void* file, const void* buffer, std::uint32_t bytes_to_write,
                          std::uint32_t* bytes_written, void* overlapped) noexcept;
TL_MSABI int tl_ReadFile(const void* file, void* buffer, std::uint32_t bytes_to_read,
                         std::uint32_t* bytes_read, void* overlapped) noexcept;
TL_MSABI void tl_ExitProcess(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_GetLastError() noexcept;
TL_MSABI void tl_SetLastError(std::uint32_t error) noexcept;
TL_MSABI void* tl_VirtualAlloc(void* address, std::size_t size, std::uint32_t allocation_type,
                               std::uint32_t protection) noexcept;
TL_MSABI int tl_VirtualFree(void* address, std::size_t size,
                            std::uint32_t free_type) noexcept;
TL_MSABI void* tl_CreateFileA(const char* path, std::uint32_t desired_access,
                              std::uint32_t share_mode, const void* security_attributes,
                              std::uint32_t creation_disposition, std::uint32_t flags,
                              const void* template_file) noexcept;
TL_MSABI void* tl_CreateFileW(const std::uint16_t* path, std::uint32_t desired_access,
                              std::uint32_t share_mode, const void* security_attributes,
                              std::uint32_t creation_disposition, std::uint32_t flags,
                              const void* template_file) noexcept;
TL_MSABI int tl_CloseHandle(const void* handle) noexcept;
TL_MSABI void* tl_CreateMutexA(const void* security_attributes, int initial_owner,
                               const char* name) noexcept;
TL_MSABI void* tl_CreateMutexW(const void* security_attributes, int initial_owner,
                               const std::uint16_t* name) noexcept;
TL_MSABI void* tl_CreateEventA(const void* security_attributes, int manual_reset,
                               int initial_state, const char* name) noexcept;
TL_MSABI void* tl_CreateEventW(const void* security_attributes, int manual_reset,
                               int initial_state, const std::uint16_t* name) noexcept;
TL_MSABI int tl_SetEvent(const void* event_handle) noexcept;
TL_MSABI int tl_ResetEvent(const void* event_handle) noexcept;
TL_MSABI int tl_ReleaseMutex(const void* mutex) noexcept;
TL_MSABI int tl_CreateProcessA(const char* application_name, char* command_line,
                               const void* process_attributes, const void* thread_attributes,
                               int inherit_handles, std::uint32_t creation_flags,
                               const void* environment, const char* current_directory,
                               const void* startup_info, void* process_information) noexcept;
TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, int inherit_handles,
                               std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept;
TL_MSABI int tl_GetExitCodeProcess(const void* process, std::uint32_t* exit_code) noexcept;
TL_MSABI int tl_TerminateProcess(const void* process, std::uint32_t exit_code) noexcept;
TL_MSABI void* tl_CreateSemaphoreA(const void* security_attributes, std::int32_t initial_count,
                                   std::int32_t maximum_count, const char* name) noexcept;
TL_MSABI void* tl_CreateSemaphoreW(const void* security_attributes, std::int32_t initial_count,
                                   std::int32_t maximum_count, const std::uint16_t* name) noexcept;
TL_MSABI int tl_ReleaseSemaphore(const void* semaphore, std::int32_t release_count,
                                  std::int32_t* previous_count) noexcept;
TL_MSABI std::uint32_t tl_WaitForMultipleObjects(std::uint32_t count,
                                                  const void* const* handles, int wait_all,
                                                  std::uint32_t milliseconds) noexcept;
TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept;
TL_MSABI int tl_MulDiv(int number, int numerator, int denominator) noexcept;
TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept;
TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_GetConsoleMode(const void* handle, std::uint32_t* mode) noexcept;
TL_MSABI void tl_InitializeCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_InitializeCriticalSectionAndSpinCount(void* critical_section,
                                                       std::uint32_t spin_count) noexcept;
TL_MSABI int tl_InitializeCriticalSectionEx(void* critical_section, std::uint32_t spin_count,
                                             std::uint32_t flags) noexcept;
TL_MSABI int tl_IsDBCSLeadByteEx(std::uint32_t code_page, std::uint8_t test_char) noexcept;
TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_MultiByteToWideChar(std::uint32_t code_page, std::uint32_t flags, const char* mb_str,
                                   int mb_count, std::uint16_t* wide_str, int wide_count) noexcept;
TL_MSABI int tl_SetConsoleMode(const void* handle, std::uint32_t mode) noexcept;
TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t handler) noexcept;
TL_MSABI void tl_Sleep(std::uint32_t milliseconds) noexcept;
TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept;
TL_MSABI int tl_VirtualProtect(void* address, std::uintptr_t size, std::uint32_t new_protection,
                               std::uint32_t* old_protection) noexcept;
TL_MSABI std::uintptr_t tl_VirtualQuery(const void* address, void* memory_information,
                                        std::uintptr_t length) noexcept;
TL_MSABI int tl_WideCharToMultiByte(std::uint32_t code_page, std::uint32_t flags,
                                     const std::uint16_t* wide_str, int wide_count, char* mb_str,
                                     int mb_count, const char* default_char,
                                     int* used_default_char) noexcept;
TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept;
TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept;
TL_MSABI int tl_GetModuleHandleExA(std::uint32_t flags, const char* module_name, void** module) noexcept;
TL_MSABI int tl_GetModuleHandleExW(std::uint32_t flags, const std::uint16_t* module_name, void** module) noexcept;
TL_MSABI void* tl_LoadLibraryA(const char* file_name) noexcept;
TL_MSABI void* tl_LoadLibraryW(const std::uint16_t* file_name) noexcept;
TL_MSABI void* tl_LoadLibraryExA(const char* file_name, void* file, std::uint32_t flags) noexcept;
TL_MSABI void* tl_LoadLibraryExW(const std::uint16_t* file_name, void* file, std::uint32_t flags) noexcept;
TL_MSABI int tl_FreeLibrary(void* module) noexcept;
TL_MSABI void* tl_GetProcAddress(void* module, const char* name) noexcept;
TL_MSABI int tl_GetVersionExA(void* version_information) noexcept;
TL_MSABI int tl_GetVersionExW(void* version_information) noexcept;
TL_MSABI int tl_VerifyVersionInfoW(void* version_information, std::uint32_t type_mask,
                                   std::uint64_t condition_mask) noexcept;
TL_MSABI std::uint64_t tl_VerSetConditionMask(std::uint64_t condition_mask, std::uint32_t type_mask,
                                              std::uint8_t condition) noexcept;
TL_MSABI int tl_GetUserDefaultLocaleName(std::uint16_t* locale_name, int locale_name_length) noexcept;
TL_MSABI std::uint32_t tl_LocaleNameToLCID(const std::uint16_t* name, std::uint32_t flags) noexcept;
TL_MSABI int tl_WaitOnAddress(void* address, void* compare_address, std::size_t address_size,
                              std::uint32_t milliseconds) noexcept;
TL_MSABI void tl_WakeByAddressSingle(void* address) noexcept;
TL_MSABI void tl_WakeByAddressAll(void* address) noexcept;
TL_MSABI const char* tl_GetCommandLineA() noexcept;
TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI int tl_SetEnvironmentVariableW(const std::uint16_t* name,
                                        const std::uint16_t* value) noexcept;
TL_MSABI std::uint16_t* tl_GetEnvironmentStringsW() noexcept;
TL_MSABI int tl_FreeEnvironmentStringsW(std::uint16_t* block) noexcept;
TL_MSABI std::uint32_t tl_ExpandEnvironmentStringsW(const std::uint16_t* source,
                                                     std::uint16_t* destination,
                                                     std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetACP() noexcept;
TL_MSABI std::uint32_t tl_GetOEMCP() noexcept;
TL_MSABI int tl_GetCPInfo(std::uint32_t code_page, abi::GuestCpInfo* info) noexcept;
TL_MSABI int tl_GetLocaleInfoW(std::uint32_t locale, std::uint32_t locale_type,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_GetLocaleInfoEx(const std::uint16_t* locale_name, std::uint32_t locale_type,
                                std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_IsValidLocale(std::uint32_t locale, std::uint32_t flags) noexcept;
TL_MSABI int tl_IsValidCodePage(std::uint32_t code_page) noexcept;
TL_MSABI int tl_EnumSystemLocalesW(std::uintptr_t callback, std::uint32_t flags) noexcept;
TL_MSABI int tl_GetStringTypeW(std::uint32_t info_type, const std::uint16_t* source,
                               int source_count, std::uint16_t* char_type) noexcept;
TL_MSABI int tl_GetDateFormatW(std::uint32_t locale, std::uint32_t flags,
                               const abi::GuestSystemTime* date, const std::uint16_t* format,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_GetTimeFormatW(std::uint32_t locale, std::uint32_t flags,
                               const abi::GuestSystemTime* time, const std::uint16_t* format,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_LCMapStringW(std::uint32_t locale, std::uint32_t flags,
                             const std::uint16_t* source, int source_count,
                             std::uint16_t* destination, int destination_count) noexcept;
TL_MSABI int tl_LCMapStringEx(const std::uint16_t* locale_name, std::uint32_t flags,
                              const std::uint16_t* source, int source_count,
                              std::uint16_t* destination, int destination_count,
                              const void* version_information, void* reserved,
                              std::uintptr_t sort_handle) noexcept;
TL_MSABI void* tl_GetProcessHeap() noexcept;
TL_MSABI void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept;
TL_MSABI int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept;
TL_MSABI void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                               std::uintptr_t new_size) noexcept;
TL_MSABI std::uint64_t tl_GetTickCount64() noexcept;
TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept;
TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept;
TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* size) noexcept;
TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept;
TL_MSABI int tl_SetFilePointerEx(const void* handle, std::int64_t distance,
                                 std::int64_t* new_position, std::uint32_t move_method) noexcept;
TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept;
TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_SetFileAttributesW(const std::uint16_t* path,
                                   std::uint32_t attributes) noexcept;
TL_MSABI int tl_GetFileAttributesExW(const std::uint16_t* path, int info_level,
                                     void* data) noexcept;
TL_MSABI int tl_DeleteFileA(const char* path) noexcept;
TL_MSABI int tl_DeleteFileW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept;
TL_MSABI int tl_MoveFileW(const std::uint16_t* from, const std::uint16_t* to) noexcept;
TL_MSABI int tl_MoveFileExW(const std::uint16_t* from, const std::uint16_t* to,
                            std::uint32_t flags) noexcept;
TL_MSABI int tl_CopyFileW(const std::uint16_t* from, const std::uint16_t* to,
                          int fail_if_exists) noexcept;
TL_MSABI int tl_CreateDirectoryA(const char* path, const void* security_attributes) noexcept;
TL_MSABI int tl_CreateDirectoryW(const std::uint16_t* path,
                                 const void* security_attributes) noexcept;
TL_MSABI int tl_RemoveDirectoryW(const std::uint16_t* path) noexcept;
TL_MSABI void* tl_FindFirstFileA(const char* path, void* find_data) noexcept;
TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept;
TL_MSABI void* tl_FindFirstFileExW(const std::uint16_t* path, int info_level,
                                   void* find_data, int search_operation,
                                   const void* search_filter,
                                   std::uint32_t additional_flags) noexcept;
TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindClose(const void* handle) noexcept;
TL_MSABI int tl_AreFileApisANSI() noexcept;
TL_MSABI std::uint32_t tl_FormatMessageW(std::uint32_t flags, const void* source,
                                         std::uint32_t message_id, std::uint32_t language_id,
                                         std::uint16_t* buffer, std::uint32_t size,
                                         const void* arguments) noexcept;
TL_MSABI std::uint32_t tl_FormatMessageA(std::uint32_t flags, const void* source,
                                         std::uint32_t message_id, std::uint32_t language_id,
                                         char* buffer, std::uint32_t size,
                                         const void* arguments) noexcept;
TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept;
TL_MSABI int tl_SetConsoleOutputCP(std::uint32_t code_page) noexcept;
TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           std::uint32_t unique, std::uint16_t* temp_file_name) noexcept;
TL_MSABI std::uint32_t tl_GetTempPathW(std::uint32_t buffer_length,
                                       std::uint16_t* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetFullPathNameW(const std::uint16_t* path, std::uint32_t buffer_length,
                                           std::uint16_t* buffer,
                                           std::uint16_t** file_part) noexcept;
TL_MSABI std::uint32_t tl_GetFullPathNameA(const char* path, std::uint32_t buffer_length, char* buffer,
                                          char** file_part) noexcept;
TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time, void* access_time,
                            void* write_time) noexcept;
TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept;
TL_MSABI int tl_GetFileInformationByHandle(const void* handle, void* information) noexcept;
TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, int info_class,
                                             void* buffer, std::uint32_t size) noexcept;
TL_MSABI int tl_SetFileInformationByHandle(const void* handle, int info_class,
                                           const void* buffer, std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle, std::uint16_t* buffer,
                                                    std::uint32_t buffer_length,
                                                    std::uint32_t flags) noexcept;
TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept;
TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept;
TL_MSABI void* tl_LockResource(const void* resource) noexcept;
TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept;
TL_MSABI void* tl_GlobalAlloc(std::uint32_t flags, std::size_t bytes) noexcept;
TL_MSABI void* tl_GlobalLock(void* memory) noexcept;
TL_MSABI int tl_GlobalUnlock(void* memory) noexcept;
TL_MSABI void* tl_GlobalFree(void* memory) noexcept;
TL_MSABI void* tl_LocalAlloc(std::uint32_t flags, std::size_t bytes) noexcept;
TL_MSABI void* tl_LocalFree(void* memory) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(std::uint32_t buffer_length,
                                                char* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentDirectoryW(std::uint32_t buffer_length,
                                                std::uint16_t* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetModuleFileNameA(const void* module_handle, char* buffer,
                                              std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetModuleFileNameW(const void* module_handle, std::uint16_t* buffer,
                                              std::uint32_t size) noexcept;
TL_MSABI void* tl_CreateThread(const void* security_attributes, std::uintptr_t stack_size,
                                std::uintptr_t start_address, void* parameter,
                                std::uint32_t creation_flags, std::uint32_t* thread_id) noexcept;
TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* handle,
                                               std::uint32_t milliseconds) noexcept;
TL_MSABI std::uint32_t tl_WaitForMultipleObjects(std::uint32_t count, const void* const* handles,
                                                 int wait_all, std::uint32_t milliseconds) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept;
TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept;
TL_MSABI void* tl_GetCurrentProcess() noexcept;
TL_MSABI std::uint32_t tl_TlsAlloc() noexcept;
TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* tls_value) noexcept;
TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept;
TL_MSABI std::uint32_t tl_FlsAlloc(std::uintptr_t callback) noexcept;
TL_MSABI int tl_FlsFree(std::uint32_t fls_index) noexcept;
TL_MSABI void* tl_FlsGetValue(std::uint32_t fls_index) noexcept;
TL_MSABI int tl_FlsSetValue(std::uint32_t fls_index, void* value) noexcept;
TL_MSABI int tl_QueryPerformanceCounter(std::int64_t* performance_count) noexcept;
TL_MSABI int tl_QueryPerformanceFrequency(std::int64_t* frequency) noexcept;
TL_MSABI void tl_GetSystemInfo(void* system_info) noexcept;
TL_MSABI void tl_GetNativeSystemInfo(void* system_info) noexcept;
TL_MSABI int tl_GlobalMemoryStatusEx(void* buffer) noexcept;
TL_MSABI void* tl_CreateFileMappingA(const void* file, const void* file_mapping_attributes,
                                     std::uint32_t protect, std::uint32_t maximum_size_high,
                                     std::uint32_t maximum_size_low, const char* name) noexcept;
TL_MSABI void* tl_CreateFileMappingW(const void* file, const void* file_mapping_attributes,
                                     std::uint32_t protect, std::uint32_t maximum_size_high,
                                     std::uint32_t maximum_size_low, const std::uint16_t* name) noexcept;
TL_MSABI void* tl_MapViewOfFile(const void* file_mapping_object, std::uint32_t desired_access,
                                std::uint32_t file_offset_high, std::uint32_t file_offset_low,
                                std::size_t number_of_bytes_to_map) noexcept;
TL_MSABI int tl_UnmapViewOfFile(const void* base_address) noexcept;
TL_MSABI int tl_FlushViewOfFile(const void* base_address, std::size_t number_of_bytes_to_flush) noexcept;
TL_MSABI int tl_GetDiskFreeSpaceExA(const char* directory_name, std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes, std::uint64_t* total_number_of_free_bytes) noexcept;
TL_MSABI int tl_GetDiskFreeSpaceExW(const std::uint16_t* directory_name, std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes, std::uint64_t* total_number_of_free_bytes) noexcept;
TL_MSABI std::uint32_t tl_GetDriveTypeA(const char* root_path_name) noexcept;
TL_MSABI std::uint32_t tl_GetDriveTypeW(const std::uint16_t* root_path_name) noexcept;
TL_MSABI int tl_GetVolumeInformationA(const char* root_path_name, char* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      char* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept;
TL_MSABI int tl_GetVolumeInformationW(const std::uint16_t* root_path_name, std::uint16_t* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      std::uint16_t* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept;
TL_MSABI void tl_GetSystemTime(void* system_time) noexcept;
TL_MSABI void tl_GetLocalTime(void* system_time) noexcept;
TL_MSABI int tl_FileTimeToSystemTime(const void* file_time, void* system_time) noexcept;
TL_MSABI int tl_SystemTimeToFileTime(const void* system_time, void* file_time) noexcept;
TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept;
TL_MSABI int tl_SetFilePointerEx(const void* handle, std::int64_t distance_to_move,
                                 std::int64_t* new_file_pointer, std::uint32_t move_method) noexcept;
TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* file_size) noexcept;
TL_MSABI int tl_CompareStringA(std::uint32_t locale, std::uint32_t flags,
                               const char* string1, int count1, const char* string2, int count2) noexcept;
TL_MSABI int tl_CompareStringW(std::uint32_t locale, std::uint32_t flags,
                               const std::uint16_t* string1, int count1, const std::uint16_t* string2, int count2) noexcept;
TL_MSABI std::uint32_t tl_GetUserDefaultLCID() noexcept;
TL_MSABI std::uint32_t tl_GetSystemDefaultLCID() noexcept;
TL_MSABI int tl_GetComputerNameA(char* buffer, std::uint32_t* size) noexcept;
TL_MSABI int tl_GetComputerNameW(std::uint16_t* buffer, std::uint32_t* size) noexcept;
TL_MSABI void tl_InitializeSRWLock(void* srw_lock) noexcept;
TL_MSABI void tl_AcquireSRWLockExclusive(void* srw_lock) noexcept;
TL_MSABI void tl_ReleaseSRWLockExclusive(void* srw_lock) noexcept;
TL_MSABI void tl_AcquireSRWLockShared(void* srw_lock) noexcept;
TL_MSABI void tl_ReleaseSRWLockShared(void* srw_lock) noexcept;
TL_MSABI int tl_SleepConditionVariableSRW(void* cond, void* srw_lock,
                                          std::uint32_t milliseconds, std::uint32_t flags) noexcept;
TL_MSABI void tl_WakeConditionVariable(void* cond) noexcept;
TL_MSABI void tl_WakeAllConditionVariable(void* cond) noexcept;
TL_MSABI void* tl_AddVectoredExceptionHandler(std::uint32_t first, void* handler) noexcept;
TL_MSABI std::uint32_t tl_RemoveVectoredExceptionHandler(void* handle) noexcept;
TL_MSABI void tl_RaiseException(std::uint32_t exception_code, std::uint32_t exception_flags,
                                std::uint32_t number_of_arguments, const std::uint64_t* arguments) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileStringA(const char* app_name, const char* key_name,
                                                   const char* default_val, char* returned_string,
                                                   std::uint32_t size, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                   const std::uint16_t* default_val, std::uint16_t* returned_string,
                                                   std::uint32_t size, const std::uint16_t* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileIntA(const char* app_name, const char* key_name,
                                                int default_val, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileIntW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                int default_val, const std::uint16_t* file_name) noexcept;
TL_MSABI int tl_WritePrivateProfileStringA(const char* app_name, const char* key_name,
                                           const char* string_val, const char* file_name) noexcept;
TL_MSABI int tl_WritePrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                           const std::uint16_t* string_val, const std::uint16_t* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionA(const char* app_name, char* returned_string,
                                                    std::uint32_t size, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionW(const std::uint16_t* app_name, std::uint16_t* returned_string,
                                                    std::uint32_t size, const std::uint16_t* file_name) noexcept;
TL_MSABI int tl_GetConsoleScreenBufferInfo(const void* console_handle, void* buffer_info) noexcept;
TL_MSABI int tl_SetConsoleTextAttribute(const void* console_handle, std::uint16_t attributes) noexcept;
TL_MSABI void* tl_CreateThreadpoolWork(void* callback, void* context, void* environment) noexcept;
TL_MSABI void tl_SubmitThreadpoolWork(void* work) noexcept;
TL_MSABI void tl_WaitForThreadpoolWorkCallbacks(void* work, int cancel_pending) noexcept;
TL_MSABI void tl_CloseThreadpoolWork(void* work) noexcept;
TL_MSABI void* tl_CreateThreadpoolTimer(void* callback, void* context, void* environment) noexcept;
TL_MSABI void tl_SetThreadpoolTimer(void* timer, const void* due_time, std::uint32_t period, std::uint32_t window_length) noexcept;
TL_MSABI void tl_WaitForThreadpoolTimerCallbacks(void* timer, int cancel_pending) noexcept;
TL_MSABI void tl_CloseThreadpoolTimer(void* timer) noexcept;
TL_MSABI void* tl_ConvertThreadToFiber(void* parameter) noexcept;
TL_MSABI void* tl_ConvertThreadToFiberEx(void* parameter, std::uint32_t flags) noexcept;
TL_MSABI int tl_ConvertFiberToThread() noexcept;
TL_MSABI void* tl_CreateFiber(std::size_t stack_size, void* start_address, void* parameter) noexcept;
TL_MSABI void* tl_CreateFiberEx(std::size_t stack_commit, std::size_t stack_reserve, std::uint32_t flags,
                                void* start_address, void* parameter) noexcept;
TL_MSABI void tl_SwitchToFiber(void* fiber) noexcept;
TL_MSABI void tl_DeleteFiber(void* fiber) noexcept;
TL_MSABI void* tl_GetFiberData() noexcept;
TL_MSABI void* tl_HeapCreate(std::uint32_t options, std::size_t initial_size, std::size_t maximum_size) noexcept;
TL_MSABI int tl_HeapDestroy(void* heap) noexcept;
TL_MSABI int tl_HeapValidate(void* heap, std::uint32_t flags, const void* memory) noexcept;
TL_MSABI std::size_t tl_HeapSize(void* heap, std::uint32_t flags, const void* memory) noexcept;
TL_MSABI std::size_t tl_HeapCompact(void* heap, std::uint32_t flags) noexcept;
TL_MSABI void* tl_CreateToolhelp32Snapshot(std::uint32_t flags, std::uint32_t process_id) noexcept;
TL_MSABI int tl_Process32FirstW(void* snapshot, void* entry) noexcept;
TL_MSABI int tl_Process32NextW(void* snapshot, void* entry) noexcept;
TL_MSABI void* tl_OpenProcess(std::uint32_t desired_access, int inherit_handle, std::uint32_t process_id) noexcept;
TL_MSABI void tl_OutputDebugStringA(const char* output_string) noexcept;
TL_MSABI void tl_OutputDebugStringW(const std::uint16_t* output_string) noexcept;
TL_MSABI int tl_SetDllDirectoryW(const std::uint16_t* path_name) noexcept;
TL_MSABI std::size_t tl_VirtualQueryEx(const void* process_handle, const void* address,
                                       void* buffer, std::size_t length) noexcept;
TL_MSABI std::uint32_t tl_GetTimeZoneInformation(void* tz_info) noexcept;
TL_MSABI std::uint32_t tl_GetProcessId(const void* process) noexcept;
TL_MSABI int tl_QueryFullProcessImageNameW(const void* process, std::uint32_t flags,
                                           std::uint16_t* exe_name, std::uint32_t* size) noexcept;
TL_MSABI int tl_FileTimeToLocalFileTime(const void* file_time, void* local_file_time) noexcept;
TL_MSABI std::uint32_t tl_GetLongPathNameW(const std::uint16_t* short_path,
                                           std::uint16_t* long_path, std::uint32_t buffer_length) noexcept;
TL_MSABI std::uint32_t tl_GetShortPathNameW(const std::uint16_t* long_path,
                                            std::uint16_t* short_path, std::uint32_t buffer_length) noexcept;
TL_MSABI int tl_SetThreadPriority(const void* thread_handle, int priority) noexcept;
TL_MSABI int tl_GetProcessAffinityMask(const void* process_handle, std::uintptr_t* process_affinity_mask,
                                       std::uintptr_t* system_affinity_mask) noexcept;
TL_MSABI int tl_CreateHardLinkW(const std::uint16_t* new_file_name,
                                const std::uint16_t* existing_file_name,
                                void* security_attributes) noexcept;
TL_MSABI std::uint32_t tl_K32GetModuleFileNameExW(const void* process,
                                                  const void* module_handle,
                                                  std::uint16_t* filename,
                                                  std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetTickCount(void) noexcept;
TL_MSABI int tl_SetCurrentDirectoryW(const std::uint16_t* path_name) noexcept;
TL_MSABI int tl_DeviceIoControl(void* device, std::uint32_t io_control_code, void* in_buffer,
                                std::uint32_t in_buffer_size, void* out_buffer,
                                std::uint32_t out_buffer_size, std::uint32_t* bytes_returned,
                                void* overlapped) noexcept;
TL_MSABI int tl_FoldStringW(std::uint32_t map_flags, const std::uint16_t* src_str, int cch_src,
                            std::uint16_t* dest_str, int cch_dest) noexcept;
TL_MSABI std::uint32_t tl_SetThreadExecutionState(std::uint32_t es_flags) noexcept;
TL_MSABI int tl_AllocConsole(void) noexcept;
TL_MSABI int tl_AttachConsole(std::uint32_t process_id) noexcept;
TL_MSABI int tl_FreeConsole(void) noexcept;
TL_MSABI int tl_SystemTimeToTzSpecificLocalTime(const void* tz_info, const void* universal_time,
                                               void* local_time) noexcept;
TL_MSABI int tl_IsDBCSLeadByte(std::uint8_t test_char) noexcept;
TL_MSABI int tl_GetNumberFormatW(std::uint32_t locale, std::uint32_t flags,
                                 const std::uint16_t* value, const void* format,
                                 std::uint16_t* number_str, int cch_number) noexcept;
TL_MSABI std::uint32_t tl_GetVersion(void) noexcept;
TL_MSABI std::size_t tl_GetLargePageMinimum(void) noexcept;
TL_MSABI void tl_SetFileApisToOEM(void) noexcept;
TL_MSABI int tl_SetConsoleCtrlHandler(void* handler, int add) noexcept;
TL_MSABI int tl_GetProcessTimes(void* process, void* creation_time, void* exit_time,
                                void* kernel_time, void* user_time) noexcept;
TL_MSABI int tl_SetProcessAffinityMask(void* process, std::uintptr_t process_affinity_mask) noexcept;
TL_MSABI std::uintptr_t tl_SetThreadAffinityMask(void* thread, std::uintptr_t thread_affinity_mask) noexcept;
TL_MSABI std::uint32_t tl_ResumeThread(void* thread) noexcept;
TL_MSABI void* tl_OpenEventW(std::uint32_t desired_access, int inherit_handle,
                             const std::uint16_t* name) noexcept;
TL_MSABI void* tl_OpenFileMappingW(std::uint32_t desired_access, int inherit_handle,
                                   const std::uint16_t* name) noexcept;
TL_MSABI int tl_FileTimeToDosDateTime(const void* file_time, std::uint16_t* fat_date,
                                       std::uint16_t* fat_time) noexcept;
TL_MSABI int tl_DosDateTimeToFileTime(std::uint16_t fat_date, std::uint16_t fat_time, void* file_time) noexcept;
TL_MSABI std::int32_t tl_CompareFileTime(const void* file_time1, const void* file_time2) noexcept;
TL_MSABI int tl_GetDiskFreeSpaceW(const std::uint16_t* root_path_name,
                                  std::uint32_t* sectors_per_cluster,
                                  std::uint32_t* bytes_per_sector,
                                  std::uint32_t* number_of_free_clusters,
                                  std::uint32_t* total_number_of_clusters) noexcept;
TL_MSABI void* tl_FindFirstStreamW(const std::uint16_t* file_name, int info_level,
                                   void* find_stream_data, std::uint32_t flags) noexcept;
TL_MSABI int tl_FindNextStreamW(void* find_stream, void* find_stream_data) noexcept;
TL_MSABI std::uint32_t tl_GetLogicalDriveStringsW(std::uint32_t buffer_length,
                                                  std::uint16_t* buffer) noexcept;
TL_MSABI int tl_SetNamedPipeHandleState(void* named_pipe, std::uint32_t* mode,
                                        std::uint32_t* max_collection_count,
                                        std::uint32_t* collect_data_timeout) noexcept;
TL_MSABI int tl_TransactNamedPipe(void* named_pipe, void* in_buffer, std::uint32_t in_buffer_size,
                                  void* out_buffer, std::uint32_t out_buffer_size,
                                  std::uint32_t* bytes_read, void* overlapped) noexcept;
TL_MSABI int tl_WaitNamedPipeW(const std::uint16_t* named_pipe_name, std::uint32_t time_out) noexcept;
TL_MSABI int tl_PeekNamedPipe(void* named_pipe, void* buffer, std::uint32_t buffer_size,
                              std::uint32_t* bytes_read, std::uint32_t* total_bytes_avail,
                              std::uint32_t* bytes_left_this_message) noexcept;
TL_MSABI std::uint32_t tl_WaitForSingleObjectEx(void* handle, std::uint32_t milliseconds,
                                                int alertable) noexcept;
TL_MSABI int tl_GetExitCodeThread(void* thread, std::uint32_t* exit_code) noexcept;
TL_MSABI int tl_TryAcquireSRWLockExclusive(void* srw_lock) noexcept;
TL_MSABI void tl_FreeLibraryAndExitThread(void* module_handle, std::uint32_t exit_code) noexcept;
TL_MSABI int tl_SetThreadLocale(std::uint32_t locale) noexcept;
TL_MSABI std::uint16_t tl_SetThreadUILanguage(std::uint16_t lang_id) noexcept;
TL_MSABI std::uint16_t tl_GetUserDefaultUILanguage(void) noexcept;
TL_MSABI std::uint32_t tl_GetLogicalDrives(void) noexcept;
TL_MSABI int tl_GetPhysicallyInstalledSystemMemory(std::uint64_t* total_memory_in_kilobytes) noexcept;
TL_MSABI int tl_GetVolumePathNameA(const char* file_name, char* volume_path_name,
                                   std::uint32_t buffer_length) noexcept;
TL_MSABI int tl_TzSpecificLocalTimeToSystemTime(const void* tz_info, const void* local_time,
                                               void* universal_time) noexcept;
TL_MSABI int tl_UnregisterWaitEx(void* wait_handle, void* completion_event) noexcept;
TL_MSABI int tl_RegisterWaitForSingleObject(void** ph_new_wait_object, void* h_object,
                                            void* callback, void* context,
                                            std::uint32_t ms, std::uint32_t flags) noexcept;
TL_MSABI int tl_SetSearchPathMode(std::uint32_t flags) noexcept;
TL_MSABI void* tl_InterlockedPushEntrySList(void* list_head, void* list_entry) noexcept;
TL_MSABI int tl_CopyFileExW(const std::uint16_t* existing_file, const std::uint16_t* new_file,
                            void* progress_routine, void* data, int* cancel, std::uint32_t flags) noexcept;
TL_MSABI int tl_MoveFileWithProgressW(const std::uint16_t* existing_file, const std::uint16_t* new_file,
                                     void* progress_routine, void* data, std::uint32_t flags) noexcept;
TL_MSABI std::uint32_t tl_GetCompressedFileSizeW(const std::uint16_t* file_name, std::uint32_t* high) noexcept;
TL_MSABI void* tl_FindFirstChangeNotificationW(const std::uint16_t* path, int watch_subtree, std::uint32_t notify_filter) noexcept;
TL_MSABI int tl_FindNextChangeNotification(void* handle) noexcept;
TL_MSABI int tl_FindCloseChangeNotification(void* handle) noexcept;
TL_MSABI std::uint16_t tl_GetSystemDefaultLangID() noexcept;
TL_MSABI std::uint16_t tl_GetUserDefaultLangID() noexcept;
TL_MSABI std::uint32_t tl_GetWindowsDirectoryW(std::uint16_t* buffer, std::uint32_t size) noexcept;
TL_MSABI std::size_t tl_GlobalSize(void* mem) noexcept;
TL_MSABI int tl_SetPriorityClass(void* process, std::uint32_t priority_class) noexcept;
TL_MSABI int tl_lstrlenW(const std::uint16_t* str) noexcept;
TL_MSABI int tl_K32GetProcessMemoryInfo(void* process, void* counters, std::uint32_t cb) noexcept;
TL_MSABI std::uint32_t tl_K32GetProcessImageFileNameA(void* process, char* image_file_name, std::uint32_t size) noexcept;
TL_MSABI int tl_Process32First(void* snapshot, void* entry) noexcept;
TL_MSABI int tl_Process32Next(void* snapshot, void* entry) noexcept;
TL_MSABI int tl_DuplicateHandle(void* src_process, void* src_handle, void* target_process, void** target_handle, std::uint32_t desired_access, int inherit_handle, std::uint32_t options) noexcept;
TL_MSABI int tl_LockFile(void* file, std::uint32_t offset_low, std::uint32_t offset_high, std::uint32_t count_low, std::uint32_t count_high) noexcept;
TL_MSABI int tl_LockFileEx(void* file, std::uint32_t flags, std::uint32_t reserved, std::uint32_t count_low, std::uint32_t count_high, void* overlapped) noexcept;
TL_MSABI int tl_UnlockFile(void* file, std::uint32_t offset_low, std::uint32_t offset_high, std::uint32_t count_low, std::uint32_t count_high) noexcept;
TL_MSABI int tl_UnlockFileEx(void* file, std::uint32_t reserved, std::uint32_t count_low, std::uint32_t count_high, void* overlapped) noexcept;
TL_MSABI int tl_GetDiskFreeSpaceA(const char* root_path_name, std::uint32_t* sectors_per_cluster, std::uint32_t* bytes_per_sector, std::uint32_t* number_of_free_clusters, std::uint32_t* total_number_of_clusters) noexcept;
TL_MSABI std::uint32_t tl_GetTempPathA(std::uint32_t buffer_length, char* buffer) noexcept;
TL_MSABI int tl_MoveFileExA(const char* existing_file, const char* new_file, std::uint32_t flags) noexcept;
TL_MSABI std::uint32_t tl_SleepEx(std::uint32_t milliseconds, int alertable) noexcept;
TL_MSABI std::uint32_t tl_WaitForMultipleObjectsEx(std::uint32_t count, const void* const* handles, int wait_all, std::uint32_t milliseconds, int alertable) noexcept;
TL_MSABI int tl_TryEnterCriticalSection(void* critical_section) noexcept;
TL_MSABI void tl_InitializeConditionVariable(void* condition_variable) noexcept;
TL_MSABI int tl_SleepConditionVariableCS(void* condition_variable, void* critical_section, std::uint32_t milliseconds) noexcept;
TL_MSABI void* tl_FindResourceExW(void* module, const wchar_t* type, const wchar_t* name, std::uint16_t language) noexcept;
TL_MSABI int tl_CompareStringEx(const wchar_t* locale_name, std::uint32_t flags, const wchar_t* string1, int count1, const wchar_t* string2, int count2, void* version_information, void* reserved, std::intptr_t param) noexcept;
TL_MSABI void* tl_CreateFile2(const wchar_t* file_name, std::uint32_t desired_access, std::uint32_t share_mode, std::uint32_t creation_disposition, void* create_parameters) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentProcessorNumber() noexcept;
TL_MSABI int tl_InitializeProcThreadAttributeList(void* attribute_list, std::uint32_t attribute_count, std::uint32_t flags, std::size_t* size) noexcept;
TL_MSABI int tl_UpdateProcThreadAttribute(void* attribute_list, std::uint32_t flags, std::uintptr_t attribute, void* value, std::size_t size, void* previous_value, std::size_t* return_size) noexcept;
TL_MSABI std::uint32_t tl_GetSystemDirectoryA(char* buffer, std::uint32_t size) noexcept;
TL_MSABI int tl_ReadConsoleA(void* console_input, void* buffer, std::uint32_t number_of_chars_to_read, std::uint32_t* number_of_chars_read, void* input_control) noexcept;
TL_MSABI void* tl_CreateWaitableTimerA(void* timer_attributes, int manual_reset, const char* timer_name) noexcept;
TL_MSABI void* tl_CreateWaitableTimerW(void* timer_attributes, int manual_reset, const wchar_t* timer_name) noexcept;
TL_MSABI int tl_SetWaitableTimer(void* timer, const std::int64_t* due_time, std::int32_t period, void* completion_routine, void* arg_to_completion_routine, int resume) noexcept;
TL_MSABI int tl_CancelWaitableTimer(void* timer) noexcept;
TL_MSABI int tl_GetLogicalProcessorInformation(void* buffer, std::uint32_t* returned_length) noexcept;
TL_MSABI int tl_GetVolumePathNameW(const wchar_t* file_name, wchar_t* volume_path_name, std::uint32_t buffer_length) noexcept;
TL_MSABI std::int32_t tl_SetThreadDescription(void* thread, const wchar_t* description) noexcept;
TL_MSABI void* tl_GetCurrentThread() noexcept;
TL_MSABI void* tl_CreateSemaphoreExW(void* semaphore_attributes, std::int32_t initial_count, std::int32_t maximum_count, const wchar_t* name, std::uint32_t flags, std::uint32_t desired_access) noexcept;
TL_MSABI void* tl_OpenSemaphoreW(std::uint32_t desired_access, int inherit_handle, const wchar_t* name) noexcept;
TL_MSABI void* tl_CreateMutexExW(void* mutex_attributes, const wchar_t* name, std::uint32_t flags, std::uint32_t desired_access) noexcept;
TL_MSABI void tl_DebugBreak() noexcept;
TL_MSABI int tl_InitOnceBeginInitialize(void* init_once, std::uint32_t flags, int* pending, void** context) noexcept;
TL_MSABI int tl_InitOnceComplete(void* init_once, std::uint32_t flags, void* context) noexcept;
TL_MSABI int tl_SwitchToThread() noexcept;
TL_MSABI std::uint32_t tl_GetSystemFirmwareTable(std::uint32_t firmware_table_provider_signature, std::uint32_t firmware_table_id, void* firmware_table_buffer, std::uint32_t buffer_size) noexcept;
TL_MSABI int tl_Beep(std::uint32_t freq, std::uint32_t duration) noexcept;
TL_MSABI int tl_ClearCommBreak(void* file) noexcept;
TL_MSABI int tl_ConnectNamedPipe(void* named_pipe, void* overlapped) noexcept;
TL_MSABI void* tl_CreateNamedPipeA(const char* name, std::uint32_t open_mode, std::uint32_t pipe_mode, std::uint32_t max_instances, std::uint32_t out_buf_size, std::uint32_t in_buf_size, std::uint32_t default_time_out, void* sec_attr) noexcept;
TL_MSABI int tl_CreatePipe(void** read_pipe, void** write_pipe, void* pipe_attr, std::uint32_t size) noexcept;
TL_MSABI void* tl_FindResourceA(void* module, const char* name, const char* type) noexcept;
TL_MSABI int tl_GetCommState(void* file, void* dcb) noexcept;
TL_MSABI int tl_GetLocaleInfoA(std::uint32_t lcid, std::uint32_t lctype, char* lcdata, int cch_data) noexcept;
TL_MSABI int tl_GetOverlappedResult(void* file, void* overlapped, std::uint32_t* bytes_transferred, int wait) noexcept;
TL_MSABI int tl_GetThreadTimes(void* thread, void* creation_time, void* exit_time, void* kernel_time, void* user_time) noexcept;
TL_MSABI std::uint32_t tl_GetWindowsDirectoryA(char* buffer, std::uint32_t size) noexcept;
TL_MSABI void tl_GlobalMemoryStatus(void* buffer) noexcept;
TL_MSABI int tl_LocalFileTimeToFileTime(const void* local_file_time, void* file_time) noexcept;
TL_MSABI int tl_SetCommBreak(void* file) noexcept;
TL_MSABI int tl_SetCommState(void* file, void* dcb) noexcept;
TL_MSABI int tl_SetCommTimeouts(void* file, void* timeouts) noexcept;
TL_MSABI int tl_SetCurrentDirectoryA(const char* path_name) noexcept;
TL_MSABI int tl_SetHandleInformation(void* object, std::uint32_t mask, std::uint32_t flags) noexcept;
TL_MSABI int tl_WaitNamedPipeA(const char* name, std::uint32_t timeout) noexcept;
TL_MSABI int tl_GetTimeFormatEx(const wchar_t* lpLocaleName, std::uint32_t dwFlags, const void* lpTime, const wchar_t* lpFormat, wchar_t* lpTimeStr, int cchTime) noexcept;
TL_MSABI int tl_GetDateFormatEx(const wchar_t* lpLocaleName, std::uint32_t dwFlags, const void* lpDate, const wchar_t* lpFormat, wchar_t* lpDateStr, int cchDate, const wchar_t* lpCalendar) noexcept;
TL_MSABI std::uint16_t* tl_lstrcpynW(std::uint16_t* lpString1, const std::uint16_t* lpString2,
                                     int iMaxLength) noexcept;
TL_MSABI int tl_GetApplicationRestartSettings(void* hProcess, wchar_t* pwzCommandLine, std::uint32_t* pcchSize, std::uint32_t* pdwFlags) noexcept;
TL_MSABI int tl_UnregisterApplicationRestart() noexcept;
TL_MSABI int tl_lstrcmpiA(const char* lpString1, const char* lpString2) noexcept;
TL_MSABI int tl_RegisterApplicationRestart(const wchar_t* pwzCommandLine, std::uint32_t dwFlags) noexcept;
TL_MSABI char* tl_lstrcpynA(char* lpString1, const char* lpString2, int iMaxLength) noexcept;
TL_MSABI int tl_CancelIo(void* hFile) noexcept;
TL_MSABI int tl_ReadDirectoryChangesW(void* hDirectory, void* lpBuffer, std::uint32_t nBufferLength, int bWatchSubtree, std::uint32_t dwNotifyFilter, std::uint32_t* lpBytesReturned, void* lpOverlapped, void* lpCompletionRoutine) noexcept;
TL_MSABI int tl_GetStringTypeExW(std::uint32_t Locale, std::uint32_t dwInfoType, const wchar_t* lpSrcStr, int cchSrc, std::uint16_t* lpCharType) noexcept;
TL_MSABI int tl_LCMapStringA(std::uint32_t Locale, std::uint32_t dwMapFlags, const char* lpSrcStr, int cchSrc, char* lpDestStr, int cchDest) noexcept;
TL_MSABI int tl_GetStringTypeExA(std::uint32_t Locale, std::uint32_t dwInfoType, const char* lpSrcStr, int cchSrc, std::uint16_t* lpCharType) noexcept;
TL_MSABI void tl_FreeLibraryWhenCallbackReturns(void* pci, void* module) noexcept;
TL_MSABI std::uint16_t* tl_lstrcpyW(std::uint16_t* lpString1,
                                    const std::uint16_t* lpString2) noexcept;
TL_MSABI int tl_ReplaceFileW(const wchar_t* lpReplacedFileName, const wchar_t* lpReplacementFileName, const wchar_t* lpBackupFileName, std::uint32_t dwReplaceFlags, void* lpExclude, void* lpReserved) noexcept;
TL_MSABI std::uint32_t tl_QueueUserAPC(void* pfnAPC, void* hThread, std::uintptr_t dwData) noexcept;
TL_MSABI int tl_lstrcmpW(const std::uint16_t* lpString1,
                         const std::uint16_t* lpString2) noexcept;
TL_MSABI int tl_lstrcmpiW(const std::uint16_t* lpString1,
                          const std::uint16_t* lpString2) noexcept;
TL_MSABI void* tl_CreateRemoteThread(void* process, void* attr, std::size_t stack, void* start, void* param, std::uint32_t flags, std::uint32_t* tid) noexcept;
TL_MSABI void* tl_VirtualAllocEx(void* process, void* addr, std::size_t size, std::uint32_t type, std::uint32_t protect) noexcept;
TL_MSABI int tl_VirtualFreeEx(void* process, void* addr, std::size_t size, std::uint32_t type) noexcept;
TL_MSABI int tl_WriteProcessMemory(void* process, void* base, const void* buf, std::size_t size, std::size_t* written) noexcept;
TL_MSABI int tl_OpenFile(const char* file, void* of_struct, std::uint32_t style) noexcept;
TL_MSABI void* tl_OpenEventA(std::uint32_t access, int inherit, const char* name) noexcept;
TL_MSABI void* tl_OpenFileMappingA(std::uint32_t access, int inherit, const char* name) noexcept;
TL_MSABI int tl__lclose(int fd) noexcept;
TL_MSABI int tl_FlushInstructionCache(void* process, const void* base, std::size_t size) noexcept;
TL_MSABI int tl_SetThreadContext(void* thread, const void* ctx) noexcept;
TL_MSABI int tl_GetThreadContext(void* thread, void* ctx) noexcept;
TL_MSABI std::uint32_t tl_SuspendThread(void* thread) noexcept;
TL_MSABI int tl_VirtualProtectEx(void* process, void* addr, std::size_t size, std::uint32_t prot, std::uint32_t* old) noexcept;
TL_MSABI int tl_lstrcmpA(const char* s1, const char* s2) noexcept;
TL_MSABI int tl_IsThreadAFiber(void) noexcept;
TL_MSABI void* tl_InterlockedFlushSList(void* head) noexcept;

}  // extern "C"
}  // namespace tradutorlinux
