#include "tradutorlinux/package/msix.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

namespace tradutorlinux::package {

namespace {

constexpr std::uint32_t kZipLocalHeaderMagic = 0x04034b50;
constexpr std::uint32_t kZipCentralHeaderMagic = 0x02014b50;
constexpr std::uint64_t kMaxPackageFileSize = 2ULL * 1024 * 1024 * 1024; // 2 GiB

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
    if (xml_content.empty()) {
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
    if (!std::filesystem::exists(package_path)) {
        return std::nullopt;
    }
    const std::uintmax_t file_size = std::filesystem::file_size(package_path);
    if (file_size == 0 || file_size > kMaxPackageFileSize) {
        return std::nullopt;
    }

    std::ifstream stream(package_path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }

    // Traverse ZIP local headers looking for AppxManifest.xml
    while (stream) {
        std::uint32_t signature = 0;
        stream.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        if (!stream || signature != kZipLocalHeaderMagic) {
            break;
        }

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

        if (!stream) {
            break;
        }

        std::string filename(filename_len, '\0');
        if (filename_len > 0) {
            stream.read(filename.data(), filename_len);
        }

        // Skip extra field
        if (extra_len > 0) {
            stream.seekg(extra_len, std::ios::cur);
        }

        if (filename == "AppxManifest.xml") {
            // Found AppxManifest.xml
            if (compression_method == 0) { // Stored
                std::string xml_data(uncompressed_size, '\0');
                if (uncompressed_size > 0) {
                    stream.read(xml_data.data(), uncompressed_size);
                }
                return parse_appx_manifest_xml(xml_data);
            }
            // If compressed or not directly readable stored, return placeholder info
            AppxPackageInfo placeholder;
            placeholder.package_name = package_path.stem().string();
            return placeholder;
        }

        // Skip compressed data
        if (compressed_size > 0) {
            stream.seekg(compressed_size, std::ios::cur);
        }
    }

    return std::nullopt;
}

}  // namespace tradutorlinux::package

