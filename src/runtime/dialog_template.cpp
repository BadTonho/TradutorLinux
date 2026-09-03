#include "tradutorlinux/runtime/dialog_template.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace tradutorlinux::runtime {
namespace {

constexpr std::uint32_t kDsSetFont = 0x00000040U;
constexpr std::size_t kMaxDialogControls = 64;

[[nodiscard]] bool read_u16(std::span<const std::byte> bytes, std::size_t& offset,
                            std::uint16_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

[[nodiscard]] bool read_u32(std::span<const std::byte> bytes, std::size_t& offset,
                            std::uint32_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

[[nodiscard]] bool read_i16(std::span<const std::byte> bytes, std::size_t& offset,
                            std::int16_t& value) noexcept {
    std::uint16_t raw = 0;
    if (!read_u16(bytes, offset, raw)) {
        return false;
    }
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

[[nodiscard]] bool read_utf16z(std::span<const std::byte> bytes, std::size_t& offset,
                               std::u16string& value) noexcept {
    value.clear();
    while (true) {
        std::uint16_t character = 0;
        if (!read_u16(bytes, offset, character)) {
            return false;
        }
        if (character == 0) {
            return true;
        }
        value.push_back(static_cast<char16_t>(character));
        if (value.size() > bytes.size()) {
            return false;
        }
    }
}

[[nodiscard]] bool align_dword(std::span<const std::byte> bytes, std::size_t& offset) noexcept {
    constexpr std::size_t mask = 3;
    if (offset > std::numeric_limits<std::size_t>::max() - mask) {
        return false;
    }
    const std::size_t aligned = (offset + mask) & ~mask;
    if (aligned > bytes.size()) {
        return false;
    }
    offset = aligned;
    return true;
}

[[nodiscard]] bool read_field(std::span<const std::byte> bytes, std::size_t& offset,
                              std::u16string& value) noexcept {
    std::uint16_t first = 0;
    if (!read_u16(bytes, offset, first)) {
        return false;
    }
    if (first == 0) {
        value.clear();
        return true;
    }
    if (first == 0xFFFFU) {
        // Ordinal titles (icons and predefined resources) are deliberately
        // outside this standard-text dialog subset.
        return false;
    }
    value.assign(1, static_cast<char16_t>(first));
    std::u16string tail;
    if (!read_utf16z(bytes, offset, tail)) {
        return false;
    }
    value += tail;
    return true;
}

[[nodiscard]] bool ascii_class_equals(const std::u16string& value,
                                      const char* expected) noexcept {
    std::size_t index = 0;
    for (; expected[index] != '\0'; ++index) {
        if (index >= value.size()) {
            return false;
        }
        char16_t actual = value[index];
        if (actual >= u'a' && actual <= u'z') {
            actual = static_cast<char16_t>(actual - (u'a' - u'A'));
        }
        if (actual != static_cast<char16_t>(expected[index])) {
            return false;
        }
    }
    return index == value.size();
}

[[nodiscard]] bool classify_control_name(const std::u16string& value,
                                         DialogControlClass& control_class) noexcept {
    if (ascii_class_equals(value, "BUTTON")) {
        control_class = DialogControlClass::Button;
        return true;
    }
    if (ascii_class_equals(value, "EDIT")) {
        control_class = DialogControlClass::Edit;
        return true;
    }
    if (ascii_class_equals(value, "STATIC")) {
        control_class = DialogControlClass::Static;
        return true;
    }
    if (ascii_class_equals(value, "COMBOBOX")) {
        control_class = DialogControlClass::ComboBox;
        return true;
    }
    return false;
}

}  // namespace

DialogTemplateStatus parse_dialog_template(const std::span<const std::byte> bytes,
                                            DialogTemplate& output) noexcept {
    output = {};
    if (bytes.size() < sizeof(GuestDialogTemplate)) {
        return DialogTemplateStatus::Malformed;
    }

    std::size_t offset = 0;
    std::uint32_t first_dword = 0;
    if (!read_u32(bytes, offset, first_dword)) {
        return DialogTemplateStatus::Malformed;
    }
    if ((first_dword & 0xFFFFU) == 1U && (first_dword >> 16U) == 0xFFFFU) {
        return DialogTemplateStatus::DialogEx;
    }
    output.style = first_dword;
    if (!read_u32(bytes, offset, output.extended_style)) {
        return DialogTemplateStatus::Malformed;
    }
    std::uint16_t item_count = 0;
    if (!read_u16(bytes, offset, item_count) || !read_i16(bytes, offset, output.x) ||
        !read_i16(bytes, offset, output.y) || !read_i16(bytes, offset, output.width) ||
        !read_i16(bytes, offset, output.height)) {
        return DialogTemplateStatus::Malformed;
    }
    if (item_count > kMaxDialogControls || (output.style & kDsSetFont) != 0U) {
        return DialogTemplateStatus::Unsupported;
    }

    std::uint16_t marker = 0;
    if (!read_u16(bytes, offset, marker)) {
        return DialogTemplateStatus::Malformed;
    }
    if (marker != 0) {
        return DialogTemplateStatus::Unsupported;
    }
    if (!read_u16(bytes, offset, marker)) {
        return DialogTemplateStatus::Malformed;
    }
    if (marker != 0) {
        return DialogTemplateStatus::Unsupported;
    }
    if (!read_utf16z(bytes, offset, output.title)) {
        return DialogTemplateStatus::Malformed;
    }
    if (!align_dword(bytes, offset)) {
        return DialogTemplateStatus::Malformed;
    }

    output.controls.reserve(item_count);
    for (std::uint16_t index = 0; index < item_count; ++index) {
        if (!align_dword(bytes, offset) || bytes.size() - offset < sizeof(GuestDialogItemTemplate)) {
            return DialogTemplateStatus::Malformed;
        }
        DialogControl control{};
        if (!read_u32(bytes, offset, control.style) ||
            !read_u32(bytes, offset, control.extended_style) || !read_i16(bytes, offset, control.x) ||
            !read_i16(bytes, offset, control.y) || !read_i16(bytes, offset, control.width) ||
            !read_i16(bytes, offset, control.height) || !read_u16(bytes, offset, control.id) ||
            !read_u16(bytes, offset, marker)) {
            return DialogTemplateStatus::Malformed;
        }
        if (marker == 0xFFFFU) {
            std::uint16_t class_ordinal = 0;
            if (!read_u16(bytes, offset, class_ordinal)) {
                return DialogTemplateStatus::Malformed;
            }
            switch (class_ordinal) {
                case 0x0080U: control.control_class = DialogControlClass::Button; break;
                case 0x0081U: control.control_class = DialogControlClass::Edit; break;
                case 0x0082U: control.control_class = DialogControlClass::Static; break;
                case 0x0085U: control.control_class = DialogControlClass::ComboBox; break;
                default: return DialogTemplateStatus::Unsupported;
            }
        } else {
            std::u16string class_name(1, static_cast<char16_t>(marker));
            std::u16string tail;
            if (!read_utf16z(bytes, offset, tail)) {
                return DialogTemplateStatus::Malformed;
            }
            class_name += tail;
            if (!classify_control_name(class_name, control.control_class)) {
                return DialogTemplateStatus::Unsupported;
            }
        }
        if (!read_field(bytes, offset, control.title) || !read_u16(bytes, offset, marker)) {
            return DialogTemplateStatus::Malformed;
        }
        if (marker != 0) {
            return DialogTemplateStatus::Unsupported;
        }
        output.controls.push_back(std::move(control));
    }
    return DialogTemplateStatus::Success;
}

}  // namespace tradutorlinux::runtime
