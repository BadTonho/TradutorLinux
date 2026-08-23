#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint16_t kPe32PlusMagic = 0x20BU;
constexpr std::uint16_t kAmd64Machine = 0x8664U;
constexpr std::size_t kDataDirectoryOffset = 112U;
constexpr std::size_t kImportDirectory = 1U;
constexpr std::size_t kExceptionDirectory = 3U;
constexpr std::size_t kImportDescriptorSize = 20U;
constexpr std::size_t kRuntimeFunctionSize = 12U;
constexpr std::uint8_t kUnwindVersion1 = 1U;
constexpr std::uint8_t kUnwindVersion2 = 2U;
constexpr std::uint8_t kUnwindOpEpilog = 6U;
constexpr std::uint8_t kUnwindV2EpilogAtFunctionEnd = 1U;

struct Section {
    std::uint32_t virtual_address{};
    std::uint32_t virtual_size{};
    std::uint32_t raw_address{};
    std::uint32_t raw_size{};
};

class PeImage {
public:
    explicit PeImage(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] bool parse() {
        const auto lfanew = read<std::uint32_t>(0x3CU);
        if (!lfanew || !in_bounds(*lfanew, 24U) || read<std::uint32_t>(*lfanew) != 0x00004550U ||
            read<std::uint16_t>(*lfanew + 4U) != kAmd64Machine) {
            return false;
        }
        const auto section_count = read<std::uint16_t>(*lfanew + 6U);
        const auto optional_size = read<std::uint16_t>(*lfanew + 20U);
        if (!section_count || !optional_size) {
            return false;
        }
        optional_offset_ = *lfanew + 24U;
        if (!in_bounds(optional_offset_, *optional_size) ||
            read<std::uint16_t>(optional_offset_) != kPe32PlusMagic ||
            *optional_size < kDataDirectoryOffset + 16U * 8U) {
            return false;
        }
        const auto directory_count = read<std::uint32_t>(optional_offset_ + 108U);
        if (!directory_count || *directory_count < 4U) {
            return false;
        }
        const std::size_t section_offset = optional_offset_ + *optional_size;
        if (!in_bounds(section_offset, static_cast<std::size_t>(*section_count) * 40U)) {
            return false;
        }
        for (std::size_t index = 0; index < *section_count; ++index) {
            const std::size_t offset = section_offset + index * 40U;
            const auto virtual_size = read<std::uint32_t>(offset + 8U);
            const auto virtual_address = read<std::uint32_t>(offset + 12U);
            const auto raw_size = read<std::uint32_t>(offset + 16U);
            const auto raw_address = read<std::uint32_t>(offset + 20U);
            if (!virtual_size || !virtual_address || !raw_size || !raw_address ||
                !in_bounds(*raw_address, *raw_size)) {
                return false;
            }
            sections_.push_back({*virtual_address, *virtual_size, *raw_address, *raw_size});
        }
        return true;
    }

    [[nodiscard]] bool promote_calling_function(const std::string_view symbol) {
        const auto iat_rva = find_iat_rva(symbol);
        if (!iat_rva) {
            return false;
        }
        const auto call_rva = find_indirect_call(*iat_rva);
        if (!call_rva) {
            return false;
        }
        const auto function = find_runtime_function(*call_rva);
        if (!function || function->end_rva - function->begin_rva < 1U) {
            return false;
        }
        return promote_unwind_info(function->unwind_rva);
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    struct RuntimeFunction {
        std::uint32_t begin_rva{};
        std::uint32_t end_rva{};
        std::uint32_t unwind_rva{};
    };

    [[nodiscard]] bool in_bounds(const std::size_t offset, const std::size_t length) const {
        return offset <= bytes_.size() && length <= bytes_.size() - offset;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> read(const std::size_t offset) const {
        if (!in_bounds(offset, sizeof(T))) {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, bytes_.data() + static_cast<std::ptrdiff_t>(offset), sizeof(value));
        return value;
    }

    template <typename T>
    [[nodiscard]] bool write(const std::size_t offset, const T value) {
        if (!in_bounds(offset, sizeof(T))) {
            return false;
        }
        std::memcpy(bytes_.data() + static_cast<std::ptrdiff_t>(offset), &value, sizeof(value));
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> rva_to_file(const std::uint32_t rva,
                                                          const std::size_t length) const {
        for (const Section& section : sections_) {
            if (rva < section.virtual_address) {
                continue;
            }
            const std::uint64_t delta = static_cast<std::uint64_t>(rva) - section.virtual_address;
            if (delta > section.raw_size || length > section.raw_size - delta) {
                continue;
            }
            const std::uint64_t offset = static_cast<std::uint64_t>(section.raw_address) + delta;
            if (offset > std::numeric_limits<std::size_t>::max() || !in_bounds(
                    static_cast<std::size_t>(offset), length)) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(offset);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string_view> string_at_rva(const std::uint32_t rva,
                                                                  const std::size_t skip = 0U) const {
        const auto offset = rva_to_file(rva, skip + 1U);
        if (!offset) {
            return std::nullopt;
        }
        const std::size_t start = *offset + skip;
        for (std::size_t end = start; end < bytes_.size(); ++end) {
            if (bytes_[end] == std::byte{0}) {
                return std::string_view(reinterpret_cast<const char*>(bytes_.data() +
                                                                        static_cast<std::ptrdiff_t>(start)),
                                        end - start);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> directory_rva(const std::size_t index) const {
        const auto count = read<std::uint32_t>(optional_offset_ + 108U);
        if (!count || index >= *count) {
            return std::nullopt;
        }
        return read<std::uint32_t>(optional_offset_ + kDataDirectoryOffset + index * 8U);
    }

    [[nodiscard]] std::optional<std::uint32_t> directory_size(const std::size_t index) const {
        const auto count = read<std::uint32_t>(optional_offset_ + 108U);
        if (!count || index >= *count) {
            return std::nullopt;
        }
        return read<std::uint32_t>(optional_offset_ + kDataDirectoryOffset + index * 8U + 4U);
    }

    [[nodiscard]] std::optional<std::uint32_t> find_iat_rva(const std::string_view symbol) const {
        const auto import_rva = directory_rva(kImportDirectory);
        const auto import_size = directory_size(kImportDirectory);
        if (!import_rva || !import_size || *import_size < kImportDescriptorSize ||
            !rva_to_file(*import_rva, *import_size)) {
            return std::nullopt;
        }
        for (std::size_t descriptor_index = 0;
             descriptor_index < *import_size / kImportDescriptorSize; ++descriptor_index) {
            const std::uint32_t descriptor_rva = *import_rva +
                                                 static_cast<std::uint32_t>(descriptor_index *
                                                                            kImportDescriptorSize);
            const auto descriptor_file = rva_to_file(descriptor_rva, kImportDescriptorSize);
            if (!descriptor_file) {
                return std::nullopt;
            }
            const auto original_first_thunk = read<std::uint32_t>(*descriptor_file);
            const auto name_rva = read<std::uint32_t>(*descriptor_file + 12U);
            const auto first_thunk = read<std::uint32_t>(*descriptor_file + 16U);
            if (!original_first_thunk || !name_rva || !first_thunk) {
                return std::nullopt;
            }
            const auto dll_name = string_at_rva(*name_rva);
            if (!dll_name || *dll_name != "KERNEL32.dll") {
                continue;
            }
            const std::uint32_t lookup_rva = *original_first_thunk == 0U ? *first_thunk
                                                                           : *original_first_thunk;
            for (std::size_t index = 0; index < 4096U; ++index) {
                const std::uint64_t slot_rva64 = static_cast<std::uint64_t>(lookup_rva) + index * 8U;
                if (slot_rva64 > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                const auto thunk_file = rva_to_file(static_cast<std::uint32_t>(slot_rva64), 8U);
                if (!thunk_file) {
                    return std::nullopt;
                }
                const auto thunk = read<std::uint64_t>(*thunk_file);
                if (!thunk || *thunk == 0U) {
                    break;
                }
                if ((*thunk & 0x8000000000000000ULL) != 0U || *thunk >
                        std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                const auto import_name = string_at_rva(static_cast<std::uint32_t>(*thunk), 2U);
                if (import_name && *import_name == symbol) {
                    const std::uint64_t iat_rva64 = static_cast<std::uint64_t>(*first_thunk) + index * 8U;
                    if (iat_rva64 <= std::numeric_limits<std::uint32_t>::max()) {
                        return static_cast<std::uint32_t>(iat_rva64);
                    }
                    return std::nullopt;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> find_indirect_call(const std::uint32_t target_rva) const {
        for (const Section& section : sections_) {
            if (section.raw_size < 6U) {
                continue;
            }
            for (std::size_t offset = 0; offset + 6U <= section.raw_size; ++offset) {
                const std::size_t file_offset = static_cast<std::size_t>(section.raw_address) + offset;
                if (bytes_[file_offset] != std::byte{0xFF} || bytes_[file_offset + 1U] != std::byte{0x15}) {
                    continue;
                }
                const auto displacement = read<std::int32_t>(file_offset + 2U);
                if (!displacement) {
                    return std::nullopt;
                }
                const std::uint64_t instruction_rva =
                    static_cast<std::uint64_t>(section.virtual_address) + offset;
                const std::int64_t resolved_rva = static_cast<std::int64_t>(instruction_rva + 6U) +
                                                  *displacement;
                if (resolved_rva == static_cast<std::int64_t>(target_rva)) {
                    return static_cast<std::uint32_t>(instruction_rva);
                }
            }
            // mingw pode materializar a entrada IAT em RAX antes do call:
            // mov rax, [rip + disp32]; call rax.
            for (std::size_t offset = 0; offset + 9U <= section.raw_size; ++offset) {
                const std::size_t file_offset = static_cast<std::size_t>(section.raw_address) + offset;
                if (bytes_[file_offset] != std::byte{0x48} || bytes_[file_offset + 1U] != std::byte{0x8B} ||
                    bytes_[file_offset + 2U] != std::byte{0x05} || bytes_[file_offset + 7U] != std::byte{0xFF} ||
                    bytes_[file_offset + 8U] != std::byte{0xD0}) {
                    continue;
                }
                const auto displacement = read<std::int32_t>(file_offset + 3U);
                if (!displacement) {
                    return std::nullopt;
                }
                const std::uint64_t instruction_rva =
                    static_cast<std::uint64_t>(section.virtual_address) + offset;
                const std::int64_t resolved_rva = static_cast<std::int64_t>(instruction_rva + 7U) +
                                                  *displacement;
                if (resolved_rva == static_cast<std::int64_t>(target_rva)) {
                    return static_cast<std::uint32_t>(instruction_rva + 7U);
                }
            }
            // Com certos modos de ligação do MinGW, a chamada é relativa a
            // um thunk local que, por sua vez, salta indiretamente pela IAT.
            // O call ainda pertence à função que queremos promover.
            for (std::size_t offset = 0; offset + 5U <= section.raw_size; ++offset) {
                const std::size_t file_offset = static_cast<std::size_t>(section.raw_address) + offset;
                if (bytes_[file_offset] != std::byte{0xE8}) {
                    continue;
                }
                const auto displacement = read<std::int32_t>(file_offset + 1U);
                if (!displacement) {
                    return std::nullopt;
                }
                const std::uint64_t instruction_rva =
                    static_cast<std::uint64_t>(section.virtual_address) + offset;
                const std::int64_t thunk_rva = static_cast<std::int64_t>(instruction_rva + 5U) +
                                               *displacement;
                if (thunk_rva < 0 || thunk_rva > std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                const auto thunk_file = rva_to_file(static_cast<std::uint32_t>(thunk_rva), 6U);
                if (!thunk_file || bytes_[*thunk_file] != std::byte{0xFF} ||
                    bytes_[*thunk_file + 1U] != std::byte{0x25}) {
                    continue;
                }
                const auto thunk_displacement = read<std::int32_t>(*thunk_file + 2U);
                if (!thunk_displacement) {
                    return std::nullopt;
                }
                const std::int64_t resolved_rva = thunk_rva + 6U + *thunk_displacement;
                if (resolved_rva == static_cast<std::int64_t>(target_rva)) {
                    return static_cast<std::uint32_t>(instruction_rva);
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeFunction> find_runtime_function(
        const std::uint32_t control_rva) const {
        const auto exception_rva = directory_rva(kExceptionDirectory);
        const auto exception_size = directory_size(kExceptionDirectory);
        if (!exception_rva || !exception_size || *exception_size == 0U ||
            *exception_size % kRuntimeFunctionSize != 0U ||
            !rva_to_file(*exception_rva, *exception_size)) {
            return std::nullopt;
        }
        std::optional<RuntimeFunction> result;
        for (std::size_t index = 0; index < *exception_size / kRuntimeFunctionSize; ++index) {
            const std::uint32_t entry_rva = *exception_rva +
                                            static_cast<std::uint32_t>(index * kRuntimeFunctionSize);
            const auto file_offset = rva_to_file(entry_rva, kRuntimeFunctionSize);
            if (!file_offset) {
                return std::nullopt;
            }
            const auto begin_rva = read<std::uint32_t>(*file_offset);
            const auto end_rva = read<std::uint32_t>(*file_offset + 4U);
            const auto unwind_rva = read<std::uint32_t>(*file_offset + 8U);
            if (!begin_rva || !end_rva || !unwind_rva || *begin_rva >= *end_rva ||
                (*unwind_rva & 3U) != 0U) {
                return std::nullopt;
            }
            if (*begin_rva <= control_rva && control_rva < *end_rva) {
                if (result) {
                    return std::nullopt;
                }
                result = {*begin_rva, *end_rva, *unwind_rva};
            }
        }
        return result;
    }

    [[nodiscard]] bool promote_unwind_info(const std::uint32_t unwind_rva) {
        const auto unwind_file = rva_to_file(unwind_rva, 4U);
        if (!unwind_file) {
            return false;
        }
        const auto header = read<std::uint8_t>(*unwind_file);
        const auto code_count = read<std::uint8_t>(*unwind_file + 2U);
        if (!header || !code_count || (*header & 7U) != kUnwindVersion1 ||
            ((*header >> 3U) != 0U && (*header >> 3U) != 1U) ||
            *code_count == 0U || (*code_count & 1U) == 0U) {
            return false;
        }
        const std::size_t old_codes_size = static_cast<std::size_t>(*code_count) * 2U;
        const std::size_t full_codes_size = old_codes_size + 2U;
        if (!in_bounds(*unwind_file + 4U, full_codes_size)) {
            return false;
        }
        const std::size_t padding_offset = *unwind_file + 4U + old_codes_size;
        if (bytes_[padding_offset] != std::byte{0} || bytes_[padding_offset + 1U] != std::byte{0}) {
            return false;
        }
        std::memmove(bytes_.data() + static_cast<std::ptrdiff_t>(*unwind_file + 6U),
                     bytes_.data() + static_cast<std::ptrdiff_t>(*unwind_file + 4U), old_codes_size);
        const std::uint8_t flags = static_cast<std::uint8_t>(*header & 0xF8U);
        if (!write<std::uint8_t>(*unwind_file, static_cast<std::uint8_t>(flags | kUnwindVersion2)) ||
            !write<std::uint8_t>(*unwind_file + 2U, static_cast<std::uint8_t>(*code_count + 1U)) ||
            !write<std::uint8_t>(*unwind_file + 4U, 1U) ||
            !write<std::uint8_t>(*unwind_file + 5U,
                                 static_cast<std::uint8_t>(kUnwindOpEpilog |
                                                           (kUnwindV2EpilogAtFunctionEnd << 4U)))) {
            return false;
        }
        return true;
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
    const auto input = read_file(argv[1]);
    if (!input) {
        return 3;
    }
    PeImage image(*input);
    const std::string_view symbol = argc == 4 ? argv[3] : "RtlVirtualUnwind";
    if (!image.parse() || !image.promote_calling_function(symbol)) {
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
