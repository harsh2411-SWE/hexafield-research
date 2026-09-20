#pragma once

#include <cstdint>
#include <thread>

#if defined(_MSC_VER)
  #include <intrin.h>
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <x86intrin.h>
  #include <pthread.h>
  #include <sched.h>
#endif

namespace hft {

// Serialize then read the timestamp counter. lfence+rdtsc is cheaper than
// rdtscp on many uarchs and sufficient for intra-process latency.
[[nodiscard]] inline std::uint64_t rdtsc() noexcept {
#if defined(_MSC_VER)
  _mm_lfence();
  return static_cast<std::uint64_t>(__rdtsc());
#else
  _mm_lfence();
  return static_cast<std::uint64_t>(__rdtsc());
#endif
}

inline void cpu_pause() noexcept {
#if defined(_MSC_VER)
  _mm_pause();
#else
  __builtin_ia32_pause();
#endif
}

// Pin the *calling* thread to a single logical CPU. Returns false if the OS
// rejected the affinity request (still safe to run, just noisier tails).
inline bool pin_this_thread(unsigned cpu_index) noexcept {
#if defined(_WIN32)
  const DWORD_PTR mask = static_cast<DWORD_PTR>(1ull << (cpu_index % 64u));
  return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#else
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_index, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#endif
}

}  // namespace hft
