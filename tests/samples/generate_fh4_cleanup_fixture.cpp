#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t kPe32PlusMagic = 0x20BU;
constexpr std::uint16_t kAmd64Machine = 0x8664U;
constexpr std::size_t kCoffHeaderSize = 20U;
constexpr std::size_t kOptionalHeaderDataDirectoryOffset = 112U;
constexpr std::size_t kExceptionDirectory = 3U;
constexpr std::size_t kRuntimeFunctionSize = 12U;
constexpr std::size_t kUnwindInfoHeaderSize = 4U;
constexpr std::size_t kFh4BlobSize = 512U;

struct Section {
    std::uint32_t virtual_address{};
    std::uint32_t raw_address{};
    std::uint32_t raw_size{};
};

struct RuntimeFunction {
    std::uint32_t begin_rva{};
    std::uint32_t end_rva{};
    std::uint32_t unwind_rva{};
    std::uint32_t entry_rva{};
};

class PeImage {
public:
    explicit PeImage(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] bool parse() {
        const auto lfanew = read<std::uint32_t>(0x3CU);
        if (!lfanew || !in_bounds(*lfanew, 24U) ||
            read<std::uint32_t>(*lfanew) != 0x00004550U ||
            read<std::uint16_t>(*lfanew + 4U) != kAmd64Machine) {
            return false;
        }
        const auto section_count = read<std::uint16_t>(*lfanew + 6U);
        const auto optional_size = read<std::uint16_t>(*lfanew + 20U);
        if (!section_count || !optional_size) {
            return false;
        }
        optional_offset_ = static_cast<std::size_t>(*lfanew) + 24U;
        if (!in_bounds(optional_offset_, *optional_size) ||
            read<std::uint16_t>(optional_offset_) != kPe32PlusMagic ||
            *optional_size < kOptionalHeaderDataDirectoryOffset + 16U * 8U) {
            return false;
        }
        const auto directory_count = read<std::uint32_t>(optional_offset_ + 108U);
        if (!directory_count || *directory_count <= kExceptionDirectory) {
            return false;
        }
        const std::size_t section_offset = optional_offset_ + *optional_size;
        if (!in_bounds(section_offset, static_cast<std::size_t>(*section_count) * 40U)) {
            return false;
        }
        for (std::size_t index = 0U; index < *section_count; ++index) {
            const std::size_t offset = section_offset + index * 40U;
            const auto virtual_address = read<std::uint32_t>(offset + 12U);
            const auto raw_size = read<std::uint32_t>(offset + 16U);
            const auto raw_address = read<std::uint32_t>(offset + 20U);
            if (!virtual_address || !raw_size || !raw_address ||
                !in_bounds(*raw_address, *raw_size)) {
                return false;
            }
            sections_.push_back({*virtual_address, *raw_address, *raw_size});
        }
        return true;
    }

    [[nodiscard]] bool patch(const std::string_view variant) {
        if (variant != "normal" && variant != "cycle") {
            return false;
        }
        const auto entry_rva = symbol_rva("tl_entry");
        const auto object_action_rva = symbol_rva("tl_fh4_dtor_with_object");
        const auto rva_action_rva = symbol_rva("tl_fh4_dtor_rva");
        const auto catch_rva = symbol_rva("tl_fh4_catch");
        const auto blob_rva = symbol_rva("fh4_metadata_blob");
        if (!entry_rva || !object_action_rva || !rva_action_rva || !catch_rva || !blob_rva ||
            !rva_to_file(*blob_rva, kFh4BlobSize)) {
            return false;
        }

        const auto function = find_runtime_function(*entry_rva);
        const auto moved_function = function
                                        ? find_runtime_function_with_unwind(function->unwind_rva + 0x10U)
                                        : std::nullopt;
        if (!function || !moved_function) {
            return false;
        }
        const auto handler_data_field_rva = find_handler_data_field(*function);
        const std::uint32_t relocated_unwind_rva = function->unwind_rva + 0x28U;
        if (!handler_data_field_rva || !rva_to_file(*handler_data_field_rva, 8U) ||
            !rva_to_file(relocated_unwind_rva, 8U)) {
            return false;
        }

        // The seed has a second funclet whose UNWIND_INFO occupies the bytes
        // immediately after tl_entry's handler RVA. Move that funclet to the
        // reserved tail so those bytes can hold the inline handler data required
        // by the PE UNWIND_INFO contract.
        if (!write_u32(moved_function->entry_rva + 8U, relocated_unwind_rva) ||
            !write_u32(relocated_unwind_rva, 1U) ||
            !write_u32(relocated_unwind_rva + 4U, 0U)) {
            return false;
        }

        // FuncInfo4: header, UnwindMap, TryBlockMap and IP-to-state map.
        if (!write_u8(*blob_rva, 0x18U) ||
            !write_u32(*blob_rva + 1U, *blob_rva + 0x20U) ||
            !write_u32(*blob_rva + 5U, *blob_rva + 0x40U) ||
            !write_u32(*blob_rva + 9U, *blob_rva + 0x50U)) {
            return false;
        }

        // State 0 is the sentinel transition. State 2 invokes the object
        // destructor, and state 1 invokes an RVA action after state 2.
        if (!write_u8(*blob_rva + 0x20U, 6U) ||
            !write_u8(*blob_rva + 0x21U, 8U) ||
            !write_u8(*blob_rva + 0x22U, 14U) ||
            !write_u32(*blob_rva + 0x23U, *rva_action_rva) ||
            !write_u8(*blob_rva + 0x27U, 42U) ||
            !write_u32(*blob_rva + 0x28U, *object_action_rva) ||
            !write_u8(*blob_rva + 0x2CU, 16U)) {
            return false;
        }

        // The exception is raised while state 2 is active.
        if (!write_u8(*blob_rva + 0x40U, 2U) ||
            !write_u8(*blob_rva + 0x41U, 0U) ||
            !write_u8(*blob_rva + 0x42U, 4U) ||
            !write_u8(*blob_rva + 0x43U, 4U) ||
            !write_u32(*blob_rva + 0x44U, *blob_rva + 0x60U) ||
            !write_u8(*blob_rva + 0x50U, 2U) ||
            !write_u8(*blob_rva + 0x51U, 0U) ||
            !write_u8(*blob_rva + 0x52U, 6U)) {
            return false;
        }

        // One catch-all handler; its destination is a normal guest function
        // that verifies both cleanup markers and calls ExitProcess(0).
        if (!write_u8(*blob_rva + 0x60U, 2U) ||
            !write_u8(*blob_rva + 0x61U, 0U) ||
            !write_u32(*blob_rva + 0x62U, *catch_rva) ||
            !write_u32(*handler_data_field_rva, *blob_rva) ||
            !write_u32(*handler_data_field_rva + 4U, 0x03U)) {
            return false;
        }
        if (variant == "cycle" && !write_u8(*blob_rva + 0x27U, 0x02U)) {
            // O primeiro byte do estado 2 passa a codificar type=1 e
            // nextOffset=0: o estado aponta para si próprio. O parser deve
            // rejeitar a cadeia antes de qualquer transferência para guest.
            return false;
        }
        return true;
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
    [[nodiscard]] bool in_bounds(const std::size_t offset, const std::size_t length) const noexcept {
        return offset <= bytes_.size() && length <= bytes_.size() - offset;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> read(const std::size_t offset) const noexcept {
        if (!in_bounds(offset, sizeof(T))) {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, bytes_.data() + static_cast<std::ptrdiff_t>(offset), sizeof(value));
        return value;
    }

    template <typename T>
    [[nodiscard]] bool write_file(const std::size_t offset, const T value) noexcept {
        if (!in_bounds(offset, sizeof(T))) {
            return false;
        }
        std::memcpy(bytes_.data() + static_cast<std::ptrdiff_t>(offset), &value, sizeof(value));
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> rva_to_file(const std::uint32_t rva,
                                                         const std::size_t length) const noexcept {
        for (const Section& section : sections_) {
            if (rva < section.virtual_address) {
                continue;
            }
            const std::uint64_t delta = static_cast<std::uint64_t>(rva) - section.virtual_address;
            if (delta > section.raw_size || length > section.raw_size - delta) {
                continue;
            }
            const std::uint64_t file_offset = static_cast<std::uint64_t>(section.raw_address) + delta;
            if (file_offset > std::numeric_limits<std::size_t>::max() ||
                !in_bounds(static_cast<std::size_t>(file_offset), length)) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(file_offset);
        }
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> read_rva(const std::uint32_t rva) const noexcept {
        const auto file_offset = rva_to_file(rva, sizeof(T));
        return file_offset ? read<T>(*file_offset) : std::nullopt;
    }

    [[nodiscard]] bool write_u8(const std::uint32_t rva, const std::uint8_t value) noexcept {
        const auto file_offset = rva_to_file(rva, sizeof(value));
        return file_offset && write_file(*file_offset, value);
    }

    [[nodiscard]] bool write_u32(const std::uint32_t rva, const std::uint32_t value) noexcept {
        const auto file_offset = rva_to_file(rva, sizeof(value));
        return file_offset && write_file(*file_offset, value);
    }

    [[nodiscard]] std::optional<std::string> symbol_name(const std::size_t offset,
                                                         const std::size_t string_offset,
                                                         const std::size_t string_size) const {
        const auto zero = read<std::uint32_t>(offset);
        if (!zero) {
            return std::nullopt;
        }
        if (*zero != 0U) {
            std::string name;
            name.reserve(8U);
            for (std::size_t index = 0U; index < 8U; ++index) {
                const auto character = read<std::uint8_t>(offset + index);
                if (!character) {
                    return std::nullopt;
                }
                if (*character == 0U) {
                    break;
                }
                name.push_back(static_cast<char>(*character));
            }
            return name;
        }
        const auto relative = read<std::uint32_t>(offset + 4U);
        if (!relative || *relative < 4U || *relative >= string_size) {
            return std::nullopt;
        }
        const std::size_t start = string_offset + *relative;
        if (!in_bounds(start, 1U)) {
            return std::nullopt;
        }
        std::string name;
        for (std::size_t cursor = start; cursor < string_offset + string_size; ++cursor) {
            const auto character = read<std::uint8_t>(cursor);
            if (!character) {
                return std::nullopt;
            }
            if (*character == 0U) {
                return name;
            }
            name.push_back(static_cast<char>(*character));
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> symbol_rva(const std::string_view wanted) const {
        const auto lfanew = read<std::uint32_t>(0x3CU);
        if (!lfanew || !in_bounds(*lfanew, kCoffHeaderSize)) {
            return std::nullopt;
        }
        const auto table_offset = read<std::uint32_t>(*lfanew + 12U);
        const auto symbol_count = read<std::uint32_t>(*lfanew + 16U);
        if (!table_offset || !symbol_count || *symbol_count == 0U ||
            *symbol_count > std::numeric_limits<std::size_t>::max() / 18U) {
            return std::nullopt;
        }
        const std::size_t symbols_size = static_cast<std::size_t>(*symbol_count) * 18U;
        if (!in_bounds(*table_offset, symbols_size) ||
            !in_bounds(static_cast<std::size_t>(*table_offset) + symbols_size, 4U)) {
            return std::nullopt;
        }
        const std::size_t string_offset = static_cast<std::size_t>(*table_offset) + symbols_size;
        const auto string_size = read<std::uint32_t>(string_offset);
        if (!string_size || *string_size < 4U || !in_bounds(string_offset, *string_size)) {
            return std::nullopt;
        }

        for (std::uint32_t index = 0U; index < *symbol_count;) {
            const std::size_t offset = static_cast<std::size_t>(*table_offset) + index * 18U;
            const auto name = symbol_name(offset, string_offset, *string_size);
            const auto value = read<std::uint32_t>(offset + 8U);
            const auto section = read<std::int16_t>(offset + 12U);
            const auto auxiliary_count = read<std::uint8_t>(offset + 17U);
            if (!name || !value || !section || !auxiliary_count ||
                *auxiliary_count >= *symbol_count - index) {
                return std::nullopt;
            }
            if (*name == wanted && *section > 0 &&
                static_cast<std::size_t>(*section) <= sections_.size()) {
                const Section& selected = sections_[static_cast<std::size_t>(*section) - 1U];
                const std::uint64_t rva = static_cast<std::uint64_t>(selected.virtual_address) + *value;
                if (rva > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(rva);
            }
            index += 1U + *auxiliary_count;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeFunction> find_runtime_function(
        const std::uint32_t begin_rva) const noexcept {
        const auto exception_rva = read<std::uint32_t>(optional_offset_ +
                                                        kOptionalHeaderDataDirectoryOffset +
                                                        kExceptionDirectory * 8U);
        const auto exception_size = read<std::uint32_t>(optional_offset_ +
                                                         kOptionalHeaderDataDirectoryOffset +
                                                         kExceptionDirectory * 8U + 4U);
        if (!exception_rva || !exception_size || *exception_size == 0U ||
            *exception_size % kRuntimeFunctionSize != 0U ||
            !rva_to_file(*exception_rva, *exception_size)) {
            return std::nullopt;
        }
        for (std::size_t index = 0U; index < *exception_size / kRuntimeFunctionSize; ++index) {
            const std::uint64_t entry_rva64 = static_cast<std::uint64_t>(*exception_rva) +
                                               index * kRuntimeFunctionSize;
            if (entry_rva64 > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto entry_rva = static_cast<std::uint32_t>(entry_rva64);
            const auto begin = read_rva<std::uint32_t>(entry_rva);
            const auto end = read_rva<std::uint32_t>(entry_rva + 4U);
            const auto unwind = read_rva<std::uint32_t>(entry_rva + 8U);
            if (!begin || !end || !unwind || *begin >= *end || *begin != begin_rva) {
                if (begin && *begin == begin_rva) {
                    return std::nullopt;
                }
                continue;
            }
            return RuntimeFunction{*begin, *end, *unwind, entry_rva};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeFunction> find_runtime_function_with_unwind(
        const std::uint32_t unwind_rva) const noexcept {
        const auto exception_rva = read<std::uint32_t>(optional_offset_ +
                                                        kOptionalHeaderDataDirectoryOffset +
                                                        kExceptionDirectory * 8U);
        const auto exception_size = read<std::uint32_t>(optional_offset_ +
                                                         kOptionalHeaderDataDirectoryOffset +
                                                         kExceptionDirectory * 8U + 4U);
        if (!exception_rva || !exception_size || *exception_size == 0U ||
            *exception_size % kRuntimeFunctionSize != 0U ||
            !rva_to_file(*exception_rva, *exception_size)) {
            return std::nullopt;
        }
        for (std::size_t index = 0U; index < *exception_size / kRuntimeFunctionSize; ++index) {
            const std::uint64_t entry_rva64 = static_cast<std::uint64_t>(*exception_rva) +
                                               index * kRuntimeFunctionSize;
            if (entry_rva64 > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto entry_rva = static_cast<std::uint32_t>(entry_rva64);
            const auto begin = read_rva<std::uint32_t>(entry_rva);
            const auto end = read_rva<std::uint32_t>(entry_rva + 4U);
            const auto unwind = read_rva<std::uint32_t>(entry_rva + 8U);
            if (!begin || !end || !unwind || *begin >= *end) {
                return std::nullopt;
            }
            if (*unwind == unwind_rva) {
                return RuntimeFunction{*begin, *end, *unwind, entry_rva};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> find_handler_data_field(
        const RuntimeFunction& function) const noexcept {
        const auto unwind_file = rva_to_file(function.unwind_rva, kUnwindInfoHeaderSize);
        if (!unwind_file) {
            return std::nullopt;
        }
        const auto flags_and_version = read<std::uint8_t>(*unwind_file);
        const auto code_count = read<std::uint8_t>(*unwind_file + 2U);
        if (!flags_and_version || !code_count || ((*flags_and_version >> 3U) & 0x03U) == 0U) {
            return std::nullopt;
        }
        const std::size_t codes_end = kUnwindInfoHeaderSize + static_cast<std::size_t>(*code_count) * 2U;
        const std::size_t handler_offset = (codes_end + 3U) & ~static_cast<std::size_t>(3U);
        const auto handler_rva = read<std::uint32_t>(*unwind_file + handler_offset);
        if (!handler_rva || !rva_to_file(*handler_rva, 1U) ||
            !rva_to_file(function.unwind_rva + static_cast<std::uint32_t>(handler_offset + 4U),
                         sizeof(std::uint32_t))) {
            return std::nullopt;
        }
        return function.unwind_rva + static_cast<std::uint32_t>(handler_offset + 4U);
    }

    std::vector<std::byte> bytes_;
    std::vector<Section> sections_;
    std::size_t optional_offset_{};
};

[[nodiscard]] std::optional<std::vector<std::byte>> read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input && size != 0) {
        return std::nullopt;
    }
    return bytes;
}

}  // namespace

int main(const int argc, char* const argv[]) {
    if (argc != 3 && argc != 4) {
        return 2;
    }
    const std::string_view variant = argc == 4 ? argv[3] : "normal";
    const auto input = read_file(argv[1]);
    if (!input) {
        return 3;
    }
    PeImage image(*input);
    if (!image.parse() || !image.patch(variant)) {
        return 4;
    }
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) {
        return 5;
    }
    const std::vector<std::byte>& bytes = image.bytes();
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output ? 0 : 6;
}
