#pragma once

#include "hft/config.hpp"
#include "hft/cpu.hpp"
#include "hft/spsc_ring.hpp"
#include "hft/types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hft {

// One decoded feed row. Filled on the cold path (CSV / mmap parse).
struct FeedEvent {
  WireMsg msg;
};

class MarketDataReplay {
 public:
  using OutRing = SpscRing<WireMsg, kMdToEngineRing>;

  void bind(FeedEvent* events, std::size_t count, OutRing* out) noexcept {
    events_ = events;
    count_  = count;
    out_    = out;
    cursor_ = 0;
    stop_.store(false, std::memory_order_relaxed);
  }

  void request_stop() noexcept { stop_.store(true, std::memory_order_release); }

  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

  // Pinned feeder: stamp rdtsc, mark aggressor=1 (external), push.
  void run() noexcept {
    while (cursor_ < count_ && !stop_.load(std::memory_order_acquire)) {
      WireMsg m = events_[cursor_].msg;
      m.tsc       = rdtsc();
      m.corr_tsc  = m.tsc;
      m.aggressor = 1;  // market-data origin
      out_->push_spin(m);
      ++cursor_;
    }
  }

 private:
  FeedEvent*    events_{nullptr};
  std::size_t   count_{0};
  std::size_t   cursor_{0};
  OutRing*      out_{nullptr};
  alignas(kCacheLine) std::atomic<bool> stop_{false};
};

// Cold path: parse CSV into a caller-provided arena. Returns events loaded.
// Format: timestamp_ns,type,side,price,qty,order_id
//   type = L|M|C|R  (limit, market, cancel, modify)
//   side = B|S
std::size_t load_csv_feed(const char* path, FeedEvent* out, std::size_t cap);

}  // namespace hft
