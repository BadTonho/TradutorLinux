#pragma once

namespace tradutorlinux::loader {

void register_kernel32_module();
void register_user32_module();
void register_gdi32_module();
void register_ws2_32_module();
void register_wininet_module();
void register_msvcrt_module();
void register_shell32_module();
void register_advapi32_module();
void register_ole32_module();
void register_oleaut32_module();
void register_wintrust_module();
void register_crypt32_module();
void register_shlwapi_module();
void register_version_module();
void register_winmm_module();
void register_gdiplus_module();
void register_uxtheme_module();
void register_dbghelp_module();
void register_powrprof_module();
void register_iphlpapi_module();
void register_comctl32_module();
void register_comdlg32_module();
void register_imm32_module();
void register_psapi_module();
void register_mpr_module();
void register_dwmapi_module();
void register_winapi_stubs_module();

}  // namespace tradutorlinux::loader
