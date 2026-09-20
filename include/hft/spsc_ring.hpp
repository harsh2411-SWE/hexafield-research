#pragma once

#include "hft/config.hpp"
#include "hft/cpu.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hft {

// Lock-free SPSC ring. Capacity is Size-1 live messages so the producer never
// overwrites an unread slot.
//
// False-sharing isolation: producer cursor + cached consumer index live on one
// line; consumer cursor + cached producer index on another. The payload array
// starts on its own line.
template <typename T, std::size_t Size>
class SpscRing {
  static_assert((Size & (Size - 1u)) == 0u, "Size must be a power of two");
  static_assert(Size >= 2u);
  static_assert(std::is_trivially_copyable_v<T>);

  static constexpr std::size_t kMask = Size - 1u;

 public:
  SpscRing() noexcept = default;

  [[nodiscard]] bool try_push(const T& value) noexcept {
    const std::size_t w = prod_.write_idx.load(std::memory_order_relaxed);
    if ((w - prod_.read_cached) >= (Size - 1u)) {
      prod_.read_cached = cons_.read_idx.load(std::memory_order_acquire);
      if ((w - prod_.read_cached) >= (Size - 1u)) {
        return false;
      }
    }
    slots_[w & kMask] = value;
    prod_.write_idx.store(w + 1u, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& out) noexcept {
    const std::size_t r = cons_.read_idx.load(std::memory_order_relaxed);
    if (cons_.write_cached == r) {
      cons_.write_cached = prod_.write_idx.load(std::memory_order_acquire);
      if (cons_.write_cached == r) {
        return false;
      }
    }
    out = slots_[r & kMask];
    cons_.read_idx.store(r + 1u, std::memory_order_release);
    return true;
  }

  void push_spin(const T& value) noexcept {
    while (!try_push(value)) {
      cpu_pause();
    }
  }

  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t w = prod_.write_idx.load(std::memory_order_acquire);
    const std::size_t r = cons_.read_idx.load(std::memory_order_acquire);
    return w - r;
  }

 private:
  struct alignas(kCacheLine) ProducerLine {
    std::atomic<std::size_t> write_idx{0};
    std::size_t              read_cached{0};
  };
  struct alignas(kCacheLine) ConsumerLine {
    std::atomic<std::size_t> read_idx{0};
    std::size_t              write_cached{0};
  };

  ProducerLine prod_{};
  ConsumerLine cons_{};
  alignas(kCacheLine) T slots_[Size]{};
};

}  // namespace hft
