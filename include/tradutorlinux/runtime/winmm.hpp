#pragma once

#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TL_WINMM_MSABI __attribute__((ms_abi))
#else
#error "TL_WINMM_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

extern "C" {

TL_WINMM_MSABI std::uint32_t tl_timeGetTime() noexcept;
TL_WINMM_MSABI std::uint32_t tl_timeBeginPeriod(std::uint32_t period) noexcept;
TL_WINMM_MSABI std::uint32_t tl_timeEndPeriod(std::uint32_t period) noexcept;
TL_WINMM_MSABI std::uint32_t tl_timeGetDevCaps(void* time_caps, std::uint32_t size) noexcept;
TL_WINMM_MSABI std::uint32_t tl_timeSetEvent(std::uint32_t delay, std::uint32_t resolution, void* callback,
                                             std::uintptr_t user, std::uint32_t event) noexcept;
TL_WINMM_MSABI std::uint32_t tl_timeKillEvent(std::uint32_t id) noexcept;
TL_WINMM_MSABI int tl_PlaySoundA(const char* sound, void* module, std::uint32_t flags) noexcept;
TL_WINMM_MSABI int tl_PlaySoundW(const std::uint16_t* sound, void* module, std::uint32_t flags) noexcept;

}  // extern "C"

}  // namespace tradutorlinux
