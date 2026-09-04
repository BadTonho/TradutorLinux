#pragma once

#include "tradutorlinux/runtime/winapi.hpp"
#include "tradutorlinux/runtime/advapi.hpp"
#include "tradutorlinux/runtime/comctl32.hpp"
#include "tradutorlinux/runtime/dialog_template.hpp"
#include "tradutorlinux/runtime/ole32.hpp"
#include "tradutorlinux/runtime/oleaut32.hpp"
#include "tradutorlinux/runtime/wininet.hpp"
#include "tradutorlinux/runtime/wintrust.hpp"
#include "tradutorlinux/runtime/crypt32.hpp"
#include "tradutorlinux/runtime/mpr.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ws2_32.hpp"
#include "tradutorlinux/runtime/dwmapi.hpp"
#include "tradutorlinux/runtime/version.hpp"
#include "tradutorlinux/package/msix.hpp"
#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "../src/runtime/core/runtime_context.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>
#include <unistd.h>
#include <sys/stat.h>

namespace tradutorlinux {

inline constexpr std::uint32_t kWNetNoMoreEntries = 259U;
inline constexpr std::uint32_t kRegisteredClipboardFormat = 0xC002U;

inline void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

inline void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value & 0xFFFFU));
    append_u16(bytes, static_cast<std::uint16_t>(value >> 16U));
}

inline void append_bytes(std::vector<std::byte>& bytes, const std::string_view value) {
    const auto* const begin = reinterpret_cast<const std::byte*>(value.data());
    bytes.insert(bytes.end(), begin, begin + value.size());
}

inline std::vector<std::byte> raw_deflate(const std::string_view input) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<std::byte> compressed(compressBound(static_cast<uLong>(input.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    stream.avail_out = static_cast<uInt>(compressed.size());
    const int result = deflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END;
    const std::size_t written = static_cast<std::size_t>(stream.total_out);
    deflateEnd(&stream);
    if (!valid) return {};
    compressed.resize(written);
    return compressed;
}

inline bool write_deflated_msix(const std::filesystem::path& path,
                                const std::string_view manifest) {
    const std::string filename = "AppxManifest.xml";
    const std::vector<std::byte> compressed = raw_deflate(manifest);
    if (compressed.empty()) return false;
    const std::uint32_t checksum = static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(manifest.data()),
              static_cast<uInt>(manifest.size())));
    std::vector<std::byte> bytes;
    append_u32(bytes, 0x04034B50U);
    append_u16(bytes, 20);
    append_u16(bytes, 0x0008U);
    append_u16(bytes, 8);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u16(bytes, static_cast<std::uint16_t>(filename.size()));
    append_u16(bytes, 0);
    append_bytes(bytes, filename);
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    append_u32(bytes, 0x08074B50U);
    append_u32(bytes, checksum);
    append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
    append_u32(bytes, static_cast<std::uint32_t>(manifest.size()));

    const std::uint32_t central_offset = static_cast<std::uint32_t>(bytes.size());
    append_u32(bytes, 0x02014B50U);
    append_u16(bytes, 20);
    append_u16(bytes, 20);
    append_u16(bytes, 0x0008U);
    append_u16(bytes, 8);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u32(bytes, checksum);
    append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
    append_u32(bytes, static_cast<std::uint32_t>(manifest.size()));
    append_u16(bytes, static_cast<std::uint16_t>(filename.size()));
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_bytes(bytes, filename);

    const std::uint32_t central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054B50U);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 1);
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

inline std::vector<std::byte> valid_dialog_template() {
    std::vector<std::byte> bytes;
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 100);
    append_u16(bytes, 40);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 'D');
    append_u16(bytes, 0);
    while ((bytes.size() & 3U) != 0U) bytes.push_back(std::byte{0});
    append_u32(bytes, 0x00010000U);
    append_u32(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 1);
    append_u16(bytes, 80);
    append_u16(bytes, 18);
    append_u16(bytes, 7);
    append_u16(bytes, 0xFFFFU);
    append_u16(bytes, 0x0080U);
    append_u16(bytes, 'O');
    append_u16(bytes, 'K');
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    return bytes;
}

class TempDirFixture {
public:
    TempDirFixture() { mkdir("_tl_test", 0777); }
    ~TempDirFixture() {
        for (const auto& file : created_files_) std::remove(file.c_str());
        rmdir("_tl_test");
    }
    std::string path(const char* name) const {
        const std::string path = std::string("_tl_test/") + name;
        created_files_.push_back(path);
        return path;
    }

private:
    mutable std::vector<std::string> created_files_;
};

using abi::GuestMemoryBasicInformation;

}  // namespace tradutorlinux
