#pragma once

#include "tradutorlinux/compat/profile.hpp"
#include "tradutorlinux/ffi/rust_profile_parser.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace tradutorlinux::compat {

struct RustProfileParseResult {
    tl_profile_status_t status{TL_PROFILE_STATUS_SUCCESS};
    bool internal_failure{false};
    tl_profile_error_v1 error{};
    std::string error_message;
    Profile profile;
};

// Decodes and validates the complete TLPR v1.0 buffer. The decoder is shared
// by the production adapter and the differential contract tests.
[[nodiscard]] bool decode_tlpr_v1(std::span<const std::uint8_t> wire,
                                  Profile& profile,
                                  tl_profile_error_v1& error,
                                  std::string& error_message);

// Calls both stateless Rust functions and decodes their caller-owned result.
[[nodiscard]] RustProfileParseResult parse_profile_rust(
    std::span<const std::byte> input,
    std::string_view expected_app_id,
    std::string_view expected_app_sha256,
    std::string_view expected_app_version);

}  // namespace tradutorlinux::compat
