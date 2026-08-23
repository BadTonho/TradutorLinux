#include "pe_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace tradutorlinux::pe::testutil;

[[nodiscard]] std::vector<std::byte> make_fixture(const std::string& symbol) {
    // sub rsp, 0x28; xor ecx, ecx; call qword ptr [rip + 0x104c]; ud2
    // The call reaches the first delay IAT slot (RVA 0x2058), which the
    // runtime patches to KERNEL32!ExitProcess before the entry point starts.
    const std::vector<std::byte> text{
        std::byte{0x48}, std::byte{0x83}, std::byte{0xEC}, std::byte{0x28},
        std::byte{0x31}, std::byte{0xC9},
        std::byte{0xFF}, std::byte{0x15}, std::byte{0x4C}, std::byte{0x10},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x0F}, std::byte{0x0B},
    };

    std::vector<std::byte> delay_data =
        make_delay_import_data({{"KERNEL32.dll", {symbol}, {}}});
    while (delay_data.size() % 4U != 0U) {
        delay_data.push_back(std::byte{0});
    }
    const std::uint32_t reloc_rva =
        kImportDataRva + static_cast<std::uint32_t>(delay_data.size());
    push_u32(delay_data, 0);
    push_u32(delay_data, 8);

    BuildSpec spec;
    spec.section_names = {".text", ".didat"};
    spec.section_data = {text, std::move(delay_data)};
    spec.delay_import_rva = kImportDataRva;
    spec.delay_import_size = 64;
    spec.reloc_rva = reloc_rva;
    spec.reloc_size = 8;
    return build(spec);
}

}  // namespace

int main(const int argc, char* const argv[]) {
    if (argc != 3) {
        return 2;
    }
    const std::vector<std::byte> bytes = make_fixture(argv[2]);
    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    if (!output) {
        return 3;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output ? 0 : 4;
}
