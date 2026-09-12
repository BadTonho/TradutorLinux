#include "kernel32_file_internal.hpp"

#include <filesystem>

#include <unistd.h>

namespace tradutorlinux::file_internal {

std::uint64_t current_file_size(const FileSlot& slot) noexcept {
    struct stat st{};
    if (::fstat(slot.fd, &st) == 0) {
        return static_cast<std::uint64_t>(st.st_size);
    }
    return 0;
}

void synchronize_file_position(FileSlot* const slot, const int fd) noexcept {
    if (slot == nullptr) {
        return;
    }
    const off_t position = ::lseek(fd, 0, SEEK_CUR);
    if (position >= 0 && position <= std::numeric_limits<std::int64_t>::max()) {
        slot->position = static_cast<std::int64_t>(position);
    }
}

bool normalize_wide_path(const std::uint16_t* path, std::string& result) noexcept {
    char normalized[4096]{};
    if (!normalized_wide_path(path, normalized)) {
        return false;
    }
    result = normalized;
    return true;
}

std::int64_t filetime_ticks(const timespec& value) noexcept {
    constexpr std::int64_t kEpochDifference = 11644473600LL;
    return (static_cast<std::int64_t>(value.tv_sec) + kEpochDifference) * 10000000LL +
           static_cast<std::int64_t>(value.tv_nsec) / 100LL;
}

void write_filetime(const timespec& source, LegacyFileTime& target) noexcept {
    const auto ticks = static_cast<std::uint64_t>(std::max<std::int64_t>(filetime_ticks(source), 0));
    target.low = static_cast<std::uint32_t>(ticks & 0xFFFFFFFFU);
    target.high = static_cast<std::uint32_t>(ticks >> 32U);
}

bool filetime_to_timespec(const LegacyFileTime& value, timespec& result) noexcept {
    constexpr std::uint64_t kEpochDifference = 11644473600ULL;
    constexpr std::uint64_t kTicksPerSecond = 10000000ULL;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(value.high) << 32U) | value.low;
    if (ticks < kEpochDifference * kTicksPerSecond) {
        return false;
    }
    const std::uint64_t unix_ticks = ticks - kEpochDifference * kTicksPerSecond;
    result.tv_sec = static_cast<time_t>(unix_ticks / kTicksPerSecond);
    result.tv_nsec = static_cast<long>((unix_ticks % kTicksPerSecond) * 100ULL);
    return true;
}

std::string normalize_windows_path_segments(const std::string& absolute_with_drive) {
    std::string drive;
    std::string_view rest;
    bool is_unc = false;
    if (absolute_with_drive.size() >= 2 && absolute_with_drive[0] == '\\' &&
        absolute_with_drive[1] == '\\') {
        is_unc = true;
        rest = std::string_view(absolute_with_drive).substr(2);
        drive = "\\\\";
    } else if (absolute_with_drive.size() >= 2 && absolute_with_drive[1] == ':') {
        drive = absolute_with_drive.substr(0, 2);
        rest = std::string_view(absolute_with_drive).substr(2);
    } else {
        rest = absolute_with_drive;
    }

    std::vector<std::string> stack;
    std::string current;
    for (std::size_t i = 0; i <= rest.size(); ++i) {
        const char c = i < rest.size() ? rest[i] : '\\';
        if (c == '\\' || c == '/') {
            if (current.empty() || current == ".") {
            } else if (current == "..") {
                if (!stack.empty()) stack.pop_back();
            } else {
                stack.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    std::string out = drive;
    if (is_unc) {
        for (const auto& seg : stack) {
            out += seg;
            out.push_back('\\');
        }
        if (!stack.empty()) out.pop_back();
        if (out == "\\\\") out = "\\\\";
    } else {
        if (!drive.empty()) {
            out.push_back('\\');
        } else if (!stack.empty()) {
            out.push_back('\\');
        }
        for (std::size_t i = 0; i < stack.size(); ++i) {
            out += stack[i];
            if (i + 1 < stack.size()) out.push_back('\\');
        }
        if (out.empty()) out = drive.empty() ? "\\" : drive + "\\";
    }
    return out;
}

std::string build_full_windows_path(const std::string& input_raw) {
    std::string input = input_raw;
    std::replace(input.begin(), input.end(), '/', '\\');

    char cwd_buf[4096]{};
    const char* cwd_cstr = ::getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr ? cwd_buf : ".";
    std::string win_cwd =
        prefix::to_windows_path(std::filesystem::path(cwd_cstr), guest_prefix_root());
    std::replace(win_cwd.begin(), win_cwd.end(), '/', '\\');

    if (win_cwd.size() == 2 && win_cwd[1] == ':') win_cwd += "\\";

    const bool is_unc = input.size() >= 2 && input[0] == '\\' && input[1] == '\\';
    const bool is_drive_abs =
        input.size() >= 3 && std::isalpha(static_cast<unsigned char>(input[0])) &&
        input[1] == ':' && (input[2] == '\\' || input[2] == '/');
    const bool is_rooted = !input.empty() && (input[0] == '\\' || input[0] == '/');
    const bool is_drive_relative =
        input.size() >= 2 && std::isalpha(static_cast<unsigned char>(input[0])) && input[1] == ':';

    std::string combined;
    if (is_unc || is_drive_abs) {
        combined = input;
    } else if (is_rooted) {
        std::string drive = "C:";
        if (win_cwd.size() >= 2 && win_cwd[1] == ':') {
            drive = win_cwd.substr(0, 2);
        }
        combined = drive + input;
    } else if (is_drive_relative) {
        const char drive_letter = input[0];
        const bool same_drive =
            win_cwd.size() >= 2 &&
            std::toupper(static_cast<unsigned char>(win_cwd[0])) ==
                std::toupper(static_cast<unsigned char>(drive_letter));
        const std::string base = same_drive ? win_cwd : (std::string(1, drive_letter) + ":\\");
        std::string rel = input.substr(2);
        while (!rel.empty() && (rel.front() == '\\' || rel.front() == '/')) {
            rel.erase(rel.begin());
        }
        if (base.back() == '\\') {
            combined = base + rel;
        } else {
            combined = base + "\\" + rel;
        }
    } else {
        if (win_cwd.back() == '\\') {
            combined = win_cwd + input;
        } else {
            combined = win_cwd + "\\" + input;
        }
    }

    return normalize_windows_path_segments(combined);
}

std::u16string final_windows_path(const std::string& path) {
    return util::utf8_to_wide(
        prefix::to_windows_path(std::filesystem::path(path), guest_prefix_root()));
}

}  // namespace tradutorlinux::file_internal
