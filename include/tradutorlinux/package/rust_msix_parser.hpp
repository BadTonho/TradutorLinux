#pragma once

#include "tradutorlinux/ffi/rust_msix_parser.h"
#include "tradutorlinux/package/msix.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace tradutorlinux::package {

struct RustMsixParseResult {
    std::uint32_t status{};
    bool internal_failure{false};
    tl_msix_error_v1 error{};
    std::string error_message;
    AppxPackageInfo info;
};

[[nodiscard]] bool decode_tlms_v1(std::span<const std::uint8_t> wire,
                                  AppxPackageInfo& info,
                                  tl_msix_error_v1& error,
                                  std::string& error_message);

[[nodiscard]] RustMsixParseResult parse_msix_rust(
    std::span<const std::uint8_t> input);

}  // namespace tradutorlinux::package
