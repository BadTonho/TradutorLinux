#include "tradutorlinux/gui/wayland.hpp"

#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <linux/memfd.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <poll.h>
#include <vector>

namespace tradutorlinux::gui::wayland {
namespace {

constexpr std::size_t kMaxWindows = 16;

struct WaylandWindow {
    bool used{false};
    wl_surface* surface{nullptr};
    xdg_surface* xdg{nullptr};
    xdg_toplevel* toplevel{nullptr};
    wl_buffer* buffer{nullptr};
    void* pixels{nullptr};
    std::size_t pixel_bytes{0};
    int width{800};
    int height{600};
    bool configured{false};
    bool mapped{false};
    bool buffer_available{true};
    bool dirty{false};
    bool closed{false};
    std::string caption;
    std::deque<WindowEvent> pending;
};

struct Context {
    wl_display* display{nullptr};
    wl_registry* registry{nullptr};
    wl_compositor* compositor{nullptr};
    wl_shm* shm{nullptr};
    wl_seat* seat{nullptr};
    xdg_wm_base* wm_base{nullptr};
    wl_keyboard* keyboard{nullptr};
    wl_pointer* pointer{nullptr};
    xkb_context* xkb_ctx{nullptr};
    xkb_keymap* keymap{nullptr};
    xkb_state* key_state{nullptr};
    WaylandWindow* focus{nullptr};
    std::array<WaylandWindow, kMaxWindows> windows{};
    bool initialized{false};
    bool failed{false};
};

Context g_context{};

void wm_ping(void*, xdg_wm_base* wm, std::uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener{wm_ping};

void xdg_configure(void* data, xdg_surface* surface, std::uint32_t serial) {
    auto* window = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(surface, serial);
    window->configured = true;
    window->pending.push_back({WindowEventType::Redraw, 0, 0});
}
const xdg_surface_listener kXdgListener{xdg_configure};

void top_configure(void* data, xdg_toplevel*, std::int32_t width, std::int32_t height,
                   wl_array*) {
    auto* window = static_cast<WaylandWindow*>(data);
    if (width > 0 && height > 0 && (width != window->width || height != window->height)) {
        window->width = width;
        window->height = height;
        window->pending.push_back({WindowEventType::Redraw, 0, 0});
    }
}
void top_close(void* data, xdg_toplevel*) {
    auto* window = static_cast<WaylandWindow*>(data);
    window->closed = true;
    window->pending.push_back({WindowEventType::CloseRequested, 0, 0});
}
const xdg_toplevel_listener kTopListener{top_configure, top_close, nullptr, nullptr};

WaylandWindow* find_window(void* value) noexcept {
    auto* window = static_cast<WaylandWindow*>(value);
    for (auto& candidate : g_context.windows) {
        if (&candidate == window && candidate.used) return &candidate;
    }
    return nullptr;
}

WaylandWindow* find_surface(wl_surface* surface) noexcept {
    for (auto& window : g_context.windows) {
        if (window.used && window.surface == surface) return &window;
    }
    return nullptr;
}

void keyboard_keymap(void*, wl_keyboard*, std::uint32_t format, int fd, std::uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char* data = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data != MAP_FAILED) {
        if (g_context.key_state != nullptr) xkb_state_unref(g_context.key_state);
        if (g_context.keymap != nullptr) xkb_keymap_unref(g_context.keymap);
        if (g_context.xkb_ctx == nullptr) g_context.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (g_context.xkb_ctx != nullptr) {
            g_context.keymap = xkb_keymap_new_from_string(g_context.xkb_ctx, data,
                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (g_context.keymap != nullptr) g_context.key_state = xkb_state_new(g_context.keymap);
        }
        munmap(data, size);
    }
    close(fd);
}
void keyboard_enter(void*, wl_keyboard*, std::uint32_t, wl_surface* surface, wl_array*) {
    g_context.focus = find_surface(surface);
}
void keyboard_leave(void*, wl_keyboard*, std::uint32_t, wl_surface*) { g_context.focus = nullptr; }
void keyboard_key(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key,
                  std::uint32_t state) {
    if (g_context.focus == nullptr || g_context.key_state == nullptr) return;
    const xkb_keycode_t code = static_cast<xkb_keycode_t>(key + 8U);
    const xkb_keysym_t symbol = xkb_state_key_get_one_sym(g_context.key_state, code);
    const std::uint32_t unicode = xkb_state_key_get_utf32(g_context.key_state, code);
    g_context.focus->pending.push_back({state == WL_KEYBOARD_KEY_STATE_PRESSED
                                             ? WindowEventType::KeyDown : WindowEventType::KeyUp,
                                         0, 0, unicode < 128U ? static_cast<char>(unicode) : '\0',
                                         static_cast<unsigned long>(symbol)});
}
void keyboard_modifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t depressed,
                        std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
    if (g_context.key_state != nullptr) xkb_state_update_mask(g_context.key_state, depressed, latched,
                                                               locked, 0, 0, group);
}
void keyboard_repeat_info(void*, wl_keyboard*, std::int32_t, std::int32_t) {}
const wl_keyboard_listener kKeyboardListener{keyboard_keymap, keyboard_enter, keyboard_leave,
                                             keyboard_key, keyboard_modifiers, keyboard_repeat_info};

void pointer_enter(void*, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t, wl_fixed_t) {
    g_context.focus = find_surface(surface);
}
void pointer_leave(void*, wl_pointer*, std::uint32_t, wl_surface*) {}
void pointer_motion(void*, wl_pointer*, std::uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    if (g_context.focus != nullptr) g_context.focus->pending.push_back(
        {WindowEventType::MouseMove, wl_fixed_to_int(sx), wl_fixed_to_int(sy)});
}
void pointer_button(void*, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button,
                    std::uint32_t state) {
    if (g_context.focus == nullptr) return;
    if (button == 0x110U) g_context.focus->pending.push_back(
        {state == WL_POINTER_BUTTON_STATE_PRESSED ? WindowEventType::Press : WindowEventType::Release, 0, 0});
    else if (button == 0x111U && state == WL_POINTER_BUTTON_STATE_PRESSED)
        g_context.focus->pending.push_back({WindowEventType::RightPress, 0, 0});
}
void pointer_axis(void*, wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t) {}
const wl_pointer_listener kPointerListener{pointer_enter, pointer_leave, pointer_motion, pointer_button,
                                           pointer_axis, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           nullptr};

void seat_capabilities(void*, wl_seat* seat, std::uint32_t capabilities) {
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && g_context.keyboard == nullptr) {
        g_context.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_context.keyboard, &kKeyboardListener, nullptr);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && g_context.pointer == nullptr) {
        g_context.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_context.pointer, &kPointerListener, nullptr);
    }
}
void seat_name(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener{seat_capabilities, seat_name};

void registry_global(void*, wl_registry* registry, std::uint32_t name, const char* interface,
                     std::uint32_t version) {
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        g_context.compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 4U)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        g_context.shm = static_cast<wl_shm*>(wl_registry_bind(
            registry, name, &wl_shm_interface, std::min(version, 1U)));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        g_context.wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(
            registry, name, &xdg_wm_base_interface, std::min(version, 2U)));
        xdg_wm_base_add_listener(g_context.wm_base, &kWmListener, nullptr);
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        g_context.seat = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, std::min(version, 7U)));
    }
}
void registry_remove(void*, wl_registry*, std::uint32_t) {}
const wl_registry_listener kRegistryListener{registry_global, registry_remove};

bool initialize() noexcept {
    if (g_context.initialized) return !g_context.failed;
    g_context.initialized = true;
    g_context.display = wl_display_connect(nullptr);
    if (g_context.display == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=connect-failed errno=%d\n", errno);
        g_context.failed = true; return false;
    }
    g_context.registry = wl_display_get_registry(g_context.display);
    wl_registry_add_listener(g_context.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g_context.display) < 0 || g_context.compositor == nullptr ||
        g_context.shm == nullptr || g_context.wm_base == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=missing-global compositor=%s shm=%s xdg_wm_base=%s\n",
                     g_context.compositor != nullptr ? "yes" : "no",
                     g_context.shm != nullptr ? "yes" : "no",
                     g_context.wm_base != nullptr ? "yes" : "no");
        g_context.failed = true;
        return false;
    }
    if (g_context.seat != nullptr) {
        wl_seat_add_listener(g_context.seat, &kSeatListener, nullptr);
        wl_display_roundtrip(g_context.display);
    }
    return true;
}

int create_shm_file(std::size_t bytes) noexcept {
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "tradutorlinux-wayland", MFD_CLOEXEC));
    if (fd < 0) return -1;
    if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) { close(fd); return -1; }
    return fd;
}

extern const wl_buffer_listener kBufferListener;

bool allocate_buffer(WaylandWindow& window) noexcept {
    const std::size_t stride = static_cast<std::size_t>(window.width) * 4U;
    window.pixel_bytes = stride * static_cast<std::size_t>(window.height);
    const int fd = create_shm_file(window.pixel_bytes);
    if (fd < 0) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-shm-file-failed errno=%d\n", errno);
        return false;
    }
    window.pixels = mmap(nullptr, window.pixel_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (window.pixels == MAP_FAILED) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=mmap-failed errno=%d\n", errno);
        close(fd); window.pixels = nullptr; return false;
    }
    auto* pool = wl_shm_create_pool(g_context.shm, fd, static_cast<int>(window.pixel_bytes));
    if (pool == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-shm-pool-failed\n");
        munmap(window.pixels, window.pixel_bytes); window.pixels = nullptr; close(fd); return false;
    }
    window.buffer = wl_shm_pool_create_buffer(pool, 0, window.width, window.height,
                                               static_cast<int>(stride), WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (window.buffer == nullptr) {
        munmap(window.pixels, window.pixel_bytes);
        window.pixels = nullptr;
        return false;
    }
    wl_buffer_add_listener(window.buffer, &kBufferListener, &window);
    std::fill_n(static_cast<std::uint32_t*>(window.pixels), window.pixel_bytes / 4U, 0xFFEFEFEFU);
    return true;
}

void commit(WaylandWindow& window) noexcept {
    if (window.surface == nullptr || window.buffer == nullptr) return;
    if (!window.buffer_available) { window.dirty = true; return; }
    wl_surface_attach(window.surface, window.buffer, 0, 0);
    wl_surface_damage_buffer(window.surface, 0, 0, window.width, window.height);
    wl_surface_commit(window.surface);
    window.buffer_available = false;
    window.dirty = false;
    wl_display_flush(g_context.display);
}

void buffer_release(void* data, wl_buffer* buffer) {
    auto* window = static_cast<WaylandWindow*>(data);
    if (window == nullptr || window->buffer != buffer) return;
    window->buffer_available = true;
    if (window->dirty) commit(*window);
}
const wl_buffer_listener kBufferListener{buffer_release};

std::uint32_t pixel(const std::uint32_t rgb) noexcept { return 0xFF000000U | (rgb & 0xFFFFFFU); }
void fill(WaylandWindow& w, int x, int y, int width, int height, std::uint32_t color) noexcept {
    if (w.pixels == nullptr || width <= 0 || height <= 0 || w.width <= 0 || w.height <= 0) return;
    const std::int64_t left = std::max<std::int64_t>(0, x);
    const std::int64_t top = std::max<std::int64_t>(0, y);
    const std::int64_t right = std::min<std::int64_t>(w.width,
                                                       static_cast<std::int64_t>(x) + width);
    const std::int64_t bottom = std::min<std::int64_t>(w.height,
                                                        static_cast<std::int64_t>(y) + height);
    if (left >= right || top >= bottom) return;
    auto* data = static_cast<std::uint32_t*>(w.pixels);
    for (std::int64_t row = top; row < bottom; ++row) {
        auto* const row_start = data + row * w.width;
        std::fill(row_start + left, row_start + right, pixel(color));
    }
}

}  // namespace

bool available() noexcept { return initialize(); }

NativeWindow create_window(const char* caption, const int width, const int height) noexcept {
    if (!initialize()) return nullptr;
    auto it = std::find_if(g_context.windows.begin(), g_context.windows.end(),
                           [](const WaylandWindow& w) { return !w.used; });
    if (it == g_context.windows.end()) return nullptr;
    WaylandWindow& window = *it;
    window = {};
    window.used = true;
    window.width = width > 0 ? width : 800;
    window.height = height > 0 ? height : 600;
    window.caption = caption != nullptr ? caption : "TradutorLinux";
    window.surface = wl_compositor_create_surface(g_context.compositor);
    if (window.surface == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-surface-failed\n");
        destroy_window(&window); return nullptr;
    }
    if (!allocate_buffer(window)) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=allocate-buffer-failed errno=%d\n", errno);
        destroy_window(&window); return nullptr;
    }
    window.xdg = xdg_wm_base_get_xdg_surface(g_context.wm_base, window.surface);
    if (window.xdg == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-xdg-surface-failed\n");
        destroy_window(&window); return nullptr;
    }
    xdg_surface_add_listener(window.xdg, &kXdgListener, &window);
    window.toplevel = xdg_surface_get_toplevel(window.xdg);
    if (window.toplevel == nullptr) {
        std::fprintf(stderr, "[tl][gui][error] backend=wayland status=create-toplevel-failed\n");
        destroy_window(&window); return nullptr;
    }
    xdg_toplevel_add_listener(window.toplevel, &kTopListener, &window);
    xdg_toplevel_set_title(window.toplevel, window.caption.c_str());
    // xdg-shell requires an initial empty commit to obtain the compositor's
    // configure event before attaching the first buffer.
    wl_surface_commit(window.surface);
    wl_display_roundtrip(g_context.display);
    commit(window);
    std::fprintf(stderr, "[tl][gui][info] backend=wayland window-created caption=\"%s\" size=%dx%d\n",
                 window.caption.c_str(), window.width, window.height);
    return &window;
}

void destroy_window(const NativeWindow value) noexcept {
    WaylandWindow* window = find_window(value);
    if (window == nullptr) return;
    if (window->buffer != nullptr) wl_buffer_destroy(window->buffer);
    if (window->pixels != nullptr) munmap(window->pixels, window->pixel_bytes);
    if (window->toplevel != nullptr) xdg_toplevel_destroy(window->toplevel);
    if (window->xdg != nullptr) xdg_surface_destroy(window->xdg);
    if (window->surface != nullptr) wl_surface_destroy(window->surface);
    *window = {};
}

bool map_window(const NativeWindow value) noexcept {
    WaylandWindow* window = find_window(value);
    if (window == nullptr) return false;
    window->mapped = true;
    commit(*window);
    std::fprintf(stderr, "[tl][gui][info] backend=wayland window-mapped size=%dx%d\n",
                 window->width, window->height);
    return true;
}
void unmap_window(const NativeWindow value) noexcept {
    WaylandWindow* window = find_window(value);
    if (window != nullptr) window->mapped = false;
}
void flush_window(const NativeWindow value) noexcept {
    WaylandWindow* window = find_window(value);
    if (window != nullptr) commit(*window);
}
void draw_text(const NativeWindow value, const char*, int, int) noexcept { (void)value; }
void draw_text_len(const NativeWindow value, const char* text, const int length, const int x, const int y) noexcept {
    draw_text_len_color(value, text, length, x, y, 0x1F2937U, false);
}
void draw_text_color(const NativeWindow value, const char* text, const int x, const int y,
                     const std::uint32_t rgb, const bool bold) noexcept {
    draw_text_len_color(value, text, text == nullptr ? 0 : static_cast<int>(std::strlen(text)), x, y, rgb, bold);
}
void draw_text_len_color(const NativeWindow value, const char* text, const int length, const int x,
                         const int y, const std::uint32_t rgb, const bool) noexcept {
    WaylandWindow* window = find_window(value);
    if (window == nullptr || text == nullptr || length <= 0) return;
    // Minimal 5x7 glyphs: the framebuffer path remains toolkit-free.
    for (int index = 0; index < length; ++index) {
        const unsigned char c = static_cast<unsigned char>(text[index]);
        for (int bit = 0; bit < 8; ++bit) if ((c >> bit) & 1U) fill(*window, x + index * 8, y - bit, 6, 1, rgb);
    }
}
void draw_rectangle(const NativeWindow value, const int x, const int y, const int width, const int height) noexcept {
    draw_rectangle_color(value, x, y, width, height, 0x1F2937U);
}
void draw_rectangle_color(const NativeWindow value, const int x, const int y, const int width,
                          const int height, const std::uint32_t rgb) noexcept {
    WaylandWindow* window = find_window(value); if (window == nullptr) return;
    fill(*window, x, y, width, 1, rgb); fill(*window, x, y + height - 1, width, 1, rgb);
    fill(*window, x, y, 1, height, rgb); fill(*window, x + width - 1, y, 1, height, rgb);
}
void fill_rectangle(const NativeWindow value, const int x, const int y, const int width,
                    const int height, const int brush_index) noexcept {
    static constexpr std::array<std::uint32_t, 5> colors{0xFFFFFFU, 0xC0C0C0U, 0x808080U, 0x404040U, 0x000000U};
    if (brush_index >= 0 && brush_index < static_cast<int>(colors.size())) {
        fill_rectangle_color(value, x, y, width, height,
                             colors[static_cast<std::size_t>(brush_index)]);
    }
}
void fill_rectangle_color(const NativeWindow value, const int x, const int y, const int width,
                          const int height, const std::uint32_t rgb) noexcept {
    WaylandWindow* window = find_window(value); if (window != nullptr) fill(*window, x, y, width, height, rgb);
}

WindowEvent next_window_event(const NativeWindow value) noexcept {
    if (!initialize()) return {};
    WaylandWindow* window = find_window(value); if (window == nullptr) return {};
    if (wl_display_dispatch_pending(g_context.display) < 0) { g_context.failed = true; return {}; }
    if (!window->pending.empty()) { WindowEvent event = window->pending.front(); window->pending.pop_front(); return event; }
    wl_display_flush(g_context.display);
    struct pollfd descriptor{wl_display_get_fd(g_context.display), POLLIN, 0};
    if (::poll(&descriptor, 1, 10) > 0 && wl_display_dispatch(g_context.display) < 0) {
        g_context.failed = true;
    }
    if (!window->pending.empty()) { WindowEvent event = window->pending.front(); window->pending.pop_front(); return event; }
    return {};
}
std::uint32_t message_box(const char*, const char*) noexcept { return 0; }
std::uint32_t track_popup_menu(const std::vector<PopupMenuItem>&, int, int) noexcept { return 0; }

}  // namespace tradutorlinux::gui::wayland
