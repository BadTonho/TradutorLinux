#pragma once

#include "gui_controls.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace tradutorlinux::runtime_gui {

// Estado exclusivamente do host de uma extensão. Nenhum objeto desta
// interface é visível ao processo convidado ou faz parte do wire/ABI Win32.
class GuiExtensionRuntime {
public:
    virtual ~GuiExtensionRuntime() = default;
};

class GuiExtension {
public:
    virtual ~GuiExtension() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<GuiExtensionRuntime> create_runtime() const noexcept = 0;
    [[nodiscard]] virtual bool matches(const WindowSlot& parent) const noexcept = 0;

    [[nodiscard]] virtual bool render(GuiExtensionRuntime& state, WindowSlot& parent,
                                      std::span<WindowSlot> windows) const noexcept = 0;
    [[nodiscard]] virtual bool handle_key(GuiExtensionRuntime& state, WindowSlot& parent,
                                           std::span<WindowSlot> windows,
                                           WindowSlot*& focused_control,
                                           const gui::WindowEvent& event) const noexcept = 0;
    [[nodiscard]] virtual bool handle_mouse(GuiExtensionRuntime& state, WindowSlot& parent,
                                             std::span<WindowSlot> windows,
                                             WindowSlot*& focused_control,
                                             const gui::WindowEvent& event) const noexcept = 0;
};

void register_gui_extension(const GuiExtension& extension) noexcept;

// Seleciona uma extensão para a execução atual. O perfil sem extension limpa
// qualquer seleção; um ID não registrado é um erro explícito.
[[nodiscard]] bool select_gui_extension(std::string_view id, std::string& error) noexcept;

[[nodiscard]] const GuiExtension* active_gui_extension(const WindowSlot& parent) noexcept;
[[nodiscard]] GuiExtensionRuntime* active_gui_extension_runtime() noexcept;

}  // namespace tradutorlinux::runtime_gui
