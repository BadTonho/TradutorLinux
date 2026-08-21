#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tradutorlinux/loader/image_mapper.hpp"

#include "tradutorlinux/util/basics.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace tradutorlinux::loader {
namespace {

constexpr std::uint32_t kImageScnMemExecute = 0x20000000;
constexpr std::uint32_t kImageScnMemRead = 0x40000000;
constexpr std::uint32_t kImageScnMemWrite = 0x80000000;

constexpr std::uint16_t kImageRelBasedAbsolute = 0;
constexpr std::uint16_t kImageRelBasedDir64 = 10;

constexpr std::size_t kBaseRelocBlockHeaderSize = 8;
constexpr std::size_t kMaxRelocBlocks = 4096;

[[nodiscard]] std::uint64_t align_down(const std::uint64_t value, const std::uint64_t alignment) {
    return value - value % alignment;
}

[[nodiscard]] std::uint64_t align_up(const std::uint64_t value, const std::uint64_t alignment) {
    const std::uint64_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

[[nodiscard]] SectionPermissions section_permissions(const std::uint32_t characteristics) {
    const bool read = (characteristics & kImageScnMemRead) != 0;
    const bool write = (characteristics & kImageScnMemWrite) != 0;
    const bool execute = (characteristics & kImageScnMemExecute) != 0;
    if (write) {
        return SectionPermissions::ReadWrite;
    }
    if (execute) {
        return SectionPermissions::ReadExecute;
    }
    if (read) {
        return SectionPermissions::ReadOnly;
    }
    return SectionPermissions::None;
}

[[nodiscard]] int to_prot(const SectionPermissions permissions) {
    switch (permissions) {
        case SectionPermissions::None:
            return PROT_NONE;
        case SectionPermissions::ReadOnly:
            return PROT_READ;
        case SectionPermissions::ReadWrite:
            return PROT_READ | PROT_WRITE;
        case SectionPermissions::ReadExecute:
            return PROT_READ | PROT_EXEC;
    }
    return PROT_NONE;
}

[[nodiscard]] SectionPermissions permissions_for_page(const std::vector<MapRegion>& regions,
                                                      const std::uint64_t page_start,
                                                      const std::uint64_t page_end) {
    bool read = false;
    bool write = false;
    bool execute = false;
    for (const MapRegion& region : regions) {
        const std::uint64_t region_start = region.rva;
        const std::uint64_t region_end = region_start + region.size;
        if (region_start >= page_end || region_end <= page_start) {
            continue;
        }
        switch (region.permissions) {
            case SectionPermissions::ReadOnly:
                read = true;
                break;
            case SectionPermissions::ReadWrite:
                read = true;
                write = true;
                break;
            case SectionPermissions::ReadExecute:
                read = true;
                execute = true;
                break;
            case SectionPermissions::None:
                break;
        }
    }
    if (write) {
        return SectionPermissions::ReadWrite;
    }
    if (execute) {
        return SectionPermissions::ReadExecute;
    }
    return read ? SectionPermissions::ReadOnly : SectionPermissions::None;
}

}  // namespace

SectionPermissions effective_page_permissions(const MappedImage& image, const std::uint32_t rva) {
    const std::size_t page = util::host_page_size();
    const std::uint64_t page_start = align_down(rva, page);
    const std::uint64_t page_end = align_up(static_cast<std::uint64_t>(rva) + 1, page);
    return permissions_for_page(image.regions, page_start, page_end);
}

namespace {

[[nodiscard]] MapResult fail(const MapStatus status, std::string message) {
    return {.status = status, .error_message = std::move(message), .image = {}};
}

[[nodiscard]] RelocationResult fail_reloc(const MapStatus status, std::string message) {
    return {.status = status, .error_message = std::move(message), .applied = 0};
}

[[nodiscard]] bool read_le_u16(const std::span<const std::byte> bytes, const std::size_t offset,
                               std::uint16_t& out) {
    if (offset > bytes.size() || 2 > bytes.size() - offset) {
        return false;
    }
    std::uint16_t value = 0;
    for (std::size_t index = 0; index < 2; ++index) {
        value |= static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (8 * index));
    }
    out = value;
    return true;
}

[[nodiscard]] bool read_le_u32(const std::span<const std::byte> bytes, const std::size_t offset,
                               std::uint32_t& out) {
    if (offset > bytes.size() || 4 > bytes.size() - offset) {
        return false;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
            static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (8 * index));
    }
    out = value;
    return true;
}

[[nodiscard]] bool read_le_u64(const std::span<const std::byte> bytes, const std::size_t offset,
                               std::uint64_t& out) {
    if (offset > bytes.size() || 8 > bytes.size() - offset) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(
            static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (8 * index));
    }
    out = value;
    return true;
}

void write_le_u64(const std::span<std::byte> bytes, const std::size_t offset,
                  const std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (8 * index)) & 0xFFULL);
    }
}

[[nodiscard]] bool regions_overlap(const MapRegion& first, const MapRegion& second) {
    const std::uint64_t first_end = static_cast<std::uint64_t>(first.rva) + first.size;
    const std::uint64_t second_end = static_cast<std::uint64_t>(second.rva) + second.size;
    return static_cast<std::uint64_t>(first.rva) < second_end &&
           static_cast<std::uint64_t>(second.rva) < first_end;
}

}  // namespace

RelocationResult apply_relocations(const std::span<std::byte> image,
                                   const RelocationDirectory& directory,
                                   const std::int64_t delta) {
    const std::uint32_t directory_rva = directory.rva;
    const std::uint32_t directory_size = directory.size;
    if (directory_size == 0) {
        return {.status = MapStatus::Success, .error_message = {}, .applied = 0};
    }
    if (static_cast<std::uint64_t>(directory_rva) > image.size() ||
        static_cast<std::uint64_t>(directory_size) > image.size() - directory_rva) {
        return fail_reloc(MapStatus::InvalidImage,
                          "diretório de relocations fora da imagem mapeada");
    }

    const std::uint64_t directory_limit = directory_size;
    std::size_t consumed = 0;
    std::size_t applied = 0;
    std::size_t block_count = 0;
    while (consumed + kBaseRelocBlockHeaderSize <= directory_limit) {
        if (block_count >= kMaxRelocBlocks) {
            return fail_reloc(MapStatus::InvalidImage, "número excessivo de blocos de relocations");
        }
        const std::size_t block_offset = directory_rva + consumed;
        std::uint32_t page_rva{};
        std::uint32_t block_size{};
        if (!read_le_u32(image, block_offset, page_rva) ||
            !read_le_u32(image, block_offset + 4, block_size)) {
            return fail_reloc(MapStatus::InvalidImage, "cabeçalho de bloco de relocations truncado");
        }
        if (block_size < kBaseRelocBlockHeaderSize) {
            return fail_reloc(MapStatus::InvalidImage,
                              "bloco de relocations com tamanho inválido (" +
                                  std::to_string(block_size) + " bytes)");
        }
        if (static_cast<std::uint64_t>(block_size) > directory_limit - consumed) {
            return fail_reloc(MapStatus::InvalidImage, "bloco de relocations excede o diretório");
        }
        const std::size_t entry_bytes = static_cast<std::size_t>(block_size) - kBaseRelocBlockHeaderSize;
        if (entry_bytes % 2 != 0) {
            return fail_reloc(MapStatus::InvalidImage, "bloco de relocations com entradas truncadas");
        }

        for (std::size_t entry_index = 0; entry_index < entry_bytes / 2; ++entry_index) {
            std::uint16_t raw_entry{};
            if (!read_le_u16(image, block_offset + kBaseRelocBlockHeaderSize + entry_index * 2,
                             raw_entry)) {
                return fail_reloc(MapStatus::InvalidImage, "entrada de relocations truncada");
            }
            const std::uint16_t type = static_cast<std::uint16_t>(raw_entry >> 12);
            const std::uint16_t offset = static_cast<std::uint16_t>(raw_entry & 0xFFF);
            if (type == kImageRelBasedAbsolute) {
                continue;
            }
            const std::uint64_t target_rva = static_cast<std::uint64_t>(page_rva) + offset;
            if (type == kImageRelBasedDir64) {
                std::uint64_t value{};
                if (!read_le_u64(image, target_rva, value)) {
                    return fail_reloc(MapStatus::InvalidImage,
                                      "alvo DIR64 em RVA " + std::to_string(target_rva) +
                                          " fora da imagem");
                }
                write_le_u64(image, target_rva,
                             value + static_cast<std::uint64_t>(delta));
                ++applied;
            } else {
                return fail_reloc(MapStatus::InvalidImage,
                                  "tipo de relocação " + std::to_string(type) +
                                      " não suportado para PE32+");
            }
        }
        consumed += static_cast<std::size_t>(block_size);
        ++block_count;
    }

    if (consumed != directory_size) {
        return fail_reloc(MapStatus::InvalidImage, "diretório de relocations com bytes residuais");
    }
    return {.status = MapStatus::Success, .error_message = {}, .applied = applied};
}

MapResult map_image(const pe::PeInfo& info, const std::span<const std::byte> file_bytes,
                    const MapOptions& options) {
    const std::size_t page = util::host_page_size();
    const std::uint64_t mapping_size_u64 =
        align_up(static_cast<std::uint64_t>(info.size_of_image), page);
    if (mapping_size_u64 == 0) {
        return fail(MapStatus::InvalidImage, "SizeOfImage inválido (0)");
    }
    constexpr std::uint64_t kMaxSizeOfImage = 0x80000000ULL;  // limite prático do Windows
    if (mapping_size_u64 > kMaxSizeOfImage) {
        return fail(MapStatus::InvalidImage, "SizeOfImage excede o limite suportado (2 GiB)");
    }
    if (info.size_of_headers == 0 || info.size_of_headers > info.size_of_image) {
        return fail(MapStatus::InvalidImage, "SizeOfHeaders inválido");
    }
    if (mapping_size_u64 > std::numeric_limits<std::size_t>::max()) {
        return fail(MapStatus::InvalidImage, "SizeOfImage excede o espaço de endereço do host");
    }
    const std::size_t mapping_size = static_cast<std::size_t>(mapping_size_u64);

    std::vector<MapRegion> regions;
    for (const pe::SectionInfo& section : info.sections) {
        const std::uint64_t span =
            std::max<std::uint64_t>(section.virtual_size, section.raw_data_size);
        if (span == 0) {
            continue;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(section.virtual_address) + span;
        if (end > static_cast<std::uint64_t>(info.size_of_image) ||
            section.virtual_address < info.size_of_headers) {
            return fail(MapStatus::InvalidImage,
                        "seção " + section.name + " sobrepõe os headers ou excede a imagem");
        }
        MapRegion region{
            .name = section.name,
            .rva = section.virtual_address,
            .size = static_cast<std::uint32_t>(span),
            .raw_data_pointer = section.raw_data_pointer,
            .raw_data_size = section.raw_data_size,
            .permissions = section_permissions(section.characteristics),
        };
        regions.push_back(std::move(region));
    }

    for (auto first = regions.begin(); first != regions.end(); ++first) {
        const auto overlaps = std::any_of(std::next(first), regions.end(),
                                          [first](const MapRegion& second) {
                                              return regions_overlap(*first, second);
                                          });
        if (overlaps) {
            return fail(MapStatus::InvalidImage, "seções se sobrepõem na imagem");
        }
    }

    const std::uint64_t preferred_base =
        options.preferred_base != 0 ? options.preferred_base : info.image_base;
    void* mapping = MAP_FAILED;
#if defined(MAP_FIXED_NOREPLACE)
    if (preferred_base != 0) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): base numérica do PE convertida para endereço do mmap
        mapping = mmap(reinterpret_cast<void*>(static_cast<std::uintptr_t>(preferred_base)),
                       mapping_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    }
#endif
    if (mapping == MAP_FAILED) {
        mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    }
    if (mapping == MAP_FAILED) {
        return fail(MapStatus::OutOfMemory, "não foi possível reservar memória para a imagem");
    }

    auto* memory = static_cast<std::byte*>(mapping);
    const std::size_t header_copy_size = std::min<std::size_t>(
        std::min<std::size_t>(static_cast<std::size_t>(info.size_of_headers),
                              static_cast<std::size_t>(info.size_of_image)),
        file_bytes.size());
    if (header_copy_size > 0) {
        std::memcpy(memory, file_bytes.data(), header_copy_size);
    }
    for (const MapRegion& region : regions) {
        if (region.raw_data_size == 0 ||
            static_cast<std::uint64_t>(region.raw_data_pointer) >= file_bytes.size()) {
            continue;
        }
        const std::size_t copy_size = std::min<std::size_t>(
            static_cast<std::size_t>(region.raw_data_size),
            file_bytes.size() - static_cast<std::size_t>(region.raw_data_pointer));
        if (copy_size == 0) {
            continue;
        }
        std::memcpy(memory + region.rva, file_bytes.data() + region.raw_data_pointer, copy_size);
    }

    const std::int64_t delta =
        static_cast<std::int64_t>(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(mapping))) -
        static_cast<std::int64_t>(preferred_base);
    if ((info.relocation_directory_rva == 0) != (info.relocation_directory_size == 0)) {
        munmap(mapping, mapping_size);
        return fail(MapStatus::InvalidImage,
                    "diretório de relocations com RVA e tamanho inconsistentes");
    }
    const bool has_relocation_directory = info.relocation_directory_size != 0;
    if (delta != 0 && !has_relocation_directory) {
        // Sem relocations não há como corrigir endereços absolutos na base
        // real: executar aqui produziria ponteiros inválidos.
        munmap(mapping, mapping_size);
        return fail(MapStatus::InvalidImage,
                    "imagem carregada fora da base preferencial sem diretório de relocations");
    }
    std::size_t applied_relocations = 0;
    if (delta != 0 && has_relocation_directory) {
        const RelocationResult relocation =
            apply_relocations(std::span(memory, mapping_size),
                              {.rva = info.relocation_directory_rva,
                               .size = info.relocation_directory_size},
                              delta);
        if (relocation.status != MapStatus::Success) {
            munmap(mapping, mapping_size);
            return fail(MapStatus::InvalidImage, relocation.error_message);
        }
        applied_relocations = relocation.applied;
    }

    const std::uint64_t header_protect_size = align_up(
        std::min<std::uint64_t>(static_cast<std::uint64_t>(info.size_of_headers), mapping_size_u64),
        page);
    if (header_protect_size > 0 && mprotect(mapping, header_protect_size, PROT_READ) != 0) {
        const int error = errno;
        munmap(mapping, mapping_size);
        return fail(MapStatus::OutOfMemory,
                    "não foi possível proteger os headers da imagem (errno=" +
                        std::to_string(error) + ")");
    }
    for (const MapRegion& region : regions) {
        const std::uint64_t page_start = align_down(region.rva, page);
        const std::uint64_t page_end = align_up(static_cast<std::uint64_t>(region.rva) + region.size, page);
        if (page_start >= mapping_size_u64) {
            continue;
        }
        const std::size_t protect_size =
            static_cast<std::size_t>(std::min(page_end, mapping_size_u64) - page_start);
        const SectionPermissions page_permissions =
            permissions_for_page(regions, page_start, page_start + protect_size);
        if (mprotect(static_cast<std::byte*>(mapping) + static_cast<std::ptrdiff_t>(page_start),
                     protect_size, to_prot(page_permissions)) != 0) {
            const int error = errno;
            munmap(mapping, mapping_size);
            return fail(MapStatus::OutOfMemory,
                        "não foi possível proteger a seção " + region.name + " (errno=" +
                            std::to_string(error) + ")");
        }
    }

    MappedImage image{
        .preferred_base = preferred_base,
        .base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(mapping)),
        .size = mapping_size,
        .memory = memory,
        .delta = delta,
        .has_relocation_directory = has_relocation_directory,
        .applied_relocations = applied_relocations,
        .regions = std::move(regions),
    };
    return {.status = MapStatus::Success, .error_message = {}, .image = std::move(image)};
}

void unmap_image(MappedImage& image) {
    if (image.memory != nullptr && image.size > 0) {
        munmap(image.memory, image.size);
    }
    image.memory = nullptr;
    image.size = 0;
    image.base = 0;
    image.preferred_base = 0;
    image.delta = 0;
    image.has_relocation_directory = false;
    image.applied_relocations = 0;
    image.regions.clear();
}

PatchStatus write_image_bytes(MappedImage& image, const std::uint32_t rva,
                              const std::byte* data, const std::size_t size) {
    if (image.memory == nullptr || size == 0) {
        return PatchStatus::InvalidAddress;
    }
    const MapRegion* covering = nullptr;
    for (const MapRegion& region : image.regions) {
        const std::uint64_t start = region.rva;
        const std::uint64_t end = static_cast<std::uint64_t>(region.rva) + region.size;
        if (static_cast<std::uint64_t>(rva) >= start &&
            static_cast<std::uint64_t>(rva) + size <= end) {
            covering = &region;
            break;
        }
    }
    if (covering == nullptr) {
        return PatchStatus::InvalidAddress;
    }
    const std::size_t page = util::host_page_size();
    const std::uint64_t page_start = align_down(rva, page);
    std::uint64_t page_end =
        align_up(static_cast<std::uint64_t>(rva) + size, page);
    // Clamp ao tamanho real do mapeamento (evita mprotect além do mapping - M8).
    if (page_end > static_cast<std::uint64_t>(image.size)) {
        page_end = align_up(static_cast<std::uint64_t>(image.size), page);
        if (page_start >= page_end) {
            return PatchStatus::InvalidAddress;
        }
    }
    auto* page_base = image.memory + static_cast<std::ptrdiff_t>(page_start);
    const std::size_t page_size = static_cast<std::size_t>(page_end - page_start);
    if (mprotect(page_base, page_size, PROT_READ | PROT_WRITE) != 0) {
        return PatchStatus::MprotectFailed;
    }
    std::memcpy(image.memory + static_cast<std::ptrdiff_t>(rva), data, size);
    const SectionPermissions page_permissions = permissions_for_page(
        image.regions, page_start, page_end);
    if (mprotect(page_base, page_size, to_prot(page_permissions)) != 0) {
        // Tenta ao menos voltar para somente leitura para não deixar RWX/RW exposto (M10).
        (void)mprotect(page_base, page_size, PROT_READ);
        return PatchStatus::MprotectFailed;
    }
    return PatchStatus::Success;
}

}  // namespace tradutorlinux::loader
