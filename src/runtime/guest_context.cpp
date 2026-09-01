#include "tradutorlinux/runtime/guest_context.hpp"

namespace tradutorlinux::runtime {
namespace {

thread_local GuestContext* g_current_context = nullptr;

}  // namespace

GuestContext::GuestContext() noexcept
    : standard_handles{&standard_handle_tokens[0], &standard_handle_tokens[1],
                       &standard_handle_tokens[2]} {}

GuestContext& default_guest_context() noexcept {
    static GuestContext context;
    return context;
}

GuestContext* current_guest_context() noexcept {
    if (g_current_context == nullptr) {
        g_current_context = &default_guest_context();
    }
    return g_current_context;
}

GuestContext& guest_context() noexcept {
    return *current_guest_context();
}

GuestContextScope::GuestContextScope(GuestContext& context) noexcept
    : previous_(current_guest_context()) {
    g_current_context = &context;
}

GuestContextScope::~GuestContextScope() {
    g_current_context = previous_;
}

}  // namespace tradutorlinux::runtime
