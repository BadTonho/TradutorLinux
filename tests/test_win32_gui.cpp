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

TEST(Win32GuiTest, LoadsExtendedMenuResourceAndExposesHierarchy) {
    std::vector<std::byte> image(512, std::byte{0});
    const auto write_u16 = [&image](const std::size_t offset, const std::uint16_t value) {
        image[offset] = static_cast<std::byte>(value & 0xFFU);
        image[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    };
    const auto write_u32 = [&write_u16](const std::size_t offset, const std::uint32_t value) {
        write_u16(offset, static_cast<std::uint16_t>(value & 0xFFFFU));
        write_u16(offset + 2U, static_cast<std::uint16_t>(value >> 16U));
    };

    write_u16(14, 1);
    write_u32(16, 4);
    write_u32(20, 0x80000020U);
    write_u16(32 + 14, 1);
    write_u32(48, 71);
    write_u32(52, 0x80000040U);
    write_u16(64 + 14, 1);
    write_u32(80, 0x00000409U);
    write_u32(84, 0x000000A0U);
    write_u32(160, 256);

    write_u16(256, 1);
    write_u16(258, 4);
    const auto append_item = [&write_u16, &write_u32](std::size_t offset,
                                                       const std::uint32_t type,
                                                       const std::uint32_t state,
                                                       const std::uint32_t command,
                                                       const std::uint16_t flags,
                                                       const std::u16string_view text) {
        write_u32(offset, type);
        write_u32(offset + 4U, state);
        write_u32(offset + 8U, command);
        write_u16(offset + 12U, flags);
        offset += 14U;
        for (const char16_t unit : text) {
            write_u16(offset, static_cast<std::uint16_t>(unit));
            offset += 2U;
        }
        write_u16(offset, 0);
        offset += 2U;
        return (offset + 3U) & ~static_cast<std::size_t>(3U);
    };
    std::size_t menu_end = append_item(264, 0, 0, 500, 1, u"&File");
    write_u32(menu_end, 0);
    menu_end += 4U;
    menu_end = append_item(menu_end, 0, 0, 1, 0x80, u"&Open");
    menu_end = append_item(menu_end, 0, 0, 501, 0x80, u"&Edit");
    write_u32(164, static_cast<std::uint32_t>(menu_end - 256U));

    g_menus = {};
    set_guest_image_view(image.data(), image.size(), 0,
                         static_cast<std::uint32_t>(image.size()));
    const auto* const resource_name = reinterpret_cast<const std::uint16_t*>(71U);
    void* const menu = tl_LoadMenuW(nullptr, resource_name);
    EXPECT_NE(menu, nullptr);
    if (menu == nullptr) {
        set_guest_image_view(nullptr, 0, 0, 0);
        g_menus = {};
        return;
    }
    EXPECT_EQ(tl_GetMenuItemCount(menu), 2);
    void* const submenu = tl_GetSubMenu(menu, 0);
    EXPECT_NE(submenu, nullptr);
    if (submenu == nullptr) {
        EXPECT_EQ(tl_DestroyMenu(menu), 1);
        set_guest_image_view(nullptr, 0, 0, 0);
        g_menus = {};
        return;
    }
    EXPECT_EQ(tl_GetMenuItemCount(submenu), 1);

    struct GuestMenuItemInfoW {
        std::uint32_t cb_size;
        std::uint32_t f_mask;
        std::uint32_t f_type;
        std::uint32_t f_state;
        std::uint32_t item_id;
        void* sub_menu;
        void* checked_bitmap;
        void* unchecked_bitmap;
        std::uintptr_t item_data;
        std::uint16_t* type_data;
        std::uint32_t char_count;
        void* item_bitmap;
    } info{};
    static_assert(sizeof(GuestMenuItemInfoW) == 80);
    std::uint16_t text[16]{};
    info.cb_size = sizeof(info);
    info.f_mask = 0x00000004U | 0x00000008U | 0x00000040U;
    info.type_data = text;
    info.char_count = static_cast<std::uint32_t>(std::size(text));
    EXPECT_EQ(tl_GetMenuItemInfoW(menu, 0, 1, &info), 1);
    EXPECT_EQ(info.item_id, 500U);
    EXPECT_EQ(info.sub_menu, submenu);
    EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t*>(text)), u"&File");
    EXPECT_EQ(tl_DestroyMenu(menu), 1);
    set_guest_image_view(nullptr, 0, 0, 0);
    g_menus = {};
}

TEST(Win32GuiTest, InvalidateRectQueuesPaintForLogicalWindow) {
    g_windows = {};
    WindowSlot& parent = g_windows[0];
    parent.used = true;
    WindowSlot& child = g_windows[1];
    child.used = true;
    child.is_control = true;
    child.parent = &parent;

    abi::GuestRect rect{1, 2, 30, 40};
    EXPECT_EQ(tl_InvalidateRect(&child, &rect, 1), 1);
    ASSERT_EQ(child.queued_messages.size(), 1U);
    EXPECT_EQ(child.queued_messages.front().hwnd, &child);
    EXPECT_EQ(child.queued_messages.front().message, abi::kWmPaint);

    EXPECT_EQ(tl_InvalidateRect(&child, &rect, 0), 1);
    EXPECT_EQ(child.queued_messages.size(), 1U);
    EXPECT_EQ(tl_InvalidateRect(&child, reinterpret_cast<const void*>(0x1U), 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    g_windows = {};
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

TEST(Win32GuiTest, AllowsCrossThreadPostMessageToPrimaryQueue) {
    for (WindowSlot& slot : g_windows) {
        unregister_window_handle(&slot);
    }
    clear_cross_thread_window_messages();
    g_windows = {};
    g_quit_requested = false;

    WindowSlot& window = g_windows[0];
    window.used = true;
    ASSERT_TRUE(register_window_handle(&window));

    int post_result = 0;
    std::thread worker([&] {
        g_current_thread_id = 2;
        post_result = tl_PostMessageA(&window, 0x8002U, 0x1234U, 0x5678);
    });
    worker.join();

    EXPECT_EQ(post_result, 1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    abi::GuestMsg message{};
    EXPECT_EQ(tl_GetMessageA(&message, nullptr, 0, 0), 1);
    EXPECT_EQ(message.hwnd, &window);
    EXPECT_EQ(message.message, 0x8002U);
    EXPECT_EQ(message.wparam, 0x1234U);
    EXPECT_EQ(message.lparam, 0x5678);

    int second_post_result = 0;
    std::thread second_worker([&] {
        g_current_thread_id = 2;
        second_post_result = tl_PostMessageA(&window, 0x8003U, 0x9ABCU, 0xDEF0);
    });
    second_worker.join();
    EXPECT_EQ(second_post_result, 1);
    abi::GuestMsg peeked{};
    EXPECT_EQ(tl_PeekMessageA(&peeked, nullptr, 0, 0, 0), 1);
    EXPECT_EQ(peeked.message, 0x8003U);
    abi::GuestMsg removed{};
    EXPECT_EQ(tl_GetMessageA(&removed, nullptr, 0, 0), 1);
    EXPECT_EQ(removed.message, 0x8003U);

    unregister_window_handle(&window);
    clear_cross_thread_window_messages();
    g_windows = {};
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

TEST(Win32DialogTemplateTest, RejectsUnsupportedMenuAndParsesGenericControlsAndFont) {
    runtime::DialogTemplate parsed{};
    std::vector<std::byte> custom_menu = valid_dialog_template();
    custom_menu[18] = std::byte{1};
    EXPECT_EQ(runtime::parse_dialog_template(custom_menu, parsed),
              runtime::DialogTemplateStatus::Unsupported);

    std::vector<std::byte> custom_class = valid_dialog_template();
    custom_class.erase(custom_class.begin() + 20, custom_class.begin() + 22);
    std::vector<std::byte> class_field;
    for (const char16_t character : std::u16string_view{u"PuTTYConfigBox"}) {
        append_u16(class_field, static_cast<std::uint16_t>(character));
    }
    append_u16(class_field, 0);
    custom_class.insert(custom_class.begin() + 20, class_field.begin(), class_field.end());
    EXPECT_EQ(runtime::parse_dialog_template(custom_class, parsed),
              runtime::DialogTemplateStatus::Success);
    EXPECT_EQ(parsed.title, u"D");

    std::vector<std::byte> font = valid_dialog_template();
    font[0] = std::byte{0x40};
    font.insert(font.begin() + 26,
                {std::byte{9}, std::byte{0}, std::byte{'M'}, std::byte{0}, std::byte{'S'},
                 std::byte{0}, std::byte{0}, std::byte{0}});
    EXPECT_EQ(runtime::parse_dialog_template(font, parsed), runtime::DialogTemplateStatus::Success);
    ASSERT_EQ(parsed.controls.size(), 1U);
    EXPECT_EQ(parsed.controls[0].title, u"OK");

    std::vector<std::byte> custom_control = valid_dialog_template();
    custom_control[48] = std::byte{0x83};
    EXPECT_EQ(runtime::parse_dialog_template(custom_control, parsed),
              runtime::DialogTemplateStatus::Success);
    ASSERT_EQ(parsed.controls.size(), 1U);
    EXPECT_EQ(parsed.controls[0].control_class, runtime::DialogControlClass::Generic);
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
    EXPECT_TRUE(dialog.render_pending);
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

    EXPECT_EQ(tl_DialogBoxParamA(nullptr, reinterpret_cast<const char*>(101U), nullptr, nullptr, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_CreateDialogParamW(nullptr, numeric_template, nullptr, nullptr, 0), nullptr);
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
