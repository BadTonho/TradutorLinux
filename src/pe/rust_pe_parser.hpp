#pragma once

#include "tradutorlinux/ffi/rust_pe_parser.h"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace tradutorlinux::pe {

struct RustPeParseResult {
    ParseStatus status{ParseStatus::Success};
    bool internal_failure{false};
    tl_pe_error_v1 error{};
    std::string error_message;
    PeInfo info;
};

// Decodes the complete TLPE v1.0 buffer. This is shared by the production
// adapter and contract tests so that the wire validation has one owner.
[[nodiscard]] bool decode_tlpe_v1(std::span<const std::uint8_t> wire,
                                  PeInfo& info,
                                  tl_pe_error_v1& error,
                                  std::string& error_message);

// Calls the stateless Rust ABI and decodes its caller-owned result.
[[nodiscard]] RustPeParseResult parse_pe_rust(std::span<const std::byte> input);

}  // namespace tradutorlinux::pe
