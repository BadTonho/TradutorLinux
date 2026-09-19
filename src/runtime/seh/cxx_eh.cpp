#include "tradutorlinux/runtime/cxx_eh.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/win32/kernel32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace tradutorlinux::runtime {
namespace {

constexpr std::uint32_t kCxxException = 0xE06D7363U;
constexpr std::uint32_t kFuncInfoMagicV3 = 0x19930522U;
constexpr std::uint32_t kCatchAll = 0x40U;
constexpr std::size_t kFuncInfoWords = 10U;
constexpr std::size_t kUnwindMapEntrySize = 8U;
constexpr std::size_t kTryBlockEntrySize = 20U;
constexpr std::size_t kHandlerMapEntrySize = 20U;
constexpr std::size_t kIpMapEntrySize = 8U;
constexpr std::int32_t kInvalidState = -1;
constexpr std::int32_t kMaxState = 4095;
constexpr std::uint32_t kMaxTryBlocks = 256U;
constexpr std::uint32_t kMaxHandlersPerTry = 64U;
constexpr std::uint32_t kMaxIpEntries = 4096U;
constexpr std::uint32_t kMaxCatchableTypes = 64U;
constexpr std::uint32_t kMaxFh4Entries = 4096U;
constexpr std::uint32_t kMaxFh4TryBlocks = 256U;
constexpr std::uint32_t kMaxFh4Handlers = 64U;
constexpr std::uint32_t kMaxFh4Segments = 4096U;
constexpr std::size_t kThrowInfoSize = 16U;
constexpr std::size_t kCatchableTypeSize = 28U;
constexpr std::size_t kTypeDescriptorNameOffset = 16U;
constexpr std::uint32_t kCatchableTypeSimple = 0x00000001U;
constexpr std::uint32_t kCatchableTypeByReferenceOnly = 0x00000002U;
constexpr std::uint32_t kHandlerTypeReference = 0x00000008U;
constexpr std::size_t kMaxCatchObjectSize = 4096U;
constexpr std::size_t kMaxCleanupActions = 64U;
constexpr std::size_t kCxxCallbackStackSize = 65536U;
constexpr std::uint64_t kCxxCleanupStackOffset = 0x1000U;
// A cadeia FH4 pode conter destruidores arbitrários, inclusive thunks de
// destrutor virtual. O subconjunto executável cobre a profundidade observada
// no Notepad++ (até 13 estados) protegendo contra ciclos e corrupção de stack.
constexpr std::size_t kMaxFh4ExecutableCleanupActions = 16U;

enum class CleanupActionKind : std::uint8_t {
    LegacyFrame,
    DtorWithObject,
    DtorWithPointerToObject,
    Rva,
};

struct CleanupAction {
    std::uint32_t action_rva{};
    std::uint32_t object_offset{};
    CleanupActionKind kind{CleanupActionKind::LegacyFrame};
};

thread_local ContextAmd64 g_cxx_catch_context{};
thread_local bool g_cxx_catch_context_ready = false;
thread_local ContextAmd64 g_cxx_catch_resume_context{};
thread_local bool g_cxx_catch_resume_context_ready = false;
thread_local bool g_cxx_funclet_active = false;
thread_local std::uint64_t g_cxx_catch_target = 0U;
thread_local std::uint64_t g_cxx_catch_continuation = 0U;
thread_local bool g_cxx_catch_continuation_ready = false;
thread_local std::uint64_t g_cxx_catch_return_target = 0U;
thread_local bool g_cxx_catch_return_target_ready = false;
thread_local bool g_cxx_catch_return_preserve_stack = false;
thread_local std::uint64_t g_cxx_catch_resume_stack = 0U;
alignas(16) thread_local std::array<std::byte, kCxxCallbackStackSize>
    g_cxx_callback_stack{};
thread_local bool g_cxx_cleanup_context_ready = false;
thread_local std::uint64_t g_cxx_cleanup_target = 0U;
thread_local std::uint64_t g_cxx_cleanup_establisher = 0U;
thread_local std::array<struct CleanupAction, kMaxCleanupActions> g_cxx_cleanup_actions{};
thread_local std::size_t g_cxx_cleanup_count = 0U;
thread_local std::size_t g_cxx_cleanup_index = 0U;
thread_local std::array<std::uint64_t, kMaxCleanupActions>
    g_cxx_cleanup_return_slots{};
thread_local std::array<std::uint64_t, kMaxCleanupActions>
    g_cxx_cleanup_return_values{};
thread_local std::size_t g_cxx_cleanup_return_slot_count = 0U;
thread_local std::uint64_t g_cxx_catch_return_slot = 0U;
thread_local std::uint64_t g_cxx_catch_return_value = 0U;
thread_local bool g_cxx_catch_return_slot_ready = false;

extern "C" std::uintptr_t tl_cxx_callback_stack_top() noexcept {
    return reinterpret_cast<std::uintptr_t>(g_cxx_callback_stack.data() +
                                             g_cxx_callback_stack.size());
}

[[nodiscard]] bool write_guest_stack_value(const std::uint64_t stack_pointer,
                                           const std::uint64_t value) noexcept {
    void* const destination = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(stack_pointer));
    if (!validate_guest_stack_range(destination, sizeof(value), true)) {
        return false;
    }
    return write_guest_memory(destination, &value, sizeof(value)).status ==
           GuestMemoryAccessStatus::Success;
}

[[nodiscard]] bool save_guest_stack_value(const std::uint64_t stack_pointer,
                                          std::uint64_t& value) noexcept {
    if (!validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer),
                                    sizeof(value), false) ||
        read_guest_memory(reinterpret_cast<const void*>(
                              static_cast<std::uintptr_t>(stack_pointer)),
                          &value, sizeof(value)).status != GuestMemoryAccessStatus::Success) {
        return false;
    }
    return true;
}

[[nodiscard]] bool save_cleanup_return_slot(const std::uint64_t stack_pointer) noexcept {
    if (g_cxx_cleanup_return_slot_count >= g_cxx_cleanup_return_slots.size()) {
        return false;
    }
    std::uint64_t value{};
    if (!save_guest_stack_value(stack_pointer, value)) {
        return false;
    }
    const std::size_t index = g_cxx_cleanup_return_slot_count++;
    g_cxx_cleanup_return_slots[index] = stack_pointer;
    g_cxx_cleanup_return_values[index] = value;
    return true;
}

[[nodiscard]] bool restore_cleanup_return_slot(const std::size_t index,
                                               const std::uint64_t stack_pointer) noexcept {
    if (index >= g_cxx_cleanup_return_slot_count || stack_pointer < sizeof(std::uint64_t) ||
        g_cxx_cleanup_return_slots[index] != stack_pointer - sizeof(std::uint64_t)) {
        return false;
    }
    return write_guest_stack_value(g_cxx_cleanup_return_slots[index],
                                   g_cxx_cleanup_return_values[index]);
}

struct ImageReader {
    GuestUnwindView view{};

    [[nodiscard]] bool range(const std::uint32_t rva, const std::size_t size) const noexcept {
        return view.image_base != nullptr && rva <= view.image_size &&
               size <= view.image_size - static_cast<std::size_t>(rva);
    }

    [[nodiscard]] const std::byte* address(const std::uint32_t rva,
                                            const std::size_t size = 1U) const noexcept {
        if (!range(rva, size)) {
            return nullptr;
        }
        return static_cast<const std::byte*>(view.image_base) + rva;
    }

    [[nodiscard]] bool read_u32(const std::uint32_t rva, std::uint32_t& value) const noexcept {
        const std::byte* const source = address(rva, sizeof(value));
        if (source == nullptr) {
            return false;
        }
        std::memcpy(&value, source, sizeof(value));
        return true;
    }

    [[nodiscard]] bool read_u8(const std::uint32_t rva, std::uint8_t& value) const noexcept {
        const std::byte* const source = address(rva, sizeof(value));
        if (source == nullptr) {
            return false;
        }
        std::memcpy(&value, source, sizeof(value));
        return true;
    }

    [[nodiscard]] bool add_rva(const std::uint32_t rva, const std::size_t delta,
                               std::uint32_t& result) const noexcept {
        if (delta > std::numeric_limits<std::uint32_t>::max() - rva) {
            return false;
        }
        result = rva + static_cast<std::uint32_t>(delta);
        return true;
    }

    [[nodiscard]] bool read_i32(const std::uint32_t rva, std::int32_t& value) const noexcept {
        std::uint32_t raw{};
        if (!read_u32(rva, raw)) {
            return false;
        }
        std::memcpy(&value, &raw, sizeof(value));
        return true;
    }

    [[nodiscard]] bool range_for_count(const std::uint32_t rva, const std::uint32_t count,
                                       const std::size_t stride) const noexcept {
        if (stride != 0U && count > std::numeric_limits<std::size_t>::max() / stride) {
            return false;
        }
        return address(rva, static_cast<std::size_t>(count) * stride) != nullptr;
    }

    [[nodiscard]] bool rva_of(const void* const pointer, std::uint32_t& rva) const noexcept {
        if (pointer == nullptr || view.image_base == nullptr) {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
        const auto value = reinterpret_cast<std::uintptr_t>(pointer);
        if (value < base || value - base >= view.image_size || value - base >
            std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        rva = static_cast<std::uint32_t>(value - base);
        return true;
    }
};

[[nodiscard]] bool read_fh4_unsigned(const ImageReader& image, std::uint32_t& cursor,
                                     std::uint32_t& value) noexcept {
    std::uint8_t first{};
    if (!image.read_u8(cursor, first)) {
        return false;
    }
    const std::uint8_t tag = first & 0x0fU;
    std::size_t length = 0U;
    switch (tag) {
        case 0U:
        case 2U:
        case 4U:
        case 6U:
        case 8U:
        case 10U:
        case 12U:
        case 14U:
            length = 1U;
            value = static_cast<std::uint32_t>(first >> 1U);
            break;
        case 1U:
        case 5U:
        case 9U:
        case 13U: {
            std::uint8_t second{};
            std::uint32_t byte_rva{};
            if (!image.add_rva(cursor, 1U, byte_rva) || !image.read_u8(byte_rva, second)) {
                return false;
            }
            length = 2U;
            value = (static_cast<std::uint32_t>(first) >> 2U) |
                    (static_cast<std::uint32_t>(second) << 6U);
            break;
        }
        case 3U:
        case 11U: {
            std::uint8_t second{};
            std::uint8_t third{};
            std::uint32_t second_rva{};
            std::uint32_t third_rva{};
            if (!image.add_rva(cursor, 1U, second_rva) ||
                !image.add_rva(cursor, 2U, third_rva) ||
                !image.read_u8(second_rva, second) || !image.read_u8(third_rva, third)) {
                return false;
            }
            length = 3U;
            value = (static_cast<std::uint32_t>(first) >> 3U) |
                    (static_cast<std::uint32_t>(second) << 5U) |
                    (static_cast<std::uint32_t>(third) << 13U);
            break;
        }
        case 7U: {
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t index = 0U; index < bytes.size(); ++index) {
                std::uint32_t byte_rva{};
                if (!image.add_rva(cursor, index, byte_rva) ||
                    !image.read_u8(byte_rva, bytes[index])) {
                    return false;
                }
            }
            length = 4U;
            value = (static_cast<std::uint32_t>(first) >> 4U) |
                    (static_cast<std::uint32_t>(bytes[1]) << 4U) |
                    (static_cast<std::uint32_t>(bytes[2]) << 12U) |
                    (static_cast<std::uint32_t>(bytes[3]) << 20U);
            break;
        }
        case 15U: {
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t index = 0U; index < bytes.size(); ++index) {
                std::uint32_t byte_rva{};
                if (!image.add_rva(cursor, 1U + index, byte_rva) ||
                    !image.read_u8(byte_rva, bytes[index])) {
                    return false;
                }
            }
            length = 5U;
            value = static_cast<std::uint32_t>(bytes[0]) |
                    (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                    (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                    (static_cast<std::uint32_t>(bytes[3]) << 24U);
            break;
        }
        default:
            return false;
    }
    if (cursor > std::numeric_limits<std::uint32_t>::max() - length) {
        return false;
    }
    cursor += static_cast<std::uint32_t>(length);
    return true;
}

[[nodiscard]] bool read_fh4_disp(const ImageReader& image, std::uint32_t& cursor,
                                 std::uint32_t& value, const bool allow_zero) noexcept {
    std::int32_t displacement{};
    std::uint32_t raw{};
    if (cursor > std::numeric_limits<std::uint32_t>::max() - sizeof(raw) ||
        !image.read_u32(cursor, raw)) {
        return false;
    }
    cursor += sizeof(raw);
    std::memcpy(&displacement, &raw, sizeof(displacement));
    if (displacement < 0 || (!allow_zero && displacement == 0)) {
        return false;
    }
    value = static_cast<std::uint32_t>(displacement);
    return value == 0U || image.range(value, 1U);
}

struct Fh4FuncInfo {
    std::uint8_t header{};
    std::uint32_t unwind_map{};
    std::uint32_t try_block_map{};
    std::uint32_t ip_map{};
    std::uint32_t frame{};
    std::uint32_t function_begin{};
};

[[nodiscard]] bool read_fh4_func_info(const ImageReader& image,
                                      const std::uint32_t func_info_rva,
                                      const std::uint32_t function_begin,
                                      Fh4FuncInfo& info) noexcept {
    std::uint8_t header{};
    if (!image.read_u8(func_info_rva, header) || (header & 0x80U) != 0U) {
        return false;
    }

    std::uint32_t cursor{};
    if (!image.add_rva(func_info_rva, 1U, cursor)) {
        return false;
    }
    info = {};
    info.header = header;
    info.function_begin = function_begin;
    std::uint32_t ignored{};
    if ((header & 0x04U) != 0U && !read_fh4_unsigned(image, cursor, ignored)) {
        return false;
    }
    if ((header & 0x08U) != 0U &&
        !read_fh4_disp(image, cursor, info.unwind_map, true)) {
        return false;
    }
    if ((header & 0x10U) != 0U &&
        !read_fh4_disp(image, cursor, info.try_block_map, true)) {
        return false;
    }

    if ((header & 0x02U) != 0U) {
        std::uint32_t segment_map_rva{};
        if (!read_fh4_disp(image, cursor, segment_map_rva, false)) {
            return false;
        }
        std::uint32_t segment_cursor = segment_map_rva;
        std::uint32_t segment_count{};
        if (!read_fh4_unsigned(image, segment_cursor, segment_count) ||
            segment_count > kMaxFh4Segments) {
            return false;
        }
        for (std::uint32_t index = 0U; index < segment_count; ++index) {
            std::uint32_t segment_begin{};
            std::uint32_t segment_ip_map{};
            if (!read_fh4_disp(image, segment_cursor, segment_begin, false) ||
                !read_fh4_disp(image, segment_cursor, segment_ip_map, false)) {
                return false;
            }
            if (segment_begin == function_begin) {
                info.ip_map = segment_ip_map;
            }
        }
    } else if (!read_fh4_disp(image, cursor, info.ip_map, true)) {
        return false;
    }

    if ((header & 0x01U) != 0U && !read_fh4_unsigned(image, cursor, info.frame)) {
        return false;
    }
    return true;
}

struct FuncInfo {
    std::int32_t max_state{};
    std::uint32_t unwind_map{};
    std::uint32_t try_block_count{};
    std::uint32_t try_block_map{};
    std::uint32_t ip_map_count{};
    std::uint32_t ip_map{};
    std::uint32_t eh_flags{};
};

struct UnwindAction {
    std::int32_t to_state{kInvalidState};
    std::uint32_t action_rva{0xFFFFFFFFU};
};

struct CleanupPlan {
    std::array<CleanupAction, kMaxCleanupActions> actions{};
    std::size_t count{};
};

struct Fh4UnwindEntry {
    std::uint32_t entry_rva{};
    std::uint32_t next_offset{};
    std::uint32_t action_rva{};
    std::uint32_t object{};
    std::int32_t next_state{kInvalidState};
    std::uint8_t type{};
};

struct Fh4UnwindMap {
    std::uint32_t count{};
    std::array<Fh4UnwindEntry, kMaxFh4Entries> entries{};
};

struct Fh4TryBlock {
    std::uint32_t try_low{};
    std::uint32_t try_high{};
    std::uint32_t catch_high{};
    std::uint32_t handler_map{};
};

struct Fh4TryBlockMap {
    std::uint32_t count{};
    std::array<Fh4TryBlock, kMaxFh4TryBlocks> entries{};
};

struct Fh4Handler {
    std::uint32_t adjectives{};
    std::uint32_t type_rva{};
    std::uint32_t catch_object{};
    std::uint32_t handler_rva{};
    std::array<std::uint32_t, 2> continuation_rvas{};
    std::uint8_t continuation_count{};
    bool has_type{};
    bool has_catch_object{};
};

struct CatchableType {
    std::uint32_t properties{};
    std::uint32_t type_rva{};
    std::int32_t member_displacement{};
    std::int32_t vbptr_displacement{-1};
    std::int32_t vbase_displacement{};
    std::uint32_t size_or_offset{};
    std::uint32_t copy_function{};
};

[[nodiscard]] bool read_fh4_unwind_map(const ImageReader& image,
                                       const std::uint32_t map_rva,
                                       Fh4UnwindMap& map) noexcept {
    map = {};
    std::uint32_t cursor = map_rva;
    if (!read_fh4_unsigned(image, cursor, map.count) || map.count > kMaxFh4Entries) {
        return false;
    }
    std::array<std::uint32_t, kMaxFh4Entries> entry_rvas{};
    const std::uint32_t entries_begin = cursor;
    for (std::uint32_t index = 0U; index < map.count; ++index) {
        Fh4UnwindEntry& entry = map.entries[index];
        entry.entry_rva = cursor;
        entry_rvas[index] = cursor;
        std::uint32_t encoded{};
        if (!read_fh4_unsigned(image, cursor, encoded)) {
            return false;
        }
        entry.type = static_cast<std::uint8_t>(encoded & 0x03U);
        entry.next_offset = encoded >> 2U;
        if (entry.type > 3U) {
            return false;
        }
        if (entry.type == 1U || entry.type == 2U || entry.type == 3U) {
            if (!read_fh4_disp(image, cursor, entry.action_rva, false)) {
                return false;
            }
        }
        if (entry.type == 1U || entry.type == 2U) {
            if (!read_fh4_unsigned(image, cursor, entry.object)) {
                return false;
            }
        }
    }

    for (std::uint32_t index = 0U; index < map.count; ++index) {
        Fh4UnwindEntry& entry = map.entries[index];
        if (entry.next_offset > entry.entry_rva - entries_begin) {
            entry.next_state = kInvalidState;
            continue;
        }
        const std::uint32_t previous_rva = entry.entry_rva - entry.next_offset;
        bool found = false;
        for (std::uint32_t previous = 0U; previous < index; ++previous) {
            if (entry_rvas[previous] == previous_rva) {
                entry.next_state = static_cast<std::int32_t>(previous);
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_fh4_try_block_map(const ImageReader& image,
                                          const std::uint32_t map_rva,
                                          Fh4TryBlockMap& map) noexcept {
    map = {};
    std::uint32_t cursor = map_rva;
    if (!read_fh4_unsigned(image, cursor, map.count) || map.count > kMaxFh4TryBlocks) {
        return false;
    }
    for (std::uint32_t index = 0U; index < map.count; ++index) {
        Fh4TryBlock& entry = map.entries[index];
        if (!read_fh4_unsigned(image, cursor, entry.try_low) ||
            !read_fh4_unsigned(image, cursor, entry.try_high) ||
            !read_fh4_unsigned(image, cursor, entry.catch_high) ||
            !read_fh4_disp(image, cursor, entry.handler_map, true) ||
            entry.try_low > entry.try_high || entry.try_high > entry.catch_high) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_fh4_ip_state(const ImageReader& image,
                                     const Fh4FuncInfo& info,
                                     const std::uint32_t control_rva,
                                     std::int32_t& state) noexcept {
    state = kInvalidState;
    if (info.ip_map == 0U) {
        return true;
    }
    if (control_rva < info.function_begin) {
        return false;
    }
    const std::uint32_t control_offset = control_rva - info.function_begin;
    std::uint32_t cursor = info.ip_map;
    std::uint32_t count{};
    if (!read_fh4_unsigned(image, cursor, count) || count > kMaxFh4Entries) {
        return false;
    }
    std::uint32_t ip_offset = 0U;
    bool found = false;
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint32_t delta{};
        std::uint32_t encoded_state{};
        if (!read_fh4_unsigned(image, cursor, delta) ||
            delta > std::numeric_limits<std::uint32_t>::max() - ip_offset ||
            !read_fh4_unsigned(image, cursor, encoded_state)) {
            return false;
        }
        ip_offset += delta;
        if (encoded_state > kMaxFh4Entries) {
            return false;
        }
        const std::int32_t candidate = encoded_state == 0U
                                           ? kInvalidState
                                           : static_cast<std::int32_t>(encoded_state - 1U);
        if (candidate >= static_cast<std::int32_t>(kMaxFh4Entries)) {
            return false;
        }
        if (ip_offset <= control_offset) {
            state = candidate;
            found = true;
        }
    }
    return !found || state == kInvalidState || state >= 0;
}

[[nodiscard]] bool read_fh4_handler(const ImageReader& image,
                                    std::uint32_t& cursor,
                                    const std::uint32_t function_begin,
                                    Fh4Handler& handler) noexcept {
    handler = {};
    std::uint8_t header{};
    if (!image.read_u8(cursor, header) || (header & 0xC0U) != 0U ||
        ((header >> 4U) & 0x03U) == 0x03U) {
        return false;
    }
    if (!image.add_rva(cursor, 1U, cursor)) {
        return false;
    }
    const bool has_adjectives = (header & 0x01U) != 0U;
    handler.has_type = (header & 0x02U) != 0U;
    handler.has_catch_object = (header & 0x04U) != 0U;
    const bool continuation_is_rva = (header & 0x08U) != 0U;
    handler.continuation_count = static_cast<std::uint8_t>((header >> 4U) & 0x03U);
    std::uint32_t ignored{};
    if (has_adjectives && !read_fh4_unsigned(image, cursor, handler.adjectives)) {
        return false;
    }
    if (handler.has_type && !read_fh4_disp(image, cursor, handler.type_rva, false)) {
        return false;
    }
    if (handler.has_catch_object &&
        !read_fh4_unsigned(image, cursor, handler.catch_object)) {
        return false;
    }
    if (!read_fh4_disp(image, cursor, handler.handler_rva, false)) {
        return false;
    }
    for (std::uint8_t index = 0U; index < handler.continuation_count; ++index) {
        std::uint32_t continuation{};
        if (continuation_is_rva) {
            if (!read_fh4_disp(image, cursor, continuation, false)) {
                return false;
            }
        } else if (!read_fh4_unsigned(image, cursor, continuation) ||
                   continuation > std::numeric_limits<std::uint32_t>::max() - function_begin) {
            return false;
        } else {
            continuation += function_begin;
            if (!image.range(continuation, 1U)) {
                return false;
            }
        }
        handler.continuation_rvas[index] = continuation;
    }
    (void)ignored;
    return true;
}

void trace_cxx_eh(const diagnostics::TraceLevel level, const char* const state,
                  const char* const detail) noexcept {
    try {
        const std::array fields{
            diagnostics::TraceField{"state", state},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"mechanism", "msvc-x64"},
        };
        diagnostics::write_trace(
            std::cerr, diagnostics::TraceComponent::Runtime, level, "cxx-eh", fields);
    } catch (...) {
    }
}

void trace_cxx_eh_state(const diagnostics::TraceLevel level, const char* const detail,
                        const std::uint32_t control_rva, const std::int32_t state) noexcept {
    try {
        const std::array fields{
            diagnostics::TraceField{"state", "search"},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"mechanism", "msvc-x64"},
            diagnostics::TraceField{"control-rva", std::to_string(control_rva)},
            diagnostics::TraceField{"current-state", std::to_string(state)},
        };
        diagnostics::write_trace(
            std::cerr, diagnostics::TraceComponent::Runtime, level, "cxx-eh", fields);
    } catch (...) {
    }
}

void trace_cxx_eh_transfer(const diagnostics::TraceLevel level, const char* const detail,
                           const std::uint64_t context_rsp, const std::uint64_t stack_pointer,
                           const std::uint64_t target_ip,
                           const std::uint64_t return_target) noexcept {
    try {
        const std::array fields{
            diagnostics::TraceField{"state", "transfer"},
            diagnostics::TraceField{"detail", detail},
            diagnostics::TraceField{"context-rsp", std::to_string(context_rsp)},
            diagnostics::TraceField{"stack-pointer", std::to_string(stack_pointer)},
            diagnostics::TraceField{"target-ip", std::to_string(target_ip)},
            diagnostics::TraceField{"return-target", std::to_string(return_target)},
        };
        diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime, level,
                                 "cxx-eh", fields);
    } catch (...) {
    }
}

[[nodiscard]] bool read_func_info(const ImageReader& image, const std::uint32_t rva,
                                  FuncInfo& info) noexcept {
    std::array<std::uint32_t, kFuncInfoWords> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        std::uint32_t entry{};
        if (!image.add_rva(rva, index * sizeof(std::uint32_t), entry) ||
            !image.read_u32(entry, words[index])) {
            return false;
        }
    }
    if (words[0] != kFuncInfoMagicV3) {
        return false;
    }
    std::memcpy(&info.max_state, &words[1], sizeof(info.max_state));
    info.unwind_map = words[2];
    info.try_block_count = words[3];
    info.try_block_map = words[4];
    info.ip_map_count = words[5];
    info.ip_map = words[6];
    info.eh_flags = words[9];
    if (info.max_state < kInvalidState || info.max_state > kMaxState ||
        info.try_block_count > kMaxTryBlocks || info.ip_map_count > kMaxIpEntries) {
        return false;
    }
    const std::uint32_t unwind_count =
        info.max_state < 0 ? 0U : static_cast<std::uint32_t>(info.max_state);
    return image.range_for_count(info.unwind_map, unwind_count, kUnwindMapEntrySize) &&
           image.range_for_count(info.try_block_map, info.try_block_count,
                                 kTryBlockEntrySize) &&
           image.range_for_count(info.ip_map, info.ip_map_count, kIpMapEntrySize);
}

[[nodiscard]] bool valid_state(const std::int32_t state, const FuncInfo& info) noexcept {
    return state == kInvalidState ||
           (state >= 0 && state < info.max_state);
}

[[nodiscard]] bool validate_unwind_map(const ImageReader& image, const FuncInfo& info) noexcept {
    const std::uint32_t count =
        info.max_state < 0 ? 0U : static_cast<std::uint32_t>(info.max_state);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t entry{};
        std::int32_t state{};
        std::uint32_t action{};
        std::uint32_t action_entry{};
        if (!image.add_rva(info.unwind_map,
                           static_cast<std::size_t>(index) * kUnwindMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, action_entry) || !image.read_i32(entry, state) ||
            !image.read_u32(action_entry, action) ||
            !valid_state(state, info) || (action != 0xFFFFFFFFU && !image.range(action, 1U))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_unwind_action(const ImageReader& image, const FuncInfo& info,
                                      const std::int32_t state,
                                      UnwindAction& action) noexcept {
    if (state == kInvalidState || !valid_state(state, info)) {
        return false;
    }
    std::uint32_t entry{};
    std::uint32_t action_entry{};
    if (!image.add_rva(info.unwind_map,
                       static_cast<std::size_t>(state) * kUnwindMapEntrySize, entry) ||
        !image.add_rva(entry, 4U, action_entry) || !image.read_i32(entry, action.to_state) ||
        !image.read_u32(action_entry, action.action_rva) ||
        !valid_state(action.to_state, info) ||
        (action.action_rva != 0xFFFFFFFFU && !image.range(action.action_rva, 1U))) {
        return false;
    }
    // LLVM usa tanto RVA zero quanto -1 como entrada sem ação, dependendo
    // da versão do WinEH que produziu a tabela. A entrada continua válida;
    // o construtor da cadeia decide quando não há mais uma ação executável.
    return true;
}

[[nodiscard]] bool build_cleanup_plan(const ImageReader& image, const FuncInfo& info,
                                      const std::int32_t initial_state,
                                      CleanupPlan& plan) noexcept {
    std::array<std::int32_t, kMaxCleanupActions> visited{};
    std::size_t visited_count = 0U;
    std::int32_t state = initial_state;
    while (state != kInvalidState) {
        if (!valid_state(state, info) || visited_count >= visited.size()) {
            return false;
        }
        if (std::find(visited.begin(), visited.begin() +
                                      static_cast<std::ptrdiff_t>(visited_count), state) !=
            visited.begin() + static_cast<std::ptrdiff_t>(visited_count)) {
            return false;
        }
        visited[visited_count++] = state;

        UnwindAction action{};
        if (!read_unwind_action(image, info, state, action)) {
            return false;
        }
        // Uma entrada sem ação encerra a cadeia relevante para o handler
        // selecionado. Estados anteriores podem representar outra região do
        // mesmo FuncInfo (por exemplo, o caminho de terminação do catch).
        if (action.action_rva == 0U || action.action_rva == 0xFFFFFFFFU) {
            break;
        }
        if (plan.count >= plan.actions.size()) {
            return false;
        }
        plan.actions[plan.count++] = CleanupAction{
            .action_rva = action.action_rva,
            .object_offset = 0U,
            .kind = CleanupActionKind::LegacyFrame};
        state = action.to_state;
    }
    return true;
}

[[nodiscard]] bool build_fh4_cleanup_plan(const Fh4UnwindMap& unwind_map,
                                          const std::int32_t initial_state,
                                          const std::int32_t target_state,
                                          CleanupPlan& plan) noexcept {
    std::array<bool, kMaxFh4Entries> visited{};
    std::int32_t state = initial_state;
    while (state != kInvalidState &&
           (target_state == kInvalidState || state >= target_state)) {
        if (state < 0 || state >= static_cast<std::int32_t>(unwind_map.count) ||
            visited[static_cast<std::size_t>(state)]) {
            return false;
        }
        visited[static_cast<std::size_t>(state)] = true;
        const Fh4UnwindEntry& entry = unwind_map.entries[static_cast<std::size_t>(state)];
        if (entry.type != 0U) {
            if (plan.count >= plan.actions.size() || entry.action_rva == 0U ||
                entry.action_rva == 0xFFFFFFFFU) {
                return false;
            }
            const CleanupActionKind kind = entry.type == 1U
                                               ? CleanupActionKind::DtorWithObject
                                               : entry.type == 2U
                                                     ? CleanupActionKind::DtorWithPointerToObject
                                                     : CleanupActionKind::Rva;
            plan.actions[plan.count++] = CleanupAction{
                .action_rva = entry.action_rva,
                .object_offset = entry.object,
                .kind = kind};
        }
        state = entry.next_state;
    }
    return true;
}

[[nodiscard]] bool resolve_cleanup_object(const CleanupAction& action,
                                          const std::uintptr_t establisher_frame,
                                          std::uint64_t& object_argument,
                                          bool& has_object_argument) noexcept {
    has_object_argument = false;
    if (action.kind != CleanupActionKind::DtorWithObject &&
        action.kind != CleanupActionKind::DtorWithPointerToObject) {
        return true;
    }
    // The runtime's establisher frame is the lower stack base used by the
    // guest's FH4 funclets. Object offsets are positive displacements from
    // that base; check the addition before forming the guest address.
    if (action.object_offset > std::numeric_limits<std::uintptr_t>::max() -
                                  establisher_frame) {
        return false;
    }
    const std::uintptr_t object_slot = establisher_frame + action.object_offset;
    if (action.kind == CleanupActionKind::DtorWithObject) {
        if (!validate_mapped_range(reinterpret_cast<const void*>(object_slot), 1U, true)) {
            return false;
        }
        object_argument = object_slot;
        has_object_argument = true;
        return true;
    }

    if (!validate_mapped_range(reinterpret_cast<const void*>(object_slot),
                               sizeof(object_argument), false) ||
        read_guest_memory(reinterpret_cast<const void*>(object_slot), &object_argument,
                          sizeof(object_argument)).status != GuestMemoryAccessStatus::Success ||
        object_argument == 0U ||
        !validate_mapped_range(reinterpret_cast<const void*>(
                                   static_cast<std::uintptr_t>(object_argument)),
                               1U, true)) {
        return false;
    }
    has_object_argument = true;
    return true;
}

[[nodiscard]] bool validate_ip_map(const ImageReader& image, const FuncInfo& info) noexcept {
    std::uint32_t previous_ip = 0U;
    for (std::uint32_t index = 0; index < info.ip_map_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t state_entry{};
        std::uint32_t ip{};
        std::int32_t state{};
        if (!image.add_rva(info.ip_map,
                           static_cast<std::size_t>(index) * kIpMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, state_entry) || !image.read_u32(entry, ip) ||
            !image.read_i32(state_entry, state) ||
            !image.range(ip, 1U) || !valid_state(state, info) ||
            (index != 0U && ip < previous_ip)) {
            return false;
        }
        previous_ip = ip;
    }
    return true;
}

struct CatchTarget {
    std::uint32_t handler_rva{};
    std::uint32_t scope_index{};
    std::uint32_t continuation_rva{};
    std::uint32_t catch_object{};
    std::uint32_t handler_adjectives{};
    CatchableType catchable{};
    bool has_catchable{};
    bool has_catch_object{};
    bool catch_all{};
};

void trace_cxx_eh_catch(const CatchTarget& target, const std::uint64_t source_pointer,
                        const std::uint64_t adjusted_pointer,
                        const std::uintptr_t establisher) noexcept {
    try {
        std::uint64_t source_first{};
        std::uint64_t adjusted_first{};
        std::uint64_t catch_slot{};
        static_cast<void>(read_guest_memory(reinterpret_cast<const void*>(
                                                 static_cast<std::uintptr_t>(source_pointer)),
                                             &source_first, sizeof(source_first)));
        static_cast<void>(read_guest_memory(reinterpret_cast<const void*>(
                                                 static_cast<std::uintptr_t>(adjusted_pointer)),
                                             &adjusted_first, sizeof(adjusted_first)));
        static_cast<void>(read_guest_memory(reinterpret_cast<const void*>(
                                                 establisher + target.catch_object),
                                             &catch_slot, sizeof(catch_slot)));
        const std::array fields{
            diagnostics::TraceField{"state", "matched"},
            diagnostics::TraceField{"detail", "fh4-catch-object"},
            diagnostics::TraceField{"mechanism", "msvc-x64"},
            diagnostics::TraceField{"source", std::to_string(source_pointer)},
            diagnostics::TraceField{"source-first", std::to_string(source_first)},
            diagnostics::TraceField{"adjusted", std::to_string(adjusted_pointer)},
            diagnostics::TraceField{"adjusted-first", std::to_string(adjusted_first)},
            diagnostics::TraceField{"catch-slot", std::to_string(catch_slot)},
            diagnostics::TraceField{"establisher", std::to_string(establisher)},
            diagnostics::TraceField{"catch-offset", std::to_string(target.catch_object)},
            diagnostics::TraceField{"handler-rva", std::to_string(target.handler_rva)},
            diagnostics::TraceField{"continuation-rva", std::to_string(target.continuation_rva)},
            diagnostics::TraceField{"adjectives", std::to_string(target.handler_adjectives)},
            diagnostics::TraceField{"catchable-properties", std::to_string(target.catchable.properties)},
            diagnostics::TraceField{"catchable-type-rva", std::to_string(target.catchable.type_rva)},
            diagnostics::TraceField{"catchable-mdisp", std::to_string(target.catchable.member_displacement)},
        };
        diagnostics::write_trace(
            std::cerr, diagnostics::TraceComponent::Runtime, diagnostics::TraceLevel::Debug,
            "cxx-eh", fields);
    } catch (...) {
    }
}

[[nodiscard]] bool read_catchable_type(const ImageReader& image,
                                       const std::uint32_t catchable_type_rva,
                                       CatchableType& type) noexcept {
    type = {};
    if (!image.range(catchable_type_rva, kCatchableTypeSize)) {
        return false;
    }
    std::uint32_t entry{};
    if (!image.read_u32(catchable_type_rva, type.properties) ||
        !image.add_rva(catchable_type_rva, 4U, entry) ||
        !image.read_u32(entry, type.type_rva) ||
        !image.add_rva(catchable_type_rva, 8U, entry) ||
        !image.read_i32(entry, type.member_displacement) ||
        !image.add_rva(catchable_type_rva, 12U, entry) ||
        !image.read_i32(entry, type.vbptr_displacement) ||
        !image.add_rva(catchable_type_rva, 16U, entry) ||
        !image.read_i32(entry, type.vbase_displacement) ||
        !image.add_rva(catchable_type_rva, 20U, entry) ||
        !image.read_u32(entry, type.size_or_offset) ||
        !image.add_rva(catchable_type_rva, 24U, entry) ||
        !image.read_u32(entry, type.copy_function) ||
        !image.range(type.type_rva, kTypeDescriptorNameOffset + 1U) ||
        (type.copy_function != 0U && !image.range(type.copy_function, 1U))) {
        return false;
    }
    return type.size_or_offset <= std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] bool add_signed_address(const std::uintptr_t base,
                                      const std::int32_t displacement,
                                      std::uintptr_t& result) noexcept {
    if (displacement >= 0) {
        const auto delta = static_cast<std::uintptr_t>(displacement);
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) {
            return false;
        }
        result = base + delta;
        return true;
    }
    const auto magnitude = static_cast<std::uintptr_t>(-
        static_cast<std::int64_t>(displacement));
    if (base < magnitude) {
        return false;
    }
    result = base - magnitude;
    return true;
}

[[nodiscard]] bool adjust_catchable_pointer(const CatchableType& type,
                                            const std::uint64_t source_pointer,
                                            std::uint64_t& adjusted_pointer) noexcept {
    if (source_pointer == 0U) {
        return false;
    }
    std::uintptr_t adjusted{};
    if (!add_signed_address(static_cast<std::uintptr_t>(source_pointer),
                            type.member_displacement, adjusted)) {
        return false;
    }
    if (type.vbptr_displacement != -1) {
        std::uintptr_t vbptr_address{};
        if (!add_signed_address(adjusted, type.vbptr_displacement, vbptr_address)) {
            return false;
        }
        std::uint64_t vbtable_pointer{};
        if (read_guest_memory(reinterpret_cast<const void*>(vbptr_address), &vbtable_pointer,
                              sizeof(vbtable_pointer)).status !=
                GuestMemoryAccessStatus::Success ||
            vbtable_pointer == 0U) {
            return false;
        }
        std::uintptr_t vbase_address{};
        if (!add_signed_address(static_cast<std::uintptr_t>(vbtable_pointer),
                                type.vbase_displacement, vbase_address)) {
            return false;
        }
        std::int32_t vbase_offset{};
        if (read_guest_memory(reinterpret_cast<const void*>(vbase_address), &vbase_offset,
                              sizeof(vbase_offset)).status !=
            GuestMemoryAccessStatus::Success ||
            !add_signed_address(adjusted, vbase_offset, adjusted)) {
            return false;
        }
    }
    std::byte probe{};
    if (read_guest_memory(reinterpret_cast<const void*>(adjusted), &probe, sizeof(probe)).status !=
        GuestMemoryAccessStatus::Success) {
        return false;
    }
    adjusted_pointer = static_cast<std::uint64_t>(adjusted);
    return true;
}

[[nodiscard]] bool find_thrown_catchable(const ImageReader& image,
                                         const ExceptionRecordAmd64& record,
                                         const std::uint32_t handler_type_rva,
                                         CatchableType& match) noexcept {
    if (record.parameter_count < 3U) {
        return false;
    }
    std::uint32_t throw_info_rva{};
    if (!image.rva_of(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                          record.parameters[2])), throw_info_rva) ||
        !image.range(throw_info_rva, kThrowInfoSize)) {
        return false;
    }

    std::uint32_t catchable_array_rva{};
    if (!image.add_rva(throw_info_rva, 12U, catchable_array_rva) ||
        !image.read_u32(catchable_array_rva, catchable_array_rva) ||
        !image.range(catchable_array_rva, sizeof(std::uint32_t))) {
        return false;
    }
    std::uint32_t catchable_count{};
    std::uint32_t entries_rva{};
    if (!image.read_u32(catchable_array_rva, catchable_count) ||
        catchable_count > kMaxCatchableTypes ||
        !image.add_rva(catchable_array_rva, sizeof(std::uint32_t), entries_rva) ||
        !image.range_for_count(entries_rva, catchable_count, sizeof(std::uint32_t))) {
        return false;
    }

    for (std::uint32_t index = 0; index < catchable_count; ++index) {
        std::uint32_t entry_rva{};
        std::uint32_t catchable_type_rva{};
        CatchableType candidate{};
        if (!image.add_rva(entries_rva, static_cast<std::size_t>(index) * sizeof(std::uint32_t),
                           entry_rva) ||
            !image.read_u32(entry_rva, catchable_type_rva) ||
            !read_catchable_type(image, catchable_type_rva, candidate)) {
            return false;
        }
        if (candidate.type_rva == handler_type_rva) {
            match = candidate;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool thrown_type_matches(const ImageReader& image,
                                       const ExceptionRecordAmd64& record,
                                       const std::uint32_t handler_type_rva) noexcept {
    CatchableType ignored{};
    return find_thrown_catchable(image, record, handler_type_rva, ignored);
}

[[nodiscard]] bool find_catch(const ImageReader& image, const FuncInfo& info,
                              const ExceptionRecordAmd64& record, const std::int32_t state,
                              CatchTarget& target) noexcept {
    for (std::uint32_t index = 0; index < info.try_block_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t try_high_entry{};
        std::uint32_t catch_high_entry{};
        std::uint32_t handler_count_entry{};
        std::uint32_t handler_map_entry{};
        std::int32_t try_low{};
        std::int32_t try_high{};
        std::int32_t catch_high{};
        std::uint32_t handler_count{};
        std::uint32_t handler_map{};
        if (!image.add_rva(info.try_block_map,
                           static_cast<std::size_t>(index) * kTryBlockEntrySize, entry) ||
            !image.add_rva(entry, 4U, try_high_entry) ||
            !image.add_rva(entry, 8U, catch_high_entry) ||
            !image.add_rva(entry, 12U, handler_count_entry) ||
            !image.add_rva(entry, 16U, handler_map_entry) ||
            !image.read_i32(entry, try_low) || !image.read_i32(try_high_entry, try_high) ||
            !image.read_i32(catch_high_entry, catch_high) ||
            !image.read_u32(handler_count_entry, handler_count) ||
            !image.read_u32(handler_map_entry, handler_map) ||
            !valid_state(try_low, info) || !valid_state(try_high, info) ||
            !valid_state(catch_high, info) || try_low > try_high ||
            handler_count > kMaxHandlersPerTry ||
            !image.range_for_count(handler_map, handler_count, kHandlerMapEntrySize)) {
            return false;
        }
        if (state < try_low || state > catch_high) {
            continue;
        }
        for (std::uint32_t handler_index = 0; handler_index < handler_count;
             ++handler_index) {
            std::uint32_t handler{};
            std::uint32_t type_entry{};
            std::uint32_t handler_rva_entry{};
            std::uint32_t adjectives{};
            std::uint32_t type_rva{};
            std::uint32_t handler_rva{};
            if (!image.add_rva(handler_map,
                               static_cast<std::size_t>(handler_index) * kHandlerMapEntrySize,
                               handler) ||
                !image.add_rva(handler, 4U, type_entry) ||
                !image.add_rva(handler, 12U, handler_rva_entry) ||
                !image.read_u32(handler, adjectives) || !image.read_u32(type_entry, type_rva) ||
                !image.read_u32(handler_rva_entry, handler_rva) ||
                !image.range(handler_rva, 1U)) {
                return false;
            }
            if ((adjectives & kCatchAll) != 0U) {
                if (type_rva != 0U) {
                    return false;
                }
                target = {.handler_rva = handler_rva, .scope_index = index, .catch_all = true};
                return true;
            }
            if (type_rva != 0U &&
                image.range(type_rva, kTypeDescriptorNameOffset + 1U) &&
                thrown_type_matches(image, record, type_rva)) {
                target = {.handler_rva = handler_rva, .scope_index = index, .catch_all = false};
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool find_fh4_catch(const ImageReader& image, const Fh4FuncInfo& info,
                                  const Fh4TryBlockMap& try_map,
                                  const ExceptionRecordAmd64& record,
                                  const std::int32_t state,
                                  CatchTarget& target) noexcept {
    if (state < kInvalidState || state >= static_cast<std::int32_t>(kMaxFh4Entries)) {
        return false;
    }
    for (std::uint32_t try_index = 0U; try_index < try_map.count; ++try_index) {
        const Fh4TryBlock& try_block = try_map.entries[try_index];
        if (try_block.try_low > kMaxFh4Entries || try_block.try_high > kMaxFh4Entries ||
            try_block.catch_high > kMaxFh4Entries ||
            (state >= 0 && (static_cast<std::uint32_t>(state) < try_block.try_low ||
                            static_cast<std::uint32_t>(state) > try_block.catch_high))) {
            continue;
        }
        if (try_block.handler_map == 0U) {
            continue;
        }
        std::uint32_t cursor = try_block.handler_map;
        std::uint32_t handler_count{};
        if (!read_fh4_unsigned(image, cursor, handler_count) ||
            handler_count > kMaxFh4Handlers) {
            return false;
        }
        std::array<Fh4Handler, kMaxFh4Handlers> handlers{};
        for (std::uint32_t handler_index = 0U; handler_index < handler_count;
             ++handler_index) {
            if (!read_fh4_handler(image, cursor, info.function_begin,
                                  handlers[handler_index])) {
                return false;
            }
        }
        // A typed clause precedes an ellipsis in normal MSVC output, but scan
        // in two passes so malformed ordering cannot make a catch-all shadow a
        // valid exact type.
        for (std::uint32_t pass = 0U; pass < 2U; ++pass) {
            for (std::uint32_t handler_index = 0U; handler_index < handler_count;
                 ++handler_index) {
                const Fh4Handler& handler = handlers[handler_index];
                const bool catch_all = !handler.has_type;
                if ((pass == 0U && catch_all) || (pass == 1U && !catch_all)) {
                    continue;
                }
                CatchableType catchable{};
                if (!catch_all &&
                    !find_thrown_catchable(image, record, handler.type_rva, catchable)) {
                    continue;
                }
                target = {.handler_rva = handler.handler_rva,
                          .scope_index = try_index,
                          .continuation_rva = handler.continuation_count == 0U
                                                  ? 0U
                                                  : handler.continuation_rvas[0],
                          .catch_object = handler.catch_object,
                          .handler_adjectives = handler.adjectives,
                          .catchable = catchable,
                          .has_catchable = !catch_all,
                          .has_catch_object = handler.has_catch_object,
                          .catch_all = catch_all};
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::int32_t find_fh4_target_state(const ImageReader& image,
                                                 const Fh4FuncInfo& info,
                                                 const Fh4TryBlockMap& try_map,
                                                 const std::uint32_t target_rva) noexcept {
    for (std::uint32_t try_index = 0U; try_index < try_map.count; ++try_index) {
        const Fh4TryBlock& try_block = try_map.entries[try_index];
        if (try_block.handler_map == 0U) {
            continue;
        }
        std::uint32_t cursor = try_block.handler_map;
        std::uint32_t handler_count{};
        if (!read_fh4_unsigned(image, cursor, handler_count) ||
            handler_count > kMaxFh4Handlers) {
            continue;
        }
        for (std::uint32_t handler_index = 0U; handler_index < handler_count;
             ++handler_index) {
            Fh4Handler handler{};
            if (!read_fh4_handler(image, cursor, info.function_begin, handler)) {
                break;
            }
            if (handler.handler_rva == target_rva) {
                return static_cast<std::int32_t>(try_block.try_low);
            }
        }
    }
    return kInvalidState;
}

[[nodiscard]] bool current_state(const ImageReader& image, const FuncInfo& info,
                                 const std::uint32_t control_rva,
                                 std::int32_t& state) noexcept {
    state = kInvalidState;
    bool found = false;
    for (std::uint32_t index = 0; index < info.ip_map_count; ++index) {
        std::uint32_t entry{};
        std::uint32_t state_entry{};
        std::uint32_t ip{};
        std::int32_t candidate{};
        if (!image.add_rva(info.ip_map,
                           static_cast<std::size_t>(index) * kIpMapEntrySize, entry) ||
            !image.add_rva(entry, 4U, state_entry) || !image.read_u32(entry, ip) ||
            !image.read_i32(state_entry, candidate)) {
            return false;
        }
        if (ip > control_rva) {
            break;
        }
        state = candidate;
        found = true;
    }
    return !found || valid_state(state, info);
}

}  // namespace

bool is_fh4_cxx_handler_data(void* const handler_data) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva)) {
        return false;
    }
    FuncInfo v3_info{};
    if (read_func_info(image, func_info_rva, v3_info) &&
        validate_unwind_map(image, v3_info) && validate_ip_map(image, v3_info)) {
        return false;
    }

    // MSVC FH4 protects its compressed FuncInfo with __GSHandlerCheck_EH4.
    // The first DWORD is still an image-relative pointer to the FuncInfo;
    // the following DWORD is the GS/unwind flag word.
    std::uint32_t gs_unwind_info{};
    Fh4FuncInfo fh4_info{};
    if (handler_data_rva > std::numeric_limits<std::uint32_t>::max() - sizeof(std::uint32_t) ||
        !image.read_u32(handler_data_rva + sizeof(std::uint32_t), gs_unwind_info) ||
        (gs_unwind_info & 0x03U) == 0U ||
        !read_fh4_func_info(image, func_info_rva, 0U, fh4_info)) {
        return false;
    }
    return true;
}

bool is_supported_cxx_handler_data(void* const handler_data) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva)) {
        return false;
    }
    FuncInfo info{};
    if (read_func_info(image, func_info_rva, info) &&
        validate_unwind_map(image, info) && validate_ip_map(image, info)) {
        return true;
    }
    return is_fh4_cxx_handler_data(handler_data);
}

std::int32_t cxx_frame_handler4(
    ExceptionRecordAmd64* const exception_record, void* const establisher_frame,
    ContextAmd64* const context_record,
    DispatcherContextAmd64* const dispatcher_context) noexcept {
    (void)establisher_frame;
    (void)context_record;
    if (exception_record == nullptr || dispatcher_context == nullptr ||
        exception_record->code != kCxxException) {
        return kExceptionContinueSearch;
    }

    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    const auto handler_data = static_cast<const std::byte*>(dispatcher_context->handler_data);
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    std::uint32_t gs_unwind_info{};
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva) ||
        !image.add_rva(handler_data_rva, sizeof(std::uint32_t), handler_data_rva) ||
        !image.read_u32(handler_data_rva, gs_unwind_info)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-handler-data");
        return kExceptionContinueSearch;
    }

    const bool unwinding = (exception_record->flags & kExceptionUnwinding) != 0U;
    const std::uint32_t required_gs_flag = unwinding ? 0x02U : 0x01U;
    if ((gs_unwind_info & required_gs_flag) == 0U) {
        return kExceptionContinueSearch;
    }

    std::uint32_t function_entry_rva{};
    std::uint32_t function_begin{};
    if (!image.rva_of(dispatcher_context->function_entry, function_entry_rva) ||
        !image.read_u32(function_entry_rva, function_begin)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-function-entry");
        return kExceptionContinueSearch;
    }

    Fh4FuncInfo info{};
    if (!read_fh4_func_info(image, func_info_rva, function_begin, info)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-func-info");
        return kExceptionContinueSearch;
    }
    Fh4UnwindMap unwind_map{};
    if (info.unwind_map != 0U && !read_fh4_unwind_map(image, info.unwind_map, unwind_map)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-unwind-map");
        return kExceptionContinueSearch;
    }
    Fh4TryBlockMap try_map{};
    if (info.try_block_map != 0U &&
        !read_fh4_try_block_map(image, info.try_block_map, try_map)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-try-map");
        return kExceptionContinueSearch;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (dispatcher_context->control_pc < base ||
        dispatcher_context->control_pc - base > std::numeric_limits<std::uint32_t>::max()) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-control-pc");
        return kExceptionContinueSearch;
    }
    const std::uint32_t control_rva =
        static_cast<std::uint32_t>(dispatcher_context->control_pc - base);
    std::int32_t state{};
    if (!read_fh4_ip_state(image, info, control_rva, state) ||
        (state >= 0 && unwind_map.count != 0U &&
         state >= static_cast<std::int32_t>(unwind_map.count))) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-fh4-ip-state");
        return kExceptionContinueSearch;
    }

    if (unwinding) {
        // O wrapper guest __GSHandlerCheck_EH4 recebe objetos DispatcherContext
        // do host durante o unwind; por isso o mapa comprimido é interpretado
        // aqui e somente o funclet validado atravessa a fronteira ABI.
        if (dispatcher_context->target_ip == 0U) {
            return kExceptionContinueSearch;
        }
        const std::uint32_t target_rva =
            dispatcher_context->target_ip > base
                ? static_cast<std::uint32_t>(dispatcher_context->target_ip - base)
                : 0U;
        const std::int32_t target_state =
            target_rva != 0U ? find_fh4_target_state(image, info, try_map, target_rva)
                             : kInvalidState;
        CleanupPlan plan{};
        if (!build_fh4_cleanup_plan(unwind_map, state, target_state, plan) || plan.count == 0U) {
            trace_cxx_eh_state(diagnostics::TraceLevel::Info, "no-supported-fh4-cleanup",
                               control_rva, state);
            return kExceptionContinueSearch;
        }
        if (plan.count > kMaxFh4ExecutableCleanupActions) {
            trace_cxx_eh_state(diagnostics::TraceLevel::Info, "fh4-cleanup-limit",
                               control_rva, state);
            return kExceptionContinueSearch;
        }
        if (base > std::numeric_limits<std::uintptr_t>::max() - plan.actions[0].action_rva) {
            trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                         "invalid-fh4-cleanup-target");
            return kExceptionContinueSearch;
        }
        dispatcher_context->target_ip = base + plan.actions[0].action_rva;
        g_cxx_cleanup_actions = plan.actions;
        g_cxx_cleanup_count = plan.count;
        g_cxx_cleanup_index = 0U;
        g_cxx_cleanup_target = dispatcher_context->target_ip;
        trace_cxx_eh_state(diagnostics::TraceLevel::Info, "fh4-termination-cleanup",
                           control_rva, state);
        return kExceptionExecuteHandler;
    }

    CatchTarget target{};
    if (!find_fh4_catch(image, info, try_map, *exception_record, state, target)) {
        trace_cxx_eh_state(diagnostics::TraceLevel::Info, "no-supported-fh4-catch",
                           control_rva, state);
        return kExceptionContinueSearch;
    }
    if (target.scope_index >= try_map.count) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                     "invalid-fh4-scope-index");
        return kExceptionContinueSearch;
    }
    if (target.has_catch_object) {
        if (exception_record->parameter_count < 2U || exception_record->parameters[1] == 0U) {
            trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                         "missing-fh4-catch-object");
            return kExceptionContinueSearch;
        }
        if (!target.has_catchable ||
            ((target.catchable.properties & kCatchableTypeByReferenceOnly) != 0U &&
             (target.handler_adjectives & kHandlerTypeReference) == 0U)) {
            trace_cxx_eh(diagnostics::TraceLevel::Info, "search",
                         "unsupported-fh4-catch-conversion");
            return kExceptionContinueSearch;
        }
        const auto establisher = reinterpret_cast<std::uintptr_t>(establisher_frame);
        if (target.catch_object > std::numeric_limits<std::uintptr_t>::max() - establisher) {
            trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                         "invalid-fh4-catch-object-offset");
            return kExceptionContinueSearch;
        }
        const std::uint64_t source_pointer = exception_record->parameters[1];
        std::uint64_t adjusted_pointer{};
        if (!adjust_catchable_pointer(target.catchable, source_pointer, adjusted_pointer)) {
            trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                         "invalid-fh4-catch-object");
            return kExceptionContinueSearch;
        }
        if ((target.handler_adjectives & kHandlerTypeReference) != 0U) {
            if (!write_guest_stack_value(establisher + target.catch_object, adjusted_pointer)) {
                trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                             "invalid-fh4-catch-object");
                return kExceptionContinueSearch;
            }
        } else {
            if (target.catchable.copy_function != 0U ||
                (target.catchable.properties & kCatchableTypeSimple) == 0U ||
                target.catchable.size_or_offset == 0U ||
                target.catchable.size_or_offset > kMaxCatchObjectSize) {
                trace_cxx_eh(diagnostics::TraceLevel::Info, "search",
                             "unsupported-fh4-catch-copy");
                return kExceptionContinueSearch;
            }
            std::array<std::byte, kMaxCatchObjectSize> object{};
            const std::size_t object_size = target.catchable.size_or_offset;
            if (read_guest_memory(reinterpret_cast<const void*>(
                                      static_cast<std::uintptr_t>(adjusted_pointer)),
                                  object.data(), object_size).status !=
                    GuestMemoryAccessStatus::Success ||
                write_guest_memory(reinterpret_cast<void*>(establisher + target.catch_object),
                                   object.data(), object_size).status !=
                    GuestMemoryAccessStatus::Success) {
                trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected",
                             "invalid-fh4-catch-object");
                return kExceptionContinueSearch;
            }
        }
        trace_cxx_eh_catch(target, source_pointer, adjusted_pointer, establisher);
    }
    dispatcher_context->scope_index = target.scope_index;
    dispatcher_context->target_ip = base + target.handler_rva;
    g_cxx_catch_target = dispatcher_context->target_ip;
    g_cxx_catch_continuation_ready = target.continuation_rva != 0U;
    g_cxx_catch_continuation = g_cxx_catch_continuation_ready
                                   ? base + target.continuation_rva
                                   : 0U;
    trace_cxx_eh(diagnostics::TraceLevel::Info, "matched",
                 target.catch_all ? "fh4-catch-all" : "fh4-catch-typed");
    return kExceptionExecuteHandler;
}

std::int32_t cxx_frame_handler3(
    ExceptionRecordAmd64* const exception_record, void* const establisher_frame,
    ContextAmd64* const context_record,
    DispatcherContextAmd64* const dispatcher_context) noexcept {
    (void)establisher_frame;
    // Estes três objetos são criados pelo despachante em memória do runtime e
    // ficam vivos durante a chamada do handler convidado. Eles não são
    // ponteiros arbitrários recebidos de uma API; callbacks convidados podem
    // acessá-los diretamente pelo contrato ABI, enquanto dados externos são
    // sempre copiados antes de chegar aqui.
    if (exception_record == nullptr || context_record == nullptr || dispatcher_context == nullptr) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-dispatcher-data");
        return kExceptionContinueSearch;
    }
    if (exception_record->code != kCxxException) {
        return kExceptionContinueSearch;
    }
    const bool unwinding = (exception_record->flags & kExceptionUnwinding) != 0U;

    const GuestUnwindView view = current_guest_unwind_view();
    ImageReader image{view};
    std::uint32_t handler_data_rva{};
    std::uint32_t func_info_rva{};
    const auto handler_data = static_cast<const std::byte*>(dispatcher_context->handler_data);
    if (!image.rva_of(handler_data, handler_data_rva) ||
        !image.read_u32(handler_data_rva, func_info_rva)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-handler-data");
        return kExceptionContinueSearch;
    }

    FuncInfo info{};
    if (!read_func_info(image, func_info_rva, info) || !validate_unwind_map(image, info) ||
        !validate_ip_map(image, info)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-func-info");
        return kExceptionContinueSearch;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (dispatcher_context->control_pc < base ||
        dispatcher_context->control_pc - base > std::numeric_limits<std::uint32_t>::max()) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-control-pc");
        return kExceptionContinueSearch;
    }
    std::int32_t state{};
    if (!current_state(image, info, static_cast<std::uint32_t>(dispatcher_context->control_pc - base),
                       state)) {
        trace_cxx_eh(diagnostics::TraceLevel::Warning, "rejected", "invalid-ip-state");
        return kExceptionContinueSearch;
    }
    if (unwinding) {
        // Para frames intermediários a rotina de unwind continua a procura
        // sem transferir para um funclet. A etapa atual executa o cleanup do
        // frame-alvo antes do catch; isso mantém a continuação em um único
        // contexto convidado validado.
        if (dispatcher_context->target_ip == 0U) {
            return kExceptionContinueSearch;
        }
        CleanupPlan plan{};
        if (!build_cleanup_plan(image, info, state, plan) || plan.count == 0U) {
            trace_cxx_eh_state(diagnostics::TraceLevel::Info, "no-supported-cleanup",
                               static_cast<std::uint32_t>(dispatcher_context->control_pc - base),
                               state);
            return kExceptionContinueSearch;
        }
        dispatcher_context->target_ip = base + plan.actions[0].action_rva;
        g_cxx_cleanup_actions = plan.actions;
        g_cxx_cleanup_count = plan.count;
        g_cxx_cleanup_index = 0U;
        g_cxx_cleanup_target = dispatcher_context->target_ip;
        trace_cxx_eh(diagnostics::TraceLevel::Info, "matched", "termination-cleanup");
        return kExceptionExecuteHandler;
    }
    CatchTarget target{};
    if (!find_catch(image, info, *exception_record, state, target)) {
        trace_cxx_eh_state(
            diagnostics::TraceLevel::Info, "no-supported-catch",
            static_cast<std::uint32_t>(dispatcher_context->control_pc - base), state);
        return kExceptionContinueSearch;
    }

    dispatcher_context->scope_index = target.scope_index;
    dispatcher_context->target_ip = base + target.handler_rva;
    g_cxx_catch_target = dispatcher_context->target_ip;
    trace_cxx_eh(diagnostics::TraceLevel::Info, "matched",
                 target.catch_all ? "catch-all" : "catch-typed");
    return kExceptionExecuteHandler;
}

bool prepare_cxx_catch_transfer(ContextAmd64& context, void* const establisher_frame,
                                void* const target_ip) noexcept {
    const std::uint64_t target = reinterpret_cast<std::uintptr_t>(target_ip);
    if (context.rsp < sizeof(std::uint64_t)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-stack");
        return false;
    }
    const std::uint64_t resume_stack = context.rsp;
    const std::uint64_t funclet_stack = resume_stack - sizeof(std::uint64_t);
    std::uint64_t original_return{};
    if (target == 0U || target != g_cxx_catch_target ||
        !save_guest_stack_value(funclet_stack, original_return) ||
        !write_guest_stack_value(funclet_stack,
                                  reinterpret_cast<std::uintptr_t>(&tl_cxx_catch_return_trampoline))) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-transfer");
        return false;
    }
    g_cxx_cleanup_return_slot_count = 0U;
    g_cxx_catch_return_slot = funclet_stack;
    g_cxx_catch_return_value = original_return;
    g_cxx_catch_return_slot_ready = true;
    g_cxx_catch_resume_context = context;
    g_cxx_catch_resume_context_ready = true;
    context.rsp = funclet_stack;
    context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    g_cxx_catch_context = context;
    g_cxx_catch_context_ready = true;
    g_cxx_catch_return_target = g_cxx_catch_continuation;
    g_cxx_catch_return_target_ready = g_cxx_catch_continuation_ready;
    g_cxx_catch_return_preserve_stack = g_cxx_catch_return_target_ready;
    g_cxx_catch_resume_stack = resume_stack;
    trace_cxx_eh_transfer(diagnostics::TraceLevel::Debug, "prepare-catch",
                          resume_stack, funclet_stack, target,
                          g_cxx_catch_return_target_ready ? g_cxx_catch_return_target : 0U);
    g_cxx_catch_continuation = 0U;
    g_cxx_catch_continuation_ready = false;
    g_cxx_funclet_active = true;
    g_cxx_cleanup_context_ready = false;
    g_cxx_catch_target = 0U;
    return true;
}

bool prepare_cxx_cleanup_transfer(ContextAmd64& action_context,
                                  const ContextAmd64& catch_context,
                                  void* const establisher_frame,
                                  void* const cleanup_ip,
                                  void* const catch_ip) noexcept {
    const std::uint64_t cleanup_target = reinterpret_cast<std::uintptr_t>(cleanup_ip);
    const std::uint64_t catch_target = reinterpret_cast<std::uintptr_t>(catch_ip);
    std::uint64_t object_argument{};
    bool has_object_argument = false;
    const std::uint64_t catch_return_slot = catch_context.rsp - sizeof(std::uint64_t);
    const std::uint64_t cleanup_stack =
        catch_context.rsp >= kCxxCleanupStackOffset + sizeof(std::uint64_t)
            ? (((catch_context.rsp - kCxxCleanupStackOffset - sizeof(std::uint64_t)) &
                ~std::uint64_t{0x0FU}) + sizeof(std::uint64_t))
            : 0U;
    std::uint64_t original_catch_return{};
    if (cleanup_target == 0U || cleanup_target != g_cxx_cleanup_target ||
        catch_target == 0U || catch_target != g_cxx_catch_target ||
        g_cxx_cleanup_count == 0U || g_cxx_cleanup_index >= g_cxx_cleanup_count ||
        !resolve_cleanup_object(g_cxx_cleanup_actions[g_cxx_cleanup_index],
                                reinterpret_cast<std::uintptr_t>(establisher_frame),
                                object_argument, has_object_argument) ||
        catch_context.rsp < sizeof(std::uint64_t) ||
        cleanup_stack == 0U ||
        !validate_guest_stack_range(reinterpret_cast<void*>(cleanup_stack),
                                    sizeof(std::uint64_t), true) ||
        !validate_guest_stack_range(reinterpret_cast<void*>(catch_return_slot),
                                    sizeof(std::uint64_t), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-cleanup-transfer");
        return false;
    }

    g_cxx_cleanup_return_slot_count = 0U;
    g_cxx_catch_return_slot_ready = false;
    if (!save_cleanup_return_slot(cleanup_stack) ||
        !save_guest_stack_value(catch_return_slot, original_catch_return) ||
        !write_guest_stack_value(cleanup_stack,
                                 reinterpret_cast<std::uintptr_t>(&tl_cxx_cleanup_return_trampoline)) ||
        !write_guest_stack_value(catch_return_slot,
                                 reinterpret_cast<std::uintptr_t>(&tl_cxx_catch_return_trampoline))) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "guest-stack-write-failed");
        return false;
    }
    g_cxx_catch_return_slot = catch_return_slot;
    g_cxx_catch_return_value = original_catch_return;
    g_cxx_catch_return_slot_ready = true;

    // A synthetic return slot must enter the guest funclet with RSP % 16 == 8,
    // exactly as after a normal MS x64 call. The cleanup stack is kept below
    // the original frame so prologues cannot overwrite its saved return data.
    action_context.rsp = cleanup_stack;
    action_context.rip = cleanup_target;
    action_context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    if (has_object_argument) {
        action_context.rcx = object_argument;
    }

    g_cxx_catch_resume_context = catch_context;
    g_cxx_catch_resume_context_ready = true;
    g_cxx_catch_context = catch_context;
    const std::uint64_t catch_resume_stack = catch_context.rsp;
    g_cxx_catch_context.rsp = catch_resume_stack - sizeof(std::uint64_t);
    g_cxx_catch_context.rip = catch_target;
    g_cxx_catch_context.rdx = reinterpret_cast<std::uintptr_t>(establisher_frame);
    g_cxx_catch_context_ready = true;
    g_cxx_catch_return_target = g_cxx_catch_continuation;
    g_cxx_catch_return_target_ready = g_cxx_catch_continuation_ready;
    g_cxx_catch_return_preserve_stack = g_cxx_catch_return_target_ready;
    g_cxx_catch_resume_stack = catch_resume_stack;
    g_cxx_cleanup_context_ready = true;
    g_cxx_funclet_active = true;
    g_cxx_cleanup_establisher = reinterpret_cast<std::uintptr_t>(establisher_frame);
    g_cxx_cleanup_target = 0U;
    return true;
}

extern "C" [[noreturn]] void tl_cxx_cleanup_return_from_asm(
    const std::uint64_t stack_pointer) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    if (view.image_base == nullptr ||
        !validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer), 1U, false) ||
        !g_cxx_cleanup_context_ready || !g_cxx_catch_context_ready ||
        !g_cxx_catch_resume_context_ready ||
        g_cxx_cleanup_count == 0U || g_cxx_cleanup_index >= g_cxx_cleanup_count ||
        g_cxx_catch_context.rip < base ||
        g_cxx_catch_context.rip - base >= view.image_size) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-cleanupret-target");
        tl_ExitThread(kCxxException);
        std::abort();
    }

    if (!restore_cleanup_return_slot(g_cxx_cleanup_index, stack_pointer)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-cleanup-return-slot");
        tl_ExitThread(kCxxException);
        std::abort();
    }

    if (g_cxx_cleanup_index + 1U < g_cxx_cleanup_count) {
        const std::size_t next_index = g_cxx_cleanup_index + 1U;
        const CleanupAction& next_action = g_cxx_cleanup_actions[next_index];
        const std::uint32_t next_rva = next_action.action_rva;
        std::uint64_t object_argument{};
        bool has_object_argument = false;
        if (next_rva >= view.image_size ||
            base > std::numeric_limits<std::uintptr_t>::max() - next_rva ||
            !resolve_cleanup_object(next_action, g_cxx_cleanup_establisher,
                                    object_argument, has_object_argument)) {
            trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected",
                         "invalid-chained-cleanup-target");
            tl_ExitThread(kCxxException);
            std::abort();
        }
        ContextAmd64 next_context = g_cxx_catch_context;
        next_context.rip = base + next_rva;
        // O cleanup anterior chegou ao trampoline depois de consumir sua
        // palavra de retorno. Cada novo funclet deve, porém, entrar como uma
        // chamada Win64 normal: RSP+8 fica alinhado a 16 bytes no prólogo.
        // Quando o retorno anterior deixou RSP em 0 mod 16, salte uma palavra
        // de pilha antes de instalar o próximo trampoline.
        std::uint64_t next_stack_pointer = stack_pointer;
        if ((next_stack_pointer & 0x0FU) == 0U) {
            if (next_stack_pointer > std::numeric_limits<std::uint64_t>::max() -
                                         sizeof(std::uint64_t)) {
                trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected",
                             "invalid-chained-cleanup-stack");
                tl_ExitThread(kCxxException);
                std::abort();
            }
            next_stack_pointer += sizeof(std::uint64_t);
        }
        next_context.rsp = next_stack_pointer;
        if (!validate_guest_stack_range(reinterpret_cast<void*>(next_context.rsp),
                                        sizeof(std::uint64_t), true)) {
            trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected",
                         "invalid-chained-cleanup-stack");
            tl_ExitThread(kCxxException);
            std::abort();
        }
        next_context.rdx = g_cxx_cleanup_establisher;
        if (has_object_argument) {
            next_context.rcx = object_argument;
        }
        if (g_cxx_cleanup_return_slot_count != next_index ||
            !save_cleanup_return_slot(next_context.rsp) ||
            !write_guest_stack_value(next_context.rsp,
                                     reinterpret_cast<std::uintptr_t>(&tl_cxx_cleanup_return_trampoline))) {
            trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "guest-stack-write-failed");
            tl_ExitThread(kCxxException);
            std::abort();
        }
        g_cxx_cleanup_index = next_index;
        g_cxx_cleanup_target = next_context.rip;
        g_cxx_cleanup_context_ready = true;
        tl_restore_guest_context_and_jump(&next_context);
    }

    g_cxx_cleanup_context_ready = false;
    g_cxx_funclet_active = false;
    if (!g_cxx_catch_return_slot_ready ||
        g_cxx_catch_context.rsp != g_cxx_catch_return_slot ||
        !validate_guest_stack_range(reinterpret_cast<void*>(g_cxx_catch_return_slot),
                                    sizeof(std::uint64_t), true)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-return-slot");
        tl_ExitThread(kCxxException);
        std::abort();
    }
    g_cxx_cleanup_target = 0U;
    g_cxx_cleanup_establisher = 0U;
    g_cxx_cleanup_actions.fill(CleanupAction{});
    g_cxx_cleanup_count = 0U;
    g_cxx_cleanup_index = 0U;
    g_cxx_cleanup_return_slots.fill(0U);
    g_cxx_cleanup_return_values.fill(0U);
    g_cxx_cleanup_return_slot_count = 0U;
    tl_restore_guest_context_and_jump(&g_cxx_catch_context);
}

extern "C" [[noreturn]] void tl_cxx_catch_return_from_asm(
    const std::uint64_t target_ip, const std::uint64_t stack_pointer) noexcept {
    const GuestUnwindView view = current_guest_unwind_view();
    const auto base = reinterpret_cast<std::uintptr_t>(view.image_base);
    std::uint64_t effective_target = target_ip;
    if (effective_target == 0U && g_cxx_catch_return_target_ready) {
        effective_target = g_cxx_catch_return_target;
    }
    if (view.image_base == nullptr || effective_target < base ||
        effective_target - base >= view.image_size ||
        !validate_guest_stack_range(reinterpret_cast<void*>(stack_pointer), 1U, false) ||
        !g_cxx_catch_context_ready || !g_cxx_catch_resume_context_ready) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catchret-target");
        tl_ExitThread(kCxxException);
        std::abort();
    }
    trace_cxx_eh_transfer(diagnostics::TraceLevel::Debug, "return-from-catch",
                          g_cxx_catch_resume_stack, stack_pointer, effective_target,
                          g_cxx_catch_return_target_ready ? g_cxx_catch_return_target : 0U);
    ContextAmd64 resume_context = g_cxx_catch_resume_context;
    resume_context.rip = effective_target;
    // Para catchret com continuação FH4, o CONTEXT salvo já representa o ponto
    // de retorno da chamada que lançou a exceção: seu RSP é o valor após o
    // retorno normal ao corpo da função. O RSP após o RET sintético aponta para
    // além desse frame e só é usado para validar a travessia do trampoline.
    if (g_cxx_catch_return_preserve_stack) {
        resume_context.rsp = g_cxx_catch_resume_stack;
    } else {
        resume_context.rsp = stack_pointer;
    }
    if (!g_cxx_catch_return_slot_ready || stack_pointer < sizeof(std::uint64_t) ||
        g_cxx_catch_return_slot != stack_pointer - sizeof(std::uint64_t) ||
        !write_guest_stack_value(g_cxx_catch_return_slot, g_cxx_catch_return_value)) {
        trace_cxx_eh(diagnostics::TraceLevel::Error, "rejected", "invalid-catch-return-slot");
        tl_ExitThread(kCxxException);
        std::abort();
    }
    g_cxx_catch_context_ready = false;
    g_cxx_catch_resume_context_ready = false;
    g_cxx_catch_return_target = 0U;
    g_cxx_catch_return_target_ready = false;
    g_cxx_catch_return_preserve_stack = false;
    g_cxx_catch_resume_stack = 0U;
    g_cxx_catch_return_slot = 0U;
    g_cxx_catch_return_value = 0U;
    g_cxx_catch_return_slot_ready = false;
    g_cxx_funclet_active = false;
    tl_restore_guest_context_and_jump(&resume_context);
}

bool cxx_eh_funclet_active() noexcept {
    return g_cxx_funclet_active;
}

}  // namespace tradutorlinux::runtime
