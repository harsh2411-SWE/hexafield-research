#pragma once

#include "hft/config.hpp"
#include "hft/memory_pool.hpp"
#include "hft/order.hpp"
#include "hft/order_map.hpp"

#include <cstdint>
#include <cstring>

namespace hft {

// Single-instrument limit order book: price-time priority.
//
// Price lookup is O(1) via a flat array indexed by integer tick.
// Insert / cancel / fill at a known Order* is O(1) via the intrusive FIFO.
// Best-bid / best-ask are cached; a level drain walks adjacent ticks until the
// next non-empty level (acceptable for a dense tick grid).
class LimitOrderBook {
 public:
  void init() noexcept {
    std::memset(bids_, 0, sizeof(bids_));
    std::memset(asks_, 0, sizeof(asks_));
    pool_.init();
    map_.init();
    best_bid_ = kMinTick - 1;
    best_ask_ = kMaxTick + 1;
    next_id_  = 1;
  }

  [[nodiscard]] std::int32_t best_bid() const noexcept { return best_bid_; }
  [[nodiscard]] std::int32_t best_ask() const noexcept { return best_ask_; }
  [[nodiscard]] bool has_bid() const noexcept { return best_bid_ >= kMinTick; }
  [[nodiscard]] bool has_ask() const noexcept { return best_ask_ <= kMaxTick; }

  [[nodiscard]] std::uint32_t bid_qty() const noexcept {
    return has_bid() ? level(Side::Buy, best_bid_).total_qty : 0;
  }
  [[nodiscard]] std::uint32_t ask_qty() const noexcept {
    return has_ask() ? level(Side::Sell, best_ask_).total_qty : 0;
  }

  [[nodiscard]] Order* find(std::uint64_t id) const noexcept { return map_.find(id); }
  [[nodiscard]] ObjectPool<Order, kMaxOrders>& pool() noexcept { return pool_; }

  // Assign a dense id if the caller passed 0 (strategy-originated orders).
  [[nodiscard]] std::uint64_t mint_id(std::uint64_t requested) noexcept {
    if (requested != 0) {
      if (requested >= next_id_) {
        next_id_ = requested + 1;
      }
      return requested;
    }
    return next_id_++;
  }

  Order* rest(std::uint64_t id,
              Side side,
              std::int32_t price,
              std::uint32_t qty,
              std::uint64_t tsc) noexcept {
    if (!valid_price(price) || qty == 0) {
      return nullptr;
    }
    Order* o = pool_.acquire();
    if (o == nullptr) {
      return nullptr;
    }
    o->id        = id;
    o->tsc       = tsc;
    o->price     = price;
    o->remaining = qty;
    o->orig_qty  = qty;
    o->side      = side;
    o->type      = OrderType::Limit;
    o->prev      = nullptr;

    if (!map_.insert(id, o)) {
      pool_.release(o);
      return nullptr;
    }
    PriceLevel& lvl = level(side, price);
    lvl.push_back(o);
    touch_bbo_on_insert(side, price);
    return o;
  }

  // Cancel by id. Returns residual qty that was pulled, 0 if unknown.
  std::uint32_t cancel(std::uint64_t id) noexcept {
    Order* o = map_.find(id);
    if (o == nullptr || o->in_book == 0) {
      return 0;
    }
    const std::uint32_t leftover = o->remaining;
    const Side side              = o->side;
    const std::int32_t price     = o->price;
    PriceLevel& lvl              = level(side, price);
    lvl.unlink(o);
    map_.erase(id);
    pool_.release(o);
    if (lvl.empty()) {
      repair_bbo_on_empty(side, price);
    }
    return leftover;
  }

  // Reduce remaining at the head of a level after a fill. If the order dies,
  // it is unlinked and returned for the caller to emit a trade then release.
  Order* consume_head(Side maker_side, std::int32_t px, std::uint32_t take) noexcept {
    PriceLevel& lvl = level(maker_side, px);
    Order* head     = lvl.head;
    if (head == nullptr) {
      return nullptr;
    }
    head->remaining -= take;
    lvl.total_qty -= take;
    if (head->remaining == 0) {
      // remaining is already 0, so unlink must not subtract qty again.
      if (head->prev != nullptr) {
        head->prev->next = head->next;
      } else {
        lvl.head = head->next;
      }
      if (head->next != nullptr) {
        head->next->prev = head->prev;
      } else {
        lvl.tail = head->prev;
      }
      --lvl.count;
      head->next    = nullptr;
      head->prev    = nullptr;
      head->in_book = 0;
      if (lvl.empty()) {
        repair_bbo_on_empty(maker_side, px);
      }
    }
    return head;
  }

  void destroy_filled(Order* o) noexcept {
    map_.erase(o->id);
    pool_.release(o);
  }

  [[nodiscard]] static constexpr bool valid_price(std::int32_t p) noexcept {
    return p >= kMinTick && p <= kMaxTick;
  }

  [[nodiscard]] PriceLevel& level(Side side, std::int32_t price) noexcept {
    const std::size_t idx = static_cast<std::size_t>(price - kMinTick);
    return side == Side::Buy ? bids_[idx] : asks_[idx];
  }
  [[nodiscard]] const PriceLevel& level(Side side, std::int32_t price) const noexcept {
    const std::size_t idx = static_cast<std::size_t>(price - kMinTick);
    return side == Side::Buy ? bids_[idx] : asks_[idx];
  }

 private:
  void touch_bbo_on_insert(Side side, std::int32_t price) noexcept {
    if (side == Side::Buy) {
      if (price > best_bid_) {
        best_bid_ = price;
      }
    } else if (price < best_ask_) {
      best_ask_ = price;
    }
  }

  void repair_bbo_on_empty(Side side, std::int32_t emptied) noexcept {
    if (side == Side::Buy) {
      if (emptied != best_bid_) {
        return;
      }
      std::int32_t p = emptied - 1;
      while (p >= kMinTick && bids_[static_cast<std::size_t>(p - kMinTick)].empty()) {
        --p;
      }
      best_bid_ = p;  // may fall to kMinTick-1
    } else {
      if (emptied != best_ask_) {
        return;
      }
      std::int32_t p = emptied + 1;
      while (p <= kMaxTick && asks_[static_cast<std::size_t>(p - kMinTick)].empty()) {
        ++p;
      }
      best_ask_ = p;  // may rise to kMaxTick+1
    }
  }

  // 2 * kNumLevels * sizeof(PriceLevel) ≈ 2 * 200001 * 24 ≈ 9.6 MiB, static.
  PriceLevel bids_[kNumLevels];
  PriceLevel asks_[kNumLevels];
  ObjectPool<Order, kMaxOrders> pool_;
  OrderMap                      map_;
  std::int32_t                  best_bid_;
  std::int32_t                  best_ask_;
  std::uint64_t                 next_id_;
};

}  // namespace hft
