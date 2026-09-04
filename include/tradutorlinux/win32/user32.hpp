#pragma once

#include "tradutorlinux/win32/types.hpp"

namespace tradutorlinux {
extern "C" {

TL_MSABI int tl_MessageBoxA(const void* owner, const char* text, const char* caption,
                            std::uint32_t type) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExA(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassA(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExW(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassW(const void* wnd_class) noexcept;
TL_MSABI abi::HWnd tl_CreateWindowExA(std::uint32_t ex_style, const char* class_name,
                                      const char* window_name, std::uint32_t style, int x, int y,
                                      int width, int height, const void* parent, const void* menu,
                                      const void* instance, const void* param) noexcept;
TL_MSABI abi::HWnd tl_CreateWindowExW(std::uint32_t ex_style, const std::uint16_t* class_name,
                                      const std::uint16_t* window_name, std::uint32_t style, int x, int y,
                                      int width, int height, const void* parent, const void* menu,
                                      const void* instance, const void* param) noexcept;
TL_MSABI int tl_ShowWindow(const void* window, int cmd_show) noexcept;
TL_MSABI int tl_UpdateWindow(const void* window) noexcept;
TL_MSABI int tl_GetMessageA(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max) noexcept;
TL_MSABI int tl_GetMessageW(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max) noexcept;
TL_MSABI int tl_TranslateMessage(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DispatchMessageA(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DispatchMessageW(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DefWindowProcA(const void* window, std::uint32_t message,
                                        abi::Wparam wparam, abi::Lparam lparam) noexcept;
TL_MSABI abi::Lresult tl_DefWindowProcW(const void* window, std::uint32_t message,
                                        abi::Wparam wparam, abi::Lparam lparam) noexcept;
TL_MSABI int tl_DestroyWindow(const void* window) noexcept;
TL_MSABI void tl_PostQuitMessage(int exit_code) noexcept;
TL_MSABI std::uintptr_t tl_SetTimer(const void* window, std::uintptr_t id, std::uint32_t elapsed_ms,
                                    const void* timer_proc) noexcept;
TL_MSABI int tl_KillTimer(const void* window, std::uintptr_t id) noexcept;
TL_MSABI void* tl_BeginPaint(const void* window, void* paint_struct) noexcept;
TL_MSABI int tl_EndPaint(const void* window, const void* paint_struct) noexcept;
TL_MSABI int tl_FillRect(const void* dc, const void* rect, const void* brush) noexcept;
TL_MSABI void* tl_GetDC(const void* window) noexcept;
TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept;
TL_MSABI int tl_GetClientRect(const void* window, void* rect) noexcept;
TL_MSABI int tl_GetWindowRect(const void* window, void* rect) noexcept;
TL_MSABI int tl_GetCursorPos(void* point) noexcept;
TL_MSABI int tl_MoveWindow(const void* window, int x, int y, int width, int height,
                           int repaint) noexcept;
TL_MSABI std::intptr_t tl_SetWindowPos(const void* window, const void* insert_after, int x, int y,
                                       int width, int height, std::uint32_t flags) noexcept;
TL_MSABI int tl_SetWindowTextA(const void* window, const char* text) noexcept;
TL_MSABI int tl_SetWindowTextW(const void* window, const std::uint16_t* text) noexcept;
TL_MSABI int tl_GetWindowTextA(const void* window, char* text, int capacity) noexcept;
TL_MSABI int tl_GetWindowTextW(const void* window, std::uint16_t* text, int capacity) noexcept;
TL_MSABI int tl_GetWindowTextLengthA(const void* window) noexcept;
TL_MSABI int tl_GetWindowTextLengthW(const void* window) noexcept;
TL_MSABI int tl_EnableWindow(const void* window, int enable) noexcept;
TL_MSABI const void* tl_SetFocus(const void* window) noexcept;
TL_MSABI int tl_IsWindowVisible(const void* window) noexcept;
TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept;
TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept;
TL_MSABI const void* tl_FindWindowW(const std::uint16_t* class_name, const std::uint16_t* window_name) noexcept;
TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadCursorW(const void* instance, const std::uint16_t* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadIconW(const void* instance, const std::uint16_t* name) noexcept;
TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                             std::intptr_t value) noexcept;
TL_MSABI std::intptr_t tl_SetClassLongPtrW(const void* window, int index,
                                             std::intptr_t value) noexcept;
TL_MSABI int tl_SetForegroundWindow(const void* window) noexcept;
TL_MSABI int tl_SendMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI int tl_SendMessageW(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI std::intptr_t tl_DialogBoxParamW(const void* instance, const std::uint16_t* template_name,
                                           const void* parent, std::uintptr_t dialog_proc,
                                           abi::Lparam init_param) noexcept;
TL_MSABI int tl_EndDialog(const void* dialog, std::intptr_t result) noexcept;
TL_MSABI void* tl_GetDlgItem(const void* dialog, int identifier) noexcept;
TL_MSABI int tl_SetDlgItemTextW(const void* dialog, int identifier,
                                const std::uint16_t* text) noexcept;
TL_MSABI abi::Lresult tl_SendDlgItemMessageW(const void* dialog, int identifier,
                                             std::uint32_t message, abi::Wparam wparam,
                                             abi::Lparam lparam) noexcept;
TL_MSABI void* tl_GetNextDlgTabItem(const void* dialog, const void* control,
                                    int previous) noexcept;
TL_MSABI int tl_IsDialogMessageW(const void* dialog, const void* message) noexcept;
TL_MSABI std::int32_t tl_GetWindowLongW(const void* window, int index) noexcept;
TL_MSABI std::int32_t tl_SetWindowLongW(const void* window, int index,
                                        std::int32_t new_long) noexcept;
TL_MSABI void* tl_CopyImage(const void* image, std::uint32_t image_type, int width, int height,
                            std::uint32_t flags) noexcept;
TL_MSABI int tl_DestroyIcon(const void* icon) noexcept;
TL_MSABI int tl_PostMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI int tl_PostMessageW(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI void* tl_CreatePopupMenu() noexcept;
TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                             const char* text) noexcept;
TL_MSABI int tl_AppendMenuW(const void* menu, std::uint32_t flags, std::uintptr_t command,
                             const std::uint16_t* text) noexcept;
TL_MSABI int tl_DestroyMenu(const void* menu) noexcept;
TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept;
TL_MSABI void* tl_GetDesktopWindow() noexcept;
TL_MSABI void* tl_GetFocus() noexcept;
TL_MSABI void* tl_SetCapture(const void* window) noexcept;
TL_MSABI int tl_ReleaseCapture() noexcept;
TL_MSABI void* tl_GetCapture() noexcept;
TL_MSABI int tl_BringWindowToTop(const void* window) noexcept;
TL_MSABI void* tl_GetWindow(const void* window, std::uint32_t cmd) noexcept;
TL_MSABI int tl_GetClassNameA(const void* window, char* class_name, int max_count) noexcept;
TL_MSABI int tl_GetClassNameW(const void* window, std::uint16_t* class_name, int max_count) noexcept;
TL_MSABI std::uint32_t tl_GetWindowThreadProcessId(const void* window, std::uint32_t* process_id) noexcept;
TL_MSABI abi::Lresult tl_CallWindowProcA(std::uintptr_t prev_wnd_func, const void* window,
                                        std::uint32_t message, abi::Wparam wparam,
                                        abi::Lparam lparam) noexcept;
TL_MSABI abi::Lresult tl_CallWindowProcW(std::uintptr_t prev_wnd_func, const void* window,
                                        std::uint32_t message, abi::Wparam wparam,
                                        abi::Lparam lparam) noexcept;
TL_MSABI int tl_PeekMessageA(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max, std::uint32_t remove_msg) noexcept;
TL_MSABI int tl_PeekMessageW(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max, std::uint32_t remove_msg) noexcept;
TL_MSABI int tl_RedrawWindow(const void* window, const void* update_rect, const void* update_rgn,
                             std::uint32_t flags) noexcept;
TL_MSABI int tl_PtInRect(const void* rect, std::int32_t x, std::int32_t y) noexcept;
TL_MSABI int tl_CopyRect(void* dest_rect, const void* src_rect) noexcept;
TL_MSABI int tl_MapWindowPoints(const void* from_window, const void* to_window, void* points,
                                std::uint32_t count) noexcept;
TL_MSABI void* tl_MonitorFromWindow(const void* window, std::uint32_t flags) noexcept;
TL_MSABI std::uint32_t tl_GetSysColor(int index) noexcept;
TL_MSABI std::uint16_t* tl_CharUpperW(std::uint16_t* str) noexcept;
TL_MSABI std::uint16_t* tl_CharLowerW(std::uint16_t* str) noexcept;
TL_MSABI const char* tl_CharPrevExA(std::uint32_t code_page, const char* start, const char* current, std::uint32_t flags) noexcept;
TL_MSABI int tl_DrawTextA(const void* dc, const char* text, int count, void* rect,
                          std::uint32_t format) noexcept;
TL_MSABI int tl_DrawTextW(const void* dc, const std::uint16_t* text, int count, void* rect,
                          std::uint32_t format) noexcept;
TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjects(std::uint32_t count, const void* const* handles,
                                                    int wait_all, std::uint32_t milliseconds,
                                                    std::uint32_t wake_mask) noexcept;
TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjectsEx(std::uint32_t count, const void* const* handles,
                                                      std::uint32_t milliseconds, std::uint32_t wake_mask,
                                                      std::uint32_t flags) noexcept;
TL_MSABI int tl_GetSystemMetrics(int index) noexcept;
TL_MSABI std::intptr_t tl_GetWindowLongPtrA(const void* window, int index) noexcept;
TL_MSABI std::intptr_t tl_GetWindowLongPtrW(const void* window, int index) noexcept;
TL_MSABI std::intptr_t tl_SetWindowLongPtrA(const void* window, int index, std::intptr_t new_long) noexcept;
TL_MSABI std::intptr_t tl_SetWindowLongPtrW(const void* window, int index, std::intptr_t new_long) noexcept;
TL_MSABI void* tl_GetParent(const void* window) noexcept;
TL_MSABI void* tl_SetParent(const void* child_window, const void* new_parent_window) noexcept;
TL_MSABI int tl_IsWindow(const void* window) noexcept;
TL_MSABI int tl_MessageBoxW(const void* window, const std::uint16_t* text,
                            const std::uint16_t* caption, std::uint32_t type) noexcept;
TL_MSABI void* tl_GetDC(const void* window) noexcept;
TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept;
TL_MSABI void* tl_GetWindowDC(const void* window) noexcept;
TL_MSABI void* tl_SetCursor(const void* cursor) noexcept;
TL_MSABI int tl_ShowCursor(int show) noexcept;
TL_MSABI int tl_SetCursorPos(int x, int y) noexcept;
TL_MSABI std::int16_t tl_GetKeyState(int virt_key) noexcept;
TL_MSABI std::int16_t tl_GetAsyncKeyState(int virt_key) noexcept;
TL_MSABI int tl_LoadStringA(void* instance, std::uint32_t id, char* buffer, int buffer_max) noexcept;
TL_MSABI int tl_LoadStringW(void* instance, std::uint32_t id, std::uint16_t* buffer, int buffer_max) noexcept;
TL_MSABI int tl_SetUserObjectInformationW(void* obj, int index, void* info,
                                          std::uint32_t length) noexcept;
TL_MSABI std::uint32_t tl_WaitForInputIdle(void* process, std::uint32_t milliseconds) noexcept;
TL_MSABI void* tl_FindWindowExW(void* hwnd_parent, void* hwnd_child_after,
                                const std::uint16_t* class_name,
                                const std::uint16_t* window_name) noexcept;
TL_MSABI int tl_SetProcessDefaultLayout(std::uint32_t default_layout) noexcept;
TL_MSABI int tl_OpenClipboard(void* hwnd_new_owner) noexcept;
TL_MSABI int tl_CloseClipboard(void) noexcept;
TL_MSABI void* tl_SetClipboardData(std::uint32_t format, void* mem) noexcept;
TL_MSABI int tl_EmptyClipboard(void) noexcept;
TL_MSABI int tl_MessageBoxExW(void* hwnd, const std::uint16_t* text,
                              const std::uint16_t* caption, std::uint32_t type,
                              std::uint16_t language_id) noexcept;
TL_MSABI int tl_DrawIconEx(void* hdc, int x_left, int y_top, void* hicon,
                           int cx_width, int cy_width, std::uint32_t step_if_ani_cur,
                           void* hbr_flicker_free_draw, std::uint32_t flags) noexcept;
TL_MSABI void* tl_LoadImageW(void* hinst, const std::uint16_t* name, std::uint32_t type,
                             int cx, int cy, std::uint32_t fu_load) noexcept;
TL_MSABI int tl_ClientToScreen(void* hwnd, void* point) noexcept;
TL_MSABI void* tl_GetMenu(void* hwnd) noexcept;
TL_MSABI int tl_SetMenu(void* hwnd, void* menu) noexcept;
TL_MSABI void* tl_GetSubMenu(void* menu, int pos) noexcept;
TL_MSABI int tl_GetMenuItemCount(void* menu) noexcept;
TL_MSABI int tl_GetMenuItemInfoW(void* menu, std::uint32_t item, int f_by_position, void* mii) noexcept;
TL_MSABI int tl_SetMenuItemInfoW(void* menu, std::uint32_t item, int f_by_position, const void* mii) noexcept;
TL_MSABI int tl_InsertMenuItemW(void* menu, std::uint32_t item, int f_by_position, const void* mii) noexcept;
TL_MSABI int tl_RemoveMenu(void* menu, std::uint32_t position, std::uint32_t flags) noexcept;
TL_MSABI int tl_EnableMenuItem(void* menu, std::uint32_t item, std::uint32_t enable) noexcept;
TL_MSABI std::uint32_t tl_CheckMenuItem(void* menu, std::uint32_t item, std::uint32_t check) noexcept;
TL_MSABI int tl_CheckMenuRadioItem(void* menu, std::uint32_t first, std::uint32_t last, std::uint32_t check, std::uint32_t flags) noexcept;
TL_MSABI int tl_DrawMenuBar(void* hwnd) noexcept;
TL_MSABI int tl_TrackPopupMenuEx(void* menu, std::uint32_t flags, int x, int y, void* hwnd, void* params) noexcept;
TL_MSABI void* tl_LoadMenuW(void* instance, const std::uint16_t* menu_name) noexcept;
TL_MSABI int tl_CheckDlgButton(void* hdlg, int id_button, std::uint32_t check) noexcept;
TL_MSABI std::uint32_t tl_IsDlgButtonChecked(void* hdlg, int id_button) noexcept;
TL_MSABI int tl_CheckRadioButton(void* hdlg, int first_button, int last_button, int check_button) noexcept;
TL_MSABI int tl_MapDialogRect(void* hdlg, void* rect) noexcept;
TL_MSABI std::uint32_t tl_GetDialogBaseUnits() noexcept;
TL_MSABI int tl_ScreenToClient(void* hwnd, void* point) noexcept;
TL_MSABI void* tl_WindowFromPoint(std::int64_t point_coord) noexcept;
TL_MSABI void* tl_ChildWindowFromPointEx(void* hwnd, std::int64_t point_coord, std::uint32_t flags) noexcept;
TL_MSABI int tl_GetWindowPlacement(void* hwnd, void* placement) noexcept;
TL_MSABI int tl_SetWindowPlacement(void* hwnd, const void* placement) noexcept;
TL_MSABI int tl_IsWindowEnabled(void* hwnd) noexcept;
TL_MSABI int tl_IsZoomed(void* hwnd) noexcept;
TL_MSABI int tl_GetClassInfoW(void* instance, const std::uint16_t* class_name, void* wnd_class) noexcept;
TL_MSABI int tl_GetMonitorInfoA(void* monitor, void* mi) noexcept;
TL_MSABI int tl_SystemParametersInfoW(std::uint32_t action, std::uint32_t param1, void* param2, std::uint32_t win_ini) noexcept;
TL_MSABI void* tl_LoadAcceleratorsW(void* instance, const std::uint16_t* table_name) noexcept;
TL_MSABI int tl_TranslateAcceleratorW(void* hwnd, void* accel_table, void* msg) noexcept;
TL_MSABI void* tl_LoadBitmapW(void* instance, const std::uint16_t* bitmap_name) noexcept;
TL_MSABI std::uint32_t tl_MapVirtualKeyW(std::uint32_t code, std::uint32_t map_type) noexcept;
TL_MSABI std::uint32_t tl_RegisterClipboardFormatW(const std::uint16_t* format_name) noexcept;
TL_MSABI int tl_CreateCaret(void* hwnd, void* bitmap, int width, int height) noexcept;
TL_MSABI int tl_DestroyCaret() noexcept;
TL_MSABI int tl_SetCaretPos(int x, int y) noexcept;
TL_MSABI int tl_ShowCaret(void* hwnd) noexcept;
TL_MSABI int tl_HideCaret(void* hwnd) noexcept;
TL_MSABI int tl_GetCaretPos(void* point) noexcept;
TL_MSABI int tl_SetScrollInfo(void* hwnd, int bar, const void* scroll_info, int redraw) noexcept;
TL_MSABI int tl_GetScrollInfo(void* hwnd, int bar, void* scroll_info) noexcept;
TL_MSABI int tl_ShowScrollBar(void* hwnd, int bar, int show) noexcept;
TL_MSABI int tl_EnableScrollBar(void* hwnd, std::uint32_t flags, std::uint32_t arrows) noexcept;
TL_MSABI int tl_SetScrollPos(void* hwnd, int bar, int pos, int redraw) noexcept;
TL_MSABI int tl_GetScrollPos(void* hwnd, int bar) noexcept;
TL_MSABI int tl_SetScrollRange(void* hwnd, int bar, int min_pos, int max_pos, int redraw) noexcept;
TL_MSABI int tl_GetScrollRange(void* hwnd, int bar, int* min_pos, int* max_pos) noexcept;
TL_MSABI int tl_FlashWindow(void* hwnd, int invert) noexcept;
TL_MSABI int tl_FlashWindowEx(void* flash_info) noexcept;
TL_MSABI int tl_SetSysColors(int count, const int* elements, const std::uint32_t* colors) noexcept;
TL_MSABI int tl_MessageBeep(std::uint32_t type) noexcept;
TL_MSABI void* tl_GetClipboardData(std::uint32_t format) noexcept;
TL_MSABI int tl_IsClipboardFormatAvailable(std::uint32_t format) noexcept;
TL_MSABI std::uint32_t tl_RegisterClipboardFormatA(const char* format_name) noexcept;
TL_MSABI int tl_CountClipboardFormats() noexcept;
TL_MSABI std::uint32_t tl_EnumClipboardFormats(std::uint32_t format) noexcept;
TL_MSABI std::uint32_t tl_GetDpiForWindow(void* hwnd) noexcept;
TL_MSABI std::uint32_t tl_GetDpiForSystem() noexcept;
TL_MSABI int tl_SetProcessDpiAwarenessContext(void* dpi_context) noexcept;
TL_MSABI int tl_SetProcessDPIAware() noexcept;
TL_MSABI int tl_GetSystemMetricsForDpi(int index, std::uint32_t dpi) noexcept;
TL_MSABI int tl_AdjustWindowRectExForDpi(void* rect, std::uint32_t style, int menu, std::uint32_t ex_style, std::uint32_t dpi) noexcept;
TL_MSABI void* tl_CreateIconIndirect(const void* icon_info) noexcept;
TL_MSABI int tl_GetIconInfo(void* icon, void* icon_info) noexcept;
TL_MSABI int tl_GetIconInfoExW(void* icon, void* icon_info_ex) noexcept;
TL_MSABI int tl_DrawIcon(void* hdc, int x, int y, void* icon) noexcept;
TL_MSABI void* tl_CopyIcon(void* icon) noexcept;
TL_MSABI int tl_SetWindowRgn(void* hwnd, void* rgn, int redraw) noexcept;
TL_MSABI int tl_GetWindowRgn(void* hwnd, void* rgn) noexcept;
TL_MSABI int tl_GetWindowRgnBox(void* hwnd, void* rect) noexcept;
TL_MSABI int tl_DrawEdge(void* hdc, void* rect, std::uint32_t edge, std::uint32_t flags) noexcept;
TL_MSABI int tl_DrawFrameControl(void* hdc, void* rect, std::uint32_t type, std::uint32_t state) noexcept;
TL_MSABI int tl_DrawFocusRect(void* hdc, const void* rect) noexcept;
TL_MSABI int tl_FrameRect(void* hdc, const void* rect, void* brush) noexcept;
TL_MSABI int tl_InvertRect(void* hdc, const void* rect) noexcept;
TL_MSABI int tl_GetUpdateRect(void* hwnd, void* rect, int erase) noexcept;
TL_MSABI int tl_GetUpdateRgn(void* hwnd, void* rgn, int erase) noexcept;
TL_MSABI int tl_InvalidateRgn(void* hwnd, void* rgn, int erase) noexcept;
TL_MSABI int tl_ValidateRgn(void* hwnd, void* rgn) noexcept;
TL_MSABI int tl_ScrollWindow(void* hwnd, int x_amount, int y_amount, const void* rect, const void* clip_rect) noexcept;
TL_MSABI int tl_ScrollWindowEx(void* hwnd, int dx, int dy, const void* scroll_rect, const void* clip_rect, void* update_rgn, void* update_rect, std::uint32_t flags) noexcept;
TL_MSABI int tl_RegisterHotKey(void* hwnd, int id, std::uint32_t modifiers, std::uint32_t vk) noexcept;
TL_MSABI int tl_UnregisterHotKey(void* hwnd, int id) noexcept;
TL_MSABI void* tl_GetProcessWindowStation() noexcept;
TL_MSABI int tl_GetUserObjectInformationW(void* handle, int index, void* info, std::uint32_t length, std::uint32_t* length_needed) noexcept;
TL_MSABI void* tl_GetShellWindow() noexcept;
TL_MSABI int tl_EnumDisplayDevicesA(const char* device, std::uint32_t dev_num, void* display_device, std::uint32_t flags) noexcept;
TL_MSABI void* tl_CreateDialogParamA(void* instance, const char* template_name, void* wnd_parent, void* dialog_func, std::intptr_t init_param) noexcept;
TL_MSABI void* tl_CreateMenu() noexcept;
TL_MSABI std::intptr_t tl_DefDlgProcA(void* hwnd, std::uint32_t msg, std::uintptr_t wparam, std::intptr_t lparam) noexcept;
TL_MSABI int tl_DeleteMenu(void* menu, std::uint32_t position, std::uint32_t flags) noexcept;
TL_MSABI std::intptr_t tl_DialogBoxParamA(void* instance, const char* template_name, void* wnd_parent, void* dialog_func, std::intptr_t init_param) noexcept;
TL_MSABI std::uint32_t tl_GetCaretBlinkTime() noexcept;
TL_MSABI void* tl_GetClipboardOwner() noexcept;
TL_MSABI std::uint32_t tl_GetDoubleClickTime() noexcept;
TL_MSABI void* tl_GetForegroundWindow() noexcept;
TL_MSABI void* tl_GetKeyboardLayout(std::uint32_t thread_id) noexcept;
TL_MSABI int tl_GetKeyboardState(std::uint8_t* key_states) noexcept;
TL_MSABI std::uint32_t tl_GetMessageTime() noexcept;
TL_MSABI std::uint32_t tl_GetQueueStatus(std::uint32_t flags) noexcept;
TL_MSABI void* tl_GetSysColorBrush(int index) noexcept;
TL_MSABI void* tl_GetSystemMenu(void* hwnd, int b_revert) noexcept;
TL_MSABI int tl_InsertMenuA(void* menu, std::uint32_t position, std::uint32_t flags, std::uintptr_t id_new_item, const char* new_item) noexcept;
TL_MSABI int tl_IsDialogMessageA(void* hwnd, void* msg) noexcept;
TL_MSABI int tl_IsIconic(void* hwnd) noexcept;
TL_MSABI void* tl_LoadImageA(void* instance, const char* name, std::uint32_t type, int cx, int cy, std::uint32_t load) noexcept;
TL_MSABI int tl_MessageBoxIndirectW(const void* msg_box_params) noexcept;
TL_MSABI int tl_OffsetRect(void* rect, int dx, int dy) noexcept;
TL_MSABI std::uint32_t tl_RegisterWindowMessageA(const char* string) noexcept;
TL_MSABI std::intptr_t tl_SendDlgItemMessageA(void* hwnd, int id_dlg_item, std::uint32_t msg, std::uintptr_t wparam, std::intptr_t lparam) noexcept;
TL_MSABI void* tl_SetActiveWindow(void* hwnd) noexcept;
TL_MSABI int tl_SetDlgItemTextA(void* hwnd, int id_dlg_item, const char* text) noexcept;
TL_MSABI int tl_SetKeyboardState(const std::uint8_t* key_states) noexcept;
TL_MSABI int tl_SystemParametersInfoA(std::uint32_t action, std::uint32_t param1, void* param2, std::uint32_t winini) noexcept;
TL_MSABI int tl_ToAsciiEx(std::uint32_t vk, std::uint32_t scan_code, const std::uint8_t* key_state, std::uint16_t* char_out, std::uint32_t flags, void* dwhkl) noexcept;
TL_MSABI std::uint32_t tl_RegisterWindowMessageW(const wchar_t* lpString) noexcept;
TL_MSABI void* tl_RemovePropW(void* hWnd, const wchar_t* lpString) noexcept;
TL_MSABI void* tl_GetPropW(void* hWnd, const wchar_t* lpString) noexcept;
TL_MSABI int tl_SetPropW(void* hWnd, const wchar_t* lpString, void* hData) noexcept;
TL_MSABI int tl_ValidateRect(void* hWnd, const void* lpRect) noexcept;
TL_MSABI int tl_DestroyCursor(void* hCursor) noexcept;
TL_MSABI void tl_NotifyWinEvent(std::uint32_t event, void* hwnd, std::int32_t idObject, std::int32_t idChild) noexcept;
TL_MSABI void* tl_MonitorFromPoint(int x, int y, std::uint32_t dwFlags) noexcept;
TL_MSABI void* tl_MonitorFromRect(const void* lprc, std::uint32_t dwFlags) noexcept;
TL_MSABI int tl_GetMonitorInfoW(void* hMonitor, void* lpmi) noexcept;
TL_MSABI void* tl_SetWindowsHookExA(int id_hook, void* lpfn, void* hmod, std::uint32_t thread_id) noexcept;
TL_MSABI std::intptr_t tl_SendMessageTimeoutA(void* hwnd, std::uint32_t msg, std::uintptr_t w_param, std::intptr_t l_param, std::uint32_t flags, std::uint32_t timeout, std::uintptr_t* result) noexcept;
TL_MSABI void* tl_WindowFromDC(void* hdc) noexcept;
TL_MSABI void* tl_FindWindowExA(void* parent, void* after, const char* class_name, const char* window_name) noexcept;
TL_MSABI int tl_EnumDisplaySettingsA(const char* device, std::uint32_t mode, void* dev_mode) noexcept;
TL_MSABI int tl_IsRectEmpty(const void* rect) noexcept;
TL_MSABI int tl_SubtractRect(void* dest, const void* src1, const void* src2) noexcept;
TL_MSABI int tl_AdjustWindowRectEx(void* lpRect, std::uint32_t dwStyle, int bMenu, std::uint32_t dwExStyle) noexcept;
TL_MSABI std::uint32_t tl_GetDlgItemTextA(void* hDlg, int nIDDlgItem, char* lpString, int cchMax) noexcept;
TL_MSABI std::uint32_t tl_GetDlgItemTextW(void* hDlg, int nIDDlgItem, wchar_t* lpString, int cchMax) noexcept;
TL_MSABI void* tl_BeginDeferWindowPos(int nNumWindows) noexcept;
TL_MSABI void* tl_DeferWindowPos(void* hWinPosInfo, void* hWnd, void* hWndInsertAfter, int x, int y, int cx, int cy, std::uint32_t uFlags) noexcept;
TL_MSABI int tl_EndDeferWindowPos(void* hWinPosInfo) noexcept;
TL_MSABI int tl_UnregisterClassW(const wchar_t* lpClassName, void* hInstance) noexcept;
TL_MSABI void* tl_GetActiveWindow() noexcept;
TL_MSABI std::intptr_t tl_CallNextHookEx(void* hhk, int nCode, std::uintptr_t wParam, std::intptr_t lParam) noexcept;
TL_MSABI int tl_UnhookWindowsHookEx(void* hhk) noexcept;
TL_MSABI void* tl_SetWindowsHookExW(int idHook, void* lpfn, void* hmod, std::uint32_t dwThreadId) noexcept;
TL_MSABI std::uint32_t tl_GetMenuState(void* hMenu, std::uint32_t uId, std::uint32_t uFlags) noexcept;
TL_MSABI int tl_InsertMenuW(void* hMenu, std::uint32_t uPosition, std::uint32_t uFlags, std::uintptr_t uIDNewItem, const wchar_t* lpNewItem) noexcept;
TL_MSABI std::uint32_t tl_GetDlgItemInt(void* hDlg, int nIDDlgItem, int* lpTranslated, int bSigned) noexcept;
TL_MSABI int tl_SetDlgItemInt(void* hDlg, int nIDDlgItem, std::uint32_t uValue, int bSigned) noexcept;
TL_MSABI void* tl_CreateDialogParamW(void* hInstance, const wchar_t* lpTemplateName, void* hWndParent, void* lpDialogFunc, std::intptr_t dwInitParam) noexcept;
TL_MSABI void* tl_CreateDialogIndirectParamW(void* hInstance, const void* lpTemplate, void* hWndParent, void* lpDialogFunc, std::intptr_t dwInitParam) noexcept;
TL_MSABI std::intptr_t tl_DialogBoxIndirectParamW(void* hInstance, const void* hDialogTemplate, void* hWndParent, void* lpDialogFunc, std::intptr_t dwInitParam) noexcept;
TL_MSABI void* tl_SetClipboardViewer(void* hWndNewViewer) noexcept;
TL_MSABI int tl_ChangeClipboardChain(void* hWndRemove, void* hWndNewNext) noexcept;
TL_MSABI int tl_DrawTextExW(void* hdc, wchar_t* lpchText, int cchText, void* lprc, std::uint32_t format, void* lpdtp) noexcept;
TL_MSABI int tl_ToAscii(std::uint32_t uVirtKey, std::uint32_t uScanCode, const std::uint8_t* lpKeyState, std::uint16_t* lpChar, std::uint32_t uFlags) noexcept;
TL_MSABI void* tl_CreateAcceleratorTableW(void* paccel, int cAccel) noexcept;
TL_MSABI int tl_DestroyAcceleratorTable(void* hAccel) noexcept;
TL_MSABI int tl_IsCharLowerW(wchar_t ch) noexcept;
TL_MSABI int tl_IsCharAlphaNumericW(wchar_t ch) noexcept;
TL_MSABI int tl_IsCharAlphaW(wchar_t ch) noexcept;
TL_MSABI int tl_ModifyMenuW(void* hMnu, std::uint32_t uPosition, std::uint32_t uFlags, std::uintptr_t uIDNewItem, const wchar_t* lpNewItem) noexcept;
TL_MSABI int tl_InflateRect(void* lprc, int dx, int dy) noexcept;
TL_MSABI int tl_IntersectRect(void* lprcDst, const void* lprcSrc1, const void* lprcSrc2) noexcept;
TL_MSABI int tl_SetRectEmpty(void* lprc) noexcept;
TL_MSABI int tl_EnumChildWindows(void* hWndParent, void* lpEnumFunc, std::intptr_t lParam) noexcept;
TL_MSABI int tl_EnumThreadWindows(std::uint32_t dwThreadId, void* lpfn, std::intptr_t lParam) noexcept;
TL_MSABI int tl_GetMenuBarInfo(void* hwnd, std::int32_t idObject, std::int32_t idItem, void* pmbi) noexcept;
TL_MSABI int tl_TrackMouseEvent(void* lpEventTrack) noexcept;
TL_MSABI int tl_GetComboBoxInfo(void* hwndCombo, void* pcbi) noexcept;
TL_MSABI void* tl_ChildWindowFromPoint(void* hWndParent, int x, int y) noexcept;
TL_MSABI int tl_GetDlgCtrlID(void* hWnd) noexcept;
TL_MSABI int tl_wsprintfW(wchar_t* lpOut, const wchar_t* lpFmt, ...) noexcept;
TL_MSABI void* tl_GetAncestor(void* hwnd, std::uint32_t gaFlags) noexcept;
TL_MSABI std::uint32_t tl_GetMenuItemID(void* hMenu, int nPos) noexcept;
TL_MSABI int tl_SetLayeredWindowAttributes(void* hwnd, std::uint32_t crKey, std::uint8_t bAlpha, std::uint32_t dwFlags) noexcept;
TL_MSABI void* tl_GetLastActivePopup(void* hWnd) noexcept;
TL_MSABI int tl_GetMenuStringW(void* hMenu, std::uint32_t uIDItem, wchar_t* lpString, int cchMax, std::uint32_t flags) noexcept;
TL_MSABI int tl_LockWindowUpdate(void* hWndLock) noexcept;
TL_MSABI void tl_mouse_event(std::uint32_t dwFlags, std::uint32_t dx, std::uint32_t dy, std::uint32_t dwData, std::uintptr_t dwExtraInfo) noexcept;
TL_MSABI int tl_SetMenuItemBitmaps(void* hMenu, std::uint32_t uPosition, std::uint32_t uFlags, void* hBitmapUnchecked, void* hBitmapChecked) noexcept;
TL_MSABI void* tl_GetDCEx(void* hWnd, void* hrgnClip, std::uint32_t flags) noexcept;
TL_MSABI int tl_IsChild(void* hWndParent, void* hWnd) noexcept;

}  // extern "C"
}  // namespace tradutorlinux
