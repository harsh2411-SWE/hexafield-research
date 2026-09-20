#pragma once

#include "hft/config.hpp"
#include "hft/cpu.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>

#if defined(_MSC_VER)
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

namespace hft {

// Pre-allocated tick-to-trade sample buffer. `record` is wait-free for a
// single producer (the strategy thread).
class LatencyRecorder {
 public:
  void reset() noexcept { count_ = 0; }

  void record(std::uint64_t cycles) noexcept {
    if (count_ >= kMaxLatencySamples) {
      return;
    }
    samples_[count_++] = cycles;
  }

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

  void calibrate() noexcept { ns_per_cycle_ = measure_ns_per_cycle(); }

  [[nodiscard]] double to_ns(std::uint64_t cycles) const noexcept {
    return static_cast<double>(cycles) * ns_per_cycle_;
  }

  void dump(std::FILE* out) noexcept {
    if (count_ == 0) {
      std::fprintf(out, "latency samples: 0\n");
      return;
    }
    std::sort(samples_, samples_ + static_cast<std::size_t>(count_));
    const auto pct = [&](double p) {
      const std::uint64_t i =
          static_cast<std::uint64_t>(p * static_cast<double>(count_ - 1));
      return to_ns(samples_[i]);
    };
    std::fprintf(
        out,
        "tick-to-trade (n=%llu)  p50=%.1f ns  p90=%.1f ns  p99=%.1f ns  max=%.1f ns\n",
        static_cast<unsigned long long>(count_),
        pct(0.50), pct(0.90), pct(0.99), to_ns(samples_[count_ - 1]));
  }

 private:
  static std::uint64_t rdtsc_raw() noexcept {
#if defined(_MSC_VER)
    _mm_lfence();
    return static_cast<std::uint64_t>(__rdtsc());
#else
    _mm_lfence();
    return static_cast<std::uint64_t>(__rdtsc());
#endif
  }

  static double measure_ns_per_cycle() noexcept {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const std::uint64_t c0 = rdtsc_raw();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() < 20) {
    }
    const std::uint64_t c1 = rdtsc_raw();
    const auto t1 = clock::now();
    const double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double cy = static_cast<double>(c1 - c0);
    return cy > 0.0 ? ns / cy : 0.0;
  }

  std::uint64_t samples_[kMaxLatencySamples]{};
  std::uint64_t count_{0};
  double        ns_per_cycle_{1.0};
};

}  // namespace hft
