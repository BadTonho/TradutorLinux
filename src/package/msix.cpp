#include "tradutorlinux/package/msix.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <new>
#include <string_view>
#include <vector>

namespace tradutorlinux::package {

namespace {

constexpr std::uint32_t kZipLocalHeaderMagic = 0x04034b50;
constexpr std::uint32_t kZipCentralHeaderMagic = 0x02014b50;
constexpr std::uint64_t kMaxPackageFileSize = 2ULL * 1024 * 1024 * 1024; // 2 GiB
constexpr std::size_t kMaxManifestSize = 16U * 1024U * 1024U; // 16 MiB
constexpr std::uint32_t kMaxZipEntries = 10000;
constexpr std::size_t kMaxZipFilenameSize = 4096;
constexpr std::uint64_t kMaxZipEntryUncompressedSize = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxPackageUncompressedSize = 512ULL * 1024ULL * 1024ULL;

[[nodiscard]] bool safe_zip_filename(const std::string_view filename) noexcept {
    if (filename.empty() || filename.size() > kMaxZipFilenameSize ||
        filename.front() == '/' || filename.front() == '\\' ||
        (filename.size() >= 2 && std::isalpha(static_cast<unsigned char>(filename[0])) != 0 &&
         filename[1] == ':')) {
        return false;
    }
    std::size_t segment_start = 0;
    while (segment_start <= filename.size()) {
        const std::size_t separator = filename.find_first_of("/\\", segment_start);
        const std::size_t segment_length = separator == std::string_view::npos
                                               ? filename.size() - segment_start
                                               : separator - segment_start;
        if (filename.substr(segment_start, segment_length) == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        segment_start = separator + 1;
    }
    return true;
}

std::string extract_xml_attribute(std::string_view tag, std::string_view attr_name) {
    const std::string pattern = std::string(attr_name) + "=\"";
    const std::size_t pos = tag.find(pattern);
    if (pos == std::string_view::npos) {
        return "";
    }
    const std::size_t val_start = pos + pattern.size();
    const std::size_t val_end = tag.find('"', val_start);
    if (val_end == std::string_view::npos) {
        return "";
    }
    return std::string(tag.substr(val_start, val_end - val_start));
}

}  // namespace

bool is_msix_or_appx_package(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    std::string lower_ext;
    lower_ext.reserve(ext.size());
    for (const char c : ext) {
        lower_ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower_ext != ".msix" && lower_ext != ".appx" && lower_ext != ".msixbundle" && lower_ext != ".appxbundle") {
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::uint32_t magic = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    return stream.good() && magic == kZipLocalHeaderMagic;
}

std::optional<AppxPackageInfo> parse_appx_manifest_xml(const std::string_view xml_content) {
    if (xml_content.empty() || xml_content.size() > kMaxManifestSize) {
        return std::nullopt;
    }
    AppxPackageInfo info;

    // Parse <Identity ... />
    const std::size_t identity_pos = xml_content.find("<Identity");
    if (identity_pos != std::string_view::npos) {
        const std::size_t identity_end = xml_content.find('>', identity_pos);
        if (identity_end != std::string_view::npos) {
            const std::string_view identity_tag =
                xml_content.substr(identity_pos, identity_end - identity_pos + 1);
            info.package_name = extract_xml_attribute(identity_tag, "Name");
            info.publisher = extract_xml_attribute(identity_tag, "Publisher");
            info.version = extract_xml_attribute(identity_tag, "Version");
        }
    }

    // Parse <Application ... >
    std::size_t app_search_pos = 0;
    while ((app_search_pos = xml_content.find("<Application", app_search_pos)) != std::string_view::npos) {
        if (app_search_pos + 12 < xml_content.size() && xml_content[app_search_pos + 12] == 's') {
            // Matched <Applications> container, skip it
            app_search_pos += 13;
            continue;
        }
        const std::size_t app_end = xml_content.find('>', app_search_pos);
        if (app_end == std::string_view::npos) {
            break;
        }
        const std::string_view app_tag =
            xml_content.substr(app_search_pos, app_end - app_search_pos + 1);
        AppxApplication app;
        app.id = extract_xml_attribute(app_tag, "Id");
        app.executable = extract_xml_attribute(app_tag, "Executable");
        app.entry_point = extract_xml_attribute(app_tag, "EntryPoint");

        // Look for display name inside this application element
        const std::size_t app_close = xml_content.find("</Application>", app_end);
        if (app_close != std::string_view::npos && app_close > app_end) {
            const std::string_view app_body = xml_content.substr(app_end, app_close - app_end);
            const std::size_t vis_pos = app_body.find("VisualElements");
            if (vis_pos != std::string_view::npos) {
                const std::size_t vis_end = app_body.find('>', vis_pos);
                if (vis_end != std::string_view::npos) {
                    const std::string_view vis_tag =
                        app_body.substr(vis_pos, vis_end - vis_pos + 1);
                    app.display_name = extract_xml_attribute(vis_tag, "DisplayName");
                }
            }
        }
        if (!app.executable.empty() || !app.id.empty()) {
            if (!info.main_executable.has_value() && !app.executable.empty()) {
                info.main_executable = app.executable;
            }
            info.applications.push_back(std::move(app));
        }
        app_search_pos = (app_close != std::string_view::npos) ? (app_close + 14) : (app_end + 1);
    }

    return info;
}

std::optional<AppxPackageInfo> inspect_msix_package(const std::filesystem::path& package_path) {
    try {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(package_path, filesystem_error) || filesystem_error) {
            return std::nullopt;
        }
        const std::uintmax_t file_size = std::filesystem::file_size(package_path, filesystem_error);
        if (filesystem_error || file_size == 0 || file_size > kMaxPackageFileSize) {
            return std::nullopt;
        }

        std::ifstream stream(package_path, std::ios::binary);
        if (!stream) {
            return std::nullopt;
        }

        // Traverse ZIP local headers looking for AppxManifest.xml. The entry
        // limit prevents a package made of millions of tiny headers from
        // turning inspection into an unbounded CPU operation.
        std::uint32_t entry_count = 0;
        std::uint64_t total_uncompressed_size = 0;
        while (stream && entry_count < kMaxZipEntries) {
            std::uint32_t signature = 0;
            stream.read(reinterpret_cast<char*>(&signature), sizeof(signature));
            if (!stream || signature == kZipCentralHeaderMagic) {
                break;
            }
            if (signature != kZipLocalHeaderMagic) {
                break;
            }
            ++entry_count;

            std::uint16_t version = 0;
            std::uint16_t flags = 0;
            std::uint16_t compression_method = 0;
            std::uint16_t mod_time = 0;
            std::uint16_t mod_date = 0;
            std::uint32_t crc32 = 0;
            std::uint32_t compressed_size = 0;
            std::uint32_t uncompressed_size = 0;
            std::uint16_t filename_len = 0;
            std::uint16_t extra_len = 0;

            stream.read(reinterpret_cast<char*>(&version), 2);
            stream.read(reinterpret_cast<char*>(&flags), 2);
            stream.read(reinterpret_cast<char*>(&compression_method), 2);
            stream.read(reinterpret_cast<char*>(&mod_time), 2);
            stream.read(reinterpret_cast<char*>(&mod_date), 2);
            stream.read(reinterpret_cast<char*>(&crc32), 4);
            stream.read(reinterpret_cast<char*>(&compressed_size), 4);
            stream.read(reinterpret_cast<char*>(&uncompressed_size), 4);
            stream.read(reinterpret_cast<char*>(&filename_len), 2);
            stream.read(reinterpret_cast<char*>(&extra_len), 2);

            if (!stream || filename_len == 0 || filename_len > kMaxZipFilenameSize ||
                static_cast<std::uint64_t>(uncompressed_size) > kMaxZipEntryUncompressedSize ||
                static_cast<std::uint64_t>(uncompressed_size) >
                    kMaxPackageUncompressedSize - total_uncompressed_size) {
                break;
            }
            total_uncompressed_size += uncompressed_size;

            const std::streamoff after_header = stream.tellg();
            if (after_header < 0 || static_cast<std::uintmax_t>(after_header) > file_size) {
                break;
            }
            const std::uintmax_t metadata_size = static_cast<std::uintmax_t>(filename_len) + extra_len;
            if (metadata_size > file_size - static_cast<std::uintmax_t>(after_header)) {
                break;
            }

            std::string filename(filename_len, '\0');
            stream.read(filename.data(), filename_len);
            if (!stream || !safe_zip_filename(filename)) {
                break;
            }

            // Skip extra field
            if (extra_len > 0) {
                stream.seekg(extra_len, std::ios::cur);
            }

            const std::streamoff after_metadata = stream.tellg();
            if (after_metadata < 0 || static_cast<std::uintmax_t>(after_metadata) > file_size) {
                break;
            }
            const std::uintmax_t remaining =
                file_size - static_cast<std::uintmax_t>(after_metadata);
            // Bit 3 means that sizes are supplied by a data descriptor after
            // the payload. This inspector does not parse descriptors; refusing
            // the entry is safer than treating a zero size as valid metadata.
            if ((flags & 0x0008U) != 0U || compressed_size > remaining ||
                (compression_method == 0 && compressed_size != uncompressed_size)) {
                break;
            }
            const std::uintmax_t data_size = static_cast<std::uintmax_t>(compressed_size);

            if (filename == "AppxManifest.xml") {
                // Found AppxManifest.xml. Only stored manifests are parsed by
                // this small inspector; compressed manifests remain a
                // recognized package but do not produce fabricated metadata.
                if (compression_method == 0) { // Stored
                    if (uncompressed_size > kMaxManifestSize) {
                        return std::nullopt;
                    }
                    std::string xml_data(static_cast<std::size_t>(uncompressed_size), '\0');
                    if (uncompressed_size > 0) {
                        stream.read(xml_data.data(), uncompressed_size);
                    }
                    if (!stream) {
                        return std::nullopt;
                    }
                    return parse_appx_manifest_xml(xml_data);
                }
                return std::nullopt;
            }

            // Skip entry data. The bounds above make this conversion safe for
            // the stream implementations used by the runtime.
            if (data_size > 0) {
                stream.seekg(static_cast<std::streamoff>(data_size), std::ios::cur);
                if (!stream) {
                    break;
                }
            }
        }

        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

}  // namespace tradutorlinux::package
