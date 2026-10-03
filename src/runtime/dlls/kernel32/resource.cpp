#include "kernel32_process_internal.hpp"

namespace tradutorlinux {

namespace {

struct ResourceKey {
    bool named{false};
    std::uint16_t id{0};
    std::u16string name;
};

[[nodiscard]] bool resource_directory_range(const std::uint32_t relative,
                                            const std::size_t size) noexcept {
    const std::size_t image_size = g_guest_image_size;
    const std::size_t resource_base = g_guest_resource_rva;
    const std::size_t resource_size = g_guest_resource_size;
    return resource_base <= image_size && relative <= resource_size &&
           size <= resource_size - relative && relative <= image_size - resource_base &&
           size <= image_size - resource_base - relative;
}

[[nodiscard]] bool read_resource_u16(const std::uint32_t offset, std::uint16_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint16_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint16_t));
    return true;
}

[[nodiscard]] bool read_resource_u32(const std::uint32_t offset, std::uint32_t& value) noexcept {
    if (!resource_directory_range(offset, sizeof(std::uint32_t))) {
        return false;
    }
    const auto* const ptr = g_guest_image_base + g_guest_resource_rva + offset;
    std::memcpy(&value, ptr, sizeof(std::uint32_t));
    return true;
}

[[nodiscard]] bool make_resource_key(const std::uint16_t* value, ResourceKey& key) noexcept {
    if (value == nullptr) {
        return false;
    }
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    if (raw <= 0xFFFFU) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw);
        return true;
    }
    std::u16string guest_name;
    if (!runtime::copy_guest_wstring(value, kMaxModuleStringUnits, guest_name)) {
        return false;
    }
    key.named = true;
    try {
        key.name.assign(guest_name.begin(), guest_name.end());
    } catch (const std::bad_alloc&) {
        key.name.clear();
        return false;
    }
    return true;
}

[[nodiscard]] bool resource_entry_key(const std::uint32_t raw_name,
                                      ResourceKey& key) noexcept {
    if ((raw_name & 0x80000000U) == 0) {
        key.named = false;
        key.id = static_cast<std::uint16_t>(raw_name & 0xFFFFU);
        return (raw_name & 0xFFFF0000U) == 0;
    }
    const std::uint32_t name_offset = raw_name & 0x7FFFFFFFU;
    std::uint16_t length = 0;
    if (name_offset > std::numeric_limits<std::uint32_t>::max() - 2U ||
        !read_resource_u16(name_offset, length) || length > 4096U ||
        !resource_directory_range(name_offset + 2U,
                                  static_cast<std::size_t>(length) * sizeof(std::uint16_t))) {
        return false;
    }
    key.named = true;
    key.name.clear();
    for (std::uint16_t index = 0; index < length; ++index) {
        std::uint16_t unit = 0;
        const std::uint32_t offset = name_offset + 2U +
                                     static_cast<std::uint32_t>(index) *
                                         static_cast<std::uint32_t>(sizeof(std::uint16_t));
        if (!read_resource_u16(offset, unit)) {
            return false;
        }
        key.name.push_back(static_cast<char16_t>(unit));
    }
    return true;
}

[[nodiscard]] bool resource_keys_equal(const ResourceKey& left,
                                       const ResourceKey& right) noexcept {
    if (left.named != right.named) {
        return false;
    }
    if (!left.named) {
        return left.id == right.id;
    }
    if (left.name.size() != right.name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.name.size(); ++i) {
        const auto c1 = std::tolower(static_cast<unsigned char>(left.name[i]));
        const auto c2 = std::tolower(static_cast<unsigned char>(right.name[i]));
        if (c1 != c2) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool find_resource_entry(const std::uint32_t directory_offset,
                                       const ResourceKey& wanted,
                                       std::uint32_t& child_raw) noexcept {
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (directory_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(directory_offset + 12U, named_count) ||
        !read_resource_u16(directory_offset + 14U, id_count)) {
        return false;
    }
    const std::uint32_t entry_count = static_cast<std::uint32_t>(named_count) + id_count;
    if (entry_count > 4096U ||
        !resource_directory_range(directory_offset + 16U,
                                  static_cast<std::size_t>(entry_count) * 8U)) {
        return false;
    }
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::uint32_t entry_offset = directory_offset + 16U + index * 8U;
        std::uint32_t raw_name = 0;
        std::uint32_t raw_child = 0;
        if (!read_resource_u32(entry_offset, raw_name) ||
            !read_resource_u32(entry_offset + 4U, raw_child)) {
            return false;
        }
        ResourceKey actual;
        if (!resource_entry_key(raw_name, actual)) {
            return false;
        }
        if (resource_keys_equal(actual, wanted)) {
            child_raw = raw_child;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool current_resource_module(const void* module) noexcept {
    if (module == nullptr) {
        return true;
    }
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(module);
    return value == 0x1000U ||
           (g_guest_image_base != nullptr &&
            value == reinterpret_cast<std::uintptr_t>(g_guest_image_base));
}

[[nodiscard]] std::optional<ResourceSlot> resolve_resource(const void* module,
                                                           const std::uint16_t* name,
                                                           const std::uint16_t* type) noexcept {
    if (!current_resource_module(module) || g_guest_image_base == nullptr ||
        g_guest_resource_size < 16U) {
        return std::nullopt;
    }
    ResourceKey type_key;
    ResourceKey name_key;
    if (!make_resource_key(type, type_key) || !make_resource_key(name, name_key)) {
        return std::nullopt;
    }
    std::uint32_t name_dir_raw = 0;
    if (!find_resource_entry(0, type_key, name_dir_raw) || (name_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    std::uint32_t lang_dir_raw = 0;
    if (!find_resource_entry(name_dir_raw & 0x7FFFFFFFU, name_key, lang_dir_raw) ||
        (lang_dir_raw & 0x80000000U) == 0) {
        return std::nullopt;
    }
    const std::uint32_t lang_offset = lang_dir_raw & 0x7FFFFFFFU;
    std::uint16_t named_count = 0;
    std::uint16_t id_count = 0;
    if (lang_offset > std::numeric_limits<std::uint32_t>::max() - 16U ||
        !read_resource_u16(lang_offset + 12U, named_count) ||
        !read_resource_u16(lang_offset + 14U, id_count) ||
        static_cast<std::uint32_t>(named_count) + id_count == 0U) {
        return std::nullopt;
    }
    std::uint32_t data_entry_raw = 0;
    if (!read_resource_u32(lang_offset + 16U + 4U, data_entry_raw) ||
        (data_entry_raw & 0x80000000U) != 0) {
        return std::nullopt;
    }
    std::uint32_t data_rva = 0;
    std::uint32_t data_size = 0;
    if (!read_resource_u32(data_entry_raw, data_rva) ||
        !read_resource_u32(data_entry_raw + 4U, data_size) ||
        data_rva > g_guest_image_size || data_size > g_guest_image_size - data_rva) {
        return std::nullopt;
    }
    return ResourceSlot{.used = true, .data_rva = data_rva, .data_size = data_size};
}

} // namespace

extern "C" {

TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept {
    const auto slot = resolve_resource(module, name, type);
    if (!slot.has_value()) {
        const std::array<diagnostics::TraceField, 4> fields{
            diagnostics::TraceField{"symbol", "FindResourceW"},
            diagnostics::TraceField{"status", "not-found"},
            diagnostics::TraceField{"resource-rva", std::to_string(g_guest_resource_rva)},
            diagnostics::TraceField{"resource-size", std::to_string(g_guest_resource_size)},
        };
        runtime_trace("FindResourceW", fields, 4);
        set_last_error(abi::kErrorResourceNotFound);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    auto it = std::find_if(g_resources.begin(), g_resources.end(),
                           [](const ResourceSlot& s) { return !s.used; });
    if (it == g_resources.end()) {
        set_last_error(abi::kErrorNotEnoughMemory);
        return nullptr;
    }
    *it = *slot;
    const auto index = static_cast<std::size_t>(it - g_resources.begin());
    set_last_error(abi::kErrorSuccess);
    return reinterpret_cast<void*>(kResourceHandleBase + index);
}

TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<void*>(resource);
}

TL_MSABI void* tl_LockResource(const void* resource_data) noexcept {
    if (resource_data == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource_data);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used || g_guest_image_base == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return nullptr;
    }
    set_last_error(abi::kErrorSuccess);
    return const_cast<std::byte*>(g_guest_image_base + slot.data_rva);
}

TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept {
    if (!current_resource_module(module) || resource == nullptr) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto addr = reinterpret_cast<std::uintptr_t>(resource);
    if (addr < kResourceHandleBase || addr >= kResourceHandleBase + g_resources.size()) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    const auto index = static_cast<std::size_t>(addr - kResourceHandleBase);
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    const ResourceSlot& slot = g_resources[index];
    if (!slot.used) {
        set_last_error(abi::kErrorInvalidParameter);
        return 0;
    }
    set_last_error(abi::kErrorSuccess);
    return slot.data_size;
}

TL_MSABI void* tl_FindResourceExW(void* const module, const wchar_t* const type, const wchar_t* const name, const std::uint16_t language) noexcept {
    (void)language;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
}

TL_MSABI void* tl_FindResourceA(void* const module, const char* const name, const char* const type) noexcept {
    (void)module;
    (void)name;
    (void)type;
    return tl_FindResourceW(module, reinterpret_cast<const std::uint16_t*>(name), reinterpret_cast<const std::uint16_t*>(type));
}

}  // extern "C"
}  // namespace tradutorlinux
