#pragma once

#include "tradutorlinux/catalog/app_catalog.hpp"
#include "tradutorlinux/ffi/rust_app_catalog_parser.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradutorlinux::catalog {

struct RustAppCatalogParseResult {
    tl_app_catalog_status_t status{TL_APP_CATALOG_STATUS_SUCCESS};
    bool internal_failure{false};
    tl_app_catalog_error_v1 error{};
    std::string error_message;
    std::vector<AppEntry> apps;
};

// Decodes and validates a complete TLAC v1.0 buffer without using host-layout
// casts. The output vector is changed only after the complete wire has passed
// validation.
[[nodiscard]] bool decode_tlac_v1(std::span<const std::uint8_t> wire,
                                  std::vector<AppEntry>& apps,
                                  tl_app_catalog_error_v1& error,
                                  std::string& error_message);

// Calls the stateless Rust parser and decodes its caller-owned TLAC result.
[[nodiscard]] RustAppCatalogParseResult parse_app_catalog_rust(
    std::span<const std::byte> input);

}  // namespace tradutorlinux::catalog
