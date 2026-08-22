#include "tradutorlinux/runtime/winmm.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

constexpr std::uint32_t kTimerrNoError = 0;
constexpr std::uint32_t kTimerrNocando = 97;

struct GuestTimeCaps {
    std::uint32_t period_min{1};
    std::uint32_t period_max{1000000};
};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_WINMM_MSABI std::uint32_t tl_timeGetTime() noexcept {
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    const std::uint64_t ms = (static_cast<std::uint64_t>(ts.tv_sec) * 1000ULL) +
                             (static_cast<std::uint64_t>(ts.tv_nsec) / 1000000ULL);
    return static_cast<std::uint32_t>(ms & 0xFFFFFFFFU);
}

TL_WINMM_MSABI std::uint32_t tl_timeBeginPeriod(const std::uint32_t period) noexcept {
    (void)period;
    return kTimerrNoError;
}

TL_WINMM_MSABI std::uint32_t tl_timeEndPeriod(const std::uint32_t period) noexcept {
    (void)period;
    return kTimerrNoError;
}

TL_WINMM_MSABI std::uint32_t tl_timeGetDevCaps(void* time_caps, const std::uint32_t size) noexcept {
    if (time_caps == nullptr || size < sizeof(GuestTimeCaps) || !mapped_range(time_caps, sizeof(GuestTimeCaps), true)) {
        return kTimerrNocando;
    }
    GuestTimeCaps caps{};
    std::memcpy(time_caps, &caps, sizeof(caps));
    return kTimerrNoError;
}

TL_WINMM_MSABI std::uint32_t tl_timeSetEvent(const std::uint32_t delay, const std::uint32_t resolution,
                                             void* callback, const std::uintptr_t user,
                                             const std::uint32_t event) noexcept {
    (void)delay;
    (void)resolution;
    (void)callback;
    (void)user;
    (void)event;
    // Stub: retorna timer ID 1 para indicar sucesso; não agenda callback real
    return 1;
}

TL_WINMM_MSABI int tl_PlaySoundA(const char* sound, void* module, const std::uint32_t flags) noexcept {
    (void)sound;
    (void)module;
    (void)flags;
    return 1;
}

TL_WINMM_MSABI int tl_PlaySoundW(const std::uint16_t* sound, void* module, const std::uint32_t flags) noexcept {
    (void)sound;
    (void)module;
    (void)flags;
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
