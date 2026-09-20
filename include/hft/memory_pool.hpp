#pragma once

#include "hft/config.hpp"

#include <cstddef>
#include <cstdint>

namespace hft {

// Intrusive free-list object arena. `T` must expose `T* next_free`.
// All storage is a static array inside the pool object; acquire/release are
// pointer swings — never new/delete.
template <typename T, std::size_t N>
class ObjectPool {
 public:
  void init() noexcept {
    for (std::size_t i = 0; i + 1 < N; ++i) {
      storage_[i].next_free = &storage_[i + 1];
    }
    storage_[N - 1].next_free = nullptr;
    free_head_ = &storage_[0];
    in_use_    = 0;
  }

  [[nodiscard]] T* acquire() noexcept {
    T* node = free_head_;
    if (node == nullptr) {
      return nullptr;
    }
    free_head_ = node->next_free;
    node->next_free = nullptr;
    ++in_use_;
    return node;
  }

  void release(T* node) noexcept {
    node->next_free = free_head_;
    free_head_ = node;
    --in_use_;
  }

  [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  // Direct index access for cold-path dumps / tests.
  [[nodiscard]] T& at(std::size_t i) noexcept { return storage_[i]; }

 private:
  alignas(kCacheLine) T storage_[N]{};
  T*          free_head_{nullptr};
  std::size_t in_use_{0};
};

}  // namespace hft
