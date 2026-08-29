#pragma once

#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::loader {

enum class MapStatus {
    Success,
    InvalidImage,
    OutOfMemory,
};

enum class SectionPermissions {
    None,
    ReadOnly,
    ReadWrite,
    ReadExecute,
    ReadWriteExecute,
};

struct MapRegion {
    std::string name;
    std::uint32_t rva{};
    std::uint32_t size{};
    std::uint32_t raw_data_pointer{};
    std::uint32_t raw_data_size{};
    SectionPermissions permissions{SectionPermissions::None};
};

struct MappedImage {
    std::uint64_t preferred_base{};
    std::uint64_t base{};
    std::size_t size{};
    std::byte* memory{nullptr};
    std::int64_t delta{};
    bool has_relocation_directory{};
    std::size_t applied_relocations{};
    std::vector<MapRegion> regions;
};

struct MapResult {
    MapStatus status{MapStatus::Success};
    std::string error_message;
    MappedImage image;
};

struct MapOptions {
    std::uint64_t preferred_base{};
};

struct RelocationResult {
    MapStatus status{MapStatus::Success};
    std::string error_message;
    std::size_t applied{};
};

struct RelocationDirectory {
    std::uint32_t rva{};
    std::uint32_t size{};
};

// Applies base relocations to an already copied image whose memory starts at
// RVA 0. `image` must span at least the relocation targets. `delta` is the
// difference between the actual base and the preferred base. Pure algorithm
// with no OS calls, exposed for deterministic testing.
[[nodiscard]] RelocationResult apply_relocations(std::span<std::byte> image,
                                                 const RelocationDirectory& directory,
                                                 std::int64_t delta);

// Reserves a private mapping (Linux mmap), copies headers and section raw data,
// applies base relocations when the base differs from the preferred one and
// protects each region according to section characteristics. The image is never
// left fully RWX: a section requesting execute+write is downgraded to read-write.
[[nodiscard]] MapResult map_image(const pe::PeInfo& info, std::span<const std::byte> file_bytes,
                                  const MapOptions& options = {});

void unmap_image(MappedImage& image);

// Permissões efetivas da página do host que contém o RVA informado, levando em
// conta a fusão de atributos quando regiões compartilham a mesma página
// (escrita > execução > leitura). É a permissão real após mprotect, não apenas
// a característica da seção.
[[nodiscard]] SectionPermissions effective_page_permissions(const MappedImage& image,
                                                            std::uint32_t rva);

enum class PatchStatus {
    Success,
    InvalidAddress,
    MprotectFailed,
};

// Writes `size` bytes into the mapped image at the given RVA, temporarily
// relaxing the covering page permissions to read-write and restoring the
// region permissions afterwards. Used to fill the import address table, which
// lives in read-only image regions.
[[nodiscard]] PatchStatus write_image_bytes(MappedImage& image, std::uint32_t rva,
                                            const std::byte* data, std::size_t size);

}  // namespace tradutorlinux::loader
