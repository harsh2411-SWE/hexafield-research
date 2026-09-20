#pragma once

#include "hft/config.hpp"
#include "hft/types.hpp"

#include <cstdint>

namespace hft {

// Resting order. 48 bytes so two orders share a cache line when the matching
// engine walks a price level's FIFO. `next_free` overlays `next` while pooled.
struct Order {
  union {
    Order* next;       // intrusive FIFO at a price level
    Order* next_free;  // intrusive free-list while pooled
  };
  Order*        prev;
  std::uint64_t id;
  std::uint64_t tsc;          // origin tsc (MD or strategy)
  std::int32_t  price;
  std::uint32_t remaining;
  std::uint32_t orig_qty;
  Side          side;
  OrderType     type;
  std::uint8_t  in_book;      // 1 while linked into a level
  std::uint8_t  _pad;
};

static_assert(sizeof(Order) <= 64, "Order must fit in a cache line");
static_assert(alignof(Order) <= 8);

// One price level: FIFO by insertion time (price-time priority).
struct PriceLevel {
  Order*        head{nullptr};
  Order*        tail{nullptr};
  std::uint32_t total_qty{0};
  std::uint32_t count{0};

  [[nodiscard]] bool empty() const noexcept { return head == nullptr; }

  void push_back(Order* o) noexcept {
    o->prev    = tail;
    o->next    = nullptr;
    o->in_book = 1;
    if (tail != nullptr) {
      tail->next = o;
    } else {
      head = o;
    }
    tail = o;
    total_qty += o->remaining;
    ++count;
  }

  void unlink(Order* o) noexcept {
    if (o->prev != nullptr) {
      o->prev->next = o->next;
    } else {
      head = o->next;
    }
    if (o->next != nullptr) {
      o->next->prev = o->prev;
    } else {
      tail = o->prev;
    }
    total_qty -= o->remaining;
    --count;
    o->next    = nullptr;
    o->prev    = nullptr;
    o->in_book = 0;
  }
};

}  // namespace hft
