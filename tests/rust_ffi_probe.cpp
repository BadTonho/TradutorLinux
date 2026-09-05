#include "tradutorlinux/ffi/rust_validator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

namespace {

bool expect_status(const tl_rust_status_t actual, const tl_rust_status_t expected,
                   const std::string_view operation) {
    if (actual == expected) return true;
    std::cerr << "rust ffi probe: " << operation << " returned " << actual
              << ", expected " << expected << '\n';
    return false;
}

bool contains(const char* buffer, const std::string_view needle) {
    return std::string_view{buffer}.find(needle) != std::string_view::npos;
}

bool check_error_buffer(const char* buffer, const std::uint64_t capacity,
                        const std::uint64_t required, const std::string_view needle) {
    if (required <= capacity && buffer[required - 1U] != '\0') {
        std::cerr << "rust ffi probe: complete error is not NUL terminated\n";
        return false;
    }
    if (capacity != 0 && buffer[capacity - 1U] != '\0' && required > capacity) {
        std::cerr << "rust ffi probe: truncated error is not NUL terminated\n";
        return false;
    }
    if (!contains(buffer, needle)) {
        std::cerr << "rust ffi probe: error message did not contain '" << needle << "'\n";
        return false;
    }
    return true;
}

template <typename Input>
using ValidateFunction = tl_rust_status_t (*)(
    const tl_rust_validator_t*, const Input*, std::uint64_t, char*, std::uint64_t,
    std::uint64_t*);

template <typename Input>
bool check_error_buffer_boundaries(
    const tl_rust_validator_t* validator, const ValidateFunction<Input> function,
    const Input* input, const std::uint64_t input_length,
    const tl_rust_status_t expected_status, const std::string_view operation) {
    std::array<char, 64> complete_error{};
    std::uint64_t required = 0;
    if (!expect_status(function(validator, input, input_length, complete_error.data(),
                                complete_error.size(), &required),
                       expected_status, operation)) {
        return false;
    }
    if (required == 0 || required > complete_error.size() ||
        complete_error[required - 1U] != '\0') {
        std::cerr << "rust ffi probe: " << operation
                  << " returned an invalid required size\n";
        return false;
    }

    for (std::uint64_t capacity = 0; capacity <= required + 1U; ++capacity) {
        std::array<unsigned char, 128> guarded;
        guarded.fill(0xa5U);
        char* buffer = nullptr;
        if (capacity != 0) {
            buffer = reinterpret_cast<char*>(guarded.data() + 16U);
        }
        std::uint64_t observed_required = 0;
        const tl_rust_status_t status = function(
            validator, input, input_length, buffer, capacity, &observed_required);
        const tl_rust_status_t expected = capacity < required
                                               ? TL_RUST_STATUS_BUFFER_TOO_SMALL
                                               : expected_status;
        if (!expect_status(status, expected, operation) || observed_required != required) {
            std::cerr << "rust ffi probe: " << operation
                      << " changed required size at capacity " << capacity << '\n';
            return false;
        }
        const std::uint64_t written = capacity < required ? capacity : required;
        if (capacity != 0 && guarded[16U + static_cast<std::size_t>(written) - 1U] != 0U) {
            std::cerr << "rust ffi probe: " << operation
                      << " did not terminate its bounded output\n";
            return false;
        }
        for (std::size_t index = 0; index < 16U; ++index) {
            if (guarded[index] != 0xa5U) {
                std::cerr << "rust ffi probe: " << operation
                          << " wrote before its error buffer\n";
                return false;
            }
        }
        for (std::size_t index = 16U + static_cast<std::size_t>(capacity);
             index < guarded.size(); ++index) {
            if (guarded[index] != 0xa5U) {
                std::cerr << "rust ffi probe: " << operation
                          << " wrote past its error buffer\n";
                return false;
            }
        }
    }
    return true;
}

bool check_length_and_pointer_boundaries(const tl_rust_validator_t* validator) {
    const std::array<std::uint8_t, 1> byte_input{static_cast<std::uint8_t>('x')};
    const std::array<std::uint16_t, 1> utf16_input{0x0041U};
    std::array<char, 64> error{};
    std::uint64_t required = 0;

    if (!expect_status(tl_rust_validator_validate_utf8(
                           validator, byte_input.data(), std::numeric_limits<std::uint64_t>::max(),
                           error.data(), error.size(), &required),
                       TL_RUST_STATUS_INPUT_TOO_LARGE, "utf8-u64-max") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, nullptr, 0U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_OK, "utf8-null-zero") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, nullptr, 1U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "utf8-null-nonzero") ||
        !expect_status(tl_rust_validator_validate_utf16(
                           validator, utf16_input.data(), std::numeric_limits<std::uint64_t>::max(),
                           error.data(), error.size(), &required),
                       TL_RUST_STATUS_INPUT_TOO_LARGE, "utf16-u64-max") ||
        !expect_status(tl_rust_validator_validate_utf16(
                           validator, nullptr, 0U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_OK, "utf16-null-zero") ||
        !expect_status(tl_rust_validator_validate_utf16(
                           validator, nullptr, 1U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "utf16-null-nonzero")) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    tl_rust_validator_t* validator = nullptr;
    if (!expect_status(tl_rust_validator_create(64U, &validator), TL_RUST_STATUS_OK,
                       "create") || validator == nullptr) {
        return 1;
    }
    if (!expect_status(tl_rust_validator_create(64U, nullptr),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "create-null-out") ||
        !expect_status(tl_rust_validator_create(0U, &validator),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "create-zero-limit")) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::uint8_t, 4> valid_utf8{0x54U, 0x4cU, 0xc3U, 0xa9U};
    std::array<char, 64> error{};
    std::uint64_t required = 0;
    if (!expect_status(tl_rust_validator_validate_utf8(
                           validator, valid_utf8.data(), valid_utf8.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_OK, "valid-utf8") ||
        required != 1U || error[0] != '\0') {
        std::cerr << "rust ffi probe: successful UTF-8 validation returned a bad message\n";
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::uint8_t, 1> invalid_utf8{0xffU};
    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf8(
                           validator, invalid_utf8.data(), invalid_utf8.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_INVALID_UTF8, "invalid-utf8") ||
        !check_error_buffer(error.data(), error.size(), required, "UTF-8")) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf8(
                           nullptr, valid_utf8.data(), valid_utf8.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "null-validator") ||
        !check_error_buffer(error.data(), error.size(), required, "validator")) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    std::array<char, 4> short_error{'x', 'x', 'x', 'x'};
    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf8(
                           validator, invalid_utf8.data(), invalid_utf8.size(), short_error.data(),
                           short_error.size(), &required),
                       TL_RUST_STATUS_BUFFER_TOO_SMALL, "short-error") ||
        !check_error_buffer(short_error.data(), short_error.size(), required, "inp")) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::uint16_t, 3> valid_utf16{0x0054U, 0x004cU, 0x00e9U};
    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf16(
                           validator, valid_utf16.data(), valid_utf16.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_OK, "valid-utf16") ||
        required != 1U) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::uint16_t, 1> invalid_utf16{0xd800U};
    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf16(
                           validator, invalid_utf16.data(), invalid_utf16.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_INVALID_UTF16, "invalid-utf16") ||
        !check_error_buffer(error.data(), error.size(), required, "UTF-16")) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    if (!check_error_buffer_boundaries(
            validator, tl_rust_validator_validate_utf8, invalid_utf8.data(), invalid_utf8.size(),
            TL_RUST_STATUS_INVALID_UTF8, "utf8-error-boundaries") ||
        !check_error_buffer_boundaries(validator, tl_rust_validator_validate_utf16,
                                       invalid_utf16.data(), invalid_utf16.size(),
                                       TL_RUST_STATUS_INVALID_UTF16, "utf16-error-boundaries") ||
        !check_length_and_pointer_boundaries(validator)) {
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::uint8_t, 5> over_limit{1U, 2U, 3U, 4U, 5U};
    tl_rust_validator_t* small_validator = nullptr;
    if (!expect_status(tl_rust_validator_create(4U, &small_validator), TL_RUST_STATUS_OK,
                       "create-small") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           small_validator, over_limit.data(), over_limit.size(), error.data(),
                           error.size(), &required),
                       TL_RUST_STATUS_INPUT_TOO_LARGE, "input-limit") ||
        !check_error_buffer(error.data(), error.size(), required, "limit")) {
        tl_rust_validator_destroy(small_validator);
        tl_rust_validator_destroy(validator);
        return 1;
    }

    const std::array<std::string_view, 3> valid_relative_paths{
        "fixture.dat", "nested/arquivo-\xc3\xa9.dat", "C:/compat-as-relative.dat"};
    for (const std::string_view path : valid_relative_paths) {
        required = 0;
        if (!expect_status(tl_rust_validator_validate_relative_path(
                               validator, reinterpret_cast<const std::uint8_t*>(path.data()),
                               path.size(), error.data(), error.size(), &required),
                           TL_RUST_STATUS_OK, "valid-relative-path") ||
            required != 1U) {
            tl_rust_validator_destroy(small_validator);
            tl_rust_validator_destroy(validator);
            return 1;
        }
    }

    const std::array<std::string_view, 5> invalid_relative_paths{
        "", "/absolute.dat", "nested/../outside.dat", "nested\\outside.dat",
        std::string_view{"bad\0name", 8U}};
    for (const std::string_view path : invalid_relative_paths) {
        required = 0;
        if (!expect_status(tl_rust_validator_validate_relative_path(
                               validator, reinterpret_cast<const std::uint8_t*>(path.data()),
                               path.size(), error.data(), error.size(), &required),
                           TL_RUST_STATUS_INVALID_PATH, "invalid-relative-path") ||
            !check_error_buffer(error.data(), error.size(), required, "relative")) {
            tl_rust_validator_destroy(small_validator);
            tl_rust_validator_destroy(validator);
            return 1;
        }
    }

    const std::array<std::string_view, 4> valid_c_drive_paths{
        "C:\\Fixture\\compat.dat", "c:/Fixture/compat.dat", "C:\\a\\..\\b.dat",
        "C:\\a//b.dat"};
    for (const std::string_view path : valid_c_drive_paths) {
        required = 0;
        if (!expect_status(tl_rust_validator_validate_c_drive_path(
                               validator, reinterpret_cast<const std::uint8_t*>(path.data()),
                               path.size(), error.data(), error.size(), &required),
                           TL_RUST_STATUS_OK, "valid-c-drive-path") ||
            required != 1U) {
            tl_rust_validator_destroy(small_validator);
            tl_rust_validator_destroy(validator);
            return 1;
        }
    }

    const std::array<std::string_view, 6> invalid_c_drive_paths{
        "", "D:\\Fixture\\file.dat", "C:", "C:\\", "C:\\..\\outside.dat",
        std::string_view{"C:\\bad\0name", 11U}};
    for (const std::string_view path : invalid_c_drive_paths) {
        required = 0;
        if (!expect_status(tl_rust_validator_validate_c_drive_path(
                               validator, reinterpret_cast<const std::uint8_t*>(path.data()),
                               path.size(), error.data(), error.size(), &required),
                           TL_RUST_STATUS_INVALID_PATH, "invalid-c-drive-path") ||
            !check_error_buffer(error.data(), error.size(), required, "path")) {
            tl_rust_validator_destroy(small_validator);
            tl_rust_validator_destroy(validator);
            return 1;
        }
    }

    required = 0;
    if (!expect_status(tl_rust_validator_validate_utf8(
                           validator, nullptr, 0U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_OK, "empty-null-input") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, nullptr, 1U, error.data(), error.size(), &required),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "null-input") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, valid_utf8.data(), valid_utf8.size(), nullptr, 0U, &required),
                       TL_RUST_STATUS_BUFFER_TOO_SMALL, "zero-error-buffer") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, valid_utf8.data(), valid_utf8.size(), nullptr, 1U, &required),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "null-error-buffer") ||
        !expect_status(tl_rust_validator_validate_utf8(
                           validator, valid_utf8.data(), valid_utf8.size(), error.data(), error.size(),
                           nullptr),
                       TL_RUST_STATUS_INVALID_ARGUMENT, "null-required")) {
        tl_rust_validator_destroy(small_validator);
        tl_rust_validator_destroy(validator);
        return 1;
    }

    std::atomic<bool> concurrent_ok{true};
    const auto validate_concurrently = [&]() {
        std::array<char, 32> local_error{};
        std::uint64_t local_required = 0;
        const auto status = tl_rust_validator_validate_utf8(
            validator, valid_utf8.data(), valid_utf8.size(), local_error.data(),
            local_error.size(), &local_required);
        if (status != TL_RUST_STATUS_OK || local_required != 1U || local_error[0] != '\0') {
            concurrent_ok.store(false);
        }
    };
    std::array<std::thread, 8> threads;
    for (auto& thread : threads) thread = std::thread{validate_concurrently};
    for (auto& thread : threads) thread.join();
    if (!concurrent_ok.load()) {
        std::cerr << "rust ffi probe: concurrent read-only calls failed\n";
        tl_rust_validator_destroy(small_validator);
        tl_rust_validator_destroy(validator);
        return 1;
    }

    tl_rust_validator_destroy(small_validator);
    tl_rust_validator_destroy(validator);
    tl_rust_validator_destroy(nullptr);
    std::cout << "rust ffi probe\n";
    return 0;
}
