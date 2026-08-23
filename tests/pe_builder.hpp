// Synthetic PE32+ builder shared by unit tests.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tradutorlinux::pe::testutil {

inline constexpr std::size_t kDosHeaderSize = 64;
inline constexpr std::size_t kLfanewOffset = 60;
inline constexpr std::size_t kNtOffset = 0x40;
inline constexpr std::size_t kMachineOffset = 0x44;
inline constexpr std::size_t kSectionCountOffset = 0x46;
inline constexpr std::size_t kOptionalSizeOffset = 0x54;
inline constexpr std::size_t kOptionalMagicOffset = 0x58;
inline constexpr std::size_t kRelocDirSizeOffset = 0xF4;
inline constexpr std::size_t kSection0RawSizeOffset = 0x148 + 16;

inline constexpr std::uint16_t kMachineAmd64 = 0x8664;

inline void push_u16(std::vector<std::byte>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

inline void push_u32(std::vector<std::byte>& out, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    }
}

inline void push_u64(std::vector<std::byte>& out, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    }
}

inline void push_cstr(std::vector<std::byte>& out, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*cursor)));
    }
    out.push_back(std::byte{0});
}

inline void write_u16(std::vector<std::byte>& bytes, const std::size_t offset,
                      const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

inline void write_u32(std::vector<std::byte>& bytes, const std::size_t offset,
                      const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::byte>((value >> shift) & 0xFF);
    }
}

inline void write_u64(std::vector<std::byte>& bytes, const std::size_t offset,
                      const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::byte>((value >> shift) & 0xFF);
    }
}

struct BuildSpec {
    std::uint16_t machine{kMachineAmd64};
    std::uint16_t section_count{2};
    std::uint16_t optional_size{240};
    std::uint32_t entry_point{0x1000};
    std::uint64_t image_base{0x140000000ULL};
    std::uint32_t section_alignment{0x1000};
    std::uint32_t size_of_image{0x3000};
    std::uint32_t size_of_headers{0x200};
    std::uint32_t number_of_rva_and_sizes{16};
    std::uint32_t import_rva{};
    std::uint32_t import_size{};
    std::uint32_t exception_rva{};
    std::uint32_t exception_size{};
    std::uint32_t delay_import_rva{};
    std::uint32_t delay_import_size{};
    std::uint32_t reloc_rva{};
    std::uint32_t reloc_size{};
    std::vector<std::string> section_names{".text", ".rdata"};
    std::vector<std::vector<std::byte>> section_data;
};

inline std::vector<std::byte> build(const BuildSpec& spec) {
    std::vector<std::byte> out;
    out.resize(kDosHeaderSize, std::byte{0});
    write_u16(out, 0, 0x5A4D);
    write_u32(out, kLfanewOffset, kNtOffset);

    push_u32(out, 0x00004550);  // "PE\0\0" signature
    push_u16(out, spec.machine);
    push_u16(out, spec.section_count);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u16(out, spec.optional_size);
    push_u16(out, 0x22);

    const std::size_t opt_start = out.size();
    push_u16(out, 0x20B);
    push_u16(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, 0);
    push_u32(out, spec.entry_point);
    push_u32(out, 0x1000);
    push_u64(out, spec.image_base);
    push_u32(out, spec.section_alignment);
    push_u32(out, 0x200);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 5);
    push_u16(out, 2);
    push_u32(out, 0);
    push_u32(out, spec.size_of_image);
    push_u32(out, spec.size_of_headers);
    push_u32(out, 0);
    push_u16(out, 3);
    push_u16(out, 0);
    push_u64(out, 0x100000);
    push_u64(out, 0x1000);
    push_u64(out, 0x100000);
    push_u64(out, 0x1000);
    push_u32(out, 0);
    push_u32(out, spec.number_of_rva_and_sizes);
    for (std::size_t index = 0; index < 16; ++index) {
        push_u32(out, 0);
        push_u32(out, 0);
    }
    if (spec.import_rva != 0 || spec.import_size != 0) {
        write_u32(out, opt_start + 112 + 8, spec.import_rva);
        write_u32(out, opt_start + 112 + 8 + 4, spec.import_size);
    }
    if (spec.exception_rva != 0 || spec.exception_size != 0) {
        constexpr std::size_t kExceptionDirectory = 3;
        constexpr std::size_t kDataDirectorySize = 8;
        write_u32(out, opt_start + 112 + kExceptionDirectory * kDataDirectorySize,
                  spec.exception_rva);
        write_u32(out, opt_start + 112 + kExceptionDirectory * kDataDirectorySize + 4,
                  spec.exception_size);
    }
    if (spec.delay_import_rva != 0 || spec.delay_import_size != 0) {
        constexpr std::size_t kDelayImportDirectory = 13;
        constexpr std::size_t kDataDirectorySize = 8;
        write_u32(out, opt_start + 112 + kDelayImportDirectory * kDataDirectorySize,
                  spec.delay_import_rva);
        write_u32(out, opt_start + 112 + kDelayImportDirectory * kDataDirectorySize + 4,
                  spec.delay_import_size);
    }
    if (spec.reloc_rva != 0 || spec.reloc_size != 0) {
        write_u32(out, opt_start + 112 + 40, spec.reloc_rva);
        write_u32(out, opt_start + 112 + 40 + 4, spec.reloc_size);
    }
    if (out.size() - opt_start < spec.optional_size) {
        out.resize(opt_start + spec.optional_size, std::byte{0});
    }

    for (std::size_t index = 0; index < spec.section_count; ++index) {
        const std::string name = index < spec.section_names.size() ? spec.section_names[index] : "";
        for (std::size_t byte_index = 0; byte_index < 8; ++byte_index) {
            out.push_back(byte_index < name.size()
                              ? static_cast<std::byte>(static_cast<unsigned char>(name[byte_index]))
                              : std::byte{0});
        }
        const std::uint32_t data_size =
            index < spec.section_data.size()
                ? static_cast<std::uint32_t>(spec.section_data[index].size())
                : 0;
        push_u32(out, data_size);
        push_u32(out, 0x1000 + static_cast<std::uint32_t>(index) * 0x1000);
        push_u32(out, 0x200);
        push_u32(out, 0x200 + static_cast<std::uint32_t>(index) * 0x200);
        push_u32(out, 0);
        push_u32(out, 0);
        push_u16(out, 0);
        push_u16(out, 0);
        push_u32(out, index == 0 ? 0x60000020 : 0x40000040);
    }
    out.resize(0x200 + static_cast<std::size_t>(spec.section_count) * 0x200, std::byte{0});

    for (std::size_t index = 0; index < spec.section_count; ++index) {
        const std::size_t raw_pointer = 0x200 + index * 0x200;
        if (index < spec.section_data.size()) {
            const std::vector<std::byte>& data = spec.section_data[index];
            std::copy(data.begin(), data.end(), out.begin() + static_cast<std::ptrdiff_t>(raw_pointer));
        }
    }
    return out;
}

inline std::vector<std::byte> make_minimal() {
    return build(BuildSpec{});
}

struct ImportSpec {
    std::string dll;
    std::vector<std::string> symbols_by_name;
    std::vector<std::uint16_t> ordinals;
};

inline constexpr std::uint32_t kImportDataRva = 0x2000;

// Builds the raw bytes of an import directory (descriptors, original thunk
// tables, hint/name entries and the IAT) for the given DLLs. The RVAs stored
// inside assume the blob is mapped at kImportDataRva. Keep the result smaller
// than one 0x200-byte section.
inline std::vector<std::byte> make_import_data(const std::vector<ImportSpec>& dlls) {
    std::vector<std::byte> data;
    const std::size_t descriptor_area = (dlls.size() + 1) * 20;
    data.resize(descriptor_area, std::byte{0});
    for (std::size_t dll_index = 0; dll_index < dlls.size(); ++dll_index) {
        const ImportSpec& spec = dlls[dll_index];
        const std::size_t descriptor_offset = dll_index * 20;
        const std::size_t oft_offset = data.size();
        const std::size_t symbol_count = spec.symbols_by_name.size() + spec.ordinals.size();
        for (std::size_t index = 0; index < symbol_count + 1; ++index) {
            push_u64(data, 0);
        }
        const std::size_t iat_offset = data.size();
        for (std::size_t index = 0; index < symbol_count + 1; ++index) {
            push_u64(data, 0);
        }
        std::vector<std::size_t> hint_offsets;
        for (const std::string& name : spec.symbols_by_name) {
            hint_offsets.push_back(data.size());
            push_u16(data, 0);
            push_cstr(data, name.c_str());
        }
        const std::size_t name_offset = data.size();
        push_cstr(data, spec.dll.c_str());

        constexpr std::uint64_t kOrdinalFlag = 0x8000000000000000ULL;
        for (std::size_t symbol_index = 0; symbol_index < spec.symbols_by_name.size(); ++symbol_index) {
            const std::uint64_t value =
                static_cast<std::uint64_t>(kImportDataRva) + hint_offsets[symbol_index];
            write_u64(data, oft_offset + symbol_index * 8, value);
            write_u64(data, iat_offset + symbol_index * 8, value);
        }
        for (std::size_t ordinal_index = 0; ordinal_index < spec.ordinals.size(); ++ordinal_index) {
            const std::size_t symbol_index = spec.symbols_by_name.size() + ordinal_index;
            const std::uint64_t value =
                kOrdinalFlag | static_cast<std::uint64_t>(spec.ordinals[ordinal_index]);
            write_u64(data, oft_offset + symbol_index * 8, value);
            write_u64(data, iat_offset + symbol_index * 8, value);
        }
        write_u32(data, descriptor_offset + 0, kImportDataRva + static_cast<std::uint32_t>(oft_offset));
        write_u32(data, descriptor_offset + 12, kImportDataRva + static_cast<std::uint32_t>(name_offset));
        write_u32(data, descriptor_offset + 16, kImportDataRva + static_cast<std::uint32_t>(iat_offset));
    }
    return data;
}

// Builds IMAGE_DELAYLOAD_DESCRIPTOR entries and their INT/IAT/name data. The
// descriptors use dlattrRva (0x1), the representation emitted by current
// x64 linkers and supported by the runtime.
inline std::vector<std::byte> make_delay_import_data(const std::vector<ImportSpec>& dlls) {
    constexpr std::size_t kDelayDescriptorSize = 32;
    constexpr std::uint32_t kDelayImportAttrRva = 0x1;
    constexpr std::uint64_t kOrdinalFlag = 0x8000000000000000ULL;

    std::vector<std::byte> data((dlls.size() + 1) * kDelayDescriptorSize, std::byte{0});
    for (std::size_t dll_index = 0; dll_index < dlls.size(); ++dll_index) {
        const ImportSpec& spec = dlls[dll_index];
        const std::size_t descriptor_offset = dll_index * kDelayDescriptorSize;
        const std::size_t symbol_count = spec.symbols_by_name.size() + spec.ordinals.size();

        const std::size_t module_handle_offset = data.size();
        push_u64(data, 0);
        const std::size_t int_offset = data.size();
        for (std::size_t index = 0; index < symbol_count + 1; ++index) {
            push_u64(data, 0);
        }
        const std::size_t iat_offset = data.size();
        for (std::size_t index = 0; index < symbol_count + 1; ++index) {
            push_u64(data, 0);
        }

        std::vector<std::size_t> hint_offsets;
        for (const std::string& name : spec.symbols_by_name) {
            hint_offsets.push_back(data.size());
            push_u16(data, 0);
            push_cstr(data, name.c_str());
        }
        const std::size_t name_offset = data.size();
        push_cstr(data, spec.dll.c_str());

        for (std::size_t symbol_index = 0; symbol_index < spec.symbols_by_name.size();
             ++symbol_index) {
            const std::uint64_t value =
                static_cast<std::uint64_t>(kImportDataRva) + hint_offsets[symbol_index];
            write_u64(data, int_offset + symbol_index * 8, value);
            write_u64(data, iat_offset + symbol_index * 8, value);
        }
        for (std::size_t ordinal_index = 0; ordinal_index < spec.ordinals.size();
             ++ordinal_index) {
            const std::size_t symbol_index = spec.symbols_by_name.size() + ordinal_index;
            const std::uint64_t value =
                kOrdinalFlag | static_cast<std::uint64_t>(spec.ordinals[ordinal_index]);
            write_u64(data, int_offset + symbol_index * 8, value);
            write_u64(data, iat_offset + symbol_index * 8, value);
        }

        write_u32(data, descriptor_offset, kDelayImportAttrRva);
        write_u32(data, descriptor_offset + 4,
                  kImportDataRva + static_cast<std::uint32_t>(name_offset));
        write_u32(data, descriptor_offset + 8,
                  kImportDataRva + static_cast<std::uint32_t>(module_handle_offset));
        write_u32(data, descriptor_offset + 12,
                  kImportDataRva + static_cast<std::uint32_t>(iat_offset));
        write_u32(data, descriptor_offset + 16,
                  kImportDataRva + static_cast<std::uint32_t>(int_offset));
    }
    return data;
}

}  // namespace tradutorlinux::pe::testutil
