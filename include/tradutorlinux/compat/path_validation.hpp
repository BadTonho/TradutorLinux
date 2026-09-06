#pragma once

#include <cstddef>
#include <cstdint>

namespace tradutorlinux::compat {

enum class PathValidationBackend {
    NotUsed,
    Cpp,
    Rust,
};

struct PathValidationMetrics {
    PathValidationBackend backend{PathValidationBackend::NotUsed};
    std::size_t handle_count{0};
    std::size_t checks{0};
    std::size_t rejected{0};
    std::uint64_t duration_us{0};
    bool infrastructure_error{false};
};

}  // namespace tradutorlinux::compat
