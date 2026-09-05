#include "tradutorlinux/package/msix.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

namespace tradutorlinux::package {

namespace {

constexpr std::uint32_t kZipLocalHeaderMagic = 0x04034b50;
constexpr std::uint32_t kZipCentralHeaderMagic = 0x02014b50;
constexpr std::uint32_t kZipEndOfCentralDirectoryMagic = 0x06054b50;
constexpr std::uint64_t kMaxPackageFileSize = 2ULL * 1024 * 1024 * 1024; // 2 GiB
constexpr std::size_t kMaxManifestSize = 16U * 1024U * 1024U; // 16 MiB
constexpr std::size_t kMaxManifestCompressedSize = 64U * 1024U * 1024U; // 64 MiB
constexpr std::uint32_t kMaxZipEntries = 10000;
constexpr std::size_t kMaxZipFilenameSize = 4096;
constexpr std::uint64_t kMaxZipEntryUncompressedSize = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxPackageUncompressedSize = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxXmlDepth = 128;
constexpr std::size_t kMaxXmlAttributesPerElement = 256;

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

[[nodiscard]] std::uint16_t read_le16(const unsigned char* const data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8U);
}

[[nodiscard]] std::uint32_t read_le32(const unsigned char* const data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

bool read_u32(std::ifstream& stream, std::uint32_t& value) {
    std::array<unsigned char, 4> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return false;
    }
    value = read_le32(bytes.data());
    return true;
}

bool skip_bytes(std::ifstream& stream, const std::uint64_t count,
                const std::uint64_t file_size) {
    const std::streamoff current = stream.tellg();
    if (current < 0 || static_cast<std::uint64_t>(current) > file_size ||
        count > file_size - static_cast<std::uint64_t>(current) ||
        count > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    stream.seekg(static_cast<std::streamoff>(count), std::ios::cur);
    return static_cast<bool>(stream);
}

[[nodiscard]] std::string_view xml_local_name(const std::string_view qualified_name) noexcept {
    const std::size_t separator = qualified_name.rfind(':');
    return separator == std::string_view::npos
               ? qualified_name
               : qualified_name.substr(separator + 1);
}

[[nodiscard]] bool is_xml_name_start(const char value) noexcept {
    const unsigned char character = static_cast<unsigned char>(value);
    return (character >= static_cast<unsigned char>('A') &&
            character <= static_cast<unsigned char>('Z')) ||
           (character >= static_cast<unsigned char>('a') &&
            character <= static_cast<unsigned char>('z')) ||
           value == '_' || value == ':';
}

[[nodiscard]] bool is_xml_name_character(const char value) noexcept {
    const unsigned char character = static_cast<unsigned char>(value);
    return is_xml_name_start(value) ||
           (character >= static_cast<unsigned char>('0') &&
            character <= static_cast<unsigned char>('9')) ||
           value == '-' || value == '.';
}

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] std::optional<std::string> decode_xml_entities(
    const std::string_view value) {
    if (value.find('&') == std::string_view::npos) {
        return std::string(value);
    }

    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t position = 0; position < value.size();) {
        if (value[position] != '&') {
            decoded.push_back(value[position++]);
            continue;
        }
        const std::size_t end = value.find(';', position + 1);
        if (end == std::string_view::npos || end == position + 1 ||
            end - position > 16U) {
            return std::nullopt;
        }
        const std::string_view entity = value.substr(position + 1, end - position - 1);
        if (entity == "amp") {
            decoded.push_back('&');
        } else if (entity == "lt") {
            decoded.push_back('<');
        } else if (entity == "gt") {
            decoded.push_back('>');
        } else if (entity == "quot") {
            decoded.push_back('"');
        } else if (entity == "apos") {
            decoded.push_back('\'');
        } else if (entity.size() >= 2U && entity.front() == '#') {
            std::uint32_t code_point = 0;
            std::size_t digit_start = 1;
            int base = 10;
            if (entity.size() >= 3U && (entity[1] == 'x' || entity[1] == 'X')) {
                base = 16;
                digit_start = 2;
            }
            if (digit_start == entity.size()) {
                return std::nullopt;
            }
            for (std::size_t digit = digit_start; digit < entity.size(); ++digit) {
                const unsigned char character = static_cast<unsigned char>(entity[digit]);
                std::uint32_t value_digit = 0;
                if (character >= static_cast<unsigned char>('0') &&
                    character <= static_cast<unsigned char>('9')) {
                    value_digit = character - static_cast<unsigned char>('0');
                } else if (base == 16 && character >= static_cast<unsigned char>('a') &&
                           character <= static_cast<unsigned char>('f')) {
                    value_digit = character - static_cast<unsigned char>('a') + 10U;
                } else if (base == 16 && character >= static_cast<unsigned char>('A') &&
                           character <= static_cast<unsigned char>('F')) {
                    value_digit = character - static_cast<unsigned char>('A') + 10U;
                } else {
                    return std::nullopt;
                }
                if (value_digit >= static_cast<std::uint32_t>(base) ||
                    code_point > (0x10FFFFU - value_digit) /
                                     static_cast<std::uint32_t>(base)) {
                    return std::nullopt;
                }
                code_point = code_point * static_cast<std::uint32_t>(base) + value_digit;
            }
            if (code_point == 0 || code_point > 0x10FFFFU ||
                (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
                return std::nullopt;
            }
            append_utf8(decoded, code_point);
        } else {
            return std::nullopt;
        }
        position = end + 1;
    }
    return decoded;
}

struct XmlAttribute {
    std::string qualified_name;
    std::string value;
};

struct XmlNode {
    std::string qualified_name;
    std::optional<std::size_t> application_index;
};

class AppxManifestParser {
public:
    explicit AppxManifestParser(const std::string_view xml) : xml_(xml) {}

    [[nodiscard]] std::optional<AppxPackageInfo> parse() {
        if (xml_.size() >= 3U && static_cast<unsigned char>(xml_[0]) == 0xEFU &&
            static_cast<unsigned char>(xml_[1]) == 0xBBU &&
            static_cast<unsigned char>(xml_[2]) == 0xBFU) {
            position_ = 3U;
        }
        while (position_ < xml_.size()) {
            if (xml_[position_] != '<') {
                const std::size_t text_end = xml_.find('<', position_);
                const std::size_t end = text_end == std::string_view::npos
                                            ? xml_.size()
                                            : text_end;
                if (root_closed_) {
                    for (std::size_t text_position = position_; text_position < end;
                         ++text_position) {
                        if (!is_xml_space(xml_[text_position])) return std::nullopt;
                    }
                }
                if (xml_.substr(position_, end - position_).find('&') !=
                        std::string_view::npos &&
                    !decode_xml_entities(xml_.substr(position_, end - position_)).has_value()) {
                    return std::nullopt;
                }
                position_ = end;
                continue;
            }

            if (xml_.substr(position_).starts_with("<!--")) {
                if (!skip_until("-->", 4U)) return std::nullopt;
                continue;
            }
            if (xml_.substr(position_).starts_with("<![CDATA[")) {
                if (!skip_until("]]>", 9U)) return std::nullopt;
                continue;
            }
            if (xml_.substr(position_).starts_with("<?")) {
                if (!skip_until("?>", 2U)) return std::nullopt;
                continue;
            }
            if (xml_.substr(position_).starts_with("<!")) {
                // DTDs and entity declarations are deliberately unsupported:
                // the inspector never fetches external resources or expands
                // entities supplied by the package.
                return std::nullopt;
            }
            if (xml_.substr(position_).starts_with("</")) {
                if (!parse_end_element()) return std::nullopt;
                continue;
            }
            if (!parse_start_element()) return std::nullopt;
        }

        if (!root_seen_ || !root_closed_ || !stack_.empty() || root_name_ != "Package") {
            return std::nullopt;
        }
        return info_;
    }

private:
    [[nodiscard]] static bool is_xml_space(const char value) noexcept {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }

    static void skip_space(const std::string_view xml, std::size_t& position) noexcept {
        while (position < xml.size()) {
            const unsigned char character = static_cast<unsigned char>(xml[position]);
            if (character != static_cast<unsigned char>(' ') &&
                character != static_cast<unsigned char>('\t') &&
                character != static_cast<unsigned char>('\r') &&
                character != static_cast<unsigned char>('\n')) {
                break;
            }
            ++position;
        }
    }

    [[nodiscard]] std::optional<std::string> parse_name() {
        if (position_ >= xml_.size() || !is_xml_name_start(xml_[position_])) {
            return std::nullopt;
        }
        const std::size_t start = position_++;
        while (position_ < xml_.size() && is_xml_name_character(xml_[position_])) {
            ++position_;
        }
        return std::string(xml_.substr(start, position_ - start));
    }

    [[nodiscard]] bool skip_until(const std::string_view terminator,
                                  const std::size_t prefix_size) {
        const std::size_t end = xml_.find(terminator, position_ + prefix_size);
        if (end == std::string_view::npos) return false;
        position_ = end + terminator.size();
        return true;
    }

    [[nodiscard]] std::optional<std::string> attribute_value(
        const std::vector<XmlAttribute>& attributes,
        const std::string_view local_name) const {
        for (const XmlAttribute& attribute : attributes) {
            if (xml_local_name(attribute.qualified_name) == local_name) {
                return attribute.value;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool parse_end_element() {
        position_ += 2U;
        const auto name = parse_name();
        if (!name.has_value()) return false;
        skip_space(xml_, position_);
        if (position_ >= xml_.size() || xml_[position_] != '>' || stack_.empty() ||
            stack_.back().qualified_name != *name) {
            return false;
        }
        ++position_;
        stack_.pop_back();
        if (stack_.empty()) root_closed_ = true;
        return true;
    }

    [[nodiscard]] bool parse_start_element() {
        if (root_closed_ || stack_.size() >= kMaxXmlDepth) return false;
        ++position_;
        const auto name = parse_name();
        if (!name.has_value()) return false;

        std::vector<XmlAttribute> attributes;
        bool self_closing = false;
        while (true) {
            skip_space(xml_, position_);
            if (position_ >= xml_.size()) return false;
            if (xml_[position_] == '>') {
                ++position_;
                break;
            }
            if (xml_[position_] == '/') {
                self_closing = true;
                ++position_;
                skip_space(xml_, position_);
                if (position_ >= xml_.size() || xml_[position_] != '>') return false;
                ++position_;
                break;
            } else {
                const auto attribute_name = parse_name();
                if (!attribute_name.has_value() || attributes.size() >= kMaxXmlAttributesPerElement) {
                    return false;
                }
                skip_space(xml_, position_);
                if (position_ >= xml_.size() || xml_[position_] != '=') return false;
                ++position_;
                skip_space(xml_, position_);
                if (position_ >= xml_.size() ||
                    (xml_[position_] != '\'' && xml_[position_] != '"')) {
                    return false;
                }
                const char quote = xml_[position_++];
                const std::size_t value_start = position_;
                const std::size_t value_end = xml_.find(quote, value_start);
                if (value_end == std::string_view::npos ||
                    xml_.substr(value_start, value_end - value_start).find('<') !=
                        std::string_view::npos) {
                    return false;
                }
                const auto decoded = decode_xml_entities(
                    xml_.substr(value_start, value_end - value_start));
                if (!decoded.has_value()) return false;
                for (const XmlAttribute& existing : attributes) {
                    if (existing.qualified_name == *attribute_name) return false;
                }
                attributes.push_back(XmlAttribute{*attribute_name, *decoded});
                position_ = value_end + 1U;
                continue;
            }
        }

        XmlNode node{*name, stack_.empty() ? std::nullopt : stack_.back().application_index};
        const std::string_view local_name = xml_local_name(*name);
        if (stack_.empty()) {
            if (root_seen_) return false;
            root_seen_ = true;
            root_name_ = std::string(local_name);
        }
        if (local_name == "Identity") {
            if (const auto value = attribute_value(attributes, "Name")) info_.package_name = *value;
            if (const auto value = attribute_value(attributes, "Publisher")) info_.publisher = *value;
            if (const auto value = attribute_value(attributes, "Version")) info_.version = *value;
        } else if (local_name == "Application") {
            AppxApplication application;
            application.id = attribute_value(attributes, "Id").value_or("");
            application.executable = attribute_value(attributes, "Executable").value_or("");
            application.entry_point = attribute_value(attributes, "EntryPoint").value_or("");
            if (!application.executable.empty() || !application.id.empty()) {
                if (!info_.main_executable.has_value() && !application.executable.empty()) {
                    info_.main_executable = application.executable;
                }
                info_.applications.push_back(std::move(application));
                node.application_index = info_.applications.size() - 1U;
            }
        } else if (local_name == "VisualElements" && node.application_index.has_value()) {
            const auto display_name = attribute_value(attributes, "DisplayName");
            if (display_name.has_value()) {
                info_.applications[*node.application_index].display_name = *display_name;
            }
        }
        if (!self_closing) stack_.push_back(std::move(node));
        else if (stack_.empty()) root_closed_ = true;
        return true;
    }

    std::string_view xml_;
    std::size_t position_ = 0;
    std::string root_name_;
    bool root_seen_ = false;
    bool root_closed_ = false;
    std::vector<XmlNode> stack_;
    AppxPackageInfo info_;
};

struct ZipCentralEntry {
    std::uint16_t flags = 0;
    std::uint16_t compression_method = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
};

[[nodiscard]] bool crc_matches(const std::vector<unsigned char>& data,
                               const std::uint32_t expected) noexcept {
    const uLong actual = crc32(0L, Z_NULL, 0);
    const uLong computed = crc32(actual, data.data(), static_cast<uInt>(data.size()));
    return static_cast<std::uint32_t>(computed) == expected;
}

[[nodiscard]] std::optional<std::vector<unsigned char>> inflate_raw(
    const std::vector<unsigned char>& compressed, const std::size_t expected_size) {
    if (compressed.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
        expected_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        return std::nullopt;
    }
    std::vector<unsigned char> decompressed(expected_size);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = decompressed.data();
    stream.avail_out = static_cast<uInt>(decompressed.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return std::nullopt;
    const int result = inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.avail_in == 0 &&
                       stream.total_out == expected_size;
    inflateEnd(&stream);
    if (!valid) return std::nullopt;
    return decompressed;
}

struct ZipArchiveEntry {
    std::string name;
    ZipCentralEntry metadata;
};

struct ZipArchive {
    std::uint64_t file_size{};
    std::vector<ZipArchiveEntry> entries;
};

[[nodiscard]] std::string normalized_zip_name(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

[[nodiscard]] std::optional<ZipArchive> read_zip_archive(
    const std::filesystem::path& package_path) {
    try {
        std::error_code filesystem_error;
        if (!std::filesystem::is_regular_file(package_path, filesystem_error) ||
            filesystem_error) {
            return std::nullopt;
        }
        const std::uintmax_t file_size = std::filesystem::file_size(package_path, filesystem_error);
        if (filesystem_error || file_size < 22U || file_size > kMaxPackageFileSize) {
            return std::nullopt;
        }

        std::ifstream stream(package_path, std::ios::binary);
        if (!stream) return std::nullopt;

        const std::uintmax_t tail_size = std::min<std::uintmax_t>(file_size, 22U + 65535U);
        std::vector<unsigned char> tail(static_cast<std::size_t>(tail_size));
        stream.seekg(static_cast<std::streamoff>(file_size - tail_size), std::ios::beg);
        stream.read(reinterpret_cast<char*>(tail.data()),
                    static_cast<std::streamsize>(tail.size()));
        if (!stream) return std::nullopt;

        std::size_t eocd_position = std::string_view::npos;
        for (std::size_t position = tail.size() - 22U;; --position) {
            if (read_le32(tail.data() + position) == kZipEndOfCentralDirectoryMagic &&
                position + 22U + read_le16(tail.data() + position + 20U) <= tail.size()) {
                eocd_position = position;
                break;
            }
            if (position == 0) break;
        }
        if (eocd_position == std::string_view::npos) return std::nullopt;

        const std::uint16_t disk_number = read_le16(tail.data() + eocd_position + 4U);
        const std::uint16_t central_disk = read_le16(tail.data() + eocd_position + 6U);
        const std::uint16_t entries_on_disk = read_le16(tail.data() + eocd_position + 8U);
        const std::uint16_t total_entries = read_le16(tail.data() + eocd_position + 10U);
        const std::uint32_t central_size = read_le32(tail.data() + eocd_position + 12U);
        const std::uint32_t central_offset = read_le32(tail.data() + eocd_position + 16U);
        const std::uint64_t eocd_offset = file_size - tail_size + eocd_position;
        if (disk_number != 0 || central_disk != 0 || entries_on_disk != total_entries ||
            total_entries == 0 || total_entries > kMaxZipEntries ||
            central_offset > file_size || central_size > file_size - central_offset ||
            static_cast<std::uint64_t>(central_offset) + central_size > eocd_offset) {
            return std::nullopt;
        }

        ZipArchive archive;
        archive.file_size = file_size;
        archive.entries.reserve(total_entries);
        std::set<std::string> names;
        std::uint64_t total_uncompressed_size = 0;
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(central_offset), std::ios::beg);
        if (!stream) return std::nullopt;

        for (std::uint16_t index = 0; index < total_entries; ++index) {
            std::array<unsigned char, 46> central_header{};
            stream.read(reinterpret_cast<char*>(central_header.data()),
                        static_cast<std::streamsize>(central_header.size()));
            if (!stream || read_le32(central_header.data()) != kZipCentralHeaderMagic) {
                return std::nullopt;
            }
            const std::uint16_t version_made_by = read_le16(central_header.data() + 4U);
            const std::uint16_t flags = read_le16(central_header.data() + 8U);
            const std::uint16_t compression_method = read_le16(central_header.data() + 10U);
            const std::uint32_t entry_crc32 = read_le32(central_header.data() + 16U);
            const std::uint32_t compressed_size = read_le32(central_header.data() + 20U);
            const std::uint32_t uncompressed_size = read_le32(central_header.data() + 24U);
            const std::uint16_t filename_len = read_le16(central_header.data() + 28U);
            const std::uint16_t extra_len = read_le16(central_header.data() + 30U);
            const std::uint16_t comment_len = read_le16(central_header.data() + 32U);
            const std::uint16_t disk_start = read_le16(central_header.data() + 34U);
            const std::uint32_t external_attributes = read_le32(central_header.data() + 38U);
            const std::uint32_t local_header_offset = read_le32(central_header.data() + 42U);
            const std::uint64_t metadata_size = static_cast<std::uint64_t>(filename_len) +
                                                extra_len + comment_len;
            const std::streamoff current = stream.tellg();
            const std::uint32_t unix_mode = external_attributes >> 16U;
            if (current < 0 || static_cast<std::uint64_t>(current) > eocd_offset ||
                metadata_size > eocd_offset - static_cast<std::uint64_t>(current) ||
                filename_len == 0 || filename_len > kMaxZipFilenameSize ||
                disk_start != 0 || (flags & 0x0001U) != 0U ||
                (compression_method != 0 && compression_method != 8) ||
                compressed_size == 0xFFFFFFFFU || uncompressed_size == 0xFFFFFFFFU ||
                local_header_offset >= central_offset ||
                static_cast<std::uint64_t>(compressed_size) > kMaxPackageUncompressedSize ||
                static_cast<std::uint64_t>(uncompressed_size) > kMaxZipEntryUncompressedSize ||
                total_uncompressed_size > kMaxPackageUncompressedSize -
                                             std::min<std::uint64_t>(uncompressed_size,
                                                                     kMaxPackageUncompressedSize) ||
                ((version_made_by >> 8U) == 3U && (unix_mode & 0170000U) == 0120000U)) {
                return std::nullopt;
            }

            std::string filename(filename_len, '\0');
            stream.read(filename.data(), filename_len);
            if (!stream || !safe_zip_filename(filename) || !names.insert(filename).second) {
                return std::nullopt;
            }
            if (!skip_bytes(stream, static_cast<std::uint64_t>(extra_len) + comment_len,
                            file_size)) {
                return std::nullopt;
            }
            total_uncompressed_size += uncompressed_size;
            archive.entries.push_back(
                ZipArchiveEntry{std::move(filename),
                                ZipCentralEntry{flags, compression_method, entry_crc32,
                                                compressed_size, uncompressed_size,
                                                local_header_offset}});
        }

        const std::streamoff central_end = stream.tellg();
        if (central_end < 0 || static_cast<std::uint64_t>(central_end) !=
                                   static_cast<std::uint64_t>(central_offset) + central_size) {
            return std::nullopt;
        }
        return archive;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::vector<unsigned char>> read_zip_entry(
    const std::filesystem::path& package_path, const ZipArchive& archive,
    const ZipArchiveEntry& archive_entry) {
    const ZipCentralEntry& entry = archive_entry.metadata;
    if (entry.compressed_size > kMaxPackageUncompressedSize ||
        entry.uncompressed_size > kMaxZipEntryUncompressedSize ||
        entry.local_header_offset >= archive.file_size) {
        return std::nullopt;
    }
    std::ifstream stream(package_path, std::ios::binary);
    if (!stream) return std::nullopt;
    stream.seekg(static_cast<std::streamoff>(entry.local_header_offset), std::ios::beg);
    if (!stream) return std::nullopt;
    std::uint32_t local_signature = 0;
    if (!read_u32(stream, local_signature) || local_signature != kZipLocalHeaderMagic) {
        return std::nullopt;
    }
    std::array<unsigned char, 26> local_header{};
    stream.read(reinterpret_cast<char*>(local_header.data()),
                static_cast<std::streamsize>(local_header.size()));
    if (!stream) return std::nullopt;
    const std::uint16_t local_flags = read_le16(local_header.data() + 2U);
    const std::uint16_t local_method = read_le16(local_header.data() + 4U);
    const std::uint16_t local_name_len = read_le16(local_header.data() + 22U);
    const std::uint16_t local_extra_len = read_le16(local_header.data() + 24U);
    if (local_method != entry.compression_method ||
        (local_flags & 0x0001U) != 0U || local_name_len != archive_entry.name.size()) {
        return std::nullopt;
    }
    std::string local_name(local_name_len, '\0');
    stream.read(local_name.data(), local_name_len);
    if (!stream || local_name != archive_entry.name ||
        !skip_bytes(stream, local_extra_len, archive.file_size)) {
        return std::nullopt;
    }
    const std::streamoff data_offset = stream.tellg();
    if (data_offset < 0 || static_cast<std::uint64_t>(data_offset) > archive.file_size ||
        entry.compressed_size > archive.file_size - static_cast<std::uint64_t>(data_offset)) {
        return std::nullopt;
    }

    std::vector<unsigned char> compressed(entry.compressed_size);
    if (!compressed.empty()) {
        stream.read(reinterpret_cast<char*>(compressed.data()),
                    static_cast<std::streamsize>(compressed.size()));
        if (!stream) return std::nullopt;
    }
    std::optional<std::vector<unsigned char>> result;
    if (entry.compression_method == 0) {
        if (entry.compressed_size != entry.uncompressed_size) return std::nullopt;
        result = std::move(compressed);
    } else {
        result = inflate_raw(compressed, entry.uncompressed_size);
    }
    if (!result.has_value() || !crc_matches(*result, entry.crc32)) return std::nullopt;
    return result;
}

[[nodiscard]] bool path_has_symlink_component(const std::filesystem::path& root,
                                               const std::filesystem::path& relative) {
    std::error_code error;
    std::filesystem::path current = root;
    for (const auto& component : relative) {
        current /= component;
        if (std::filesystem::is_symlink(current, error)) return true;
        if (error && error != std::make_error_code(std::errc::no_such_file_or_directory)) {
            return true;
        }
        error.clear();
    }
    return false;
}

[[nodiscard]] bool path_is_symlink(const std::filesystem::path& path,
                                   std::error_code& error) {
    const bool result = std::filesystem::is_symlink(path, error);
    if (error == std::make_error_code(std::errc::no_such_file_or_directory)) {
        error.clear();
    }
    return result;
}

[[nodiscard]] bool path_exists(const std::filesystem::path& path, std::error_code& error) {
    const bool result = std::filesystem::exists(path, error);
    if (error == std::make_error_code(std::errc::no_such_file_or_directory)) {
        error.clear();
    }
    return result;
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
    try {
        return AppxManifestParser(xml_content).parse();
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
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

        if (file_size < 22U) {
            return std::nullopt;
        }

        // Read the end-of-central-directory record from the bounded ZIP
        // comment window. Using the central directory makes normal MSIX
        // packages with DEFLATE and data descriptors inspectable without
        // trusting sizes from an unverified local header.
        const std::uintmax_t tail_size = std::min<std::uintmax_t>(file_size, 22U + 65535U);
        std::vector<unsigned char> tail(static_cast<std::size_t>(tail_size));
        stream.seekg(static_cast<std::streamoff>(file_size - tail_size), std::ios::beg);
        stream.read(reinterpret_cast<char*>(tail.data()),
                    static_cast<std::streamsize>(tail.size()));
        if (!stream) {
            return std::nullopt;
        }
        std::size_t eocd_position = std::string_view::npos;
        for (std::size_t position = tail.size() - 22U;; --position) {
            if (read_le32(tail.data() + position) == kZipEndOfCentralDirectoryMagic &&
                position + 22U + read_le16(tail.data() + position + 20U) <= tail.size()) {
                eocd_position = position;
                break;
            }
            if (position == 0) break;
        }
        if (eocd_position == std::string_view::npos) {
            return std::nullopt;
        }

        const std::uint16_t disk_number = read_le16(tail.data() + eocd_position + 4U);
        const std::uint16_t central_disk = read_le16(tail.data() + eocd_position + 6U);
        const std::uint16_t entries_on_disk = read_le16(tail.data() + eocd_position + 8U);
        const std::uint16_t total_entries = read_le16(tail.data() + eocd_position + 10U);
        const std::uint32_t central_size = read_le32(tail.data() + eocd_position + 12U);
        const std::uint32_t central_offset = read_le32(tail.data() + eocd_position + 16U);
        const std::uint64_t eocd_offset = file_size - tail_size + eocd_position;
        const std::uint64_t central_end = static_cast<std::uint64_t>(central_offset) + central_size;
        if (disk_number != 0 || central_disk != 0 || entries_on_disk != total_entries ||
            total_entries == 0 || total_entries > kMaxZipEntries ||
            central_end > file_size || central_end > eocd_offset) {
            return std::nullopt;
        }

        std::uint64_t total_uncompressed_size = 0;
        std::optional<ZipCentralEntry> manifest_entry;
        std::string manifest_name;
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(central_offset), std::ios::beg);
        if (!stream) return std::nullopt;

        for (std::uint16_t index = 0; index < total_entries; ++index) {
            std::array<unsigned char, 46> central_header{};
            stream.read(reinterpret_cast<char*>(central_header.data()),
                        static_cast<std::streamsize>(central_header.size()));
            if (!stream || read_le32(central_header.data()) != kZipCentralHeaderMagic) {
                return std::nullopt;
            }
            const std::uint16_t flags = read_le16(central_header.data() + 8U);
            const std::uint16_t compression_method = read_le16(central_header.data() + 10U);
            const std::uint32_t entry_crc32 = read_le32(central_header.data() + 16U);
            const std::uint32_t compressed_size = read_le32(central_header.data() + 20U);
            const std::uint32_t uncompressed_size = read_le32(central_header.data() + 24U);
            const std::uint16_t filename_len = read_le16(central_header.data() + 28U);
            const std::uint16_t extra_len = read_le16(central_header.data() + 30U);
            const std::uint16_t comment_len = read_le16(central_header.data() + 32U);
            const std::uint16_t disk_start = read_le16(central_header.data() + 34U);
            const std::uint32_t local_header_offset = read_le32(central_header.data() + 42U);
            const std::uint64_t metadata_size = static_cast<std::uint64_t>(filename_len) +
                                                extra_len + comment_len;
            const std::streamoff current = stream.tellg();
            if (current < 0 || static_cast<std::uint64_t>(current) > central_end ||
                metadata_size > central_end - static_cast<std::uint64_t>(current) ||
                filename_len == 0 || filename_len > kMaxZipFilenameSize ||
                disk_start != 0 || (flags & 0x0001U) != 0U ||
                uncompressed_size == 0xFFFFFFFFU || compressed_size == 0xFFFFFFFFU ||
                local_header_offset >= central_offset ||
                static_cast<std::uint64_t>(uncompressed_size) > kMaxZipEntryUncompressedSize ||
                static_cast<std::uint64_t>(uncompressed_size) >
                    kMaxPackageUncompressedSize - total_uncompressed_size) {
                return std::nullopt;
            }
            total_uncompressed_size += uncompressed_size;

            std::string filename(filename_len, '\0');
            stream.read(filename.data(), filename_len);
            if (!stream || !safe_zip_filename(filename)) return std::nullopt;
            if (!skip_bytes(stream, static_cast<std::uint64_t>(extra_len) + comment_len,
                            file_size)) {
                return std::nullopt;
            }
            if (filename == "AppxManifest.xml") {
                if (manifest_entry.has_value()) return std::nullopt;
                manifest_name = filename;
                manifest_entry = ZipCentralEntry{flags, compression_method, entry_crc32,
                                                 compressed_size, uncompressed_size,
                                                 local_header_offset};
            }
        }
        if (static_cast<std::uint64_t>(stream.tellg()) != central_end) {
            return std::nullopt;
        }
        if (!manifest_entry.has_value()) return std::nullopt;

        const ZipCentralEntry entry = *manifest_entry;
        if (entry.uncompressed_size > kMaxManifestSize ||
            entry.compressed_size > kMaxManifestCompressedSize ||
            (entry.compression_method != 0 && entry.compression_method != 8)) {
            return std::nullopt;
        }

        stream.clear();
        stream.seekg(static_cast<std::streamoff>(entry.local_header_offset), std::ios::beg);
        if (!stream) return std::nullopt;
        std::uint32_t local_signature = 0;
        if (!read_u32(stream, local_signature) || local_signature != kZipLocalHeaderMagic) {
            return std::nullopt;
        }
        std::array<unsigned char, 26> local_header{};
        stream.read(reinterpret_cast<char*>(local_header.data()),
                    static_cast<std::streamsize>(local_header.size()));
        if (!stream) return std::nullopt;
        const std::uint16_t local_flags = read_le16(local_header.data() + 2U);
        const std::uint16_t local_method = read_le16(local_header.data() + 4U);
        const std::uint16_t local_name_len = read_le16(local_header.data() + 22U);
        const std::uint16_t local_extra_len = read_le16(local_header.data() + 24U);
        if (local_method != entry.compression_method ||
            (local_flags & 0x0001U) != 0U || local_name_len != manifest_name.size()) {
            return std::nullopt;
        }
        std::string local_name(local_name_len, '\0');
        stream.read(local_name.data(), local_name_len);
        if (!stream || local_name != manifest_name ||
            !skip_bytes(stream, local_extra_len, file_size)) {
            return std::nullopt;
        }
        const std::streamoff data_offset = stream.tellg();
        if (data_offset < 0 || static_cast<std::uint64_t>(data_offset) > file_size ||
            entry.compressed_size > file_size - static_cast<std::uint64_t>(data_offset)) {
            return std::nullopt;
        }

        std::vector<unsigned char> compressed(entry.compressed_size);
        if (!compressed.empty()) {
            stream.read(reinterpret_cast<char*>(compressed.data()),
                        static_cast<std::streamsize>(compressed.size()));
            if (!stream) return std::nullopt;
        }
        std::optional<std::vector<unsigned char>> manifest_data;
        if (entry.compression_method == 0) {
            if (entry.compressed_size != entry.uncompressed_size) return std::nullopt;
            manifest_data = std::move(compressed);
        } else {
            manifest_data = inflate_raw(compressed, entry.uncompressed_size);
        }
        if (!manifest_data.has_value() || !crc_matches(*manifest_data, entry.crc32)) {
            return std::nullopt;
        }
        const std::string xml_data(reinterpret_cast<const char*>(manifest_data->data()),
                                   manifest_data->size());
        return parse_appx_manifest_xml(xml_data);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> extract_msix_package(
    const std::filesystem::path& package_path,
    const std::filesystem::path& destination) {
    const std::optional<ZipArchive> archive = read_zip_archive(package_path);
    if (!archive.has_value()) return std::nullopt;

    const auto manifest_it = std::find_if(
        archive->entries.begin(), archive->entries.end(), [](const ZipArchiveEntry& entry) {
            return entry.name == "AppxManifest.xml";
        });
    if (manifest_it == archive->entries.end() ||
        manifest_it->metadata.uncompressed_size > kMaxManifestSize ||
        manifest_it->metadata.compressed_size > kMaxManifestCompressedSize) {
        return std::nullopt;
    }
    const auto manifest_data = read_zip_entry(package_path, *archive, *manifest_it);
    if (!manifest_data.has_value()) return std::nullopt;
    const std::string manifest(reinterpret_cast<const char*>(manifest_data->data()),
                               manifest_data->size());
    const std::optional<AppxPackageInfo> package_info = parse_appx_manifest_xml(manifest);
    if (!package_info.has_value() || !package_info->main_executable.has_value()) {
        return std::nullopt;
    }

    std::string executable_name = normalized_zip_name(*package_info->main_executable);
    if (!safe_zip_filename(executable_name) || executable_name.ends_with('/')) {
        return std::nullopt;
    }
    const auto executable_it = std::find_if(
        archive->entries.begin(), archive->entries.end(),
        [&executable_name](const ZipArchiveEntry& entry) {
            return normalized_zip_name(entry.name) == executable_name &&
                   !entry.name.ends_with('/') && !entry.name.ends_with('\\');
        });
    if (executable_it == archive->entries.end()) return std::nullopt;

    std::error_code filesystem_error;
    const bool destination_is_symlink = path_is_symlink(destination, filesystem_error);
    if ((filesystem_error &&
         filesystem_error != std::make_error_code(std::errc::no_such_file_or_directory)) ||
        destination_is_symlink) {
        return std::nullopt;
    }
    filesystem_error.clear();
    const bool destination_exists = path_exists(destination, filesystem_error);
    if (filesystem_error || (destination_exists &&
                             (!std::filesystem::is_directory(destination, filesystem_error) ||
                              filesystem_error))) {
        return std::nullopt;
    }
    if (destination_exists) {
        std::filesystem::directory_iterator iterator(destination, filesystem_error);
        const std::filesystem::directory_iterator end;
        if (filesystem_error || iterator != end) return std::nullopt;
    }
    if (!destination_exists && !std::filesystem::create_directories(destination, filesystem_error)) {
        if (filesystem_error) return std::nullopt;
    }
    const auto cleanup = [&]() {
        if (!destination_exists) {
            std::error_code ignored;
            std::filesystem::remove_all(destination, ignored);
        }
    };

    const std::filesystem::path normalized_destination = destination.lexically_normal();
    for (const ZipArchiveEntry& entry : archive->entries) {
        const bool directory = entry.name.ends_with('/') || entry.name.ends_with('\\');
        const std::string name = normalized_zip_name(entry.name);
        const std::string relative_name = directory && name.ends_with('/')
                                              ? name.substr(0, name.size() - 1U)
                                              : name;
        if (relative_name.empty()) {
            cleanup();
            return std::nullopt;
        }
        const std::filesystem::path relative_path{relative_name};
        const std::filesystem::path target =
            (normalized_destination / relative_path).lexically_normal();
        const std::filesystem::path within = target.lexically_relative(normalized_destination);
        const std::string within_text = within.generic_string();
        if (within_text.empty() || within_text == ".." || within_text.starts_with("../") ||
            path_has_symlink_component(normalized_destination, within.parent_path())) {
            cleanup();
            return std::nullopt;
        }

        std::error_code error;
        const std::filesystem::path parent = target.parent_path();
        if (!path_exists(parent, error)) {
            if (error || !std::filesystem::create_directories(parent, error) || error) {
                cleanup();
                return std::nullopt;
            }
        }
        if (path_is_symlink(parent, error) || error ||
            path_has_symlink_component(normalized_destination, within.parent_path())) {
            cleanup();
            return std::nullopt;
        }
        if (directory) {
            if (path_exists(target, error)) {
                if (error || path_is_symlink(target, error) ||
                    !std::filesystem::is_directory(target, error) || error) {
                    cleanup();
                    return std::nullopt;
                }
            } else if (error || !std::filesystem::create_directory(target, error) || error) {
                cleanup();
                return std::nullopt;
            }
            continue;
        }
        if (path_exists(target, error) || error || path_is_symlink(target, error) || error) {
            cleanup();
            return std::nullopt;
        }
        const auto data = read_zip_entry(package_path, *archive, entry);
        if (!data.has_value()) {
            cleanup();
            return std::nullopt;
        }
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
            cleanup();
            return std::nullopt;
        }
        if (!data->empty()) {
            output.write(reinterpret_cast<const char*>(data->data()),
                         static_cast<std::streamsize>(data->size()));
        }
        if (!output) {
            cleanup();
            return std::nullopt;
        }
    }

    const std::filesystem::path executable_path =
        (normalized_destination / std::filesystem::path{executable_name}).lexically_normal();
    if (!std::filesystem::is_regular_file(executable_path, filesystem_error) ||
        filesystem_error) {
        cleanup();
        return std::nullopt;
    }
    return executable_path;
}

}  // namespace tradutorlinux::package
