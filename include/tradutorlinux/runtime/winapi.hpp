#pragma once

#include "tradutorlinux/win32/types.hpp"
#include "tradutorlinux/win32/kernel32.hpp"
#include "tradutorlinux/win32/user32.hpp"
#include "tradutorlinux/win32/gdi32.hpp"

namespace tradutorlinux {
extern "C" {

TL_MSABI int tl_GdiplusStartup(void* token, const void* input, void* output) noexcept;
TL_MSABI void tl_GdiplusShutdown(void* token) noexcept;
TL_MSABI void* tl_GdipAlloc(std::size_t size) noexcept;
TL_MSABI void tl_GdipFree(void* ptr) noexcept;
TL_MSABI int tl_GdipCreateBitmapFromStream(void* stream, void** bitmap) noexcept;
TL_MSABI int tl_GdipCloneImage(void* image, void** clone) noexcept;
TL_MSABI int tl_GdipDisposeImage(void* image) noexcept;
TL_MSABI int tl_GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, std::uint32_t background) noexcept;
TL_MSABI int tl_SetWindowTheme(void* hwnd, const std::uint16_t* subAppName, const std::uint16_t* subIdList) noexcept;
TL_MSABI std::uint32_t tl_timeSetEvent(std::uint32_t delay, std::uint32_t resolution, void* callback,
                                       std::uintptr_t user, std::uint32_t event) noexcept;
TL_MSABI int tl_SymFromAddr(void* process, std::uint64_t address, std::uint64_t* displacement,
                            void* symbol) noexcept;
TL_MSABI std::uint32_t tl_PowerGetActiveScheme(void* UserRootPowerKey, void** ActivePolicyGuid) noexcept;
TL_MSABI std::uint32_t tl_PowerSetActiveScheme(void* UserRootPowerKey, const void* SchemeGuid) noexcept;
TL_MSABI std::uint32_t tl_CallNtPowerInformation(int InformationLevel, void* InputBuffer,
                                                 std::uint32_t InputBufferLength, void* OutputBuffer,
                                                 std::uint32_t OutputBufferLength) noexcept;
TL_MSABI std::uint32_t tl_GetAdaptersInfo(void* AdapterInfo, std::uint32_t* OutBufLen) noexcept;
TL_MSABI std::uint32_t tl_GetAdaptersAddresses(std::uint32_t Family, std::uint32_t Flags, void* Reserved,
                                               void* AdapterAddresses, std::uint32_t* SizePointer) noexcept;
TL_MSABI std::uint32_t tl_if_nametoindex(const char* ifname) noexcept;
TL_MSABI int tl_CryptAcquireContextA(void** prov_handle, const char* container,
                                     const char* provider, std::uint32_t prov_type, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptAcquireContextW(void** prov_handle, const std::uint16_t* container,
                                     const std::uint16_t* provider, std::uint32_t prov_type, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptGenRandom(void* prov_handle, std::uint32_t length, std::uint8_t* buffer) noexcept;
TL_MSABI int tl_CryptReleaseContext(void* prov_handle, std::uint32_t flags) noexcept;
TL_MSABI std::uint16_t** tl_CommandLineToArgvW(const std::uint16_t* command_line,
                                               int* argument_count) noexcept;
TL_MSABI int tl_ShellNotifyIconA(std::uint32_t message, void* data) noexcept;
TL_MSABI int tl_SHGetKnownFolderPath(const void* rfid, std::uint32_t flags, void* token,
                                     std::uint16_t** path) noexcept;
TL_MSABI int tl_SHGetFolderPathW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                 std::uint16_t* path) noexcept;
TL_MSABI int tl_SHGetFolderPathAndSubDirW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                          const std::uint16_t* sub_dir, std::uint16_t* path) noexcept;
TL_MSABI void* tl_ShellExecuteW(void* hwnd, const std::uint16_t* operation,
                                const std::uint16_t* file, const std::uint16_t* parameters,
                                const std::uint16_t* directory, int show) noexcept;
TL_MSABI int tl_ShellExecuteExW(void* exec_info) noexcept;
TL_MSABI int tl_SHAutoComplete(const void* hwnd_edit, std::uint32_t flags) noexcept;
TL_MSABI int tl_PathRemoveFileSpecW(std::uint16_t* path) noexcept;
TL_MSABI std::uint16_t* tl_PathCombineW(std::uint16_t* dest, const std::uint16_t* dir,
                                        const std::uint16_t* file) noexcept;
TL_MSABI int tl_PathIsRelativeA(const char* path) noexcept;
TL_MSABI int tl_PathIsRelativeW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_SHFileOperationW(void* file_op) noexcept;
TL_MSABI int tl_LookupPrivilegeValueW(const std::uint16_t* system_name,
                                     const std::uint16_t* name, void* luid) noexcept;
TL_MSABI int tl_AdjustTokenPrivileges(void* token_handle, int disable_all_privileges,
                                     void* new_state, std::uint32_t buffer_length,
                                     void* previous_state, std::uint32_t* return_length) noexcept;
TL_MSABI std::uintptr_t tl_SHGetFileInfoW(const std::uint16_t* path, std::uint32_t file_attributes,
                                          void* sfi, std::uint32_t cb_file_info,
                                          std::uint32_t flags) noexcept;
TL_MSABI int tl_SHGetPathFromIDListW(const void* pidl, std::uint16_t* path) noexcept;
TL_MSABI void* tl_SHBrowseForFolderW(void* bi) noexcept;
TL_MSABI int tl_SHGetMalloc(void** pp_malloc) noexcept;
TL_MSABI void tl_SHChangeNotify(std::int32_t event_id, std::uint32_t flags,
                                const void* item1, const void* item2) noexcept;
TL_MSABI int tl_GetFileSecurityW(const std::uint16_t* file_name, std::uint32_t requested_information,
                                 void* security_descriptor, std::uint32_t length,
                                 std::uint32_t* length_needed) noexcept;
TL_MSABI std::int32_t tl_RegDeleteTreeW(void* key, const std::uint16_t* sub_key) noexcept;
TL_MSABI std::int32_t tl_RegEnumValueW(void* key, std::uint32_t index, std::uint16_t* value_name,
                                       std::uint32_t* cch_value_name, std::uint32_t* reserved,
                                       std::uint32_t* type, std::uint8_t* data,
                                       std::uint32_t* cb_data) noexcept;
TL_MSABI std::int32_t tl_RegEnumKeyExW(void* key, std::uint32_t index, std::uint16_t* name,
                                       std::uint32_t* cch_name, std::uint32_t* reserved,
                                       std::uint16_t* class_name, std::uint32_t* cch_class_name,
                                       void* last_write_time) noexcept;
TL_MSABI std::int32_t tl_RegDeleteKeyExW(void* key, const std::uint16_t* sub_key,
                                        std::uint32_t sam_desired, std::uint32_t reserved) noexcept;
TL_MSABI std::int32_t tl_RegDeleteKeyW(void* key, const std::uint16_t* sub_key) noexcept;
TL_MSABI int tl_PrintDlgW(void* print_dlg) noexcept;
TL_MSABI int tl_PathStripToRootW(std::uint16_t* path) noexcept;
TL_MSABI std::uint32_t tl_ExtractIconExW(const std::uint16_t* file, int index, void** icon_large, void** icon_small, std::uint32_t icons) noexcept;
TL_MSABI int tl_SHGetDesktopFolder(void** ppshf) noexcept;
TL_MSABI int tl_SHGetSpecialFolderLocation(void* hwnd, int folder, void** ppidl) noexcept;
TL_MSABI int tl_SHGetSpecialFolderPathW(void* hwnd, std::uint16_t* path, int folder, int create) noexcept;
TL_MSABI void* tl_CreateStatusWindowW(std::int32_t style, const std::uint16_t* text, void* parent, std::uint32_t id) noexcept;
TL_MSABI void* tl_CreateToolbarEx(void* hwnd, std::uint32_t style, std::uint32_t id, int num_bitmaps, void* instance,
                                  std::uintptr_t bitmap_id, const void* buttons, int num_buttons, int cx_button,
                                  int cy_button, int cx_bitmap, int cy_bitmap, std::uint32_t struct_size) noexcept;
TL_MSABI int tl_ImageList_GetImageCount(void* image_list) noexcept;
TL_MSABI std::intptr_t tl_PropertySheetW(const void* header) noexcept;
TL_MSABI std::uint32_t tl_CommDlgExtendedError() noexcept;
TL_MSABI int tl_GetUserNameW(std::uint16_t* buffer, std::uint32_t* size) noexcept;
TL_MSABI int tl_LookupAccountNameW(const std::uint16_t* system_name, const std::uint16_t* account_name,
                                   void* sid, std::uint32_t* sid_size, std::uint16_t* referenced_domain,
                                   std::uint32_t* domain_size, void* sid_name_use) noexcept;
TL_MSABI int tl_LsaOpenPolicy(void* system_name, void* obj_attributes, std::uint32_t access_mask, void** policy_handle) noexcept;
TL_MSABI int tl_LsaClose(void* policy_handle) noexcept;
TL_MSABI std::uint32_t tl_CM_Get_Child(void* pdnDevInst, std::uintptr_t dnDevInst, std::uint32_t ulFlags) noexcept;
TL_MSABI int tl_LsaAddAccountRights(void* policy_handle, void* account_sid, void* user_rights, std::uint32_t count) noexcept;
TL_MSABI std::int32_t tl_RegQueryInfoKeyA(void* key, char* class_name, std::uint32_t* cch_class_name,
                                         std::uint32_t* reserved, std::uint32_t* sub_keys,
                                         std::uint32_t* max_sub_key_len, std::uint32_t* max_class_len,
                                         std::uint32_t* values, std::uint32_t* max_value_name_len,
                                         std::uint32_t* max_value_len, std::uint32_t* security_descriptor,
                                         void* last_write_time) noexcept;
TL_MSABI std::int32_t tl_RegQueryInfoKeyW(void* key, std::uint16_t* class_name, std::uint32_t* cch_class_name,
                                         std::uint32_t* reserved, std::uint32_t* sub_keys,
                                         std::uint32_t* max_sub_key_len, std::uint32_t* max_class_len,
                                         std::uint32_t* values, std::uint32_t* max_value_name_len,
                                         std::uint32_t* max_value_len, std::uint32_t* security_descriptor,
                                         void* last_write_time) noexcept;
TL_MSABI std::int32_t tl_RegEnumKeyA(void* key, std::uint32_t index, char* name, std::uint32_t cch_name) noexcept;
TL_MSABI std::int32_t tl_RegEnumValueA(void* key, std::uint32_t index, char* value_name,
                                       std::uint32_t* cch_value_name, std::uint32_t* reserved,
                                       std::uint32_t* type, std::uint8_t* data, std::uint32_t* cb_data) noexcept;
TL_MSABI std::int32_t tl_RegDeleteKeyA(void* key, const char* sub_key) noexcept;
TL_MSABI int tl_ChooseFontA(void* choose_font) noexcept;
TL_MSABI int tl_ChooseFontW(void* choose_font) noexcept;
TL_MSABI std::uint32_t tl_ImmGetVirtualKey(void* hwnd) noexcept;
TL_MSABI int tl_ShellNotifyIconW(std::uint32_t message, void* data) noexcept;
TL_MSABI void* tl_OpenThemeData(void* hwnd, const std::uint16_t* class_list) noexcept;
TL_MSABI std::int32_t tl_CloseThemeData(void* theme) noexcept;
TL_MSABI std::int32_t tl_DrawThemeBackground(void* theme, void* hdc, int part_id, int state_id, const void* rect, const void* clip_rect) noexcept;
TL_MSABI std::int32_t tl_DrawThemeText(void* theme, void* hdc, int part_id, int state_id, const std::uint16_t* text, int char_count, std::uint32_t text_flags, std::uint32_t text_flags2, const void* rect) noexcept;
TL_MSABI std::int32_t tl_DrawThemeTextEx(void* theme, void* hdc, int part_id, int state_id, const std::uint16_t* text, int char_count, std::uint32_t text_flags, void* rect, const void* options) noexcept;
TL_MSABI std::int32_t tl_GetThemeColor(void* theme, int part_id, int state_id, int prop_id, std::uint32_t* color) noexcept;
TL_MSABI std::int32_t tl_GetThemeFont(void* theme, void* hdc, int part_id, int state_id, int prop_id, void* font) noexcept;
TL_MSABI std::int32_t tl_GetThemeMetric(void* theme, void* hdc, int part_id, int state_id, int prop_id, int* val) noexcept;
TL_MSABI std::int32_t tl_GetThemePartSize(void* theme, void* hdc, int part_id, int state_id, const void* rect, int type, void* size) noexcept;
TL_MSABI std::uint32_t tl_GetThemeSysColor(void* theme, int color_id) noexcept;
TL_MSABI void* tl_GetThemeSysColorBrush(void* theme, int color_id) noexcept;
TL_MSABI int tl_IsThemeActive() noexcept;
TL_MSABI int tl_IsAppThemed() noexcept;
TL_MSABI int tl_IsThemeBackgroundPartiallyTransparent(void* theme, int part_id, int state_id) noexcept;
TL_MSABI std::int32_t tl_BufferedPaintInit() noexcept;
TL_MSABI std::int32_t tl_BufferedPaintUnInit() noexcept;
TL_MSABI void* tl_BeginBufferedPaint(void* hdc_target, const void* target_rect, int format, const void* animation_params, void** hdc_out) noexcept;
TL_MSABI std::int32_t tl_EndBufferedPaint(void* buffered_paint, int update_target) noexcept;
TL_MSABI std::int32_t tl_DrawThemeParentBackground(void* hwnd, void* hdc, const void* rect) noexcept;
TL_MSABI std::int32_t tl_TaskDialogIndirect(const void* config, int* button, int* radio_button, int* verification_flag_checked) noexcept;
TL_MSABI std::int32_t tl_TaskDialog(void* hwnd_parent, void* instance, const std::uint16_t* title, const std::uint16_t* main_instruction, const std::uint16_t* content, std::uint32_t common_buttons, const std::uint16_t* icon, int* button) noexcept;
TL_MSABI int tl_ImageList_Draw(void* himl, int i, void* hdc_dst, int x, int y, std::uint32_t flags) noexcept;
TL_MSABI int tl_ImageList_DrawEx(void* himl, int i, void* hdc_dst, int x, int y, int dx, int dy, std::uint32_t rgb_bk, std::uint32_t rgb_fg, std::uint32_t flags) noexcept;
TL_MSABI void* tl_ImageList_GetIcon(void* himl, int i, std::uint32_t flags) noexcept;
TL_MSABI void* tl_ImageList_Duplicate(void* himl) noexcept;
TL_MSABI std::uint32_t tl_ImageList_SetBkColor(void* himl, std::uint32_t clr_bk) noexcept;
TL_MSABI std::uint32_t tl_ImageList_GetBkColor(void* himl) noexcept;
TL_MSABI int tl_ImageList_GetIconSize(void* himl, int* cx, int* cy) noexcept;
TL_MSABI std::int32_t tl_RegGetValueW(void* key, const wchar_t* sub_key, const wchar_t* value, std::uint32_t flags, std::uint32_t* type, void* data, std::uint32_t* data_len) noexcept;
TL_MSABI void* tl_RegisterEventSourceW(const wchar_t* server_name, const wchar_t* source_name) noexcept;
TL_MSABI int tl_DeregisterEventSource(void* event_log) noexcept;
TL_MSABI int tl_ReportEventW(void* event_log, std::uint16_t type, std::uint16_t category, std::uint32_t event_id, void* user_sid, std::uint16_t num_strings, std::uint32_t data_size, const wchar_t** strings, void* raw_data) noexcept;
TL_MSABI int tl_CryptCreateHash(std::uintptr_t prov, std::uint32_t algid, std::uintptr_t key, std::uint32_t flags, std::uintptr_t* hash) noexcept;
TL_MSABI int tl_CryptHashData(std::uintptr_t hash, const std::uint8_t* data, std::uint32_t data_len, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptGetHashParam(std::uintptr_t hash, std::uint32_t param, std::uint8_t* data, std::uint32_t* data_len, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptSetHashParam(std::uintptr_t hash, std::uint32_t param, const std::uint8_t* data, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptDestroyHash(std::uintptr_t hash) noexcept;
TL_MSABI int tl_CryptSignHashW(std::uintptr_t hash, std::uint32_t key_spec, const wchar_t* description, std::uint32_t flags, std::uint8_t* signature, std::uint32_t* sig_len) noexcept;
TL_MSABI int tl_CryptDecrypt(std::uintptr_t key, std::uintptr_t hash, int final_chunk, std::uint32_t flags, std::uint8_t* data, std::uint32_t* data_len) noexcept;
TL_MSABI int tl_CryptExportKey(std::uintptr_t key, std::uintptr_t exp_key, std::uint32_t blob_type, std::uint32_t flags, std::uint8_t* data, std::uint32_t* data_len) noexcept;
TL_MSABI int tl_CryptGetUserKey(std::uintptr_t prov, std::uint32_t key_spec, std::uintptr_t* user_key) noexcept;
TL_MSABI int tl_CryptGetProvParam(std::uintptr_t prov, std::uint32_t param, std::uint8_t* data, std::uint32_t* data_len, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptDestroyKey(std::uintptr_t key) noexcept;
TL_MSABI int tl_CryptEnumProvidersW(std::uint32_t index, std::uint32_t* reserved, std::uint32_t flags, std::uint32_t* prov_type, wchar_t* prov_name, std::uint32_t* name_len) noexcept;
TL_MSABI int tl_SystemFunction036(void* buffer, std::uint32_t length) noexcept;
TL_MSABI int tl_WSAIoctl(std::uintptr_t socket, std::uint32_t io_control_code, void* in_buffer, std::uint32_t in_buffer_size, void* out_buffer, std::uint32_t out_buffer_size, std::uint32_t* bytes_returned, void* overlapped, void* completion_routine) noexcept;
TL_MSABI int tl_getnameinfo(const void* sa, int salen, char* host, std::uint32_t hostlen, char* serv, std::uint32_t servlen, int flags) noexcept;
TL_MSABI std::uintptr_t tl_WSASocketA(int af, int type, int protocol, void* protocol_info, std::uint32_t group, std::uint32_t flags) noexcept;
TL_MSABI int tl_gethostname(char* name, int namelen) noexcept;
TL_MSABI std::uint16_t tl_htons(std::uint16_t hostshort) noexcept;
TL_MSABI std::uint16_t tl_ntohs(std::uint16_t netshort) noexcept;
TL_MSABI std::uint32_t tl_htonl(std::uint32_t hostlong) noexcept;
TL_MSABI std::uint32_t tl_ntohl(std::uint32_t netlong) noexcept;
TL_MSABI void* tl_gethostbyaddr(const char* addr, int len, int type) noexcept;
TL_MSABI char* tl_inet_ntoa(std::uint32_t in) noexcept;
TL_MSABI int tl_WSAGetLastError() noexcept;
TL_MSABI int tl_TrackMouseEvent_alias(void* event_track) noexcept;
TL_MSABI int tl_StringFromGUID2(const void* rguid, wchar_t* lpsz, int cchMax) noexcept;
TL_MSABI void* tl_CertGetEnhancedKeyUsage(void* cert_context, std::uint32_t flags, void* usage, std::uint32_t* usage_size) noexcept;
TL_MSABI int tl_CertGetIntendedKeyUsage(std::uint32_t cert_encoding_type, void* cert_info, std::uint8_t* key_usage, std::uint32_t byte_count) noexcept;
TL_MSABI int tl_ImmSetCompositionFontA(void* himc, void* logfont) noexcept;
TL_MSABI void* tl_ShellExecuteA(void* hwnd, const char* operation, const char* file, const char* parameters, const char* directory, int show_cmd) noexcept;
TL_MSABI int tl_GetUserNameA(char* buffer, std::uint32_t* size) noexcept;
TL_MSABI int tl_SetSecurityDescriptorOwner(void* sec_desc, void* owner, int owner_defaulted) noexcept;
TL_MSABI int tl_IsDestinationReachableW(const wchar_t* lpszDestination, void* lpQOCInfo) noexcept;
TL_MSABI int tl_IsNetworkAlive(std::uint32_t* lpdwFlags) noexcept;
TL_MSABI void* tl_ImageNtHeader(void* base) noexcept;
TL_MSABI int tl_CLSIDFromProgID(const wchar_t* lpszProgID, void* lpclsid) noexcept;
TL_MSABI int tl_IsTextUnicode(const void* lpv, int iSize, int* lpiResult) noexcept;
TL_MSABI int tl_CryptMsgClose(void* hCryptMsg) noexcept;
TL_MSABI int tl_CryptMsgGetParam(void* hCryptMsg, std::uint32_t dwParamType, std::uint32_t dwIndex, void* pvData, std::uint32_t* pcbData) noexcept;
TL_MSABI int tl_CryptQueryObject(std::uint32_t dwObjectType, const void* pvObject, std::uint32_t dwExpectedContentTypeFlags, std::uint32_t dwExpectedFormatTypeFlags, std::uint32_t dwFlags, std::uint32_t* pdwMsgAndCertEncodingType, std::uint32_t* pdwContentType, std::uint32_t* pdwFormatType, void** phCertStore, void** phMsg, const void** ppvContext) noexcept;
TL_MSABI int tl_SHCreateItemFromParsingName(const wchar_t* pszPath, void* pbc, const void* riid, void** ppv) noexcept;
TL_MSABI std::uint32_t tl_DragQueryFileW(void* hDrop, std::uint32_t iFile, wchar_t* lpszFile, std::uint32_t cch) noexcept;
TL_MSABI int tl_DragQueryPoint(void* hDrop, void* lppt) noexcept;
TL_MSABI void tl_DragFinish(void* hDrop) noexcept;
TL_MSABI int tl_ImageList_GetImageInfo(void* himl, int i, void* pImageInfo) noexcept;
TL_MSABI int tl_ImageList_EndDrag() noexcept;
TL_MSABI int tl_ImageList_DragShowNolock(int fShow) noexcept;
TL_MSABI int tl_ImageList_DragEnter(void* hwndLock, int x, int y) noexcept;
TL_MSABI int tl_ImageList_DragMove(int x, int y) noexcept;
TL_MSABI int tl_ImageList_BeginDrag(void* himlTrack, int iTrack, int dxHotspot, int dyHotspot) noexcept;
TL_MSABI int tl_ImageList_Remove(void* himl, int i) noexcept;
TL_MSABI int tl_ImageList_SetIconSize(void* himl, int cx, int cy) noexcept;
TL_MSABI int tl_LoadIconWithScaleDown(void* hinst, const wchar_t* pszName, int cx, int cy, void** phico) noexcept;
TL_MSABI int tl_AssocQueryStringW(std::uint32_t flags, std::uint32_t str, const wchar_t* pszAssoc, const wchar_t* pszExtra, wchar_t* pszOut, std::uint32_t* pcchOut) noexcept;
TL_MSABI void tl_ColorRGBToHLS(std::uint32_t clrRGB, std::uint16_t* pwHue, std::uint16_t* pwLuminance, std::uint16_t* pwSaturation) noexcept;
TL_MSABI std::uint32_t tl_ColorHLSToRGB(std::uint16_t wHue, std::uint16_t wLuminance, std::uint16_t wSaturation) noexcept;
TL_MSABI std::uint32_t tl_ColorAdjustLuma(std::uint32_t clrRGB, int n, int fBorder) noexcept;
TL_MSABI void tl_PathStripPathW(wchar_t* pszPath) noexcept;
TL_MSABI int tl_PathAddExtensionW(wchar_t* pszPath, const wchar_t* pszExt) noexcept;
TL_MSABI int tl_PathAppendW(wchar_t* pszPath, const wchar_t* pszMore) noexcept;
TL_MSABI void tl_PathRemoveExtensionW(wchar_t* pszPath) noexcept;
TL_MSABI int tl_PathCompactPathExW(wchar_t* pszOut, const wchar_t* pszSrc, std::uint32_t cchMax, std::uint32_t dwFlags) noexcept;
TL_MSABI int tl_PathGetDriveNumberW(const wchar_t* pszPath) noexcept;
TL_MSABI int tl_PathMatchSpecW(const wchar_t* pszFile, const wchar_t* pszSpec) noexcept;
TL_MSABI int tl_EndBufferedAnimation(void* hbpAnimation, int fUpdateTarget) noexcept;
TL_MSABI int tl_GetThemeTransitionDuration(void* hTheme, int iPartId, int iStateIdFrom, int iStateIdTo, int iPropId, int* pdwDuration) noexcept;
TL_MSABI int tl_GetThemeBackgroundContentRect(void* hTheme, void* hdc, int iPartId, int iStateId, const void* pBoundingRect, void* pContentRect) noexcept;
TL_MSABI int tl_EnableThemeDialogTexture(void* hwnd, std::uint32_t dwFlags) noexcept;
TL_MSABI void tl_BufferedPaintStopAllAnimations(void* hwnd) noexcept;
TL_MSABI void* tl_BeginBufferedAnimation(void* hwnd, void* hdcTarget, const void* rcTarget, int dwFormat, void* pPaintParams, void* pAnimationParams, void** phdcFrom, void** phdcTo) noexcept;
TL_MSABI int tl_BufferedPaintRenderAnimation(void* hwnd, void* hdcTarget) noexcept;
TL_MSABI int tl_PathIsUNCW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_PathIsUNCA(const char* path) noexcept;
TL_MSABI std::uint32_t tl_NetApiBufferFree(void* buffer) noexcept;
TL_MSABI std::intptr_t tl_LresultFromObject(const void* riid, std::uintptr_t w_param, void* unk) noexcept;
TL_MSABI std::uint32_t tl_TdhGetPropertySize(void* event_record, std::uint32_t tdh_context_count, void* tdh_context, std::uint32_t property_data_count, void* property_data, std::uint32_t* property_size) noexcept;
TL_MSABI int tl_OpenPrinterW(const std::uint16_t* printer_name, void** printer_handle, void* defaults) noexcept;
TL_MSABI void tl_WTSFreeMemory(void* memory) noexcept;
TL_MSABI int tl_WTSEnumerateSessionsW(void* server, std::uint32_t reserved,
                                      std::uint32_t version, void** session_info,
                                      std::uint32_t* count) noexcept;
TL_MSABI int tl_WTSQuerySessionInformationW(void* server, std::uint32_t session_id,
                                            std::uint32_t info_class, std::uint16_t** buffer,
                                            std::uint32_t* bytes_returned) noexcept;
TL_MSABI int tl_PathRemoveExtensionA(char* path) noexcept;
TL_MSABI int tl_PathRenameExtensionA(char* path, const char* ext) noexcept;
TL_MSABI char* tl_PathStripPathA(char* path) noexcept;
TL_MSABI int tl_PathMatchSpecA(const char* file, const char* spec) noexcept;
TL_MSABI std::uint32_t tl_timeKillEvent(std::uint32_t id) noexcept;
TL_MSABI void* tl_SetupDiGetClassDevsA(const void* guid, const char* enumerator, void* parent, std::uint32_t flags) noexcept;
TL_MSABI int tl_SetupDiEnumDeviceInfo(void* dev_info, std::uint32_t idx, void* dev_data) noexcept;
TL_MSABI int tl_SetupDiEnumDeviceInterfaces(void* dev_info, void* dev_data, const void* guid, std::uint32_t idx, void* iface_data) noexcept;
TL_MSABI int tl_SetupDiGetDeviceInterfaceDetailA(void* dev_info, void* iface_data, void* detail, std::uint32_t size, std::uint32_t* needed, void* dev_data) noexcept;
TL_MSABI int tl_SetupDiGetDeviceRegistryPropertyA(void* dev_info, void* dev_data, std::uint32_t prop, std::uint32_t* reg_type, std::uint8_t* buf, std::uint32_t buf_size, std::uint32_t* needed) noexcept;
TL_MSABI int tl_SetupDiGetDeviceInstanceIdA(void* dev_info, void* dev_data, char* id, std::uint32_t size, std::uint32_t* needed) noexcept;
TL_MSABI int tl_SetupDiDestroyDeviceInfoList(void* dev_info) noexcept;
TL_MSABI int tl_D3DCompile(const void* src, std::size_t src_size, const char* src_name, const void* defines, void* include, const char* entry, const char* target, std::uint32_t flags1, std::uint32_t flags2, void** code, void** errors) noexcept;
TL_MSABI int tl_D3D12SerializeRootSignature(const void* root_sig, std::uint32_t version, void** blob, void** error) noexcept;
TL_MSABI int tl_CreateDXGIFactory1(const void* riid, void** factory) noexcept;
TL_MSABI int tl_DirectDrawCreateEx(const void* guid, void** dd, const void* iid, void* unk) noexcept;
TL_MSABI int tl_Direct3DCreate9(std::uint32_t version) noexcept;
TL_MSABI int tl_Direct3DCreate9Ex(std::uint32_t version, void** d3d) noexcept;
TL_MSABI int tl_D3D10CreateDeviceAndSwapChain(void* adapter, std::uint32_t driver, void* sw, std::uint32_t flags, std::uint32_t feature, void* swap_desc, void** swap_chain, void** device) noexcept;
TL_MSABI int tl_D3DX10CompileFromMemory(const char* src, std::size_t len, const char* src_name, const void* defines, void* include, const char* entry, const char* profile, std::uint32_t flags1, std::uint32_t flags2, void* pump, void** shader, void** errors, void** hr) noexcept;
TL_MSABI int tl_D3D11CreateDeviceAndSwapChain(void* adapter, std::uint32_t driver, void* sw, std::uint32_t flags, const void* feature_levels, std::uint32_t levels, std::uint32_t sdk, void* swap_desc, void** swap_chain, void** device, void* feature, void* ctx) noexcept;
TL_MSABI int tl_D3DX11CompileFromMemory(const char* src, std::size_t len, const char* src_name, const void* defines, void* include, const char* entry, const char* target, std::uint32_t flags1, std::uint32_t flags2, void* pump, void** code, void** errors, void** hr) noexcept;
TL_MSABI const char* tl_ldap_err2string(std::int32_t err) noexcept;
TL_MSABI void* tl_ldap_null_stub() noexcept;
TL_MSABI std::uint32_t tl_ldap_unavailable_stub() noexcept;
TL_MSABI std::uint32_t tl_ldap_success_stub() noexcept;
TL_MSABI void tl_ldap_void_stub() noexcept;
TL_MSABI int tl_IdnToAscii(std::uint32_t flags, const std::uint16_t* src, int src_len,
                           std::uint16_t* dst, int dst_len) noexcept;
TL_MSABI int tl_IdnToUnicode(std::uint32_t flags, const std::uint16_t* src, int src_len,
                             std::uint16_t* dst, int dst_len) noexcept;
TL_MSABI std::uint32_t tl_BCryptGenRandom(void* algorithm, std::uint8_t* buffer,
                                          std::uint32_t count, std::uint32_t flags) noexcept;
TL_MSABI int dummy_worker_check() noexcept;

}  // extern "C"

// Define o caminho do módulo convidado antes da execução.
void set_guest_module_path(const char* path) noexcept;
// Define o prefixo ativo da execução atual. O valor é herdado pelos processos
// convidados criados via fork e nunca é obtido implicitamente do diretório do
// launcher.
void set_guest_prefix_path(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path guest_prefix_root();
void set_guest_image_view(const void* image_base, std::size_t image_size,
                          std::uint32_t resource_rva, std::uint32_t resource_size) noexcept;
void set_guest_tls_directory(std::uint64_t start_raw, std::uint64_t end_raw,
                             std::uint64_t index_addr, std::uint32_t zero_fill_size,
                             const std::vector<std::uint64_t>& callbacks) noexcept;
void initialize_thread_tls(void* teb) noexcept;
void invoke_thread_tls_callbacks(std::uint32_t reason) noexcept;

// Executa um entry point Microsoft x64 e captura ExitProcess sem encerrar o
// processo hospedeiro. O ponteiro deve apontar para código já mapeado como
// executável e com imports resolvidos.
[[nodiscard]] GuestExecutionResult execute_guest_entry(std::uintptr_t entry_point,
                                                       std::uintptr_t stack_top) noexcept;

}  // namespace tradutorlinux
