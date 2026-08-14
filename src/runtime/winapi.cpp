#include "tradutorlinux/runtime/winapi.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <iostream>

namespace tradutorlinux {
namespace {

void stub_trace(const char* symbol) {
    const std::array<diagnostics::TraceField, 3> fields{
        diagnostics::TraceField{"dll", "KERNEL32.dll"},
        diagnostics::TraceField{"symbol", symbol},
        diagnostics::TraceField{"detail", "placeholder da Fase 3; semântica real na Fase 4"},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Warning, "stub", fields);
}

}  // namespace

extern "C" {

TL_MSABI void* tl_GetStdHandle(std::uint32_t) noexcept {
    stub_trace("GetStdHandle");
    return nullptr;
}

TL_MSABI int tl_WriteFile(void*, const void*, std::uint32_t, std::uint32_t*, void*) noexcept {
    stub_trace("WriteFile");
    return 0;
}

TL_MSABI void tl_ExitProcess(std::uint32_t) noexcept {
    stub_trace("ExitProcess");
}

}  // extern "C"
}  // namespace tradutorlinux
