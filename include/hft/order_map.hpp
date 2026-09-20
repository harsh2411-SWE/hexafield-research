#pragma once

#include "hft/config.hpp"
#include "hft/order.hpp"

#include <cstdint>

namespace hft {

// Open-addressed id → Order* map. Pre-sized, linear probing, tombstones.
// Zero heap traffic after init().
class OrderMap {
 public:
  static constexpr std::uint64_t kEmpty     = 0;
  static constexpr std::uint64_t kTombstone = ~std::uint64_t{0};

  struct Slot {
    std::uint64_t id{kEmpty};
    Order*        order{nullptr};
  };

  void init() noexcept {
    for (std::size_t i = 0; i < kOrderMapSlots; ++i) {
      slots_[i].id    = kEmpty;
      slots_[i].order = nullptr;
    }
    size_ = 0;
  }

  [[nodiscard]] bool insert(std::uint64_t id, Order* o) noexcept {
    if (id == kEmpty || id == kTombstone) {
      return false;
    }
    std::size_t i = hash(id);
    for (std::size_t n = 0; n < kOrderMapSlots; ++n) {
      Slot& s = slots_[i];
      if (s.id == kEmpty || s.id == kTombstone) {
        s.id    = id;
        s.order = o;
        ++size_;
        return true;
      }
      if (s.id == id) {
        s.order = o;
        return true;
      }
      i = (i + 1u) & (kOrderMapSlots - 1u);
    }
    return false;
  }

  [[nodiscard]] Order* find(std::uint64_t id) const noexcept {
    std::size_t i = hash(id);
    for (std::size_t n = 0; n < kOrderMapSlots; ++n) {
      const Slot& s = slots_[i];
      if (s.id == kEmpty) {
        return nullptr;
      }
      if (s.id == id) {
        return s.order;
      }
      i = (i + 1u) & (kOrderMapSlots - 1u);
    }
    return nullptr;
  }

  void erase(std::uint64_t id) noexcept {
    std::size_t i = hash(id);
    for (std::size_t n = 0; n < kOrderMapSlots; ++n) {
      Slot& s = slots_[i];
      if (s.id == kEmpty) {
        return;
      }
      if (s.id == id) {
        s.id    = kTombstone;
        s.order = nullptr;
        --size_;
        return;
      }
      i = (i + 1u) & (kOrderMapSlots - 1u);
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

 private:
  [[nodiscard]] static std::size_t hash(std::uint64_t x) noexcept {
    // SplitMix64 low bits; table is power-of-two.
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<std::size_t>(x) & (kOrderMapSlots - 1u);
  }

  alignas(kCacheLine) Slot slots_[kOrderMapSlots]{};
  std::size_t size_{0};
};

}  // namespace hft
