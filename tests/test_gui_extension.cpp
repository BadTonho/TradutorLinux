#include "../src/runtime/gui_extension.hpp"

#include "../include/tradutorlinux/runtime/guest_context.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace tradutorlinux::runtime_gui {
namespace {

class ProbeRuntime final : public GuiExtensionRuntime {};

class ProbeExtension final : public GuiExtension {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "test-probe";
    }

    [[nodiscard]] std::unique_ptr<GuiExtensionRuntime> create_runtime() const noexcept override {
        try {
            return std::make_unique<ProbeRuntime>();
        } catch (...) {
            return nullptr;
        }
    }

    [[nodiscard]] bool matches(const WindowSlot& parent) const noexcept override {
        return parent.class_name == "Probe::Window";
    }

    [[nodiscard]] bool render(GuiExtensionRuntime&, WindowSlot&,
                              std::span<WindowSlot>) const noexcept override {
        return true;
    }

    [[nodiscard]] bool handle_key(GuiExtensionRuntime&, WindowSlot&, std::span<WindowSlot>,
                                  WindowSlot*&, const gui::WindowEvent&) const noexcept override {
        return false;
    }

    [[nodiscard]] bool handle_mouse(GuiExtensionRuntime&, WindowSlot&, std::span<WindowSlot>,
                                    WindowSlot*&, const gui::WindowEvent&) const noexcept override {
        return false;
    }
};

ProbeExtension g_probe_extension{};

class GuiExtensionSelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_gui_extension(g_probe_extension);
    }
};

TEST_F(GuiExtensionSelectionTest, MissingProfileSelectionKeepsGenericRuntime) {
    runtime::GuestContext context;
    runtime::GuestContextScope scope(context);
    std::string error;

    ASSERT_TRUE(select_gui_extension({}, error));
    EXPECT_TRUE(error.empty());
    WindowSlot parent;
    parent.class_name = "7-Zip::FM";
    EXPECT_EQ(active_gui_extension(parent), nullptr);
    EXPECT_EQ(active_gui_extension_runtime(), nullptr);
}

TEST_F(GuiExtensionSelectionTest, UnknownExtensionFailsWithoutGenericFallback) {
    runtime::GuestContext context;
    runtime::GuestContextScope scope(context);
    std::string error;

    EXPECT_FALSE(select_gui_extension("not-registered", error));
    EXPECT_NE(error.find("not-registered"), std::string::npos);
    EXPECT_EQ(runtime::guest_context().gui_extension, nullptr);
    EXPECT_EQ(active_gui_extension_runtime(), nullptr);
}

TEST_F(GuiExtensionSelectionTest, SelectedExtensionMatchesOnlyItsOwnWindowClass) {
    runtime::GuestContext context;
    runtime::GuestContextScope scope(context);
    std::string error;

    ASSERT_TRUE(select_gui_extension("test-probe", error));
    WindowSlot matching;
    matching.class_name = "Probe::Window";
    WindowSlot generic;
    generic.class_name = "7-Zip::FM";
    EXPECT_EQ(active_gui_extension(matching), &g_probe_extension);
    EXPECT_EQ(active_gui_extension(generic), nullptr);
    EXPECT_NE(active_gui_extension_runtime(), nullptr);
}

TEST_F(GuiExtensionSelectionTest, HostStateBelongsToTheActiveGuestContext) {
    runtime::GuestContext first;
    runtime::GuestContext second;
    std::string error;

    {
        runtime::GuestContextScope scope(first);
        ASSERT_TRUE(select_gui_extension("test-probe", error));
        EXPECT_NE(active_gui_extension_runtime(), nullptr);
    }
    {
        runtime::GuestContextScope scope(second);
        EXPECT_EQ(active_gui_extension_runtime(), nullptr);
        ASSERT_TRUE(select_gui_extension("test-probe", error));
        EXPECT_NE(active_gui_extension_runtime(), nullptr);
    }
    {
        runtime::GuestContextScope scope(first);
        EXPECT_NE(active_gui_extension_runtime(), nullptr);
    }
}

}  // namespace
}  // namespace tradutorlinux::runtime_gui
