#include "test_win32_common.hpp"

namespace tradutorlinux {
namespace {
TEST(User32Test, GetWindowTextAReturnsCopiedLength) {
    WindowSlot& slot = g_windows[0];
    slot = {};
    slot.used = true;
    slot.text = "window title";
    char output[64]{};
    EXPECT_EQ(tl_GetWindowTextA(&slot, output, sizeof(output)), 12);
    EXPECT_STREQ(output, "window title");
    slot = {};
}

TEST(UnsupportedApiTest, StubsReportFailureInsteadOfSuccess) {
    void* printer = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_OpenPrinterW(nullptr, &printer, nullptr), 0);
    EXPECT_EQ(printer, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    std::uint32_t thread_id = 123;
    EXPECT_EQ(tl_CreateRemoteThread(nullptr, nullptr, 0, nullptr, nullptr, 0, &thread_id), nullptr);
    EXPECT_EQ(thread_id, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    std::size_t written = 123;
    EXPECT_EQ(tl_WriteProcessMemory(nullptr, nullptr, nullptr, 8, &written), 0);
    EXPECT_EQ(written, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
}

TEST(WinRarCoverageTest, TickCountPrivilegeAndClsid) {
    EXPECT_GT(tl_GetTickCount(), 0U);
    EXPECT_EQ(tl_AllocConsole(), 1);
    EXPECT_EQ(tl_FreeConsole(), 1);
    EXPECT_EQ(tl_SetThreadExecutionState(1), 1U);
    EXPECT_EQ(tl_IsDBCSLeadByte('A'), 0);

    constexpr std::uint16_t src[] = {'W', 'i', 'n', 'R', 'A', 'R', 0};
    std::uint16_t dest[16]{};
    EXPECT_GT(tl_FoldStringW(0, src, -1, dest, 16), 0);

    EXPECT_EQ(tl_SetUserObjectInformationW(nullptr, 0, nullptr, 0), 1);
    EXPECT_EQ(tl_WaitForInputIdle(nullptr, 0), 0U);
    EXPECT_EQ(tl_SetProcessDefaultLayout(0), 1);

    constexpr std::uint16_t priv_name[] = {'S', 'e', 'D', 'e', 'b', 'u', 'g', 0};
    std::uint8_t luid_buf[8]{};
    EXPECT_EQ(tl_LookupPrivilegeValueW(nullptr, priv_name, luid_buf), 1);
    EXPECT_EQ(tl_AdjustTokenPrivileges(nullptr, 0, nullptr, 0, nullptr, nullptr), 1);

    std::uint8_t clsid[16]{};
    EXPECT_EQ(tl_CLSIDFromString(nullptr, clsid), 0);
    EXPECT_EQ(tl_SHGetMalloc(nullptr), static_cast<int>(0x80070057U));
}

TEST(SevenZipCoverageTest, CrtAndStreams) {
    EXPECT_EQ(tl_GetVersion(), 0x00060001U);
    EXPECT_GT(tl_GetLargePageMinimum(), 0U);
    tl_SetFileApisToOEM();
    EXPECT_EQ(tl_SetConsoleCtrlHandler(nullptr, 1), 1);
    EXPECT_EQ(tl_GetProcessTimes(nullptr, nullptr, nullptr, nullptr, nullptr), 1);
    EXPECT_EQ(tl_SetProcessAffinityMask(nullptr, 1), 1);
    EXPECT_EQ(tl_SetThreadAffinityMask(nullptr, 1), 1U);
    EXPECT_EQ(tl_ResumeThread(nullptr), 0U);

    std::uint64_t t1 = 100, t2 = 200;
    EXPECT_EQ(tl_CompareFileTime(&t1, &t2), -1);
    std::uint16_t fat_d = 0, fat_t = 0;
    EXPECT_EQ(tl_FileTimeToDosDateTime(&t1, &fat_d, &fat_t), 1);

    std::uint32_t spc = 0, bps = 0, nfc = 0, tnc = 0;
    EXPECT_EQ(tl_GetDiskFreeSpaceW(nullptr, &spc, &bps, &nfc, &tnc), 1);
    EXPECT_EQ(spc, 8U);
    EXPECT_EQ(bps, 512U);

    std::uint16_t drives[16]{};
    EXPECT_EQ(tl_GetLogicalDriveStringsW(16, drives), 4U);

    EXPECT_EQ(tl_GetFileSecurityW(nullptr, 0, nullptr, 0, nullptr), 1);
    EXPECT_EQ(tl_memcmp("abc", "abc", 3), 0);

    constexpr std::uint16_t w1[] = {'a', 'b', 'c', 0};
    constexpr std::uint16_t w2[] = {'a', 'b', 'c', 0};
    EXPECT_EQ(tl_wcscmp(w1, w2), 0);
    EXPECT_NE(tl_wcsstr(w1, w2), nullptr);

    EXPECT_EQ(tl__XcptFilter(0, nullptr), 1);
    tl__c_exit();
}

TEST(RockstarCoverageTest, NamedPipesClipboardAndRegistry) {
    EXPECT_EQ(tl_SetNamedPipeHandleState(nullptr, nullptr, nullptr, nullptr), 1);
    constexpr std::uint16_t pipe_name[] = {'\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', 0};
    EXPECT_EQ(tl_WaitNamedPipeW(pipe_name, 0), 1);
    std::uint32_t bread = 0, bavail = 0, bleft = 0;
    EXPECT_EQ(tl_PeekNamedPipe(nullptr, nullptr, 0, &bread, &bavail, &bleft), 1);
    std::uint32_t code = 0;
    EXPECT_EQ(tl_GetExitCodeThread(nullptr, &code), 1);
    EXPECT_EQ(tl_TryAcquireSRWLockExclusive(nullptr), 1);

    EXPECT_EQ(tl_SetThreadLocale(1033), 1);
    EXPECT_EQ(tl_SetThreadUILanguage(1033), 1033);
    EXPECT_EQ(tl_GetUserDefaultUILanguage(), 0x0409);
    EXPECT_EQ(tl_GetLogicalDrives(), (1U << 2));
    std::uint64_t total_kb = 0;
    EXPECT_EQ(tl_GetPhysicallyInstalledSystemMemory(&total_kb), 1);
    EXPECT_GT(total_kb, 0U);

    char vol[16]{};
    EXPECT_EQ(tl_GetVolumePathNameA("C:\\test", vol, 16), 1);
    EXPECT_STREQ(vol, "C:\\");

    EXPECT_EQ(tl_RegDeleteTreeW(nullptr, nullptr), 0);
    EXPECT_EQ(tl_RegDeleteKeyExW(nullptr, nullptr, 0, 0), 0);

    EXPECT_EQ(tl_OpenClipboard(nullptr), 1);
    EXPECT_EQ(tl_EmptyClipboard(), 1);
    EXPECT_EQ(tl_SetClipboardData(1, nullptr), nullptr);
    EXPECT_EQ(tl_CloseClipboard(), 1);

    std::uint16_t pt[2]{};
    EXPECT_EQ(tl_ClientToScreen(nullptr, pt), 1);

    std::int32_t sz[2]{};
    EXPECT_EQ(tl_GetTextExtentPoint32W(nullptr, pipe_name, 8, sz), 1);
    EXPECT_EQ(tl_StartDocW(nullptr, nullptr), 1);
    EXPECT_EQ(tl_EndDoc(nullptr), 1);

    std::uint16_t path[16] = {'C', ':', '\\', 'a', 0};
    EXPECT_EQ(tl_PathStripToRootW(path), 1);

    EXPECT_EQ(tl_UnregisterWaitEx(nullptr, nullptr), 1);
    void* wait_obj = nullptr;
    EXPECT_EQ(tl_RegisterWaitForSingleObject(&wait_obj, nullptr, nullptr, nullptr, 0, 0), 1);
    EXPECT_NE(wait_obj, nullptr);
    EXPECT_EQ(tl_SetSearchPathMode(1), 1);

    void* list_head = nullptr;
    void* entry1 = nullptr;
    EXPECT_EQ(tl_InterlockedPushEntrySList(&list_head, &entry1), nullptr);
    EXPECT_EQ(list_head, &entry1);

    EXPECT_EQ(tl_SetWindowSubclass(nullptr, nullptr, 1, 0), 1);
    EXPECT_EQ(tl_RemoveWindowSubclass(nullptr, nullptr, 1), 1);
    EXPECT_EQ(tl_DefSubclassProc(nullptr, 0, 0, 0), 0);
}

TEST(SevenZipGuiCoverageTest, AllApisAndModules) {
    // MSVCRT rand/srand
    tl_srand(1234);
    EXPECT_GE(tl_rand(), 0);

    // KERNEL32
    std::uint16_t sample_str[] = {'t', 'e', 's', 't', 0};
    EXPECT_EQ(tl_lstrlenW(sample_str), 4);
    EXPECT_EQ(tl_GetSystemDefaultLangID(), 0x0409);
    EXPECT_EQ(tl_GetUserDefaultLangID(), 0x0409);
    std::uint16_t win_dir[32]{};
    EXPECT_GT(tl_GetWindowsDirectoryW(win_dir, 32), 0U);
    EXPECT_GT(tl_GlobalSize(reinterpret_cast<void*>(0x1000)), 0U);
    EXPECT_EQ(tl_SetPriorityClass(nullptr, 0), 1);

    void* change_handle = tl_FindFirstChangeNotificationW(sample_str, 0, 0);
    EXPECT_NE(change_handle, nullptr);
    EXPECT_EQ(tl_FindNextChangeNotification(change_handle), 1);
    EXPECT_EQ(tl_FindCloseChangeNotification(change_handle), 1);

    // USER32 Menus & Dialogs & Placement
    void* menu = tl_GetMenu(nullptr);
    EXPECT_NE(menu, nullptr);
    EXPECT_EQ(tl_SetMenu(nullptr, menu), 1);
    EXPECT_NE(tl_GetSubMenu(menu, 0), nullptr);
    EXPECT_GT(tl_GetMenuItemCount(menu), 0);
    EXPECT_EQ(tl_GetMenuItemInfoW(menu, 0, 1, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetMenuItemInfoW(menu, 0, 1, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_InsertMenuItemW(menu, 0, 1, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_RemoveMenu(menu, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_EnableMenuItem(menu, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_CheckMenuItem(menu, 0, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_CheckMenuRadioItem(menu, 0, 1, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_DrawMenuBar(nullptr), 1);
    EXPECT_EQ(tl_TrackPopupMenuEx(menu, 0, 0, 0, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_LoadMenuW(nullptr, sample_str), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorResourceNotFound);

    EXPECT_EQ(tl_CheckDlgButton(nullptr, 100, 1), 1);
    EXPECT_EQ(tl_IsDlgButtonChecked(nullptr, 100), 0U);
    EXPECT_EQ(tl_CheckRadioButton(nullptr, 100, 102, 100), 1);
    EXPECT_EQ(tl_MapDialogRect(nullptr, nullptr), 1);
    EXPECT_GT(tl_GetDialogBaseUnits(), 0U);

    EXPECT_NE(tl_WindowFromPoint(0), nullptr);
    EXPECT_NE(tl_ChildWindowFromPointEx(nullptr, 0, 0), nullptr);
    EXPECT_EQ(tl_GetWindowPlacement(nullptr, nullptr), 1);
    EXPECT_EQ(tl_SetWindowPlacement(nullptr, nullptr), 1);
    EXPECT_EQ(tl_IsWindowEnabled(nullptr), 1);
    EXPECT_EQ(tl_IsZoomed(nullptr), 0);
    EXPECT_EQ(tl_GetClassInfoW(nullptr, sample_str, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorClassDoesNotExist);
    EXPECT_EQ(tl_GetMonitorInfoA(nullptr, nullptr), 1);
    EXPECT_EQ(tl_SystemParametersInfoW(0, 0, nullptr, 0), 1);

    EXPECT_NE(tl_LoadAcceleratorsW(nullptr, sample_str), nullptr);
    EXPECT_EQ(tl_TranslateAcceleratorW(nullptr, nullptr, nullptr), 0);
    EXPECT_NE(tl_LoadBitmapW(nullptr, sample_str), nullptr);
    EXPECT_GT(tl_MapVirtualKeyW(65, 0), 0U);
    EXPECT_GT(tl_RegisterClipboardFormatW(sample_str), 0U);

    // MPR (WNet)
    void* enum_handle = nullptr;
    EXPECT_EQ(tl_WNetOpenEnumW(0, 0, 0, nullptr, &enum_handle), 0U);
    EXPECT_NE(enum_handle, nullptr);
    std::uint32_t count = 10;
    EXPECT_EQ(tl_WNetEnumResourceW(enum_handle, &count, nullptr, nullptr),
              kWNetNoMoreEntries);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(tl_WNetCloseEnum(enum_handle), 0U);
    EXPECT_EQ(tl_WNetAddConnection2W(nullptr, nullptr, nullptr, 0), 0U);
    EXPECT_EQ(tl_WNetGetResourceInformationW(nullptr, nullptr, nullptr, nullptr), 0U);
    EXPECT_EQ(tl_WNetGetResourceParentW(nullptr, nullptr, nullptr), 0U);

    // COMCTL32 & COMDLG32
    g_windows = {};
    WindowSlot& common_control_parent = g_windows[0];
    common_control_parent.used = true;
    common_control_parent.native = reinterpret_cast<gui::NativeWindow>(0x1234U);
    common_control_parent.width = 800;
    common_control_parent.height = 600;
    EXPECT_NE(tl_CreateStatusWindowW(0, sample_str, &common_control_parent, 1), nullptr);
    EXPECT_NE(tl_CreateToolbarEx(&common_control_parent, 0, 1, 0, nullptr, 0, nullptr, 0, 16,
                                 16, 16, 16, 0),
              nullptr);
    g_windows = {};
    EXPECT_EQ(tl_ImageList_GetImageCount(nullptr), 0);
    EXPECT_EQ(tl_PropertySheetW(nullptr), 1);
    EXPECT_EQ(tl_CommDlgExtendedError(), 0U);

    // SHELL32
    void* icon_lg = nullptr;
    void* icon_sm = nullptr;
    EXPECT_EQ(tl_ExtractIconExW(sample_str, 0, &icon_lg, &icon_sm, 1), 1U);
    EXPECT_NE(icon_lg, nullptr);
    EXPECT_NE(icon_sm, nullptr);
    void* ppshf = nullptr;
    EXPECT_EQ(tl_SHGetDesktopFolder(&ppshf), 0);
    EXPECT_NE(ppshf, nullptr);
    void* ppidl = nullptr;
    EXPECT_EQ(tl_SHGetSpecialFolderLocation(nullptr, 0, &ppidl), 0);
    EXPECT_NE(ppidl, nullptr);
    std::uint16_t special_path[260]{};
    EXPECT_EQ(tl_SHGetSpecialFolderPathW(nullptr, special_path, 0, 0), 1);

    // OLE32 DragDrop
    EXPECT_EQ(tl_RegisterDragDrop(nullptr, nullptr), 0);
    EXPECT_EQ(tl_RevokeDragDrop(nullptr), 0);
    std::uint32_t effect = 0;
    EXPECT_EQ(tl_DoDragDrop(nullptr, nullptr, 1, &effect), 0x00040100);
    EXPECT_EQ(effect, 0U);
    tl_ReleaseStgMedium(nullptr);

    // ADVAPI32
    std::uint16_t username[32]{};
    std::uint32_t user_len = 32;
    EXPECT_EQ(tl_GetUserNameW(username, &user_len), 1);
    EXPECT_GT(user_len, 0U);

    std::uint32_t sid_sz = 32;
    std::uint32_t dom_sz = 32;
    std::uint16_t dom[32]{};
    std::uint8_t sid[32]{};
    std::uint32_t sid_use = 0;
    EXPECT_EQ(tl_LookupAccountNameW(nullptr, username, sid, &sid_sz, dom, &dom_sz, &sid_use), 1);

    void* policy = nullptr;
    EXPECT_EQ(tl_LsaOpenPolicy(nullptr, nullptr, 0, &policy), 0);
    EXPECT_NE(policy, nullptr);
    EXPECT_EQ(tl_LsaAddAccountRights(policy, nullptr, nullptr, 0), 0);
    EXPECT_EQ(tl_LsaClose(policy), 0);
}

TEST(Win32LocaleTest, WideStringApisUseGuestUtf16Units) {
    constexpr std::uint16_t source[] = {'C', ':', '\\', 'T', 'e', 'm', 'p', 0};
    constexpr std::uint16_t other_case[] = {'c', ':', '\\', 't', 'e', 'm', 'p', 0};
    std::uint16_t copied[16]{};
    std::uint16_t bounded[16]{};

    EXPECT_EQ(tl_lstrcpyW(copied, source), copied);
    EXPECT_EQ(tl_lstrcpynW(bounded, source, 16), bounded);
    EXPECT_EQ(tl_lstrcmpW(copied, source), 0);
    EXPECT_EQ(tl_lstrcmpiW(copied, other_case), 0);
    for (std::size_t index = 0; index < sizeof(source) / sizeof(source[0]); ++index) {
        EXPECT_EQ(copied[index], source[index]);
        EXPECT_EQ(bounded[index], source[index]);
    }
}

TEST(PuttyCoverageTest, AllApisAndModules) {
    // WS2_32
    char wsa_data[512]{};
    EXPECT_EQ(tl_WSAStartup(0x0202, wsa_data), 0);
    void* wsa_ev = tl_WSACreateEvent();
    EXPECT_NE(wsa_ev, nullptr);
    EXPECT_EQ(tl_WSASetEvent(wsa_ev), 1);
    EXPECT_EQ(tl_WSAResetEvent(wsa_ev), 1);
    const void* ev_array[] = {wsa_ev};
    EXPECT_EQ(tl_WSAWaitForMultipleEvents(1, ev_array, 0, 10, 0), 0U);
    EXPECT_EQ(tl_WSACloseEvent(wsa_ev), 1);
    EXPECT_NE(tl_gethostbyname("localhost"), nullptr);
    EXPECT_NE(tl_getservbyname("ssh", "tcp"), nullptr);
    tl_WSASetLastError(0);
    EXPECT_EQ(tl_WSAGetLastError(), 0);
    EXPECT_EQ(tl___WSAFDIsSet(0, nullptr), 0);

    // GDI32
    char tm_buf[64]{};
    EXPECT_EQ(tl_GetTextMetricsW(nullptr, tm_buf), 1);
    EXPECT_EQ(tl_GetTextMetricsA(nullptr, tm_buf), 1);
    void* pen = tl_CreatePen(0, 1, 0);
    EXPECT_NE(pen, nullptr);
    EXPECT_EQ(tl_ExtTextOutW(nullptr, 0, 0, 0, nullptr, nullptr, 0, nullptr), 1);
    EXPECT_EQ(tl_ExtTextOutA(nullptr, 0, 0, 0, nullptr, nullptr, 0, nullptr), 1);
    EXPECT_EQ(tl_MoveToEx(nullptr, 0, 0, nullptr), 1);
    EXPECT_EQ(tl_LineTo(nullptr, 10, 10), 1);
    EXPECT_EQ(tl_Polyline(nullptr, nullptr, 0), 1);
    EXPECT_EQ(tl_Polygon(nullptr, nullptr, 0), 1);
    void* rgn = tl_CreateRectRgn(0, 0, 10, 10);
    EXPECT_NE(rgn, nullptr);
    EXPECT_EQ(tl_CombineRgn(nullptr, rgn, rgn, 0), 2);
    EXPECT_EQ(tl_SelectClipRgn(nullptr, rgn), 2);
    char clip_rc[16]{};
    EXPECT_EQ(tl_GetClipBox(nullptr, clip_rc), 2);
    int char_w[10]{};
    EXPECT_EQ(tl_GetCharWidthW(nullptr, 0, 4, char_w), 1);
    EXPECT_EQ(tl_GetCharWidth32W(nullptr, 0, 4, char_w), 1);
    int txt_sz[2]{};
    EXPECT_EQ(tl_GetTextExtentPoint32A(nullptr, "test", 4, txt_sz), 1);
    EXPECT_EQ(tl_SetTextAlign(nullptr, 0), 0U);
    EXPECT_EQ(tl_GetTextAlign(nullptr), 0U);
    EXPECT_EQ(tl_SetROP2(nullptr, 1), 1);
    EXPECT_EQ(tl_GetSystemPaletteEntries(nullptr, 0, 10, nullptr), 10U);

    // USER32
    EXPECT_EQ(tl_CreateCaret(nullptr, nullptr, 1, 10), 1);
    EXPECT_EQ(tl_SetCaretPos(0, 0), 1);
    EXPECT_EQ(tl_ShowCaret(nullptr), 1);
    EXPECT_EQ(tl_HideCaret(nullptr), 1);
    char pt[8]{};
    EXPECT_EQ(tl_GetCaretPos(pt), 1);
    EXPECT_EQ(tl_DestroyCaret(), 1);
    EXPECT_EQ(tl_SetScrollInfo(nullptr, 0, nullptr, 1), 0);
    EXPECT_EQ(tl_GetScrollInfo(nullptr, 0, nullptr), 1);
    EXPECT_EQ(tl_ShowScrollBar(nullptr, 0, 1), 1);
    EXPECT_EQ(tl_EnableScrollBar(nullptr, 0, 0), 1);
    EXPECT_EQ(tl_SetScrollPos(nullptr, 0, 5, 1), 5);
    EXPECT_EQ(tl_GetScrollPos(nullptr, 0), 0);
    EXPECT_EQ(tl_SetScrollRange(nullptr, 0, 0, 100, 1), 1);
    int min_p = 0, max_p = 0;
    EXPECT_EQ(tl_GetScrollRange(nullptr, 0, &min_p, &max_p), 1);
    EXPECT_EQ(tl_SetCapture(reinterpret_cast<void*>(0x1000)), nullptr);
    EXPECT_EQ(tl_ReleaseCapture(), 1);
    EXPECT_EQ(tl_GetCapture(), nullptr);
    EXPECT_EQ(tl_GetAsyncKeyState(0), 0);
    EXPECT_EQ(tl_GetKeyState(0), 0);
    EXPECT_EQ(tl_FlashWindow(nullptr, 1), 0);
    EXPECT_EQ(tl_FlashWindowEx(nullptr), 1);
    EXPECT_EQ(tl_GetSysColor(0), 0x00FFFFFFU);
    int sys_el = 0;
    std::uint32_t sys_c = 0;
    EXPECT_EQ(tl_SetSysColors(1, &sys_el, &sys_c), 1);
    EXPECT_EQ(tl_MessageBeep(0), 1);
    EXPECT_EQ(tl_TrackPopupMenu(nullptr, 0, 0, 0, 0, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetClipboardData(1), nullptr);
    EXPECT_EQ(tl_IsClipboardFormatAvailable(1), 0);
    EXPECT_EQ(tl_RegisterClipboardFormatA("test_fmt"), kRegisteredClipboardFormat);
    EXPECT_EQ(tl_CountClipboardFormats(), 0);
    EXPECT_EQ(tl_EnumClipboardFormats(0), 0U);

    // COMDLG32 & IMM32 & SHELL32
    EXPECT_EQ(tl_ChooseFontA(nullptr), 1);
    EXPECT_EQ(tl_ChooseFontW(nullptr), 1);
    EXPECT_EQ(tl_ImmGetVirtualKey(nullptr), 0U);
    EXPECT_EQ(tl_ShellNotifyIconW(0, nullptr), 1);

    // ADVAPI32
    std::uint32_t subk = 0;
    EXPECT_EQ(tl_RegQueryInfoKeyA(nullptr, nullptr, nullptr, nullptr, &subk, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_RegQueryInfoKeyW(nullptr, nullptr, nullptr, nullptr, &subk, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), 0);
    char reg_name[32]{};
    EXPECT_EQ(tl_RegEnumKeyA(nullptr, 0, reg_name, 32), 259);
    EXPECT_EQ(tl_RegEnumValueA(nullptr, 0, reg_name, nullptr, nullptr, nullptr, nullptr, nullptr), 259);
    EXPECT_EQ(tl_RegDeleteKeyA(nullptr, "test"), 0);
}

TEST(NotepadPlusPlusCoverageTest, AllApisAndModules) {
    // DWMAPI
    int comp_enabled = 0;
    EXPECT_EQ(tl_DwmIsCompositionEnabled(&comp_enabled), 0);
    EXPECT_EQ(comp_enabled, 1);
    EXPECT_EQ(tl_DwmSetWindowAttribute(nullptr, 0, nullptr, 0), 0);
    std::intptr_t dwm_res = 0;
    EXPECT_EQ(tl_DwmDefWindowProc(nullptr, 0, 0, 0, &dwm_res), 0);
    std::uint32_t dwm_col = 0;
    int dwm_op = 0;
    EXPECT_EQ(tl_DwmGetColorizationColor(&dwm_col, &dwm_op), 0);
    EXPECT_EQ(tl_DwmFlush(), 0);

    // VERSION
    std::uint32_t ver_h = 0;
    EXPECT_EQ(tl_GetFileVersionInfoSizeA("test.exe", &ver_h), 512U);
    EXPECT_EQ(tl_GetFileVersionInfoSizeW(nullptr, &ver_h), 512U);
    char ver_buf[512]{};
    EXPECT_EQ(tl_GetFileVersionInfoA("test.exe", 0, 512, ver_buf), 1);
    void* q_buf = nullptr;
    std::uint32_t q_len = 0;
    EXPECT_EQ(tl_VerQueryValueA(ver_buf, "\\", &q_buf, &q_len), 1);
    EXPECT_NE(q_buf, nullptr);

    // UxTheme
    void* theme = tl_OpenThemeData(nullptr, nullptr);
    EXPECT_NE(theme, nullptr);
    EXPECT_EQ(tl_IsThemeActive(), 1);
    EXPECT_EQ(tl_IsAppThemed(), 1);
    EXPECT_EQ(tl_DrawThemeBackground(theme, nullptr, 0, 0, nullptr, nullptr), 0);
    EXPECT_EQ(tl_CloseThemeData(theme), 0);
    EXPECT_EQ(tl_BufferedPaintInit(), 0);
    EXPECT_EQ(tl_BufferedPaintUnInit(), 0);

    // COMCTL32
    int btn = 0;
    EXPECT_EQ(tl_TaskDialog(nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, &btn), 0);
    EXPECT_EQ(btn, 1);
    EXPECT_EQ(tl_ImageList_Draw(nullptr, 0, nullptr, 0, 0, 0), 1);
    EXPECT_EQ(tl_ImageList_GetBkColor(nullptr), 0xFFFFFFFFU);

    // USER32 & GDI32 DPI, Regions, Blt
    EXPECT_EQ(tl_GetDpiForSystem(), 96U);
    EXPECT_EQ(tl_SetProcessDPIAware(), 1);
    void* pbrush = tl_CreatePatternBrush(nullptr);
    EXPECT_NE(pbrush, nullptr);
    EXPECT_EQ(tl_PatBlt(nullptr, 0, 0, 10, 10, 0), 1);
    EXPECT_EQ(tl_TransparentBlt(nullptr, 0, 0, 10, 10, nullptr, 0, 0, 10, 10, 0), 1);
    EXPECT_EQ(tl_AlphaBlend(nullptr, 0, 0, 10, 10, nullptr, 0, 0, 10, 10, 0), 1);
    void* poly_rgn = tl_CreatePolygonRgn(nullptr, 0, 0);
    EXPECT_NE(poly_rgn, nullptr);
    EXPECT_EQ(tl_SetWindowRgn(nullptr, poly_rgn, 1), 1);
    EXPECT_EQ(tl_RegisterHotKey(nullptr, 1, 0, 0), 1);
    EXPECT_EQ(tl_UnregisterHotKey(nullptr, 1), 1);
}

TEST(RobloxCoverageTest, AllApisAndModules) {
    // KERNEL32
    std::array<std::uint8_t, 128> mem_counters{};
    EXPECT_EQ(tl_K32GetProcessMemoryInfo(nullptr, mem_counters.data(), static_cast<std::uint32_t>(mem_counters.size())), 1);
    char img_name[256]{};
    EXPECT_GT(tl_K32GetProcessImageFileNameA(nullptr, img_name, sizeof(img_name)), 0U);
    EXPECT_EQ(tl_GetCurrentProcessorNumber(), 0U);
    EXPECT_NE(tl_GetCurrentThread(), nullptr);
    EXPECT_EQ(tl_SwitchToThread(), 1);
    EXPECT_EQ(tl_TryEnterCriticalSection(nullptr), 0);
    EXPECT_EQ(tl_SleepEx(1, 0), 0U);
    EXPECT_EQ(tl_GetDiskFreeSpaceA(nullptr, nullptr, nullptr, nullptr, nullptr), 1);
    char temp_a[64]{};
    EXPECT_GT(tl_GetTempPathA(sizeof(temp_a), temp_a), 0U);
    EXPECT_STREQ(temp_a, "C:\\windows\\temp\\");
    std::uint16_t temp_w[64]{};
    EXPECT_GT(tl_GetTempPathW(64, temp_w), 0U);
    constexpr char16_t expected_temp_w[] = u"C:\\windows\\temp\\";
    for (std::size_t index = 0; index < std::size(expected_temp_w); ++index) {
        EXPECT_EQ(temp_w[index], static_cast<std::uint16_t>(expected_temp_w[index]));
    }
    const std::string move_source = "_tl_move_source_" + std::to_string(::getpid());
    const std::string move_destination = "_tl_move_destination_" + std::to_string(::getpid());
    {
        std::FILE* const file = std::fopen(move_source.c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::fputs("move", file);
        std::fclose(file);
        std::FILE* const old_file = std::fopen(move_destination.c_str(), "wb");
        ASSERT_NE(old_file, nullptr);
        std::fputs("old", old_file);
        std::fclose(old_file);
    }
    EXPECT_EQ(tl_MoveFileExA(move_source.c_str(), move_destination.c_str(), 1), 1);
    EXPECT_FALSE(std::filesystem::exists(move_source));
    EXPECT_TRUE(std::filesystem::exists(move_destination));
    std::error_code move_error;
    std::filesystem::remove(move_destination, move_error);
    EXPECT_EQ(tl_LockFile(nullptr, 0, 0, 0, 0), 1);
    EXPECT_EQ(tl_UnlockFile(nullptr, 0, 0, 0, 0), 1);
    std::uintptr_t init_once = 0;
    int pending = 0;
    EXPECT_EQ(tl_InitOnceBeginInitialize(&init_once, 0, &pending, nullptr), 1);
    EXPECT_EQ(tl_InitOnceComplete(&init_once, 0, nullptr), 1);
    void* timer = tl_CreateWaitableTimerA(nullptr, 0, nullptr);
    EXPECT_NE(timer, nullptr);
    EXPECT_EQ(tl_SetWaitableTimer(timer, nullptr, 0, nullptr, nullptr, 0), 1);
    EXPECT_EQ(tl_CancelWaitableTimer(timer), 1);

    // ADVAPI32
    std::uintptr_t hash = 0;
    EXPECT_EQ(tl_CryptCreateHash(0, 0x8004, 0, 0, &hash), 1);
    EXPECT_EQ(tl_CryptHashData(hash, nullptr, 0, 0), 1);
    std::uint32_t hash_len = 0;
    EXPECT_EQ(tl_CryptGetHashParam(hash, 2, nullptr, &hash_len, 0), 1);
    EXPECT_EQ(tl_CryptDestroyHash(hash), 1);
    std::uint8_t rnd[16]{};
    EXPECT_EQ(tl_SystemFunction036(rnd, sizeof(rnd)), 1);
    EXPECT_NE(tl_RegisterEventSourceW(nullptr, L"Roblox"), nullptr);
    EXPECT_EQ(tl_DeregisterEventSource(nullptr), 1);
    EXPECT_EQ(tl_ReportEventW(nullptr, 1, 0, 100, nullptr, 0, 0, nullptr, nullptr), 1);

    // WS2_32
    EXPECT_EQ(tl_htons(80), 80U << 8);
    EXPECT_EQ(tl_ntohs(80U << 8), 80);
    EXPECT_EQ(tl_htonl(0x12345678), 0x78563412U);
    EXPECT_EQ(tl_ntohl(0x78563412), 0x12345678U);
    char hostname[64]{};
    EXPECT_EQ(tl_gethostname(hostname, sizeof(hostname)), 0);
    EXPECT_EQ(tl_WSAIoctl(0, 0, nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_getnameinfo(nullptr, 0, hostname, sizeof(hostname), nullptr, 0, 0), 0);

    // USER32, ole32, CRYPT32
    EXPECT_NE(tl_GetProcessWindowStation(), nullptr);
    EXPECT_NE(tl_GetShellWindow(), nullptr);
    std::uint32_t uo_len = 0;
    EXPECT_EQ(tl_GetUserObjectInformationW(nullptr, 1, nullptr, 0, &uo_len), 1);
    wchar_t guid_str[64]{};
    std::uint8_t guid_bytes[16]{};
    EXPECT_EQ(tl_StringFromGUID2(guid_bytes, guid_str, sizeof(guid_str) / sizeof(wchar_t)), 39);
    EXPECT_EQ(tl_CertGetIntendedKeyUsage(0, nullptr, nullptr, 0), 1);
    EXPECT_EQ(tl_TrackMouseEvent_alias(nullptr), 1);
}

}  // namespace
}  // namespace tradutorlinux
