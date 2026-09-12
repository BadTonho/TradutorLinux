#include "gui_extension.hpp"

#include "tradutorlinux/runtime/guest_context.hpp"

#include <array>

namespace tradutorlinux::runtime_gui {
namespace {

constexpr std::size_t kMaxGuiExtensions = 16;
std::array<const GuiExtension*, kMaxGuiExtensions> g_extensions{};
std::size_t g_extension_count = 0;

}  // namespace

void register_gui_extension(const GuiExtension& extension) noexcept {
    for (std::size_t index = 0; index < g_extension_count; ++index) {
        if (g_extensions[index] == &extension || g_extensions[index]->id() == extension.id()) {
            g_extensions[index] = &extension;
            return;
        }
    }
    if (g_extension_count < g_extensions.size()) {
        g_extensions[g_extension_count++] = &extension;
    }
}

bool select_gui_extension(const std::string_view id, std::string& error) noexcept {
    runtime::GuestContext& context = runtime::guest_context();
    context.gui_extension = nullptr;
    context.gui_extension_runtime.reset();
    error.clear();
    if (id.empty()) {
        return true;
    }

    for (std::size_t index = 0; index < g_extension_count; ++index) {
        const GuiExtension* const extension = g_extensions[index];
        if (extension == nullptr || extension->id() != id) {
            continue;
        }
        try {
            std::unique_ptr<GuiExtensionRuntime> state = extension->create_runtime();
            if (state == nullptr) {
                error = "a extensão registrada não conseguiu criar seu estado host-only";
                return false;
            }
            context.gui_extension = extension;
            context.gui_extension_runtime = std::move(state);
            return true;
        } catch (...) {
            error = "falha ao inicializar o estado host-only da extensão";
            return false;
        }
    }
    try {
        error = "extensão não registrada: " + std::string{id};
    } catch (...) {
        error.clear();
    }
    return false;
}

const GuiExtension* active_gui_extension(const WindowSlot& parent) noexcept {
    const runtime::GuestContext& context = runtime::guest_context();
    if (context.gui_extension == nullptr || context.gui_extension_runtime == nullptr) {
        return nullptr;
    }
    return context.gui_extension->matches(parent) ? context.gui_extension : nullptr;
}

GuiExtensionRuntime* active_gui_extension_runtime() noexcept {
    return runtime::guest_context().gui_extension_runtime.get();
}

}  // namespace tradutorlinux::runtime_gui
