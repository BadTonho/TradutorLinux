#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::runtime {

// The PE resource uses the byte layout published for the standard Win32
// DLGTEMPLATE/DLGITEMTEMPLATE.  These are deliberately packed descriptions:
// the parser always copies fields out instead of dereferencing unaligned data.
struct __attribute__((packed)) GuestDialogTemplate {
    std::uint32_t style{};
    std::uint32_t extended_style{};
    std::uint16_t item_count{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t width{};
    std::int16_t height{};
};
static_assert(sizeof(GuestDialogTemplate) == 18);

struct __attribute__((packed)) GuestDialogItemTemplate {
    std::uint32_t style{};
    std::uint32_t extended_style{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t width{};
    std::int16_t height{};
    std::uint16_t id{};
};
static_assert(sizeof(GuestDialogItemTemplate) == 18);

enum class DialogControlClass : std::uint8_t { Button, Edit, Static, ComboBox };

struct DialogControl {
    DialogControlClass control_class{};
    std::uint32_t style{};
    std::uint32_t extended_style{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t width{};
    std::int16_t height{};
    std::uint16_t id{};
    std::u16string title;
};

struct DialogTemplate {
    std::uint32_t style{};
    std::uint32_t extended_style{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t width{};
    std::int16_t height{};
    std::u16string title;
    std::vector<DialogControl> controls;
};

enum class DialogTemplateStatus : std::uint8_t {
    Success,
    Malformed,
    Unsupported,
    DialogEx,
};

[[nodiscard]] DialogTemplateStatus parse_dialog_template(std::span<const std::byte> bytes,
                                                          DialogTemplate& output) noexcept;

}  // namespace tradutorlinux::runtime
