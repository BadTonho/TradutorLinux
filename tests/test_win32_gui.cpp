#include "test_win32_common.hpp"

namespace tradutorlinux {
namespace {
TEST(Win32GuiAbiTest, TargetControlLayoutsMatchMicrosoftX64) {
    EXPECT_EQ(sizeof(abi::GuestWndClassA), 72U);
    EXPECT_EQ(sizeof(abi::GuestWndClassExA), 80U);
    EXPECT_EQ(sizeof(abi::GuestLvColumnA), 32U);
    EXPECT_EQ(sizeof(abi::GuestLvItemA), 72U);
    EXPECT_EQ(sizeof(abi::GuestNmListView), 64U);
}

TEST(Win32GuiTest, MulDivRoundsAndRejectsZeroDenominator) {
    EXPECT_EQ(tl_MulDiv(5, 3, 2), 8);
    EXPECT_EQ(tl_MulDiv(-5, 3, 2), -8);
    EXPECT_EQ(tl_MulDiv(1, 1, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32GuiTest, PopupMenuHandleHasLifecycle) {
    void* menu = tl_CreatePopupMenu();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(tl_AppendMenuA(menu, 0, 101, "Exit"), 1);
    EXPECT_EQ(tl_DestroyMenu(menu), 1);
}

TEST(Win32GuiTest, RejectsStatefulGuiCallsFromNonPrimaryGuestThread) {
    void* worker_menu = nullptr;
    std::uint32_t worker_menu_error = abi::kErrorSuccess;
    int worker_get_message = 0;
    std::uint32_t worker_message_error = abi::kErrorSuccess;
    std::thread worker([&] {
        g_current_thread_id = 2;
        worker_menu = tl_CreatePopupMenu();
        worker_menu_error = tl_GetLastError();
        abi::GuestMsg message{};
        worker_get_message = tl_GetMessageA(&message, nullptr, 0, 0);
        worker_message_error = tl_GetLastError();
    });
    worker.join();

    EXPECT_EQ(worker_menu, nullptr);
    EXPECT_EQ(worker_menu_error, abi::kErrorNotSupported);
    EXPECT_EQ(worker_get_message, -1);
    EXPECT_EQ(worker_message_error, abi::kErrorNotSupported);

    void* main_menu = tl_CreatePopupMenu();
    ASSERT_NE(main_menu, nullptr);
    EXPECT_EQ(tl_DestroyMenu(main_menu), 1);
}


TEST(Win32DialogTemplateTest, ParsesAlignedStandardTemplateAndRejectsBounds) {
    EXPECT_EQ(sizeof(runtime::GuestDialogTemplate), 18U);
    EXPECT_EQ(sizeof(runtime::GuestDialogItemTemplate), 18U);
    EXPECT_EQ(sizeof(abi::GuestInitCommonControlsEx), 8U);
    runtime::DialogTemplate parsed{};
    const std::vector<std::byte> bytes = valid_dialog_template();
    EXPECT_EQ(runtime::parse_dialog_template(bytes, parsed),
              runtime::DialogTemplateStatus::Success);
    ASSERT_EQ(parsed.controls.size(), 1U);
    EXPECT_EQ(parsed.controls[0].id, 7U);
    EXPECT_EQ(parsed.controls[0].title, u"OK");

    std::vector<std::byte> truncated = bytes;
    truncated.pop_back();
    EXPECT_EQ(runtime::parse_dialog_template(truncated, parsed),
              runtime::DialogTemplateStatus::Malformed);

    std::vector<std::byte> dialog_ex = bytes;
    dialog_ex[0] = std::byte{1};
    dialog_ex[1] = std::byte{0};
    dialog_ex[2] = std::byte{0xFF};
    dialog_ex[3] = std::byte{0xFF};
    EXPECT_EQ(runtime::parse_dialog_template(dialog_ex, parsed),
              runtime::DialogTemplateStatus::DialogEx);
}

TEST(Win32DialogTemplateTest, RejectsUnsupportedMenuClassFontAndControl) {
    runtime::DialogTemplate parsed{};
    std::vector<std::byte> custom_menu = valid_dialog_template();
    custom_menu[18] = std::byte{1};
    EXPECT_EQ(runtime::parse_dialog_template(custom_menu, parsed),
              runtime::DialogTemplateStatus::Unsupported);

    std::vector<std::byte> font = valid_dialog_template();
    font[0] = std::byte{0x40};
    EXPECT_EQ(runtime::parse_dialog_template(font, parsed),
              runtime::DialogTemplateStatus::Unsupported);

    std::vector<std::byte> custom_control = valid_dialog_template();
    custom_control[48] = std::byte{0x83};
    EXPECT_EQ(runtime::parse_dialog_template(custom_control, parsed),
              runtime::DialogTemplateStatus::Unsupported);
}

TEST(Win32DialogTest, LogicalChildrenTabTextGeometryAndWindowLongWrappers) {
    g_windows = {};
    g_focused_control = nullptr;
    WindowSlot& dialog = g_windows[0];
    dialog.used = true;
    dialog.is_dialog = true;
    dialog.x = 10;
    dialog.y = 20;
    dialog.width = 200;
    dialog.height = 100;
    WindowSlot& edit = g_windows[1];
    edit.used = true;
    edit.is_control = true;
    edit.parent = &dialog;
    edit.control_id = 100;
    edit.control_kind = ControlKind::Edit;
    edit.style = 0x00010000U;
    edit.x = 4;
    edit.y = 5;
    edit.width = 80;
    edit.height = 18;
    edit.text = "old";
    WindowSlot& button = g_windows[2];
    button.used = true;
    button.is_control = true;
    button.parent = &dialog;
    button.control_id = 1;
    button.control_kind = ControlKind::Button;
    button.style = 0x00010000U;
    button.x = 5;
    button.y = 70;
    button.width = 60;
    button.height = 20;
    dialog.dialog_children = {&edit, &button};

    EXPECT_EQ(tl_GetDlgItem(&dialog, 100), &edit);
    const std::uint16_t text[] = {'n', 'e', 'w', 0};
    EXPECT_EQ(tl_SetDlgItemTextW(&dialog, 100, text), 1);
    EXPECT_EQ(edit.text, "new");
    EXPECT_EQ(tl_GetNextDlgTabItem(&dialog, nullptr, 0), &edit);
    EXPECT_EQ(tl_GetNextDlgTabItem(&dialog, &edit, 0), &button);
    EXPECT_EQ(tl_GetWindowLongW(&edit, -12), 100);
    EXPECT_EQ(tl_SetWindowLongW(&edit, -21, 0x1234), 0);
    EXPECT_EQ(tl_GetWindowLongW(&edit, -21), 0x1234);
    abi::GuestRect rect{};
    EXPECT_EQ(tl_GetWindowRect(&edit, &rect), 1);
    EXPECT_EQ(rect.left, 14);
    EXPECT_EQ(rect.top, 25);
    EXPECT_EQ(rect.right, 94);
    EXPECT_EQ(rect.bottom, 43);

    abi::GuestMsg message{};
    message.hwnd = &dialog;
    message.message = abi::kWmKeyDown;
    message.wparam = abi::kVkTab;
    EXPECT_EQ(tl_IsDialogMessageW(&dialog, &message), 1);
    EXPECT_EQ(g_focused_control, &edit);
    EXPECT_EQ(tl_EndDialog(&dialog, 1), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
    g_windows = {};
    g_focused_control = nullptr;
}

TEST(Win32DialogTest, InitCommonControlsAndIconCopiesValidateInputs) {
    abi::GuestInitCommonControlsEx common{sizeof(abi::GuestInitCommonControlsEx), 0x4000U};
    EXPECT_EQ(tl_InitCommonControlsEx(&common), 1);
    common.size = 4;
    EXPECT_EQ(tl_InitCommonControlsEx(&common), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_InitCommonControlsEx(reinterpret_cast<const void*>(0x1)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    void* const copy = tl_CopyImage(reinterpret_cast<const void*>(1U), 1U, 0, 0, 0);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(tl_DestroyIcon(copy), 1);
    EXPECT_EQ(tl_DestroyIcon(reinterpret_cast<const void*>(0x1234U)), 0);
}

TEST(Win32DialogTest, RejectsInvalidModalInputsAndUnknownTemplates) {
    const auto* const numeric_template = reinterpret_cast<const std::uint16_t*>(101U);
    EXPECT_EQ(tl_DialogBoxParamW(nullptr, numeric_template, nullptr, 0, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    const std::uint16_t invalid_template[] = {0};
    EXPECT_EQ(tl_DialogBoxParamW(nullptr, invalid_template, nullptr, 1, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}


TEST(Gdi32Test, BitmapAndHardLinkOperations) {
    void* bmp = tl_CreateBitmap(64, 64, 1, 32, nullptr);
    ASSERT_NE(bmp, nullptr);

    char bmp_info[64]{};
    EXPECT_GT(tl_GetObjectW(bmp, sizeof(bmp_info), bmp_info), 0);

    EXPECT_EQ(tl_StretchBlt(nullptr, 0, 0, 10, 10, nullptr, 0, 0, 10, 10, 0), 1);

    void* dib_bits = nullptr;
    void* dib = tl_CreateDIBSection(nullptr, nullptr, 0, &dib_bits, nullptr, 0);
    ASSERT_NE(dib, nullptr);
    EXPECT_NE(dib_bits, nullptr);

    constexpr std::uint16_t non_exist1[] = {'n', 'o', 'n', 'e', 'x', 'i', 's', 't', '1', 0};
    constexpr std::uint16_t non_exist2[] = {'n', 'o', 'n', 'e', 'x', 'i', 's', 't', '2', 0};
    EXPECT_EQ(tl_CreateHardLinkW(non_exist2, non_exist1, nullptr), 0);
}

TEST(Gdi32Test, CreateDibSectionAllocatesRequestedSurface) {
    struct BitmapInfoHeader {
        std::uint32_t size{40};
        std::int32_t width{1024};
        std::int32_t height{1024};
        std::uint16_t planes{1};
        std::uint16_t bit_count{32};
        std::uint32_t compression{0};
        std::uint32_t size_image{0};
        std::int32_t x_pels_per_meter{0};
        std::int32_t y_pels_per_meter{0};
        std::uint32_t clr_used{0};
        std::uint32_t clr_important{0};
    } header;
    void* bits = nullptr;
    void* const bitmap = tl_CreateDIBSection(nullptr, &header, 0, &bits, nullptr, 0);
    ASSERT_NE(bitmap, nullptr);
    ASSERT_NE(bits, nullptr);
    auto* const pixels = static_cast<std::byte*>(bits);
    pixels[4U * 1024U * 1024U - 1U] = std::byte{0xA5};
    EXPECT_EQ(tl_DeleteObject(bitmap), 1);
}

TEST(Gdi32Test, GetTextExtentPoint32RejectsInvalidDestination) {
    constexpr std::uint16_t text[] = {'t', 'e', 's', 't', 0};
    EXPECT_EQ(tl_GetTextExtentPoint32W(nullptr, text, 4,
                                        reinterpret_cast<void*>(0x1U)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}
}  // namespace
}  // namespace tradutorlinux
