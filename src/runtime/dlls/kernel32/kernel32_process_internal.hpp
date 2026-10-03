#pragma once

#include "kernel32_common.hpp"

#include "../../core/runtime_handle_state.hpp"
#include "../../core/runtime_memory_state.hpp"
#include "../../core/runtime_process_state.hpp"
#include "../../core/runtime_thread_state.hpp"

#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/runtime/security.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tradutorlinux {

constexpr std::size_t kMaxEnvironmentStringUnits = 32768U;
constexpr std::size_t kMaxModuleStringUnits = 4096U;

template <typename Unit>
[[nodiscard]] inline bool write_guest_terminated_units(void* const destination,
                                                       const Unit* const source,
                                                       const std::size_t length) noexcept {
    if (destination == nullptr || length > std::numeric_limits<std::size_t>::max() / sizeof(Unit)) {
        return false;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(destination);
    const std::size_t bytes = length * sizeof(Unit);
    if (base > std::numeric_limits<std::uintptr_t>::max() - bytes) {
        return false;
    }
    if (bytes > 0 && runtime::write_guest_memory(destination, source, bytes).status !=
                         runtime::GuestMemoryAccessStatus::Success) {
        return false;
    }
    const Unit terminator{};
    return runtime::write_guest_memory(reinterpret_cast<void*>(base + bytes), &terminator,
                                       sizeof(terminator)).status ==
           runtime::GuestMemoryAccessStatus::Success;
}

}  // namespace tradutorlinux
