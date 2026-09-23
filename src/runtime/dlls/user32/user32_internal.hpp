#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/win32/kernel32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "../../core/runtime_state_common.hpp"
#include "../../core/runtime_process_state.hpp"
#include "../../core/runtime_thread_state.hpp"
#include "../../core/runtime_gui_state.hpp"
#include "tradutorlinux/gui/platform.hpp"
#include "tradutorlinux/runtime/dialog_template.hpp"
#include "gui_controls.hpp"
#include "tradutorlinux/util/basics.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace tradutorlinux {

using WndProc = TL_MSABI abi::Lresult (*)(abi::HWnd, std::uint32_t, abi::Wparam, abi::Lparam);

constexpr int kMaxGuestWindowDimension = 8192;

[[nodiscard]] inline int guest_window_dimension(const int value, const int fallback) noexcept {
    return value > 0 && value <= kMaxGuestWindowDimension ? value : fallback;
}

[[nodiscard]] inline bool guest_window_geometry_is_unreasonable(const int x, const int y,
                                                                const int width,
                                                                const int height) noexcept {
    constexpr int kMaxHostWindowDimension = 4096;
    return x < -kMaxGuestWindowDimension || x > kMaxGuestWindowDimension ||
           y < -kMaxGuestWindowDimension || y > kMaxGuestWindowDimension ||
           width > kMaxHostWindowDimension || height > kMaxHostWindowDimension;
}

void paint_registered_children(WindowSlot& parent) noexcept;
void clear_pending_native(WindowSlot& slot) noexcept;

inline void render_controls(WindowSlot& parent) noexcept {
    runtime_gui::render_controls(parent, std::span<WindowSlot>{g_windows});
    paint_registered_children(parent);
}

struct DialogRenderBatchState {
    WindowSlot* parent{nullptr};
    unsigned int depth{0};
    bool dirty{false};
    bool flushing{false};
};

inline thread_local DialogRenderBatchState g_dialog_render_batch{};

[[nodiscard]] inline WindowSlot* dialog_render_parent(WindowSlot* const slot) noexcept {
    if (slot == nullptr) {
        return nullptr;
    }
    if (slot->is_dialog) {
        return slot;
    }
    return slot->parent != nullptr && slot->parent->is_dialog ? slot->parent : nullptr;
}

inline void request_dialog_render(WindowSlot& parent) noexcept {
    DialogRenderBatchState& batch = g_dialog_render_batch;
    if (parent.is_dialog) {
        if (batch.parent == nullptr) {
            batch.parent = &parent;
        }
        batch.dirty = true;
        parent.render_pending = true;
        return;
    }
    if (batch.parent == &parent) {
        batch.dirty = true;
        return;
    }
    if (batch.flushing) {
        batch.dirty = true;
        return;
    }
    render_controls(parent);
}

inline void flush_dialog_render() noexcept {
    DialogRenderBatchState& batch = g_dialog_render_batch;
    if (batch.depth != 0U || batch.parent == nullptr || !batch.dirty) {
        return;
    }
    WindowSlot* const parent = batch.parent;
    batch.parent = nullptr;
    batch.dirty = false;
    parent->render_pending = false;
    if (!parent->used) {
        return;
    }
    batch.flushing = true;
    render_controls(*parent);
    batch.flushing = false;
    if (batch.dirty && parent->used) {
        batch.dirty = false;
        render_controls(*parent);
    }
}

class ScopedGuestWndProc {
public:
    explicit ScopedGuestWndProc(const abi::HWnd hwnd) noexcept {
        WindowSlot* const slot = find_window_slot(hwnd);
        parent_ = dialog_render_parent(slot);
        if (parent_ == nullptr) {
            return;
        }
        DialogRenderBatchState& batch = g_dialog_render_batch;
        if (batch.flushing) {
            return;
        }
        if (batch.depth == 0U && batch.parent != nullptr && batch.parent != parent_) {
            flush_dialog_render();
        }
        if (batch.depth == 0U) {
            batch.parent = parent_;
        }
        if (batch.parent == parent_) {
            ++batch.depth;
            active_ = true;
        }
    }

    ScopedGuestWndProc(const ScopedGuestWndProc&) = delete;
    ScopedGuestWndProc& operator=(const ScopedGuestWndProc&) = delete;

    ~ScopedGuestWndProc() noexcept {
        if (!active_) {
            return;
        }
        DialogRenderBatchState& batch = g_dialog_render_batch;
        --batch.depth;
        if (batch.depth != 0U) {
            return;
        }
    }

private:
    WindowSlot* parent_{nullptr};
    bool active_{false};
};

inline abi::Lresult call_wndproc(const std::uintptr_t wndproc, const abi::HWnd hwnd,
                                 const std::uint32_t message, const abi::Wparam wparam,
                                 const abi::Lparam lparam) noexcept {
    ScopedGuestWndProc render_batch(hwnd);
    return std::bit_cast<WndProc>(wndproc)(hwnd, message, wparam, lparam);
}

inline void handle_control_key(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_key(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                    event);
}

inline void handle_control_mouse(WindowSlot& parent, const gui::WindowEvent& event) noexcept {
    runtime_gui::handle_control_mouse(parent, std::span<WindowSlot>{g_windows}, g_focused_control,
                                      event);
}

inline void set_focus_control(WindowSlot* control) noexcept {
    runtime_gui::set_focus_control(control, g_focused_control);
}

inline bool write_guest_msg(void* const msg, const abi::HWnd hwnd, const std::uint32_t message,
                            const abi::Wparam wparam, const abi::Lparam lparam) noexcept {
    abi::GuestMsg out{};
    out.hwnd = hwnd;
    out.message = message;
    out.padding = 0;
    out.wparam = wparam;
    out.lparam = lparam;
    out.time = 0;
    out.pt_x = 0;
    out.pt_y = 0;
    return runtime::write_guest_memory(msg, &out, sizeof(out)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

constexpr unsigned long kKeysymBackspace = 0xFF08;
constexpr unsigned long kKeysymTab = 0xFF09;
constexpr unsigned long kKeysymReturn = 0xFF0D;
constexpr unsigned long kKeysymEscape = 0xFF1B;
constexpr unsigned long kKeysymLeft = 0xFF51;
constexpr unsigned long kKeysymUp = 0xFF52;
constexpr unsigned long kKeysymRight = 0xFF53;
constexpr unsigned long kKeysymDown = 0xFF54;
constexpr unsigned long kKeysymDelete = 0xFFFF;

constexpr std::uint32_t kWsVisible = 0x10000000U;
constexpr std::uint32_t kWsDisabled = 0x08000000U;
constexpr std::uint32_t kWsTabStop = 0x00010000U;
constexpr std::uint32_t kImageIcon = 1U;
constexpr int kIdOk = 1;
constexpr int kIdCancel = 2;

struct ImageSlot {
    bool used{false};
    const void* source{nullptr};
};

inline std::array<ImageSlot, 32> g_image_slots{};
inline WindowSlot* g_captured_window = nullptr;

struct GuestPoint {
    std::int32_t x{};
    std::int32_t y{};
};

struct InternalMenu {
    std::uint32_t signature{0x4D454E55};
    std::uint32_t count{5};
    void* sub_menu{nullptr};
    std::uint8_t dummy_storage[64]{};
};

inline InternalMenu g_dummy_sub_menu{0x5355424D, 1, nullptr, {}};
inline InternalMenu g_dummy_menu{0x4D454E55, 5, &g_dummy_sub_menu, {}};

[[nodiscard]] inline bool guest_callback_address_valid(const std::uintptr_t address) noexcept {
    if (address == 0 || g_guest_image_base == nullptr ||
        address < reinterpret_cast<std::uintptr_t>(g_guest_image_base) ||
        address - reinterpret_cast<std::uintptr_t>(g_guest_image_base) >= g_guest_image_size) {
        return false;
    }
    // O loader mantém a imagem convidada mapeada durante todo o callback. O
    // endereço é um alvo de execução validado pela faixa da imagem, não um
    // buffer que o runtime deva ler após uma fotografia de mapas.
    return true;
}

[[nodiscard]] inline WindowSlot* dialog_control_by_id(WindowSlot& dialog, const int identifier) noexcept {
    for (WindowSlot* const child : dialog.dialog_children) {
        if (child != nullptr && child->used &&
            child->parent == &dialog &&
            (child->control_id == static_cast<std::uint32_t>(identifier) ||
             (child->control_id <= 0xFFFFU && child->control_id == static_cast<std::uint16_t>(identifier)))) {
            return child;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool dialog_control_eligible(const WindowSlot& control) noexcept {
    return control.used && control.is_control && control.visible && control.enabled &&
           (control.style & kWsTabStop) != 0U;
}

[[nodiscard]] inline WindowSlot* next_dialog_tab_item(WindowSlot& dialog, WindowSlot* current,
                                                      const bool previous) noexcept {
    std::vector<WindowSlot*> eligible;
    for (WindowSlot* const child : dialog.dialog_children) {
        if (child != nullptr && child->parent == &dialog && dialog_control_eligible(*child)) {
            eligible.push_back(child);
        }
    }
    if (eligible.empty()) {
        return nullptr;
    }
    if (current == nullptr) {
        return previous ? eligible.back() : eligible.front();
    }
    const auto found = std::find(eligible.begin(), eligible.end(), current);
    if (found == eligible.end()) {
        return previous ? eligible.back() : eligible.front();
    }
    const std::size_t index = static_cast<std::size_t>(found - eligible.begin());
    if (previous) {
        return eligible[(index + eligible.size() - 1U) % eligible.size()];
    }
    return eligible[(index + 1U) % eligible.size()];
}

[[nodiscard]] inline abi::Wparam keydown_vkey(const unsigned long keysym,
                                              const char character) noexcept {
    switch (keysym) {
        case kKeysymBackspace: return abi::kVkBack;
        case kKeysymTab: return abi::kVkTab;
        case kKeysymReturn: return abi::kVkReturn;
        case kKeysymEscape: return abi::kVkEscape;
        case kKeysymLeft: return abi::kVkLeft;
        case kKeysymUp: return abi::kVkUp;
        case kKeysymRight: return abi::kVkRight;
        case kKeysymDown: return abi::kVkDown;
        case kKeysymDelete: return abi::kVkDelete;
        default: break;
    }
    const auto value = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
    if (value >= 'a' && value <= 'z') {
        return static_cast<abi::Wparam>(value - 0x20U);
    }
    return static_cast<abi::Wparam>(value);
}

}  // namespace tradutorlinux
