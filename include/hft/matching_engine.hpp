#pragma once

#include "hft/cpu.hpp"
#include "hft/order_book.hpp"
#include "hft/spsc_ring.hpp"
#include "hft/types.hpp"

#include <atomic>
#include <cstdint>

namespace hft {

// Matching engine: sole consumer of the market-data and strategy SPSC rings.
// Hot path is process(): match → rest/cancel → emit Trade/Ack/BookTop.
// Zero allocations, zero mutexes, no virtual calls.
class MatchingEngine {
 public:
  using MdRing    = SpscRing<WireMsg, kMdToEngineRing>;
  using StratRing = SpscRing<WireMsg, kStratToEngineRing>;
  using OutRing   = SpscRing<WireMsg, kEngineToStratRing>;

  void init(MdRing* md, StratRing* strat, OutRing* out) noexcept {
    book_.init();
    md_in_    = md;
    strat_in_ = strat;
    out_      = out;
    trades_   = 0;
    commands_ = 0;
    stop_.store(false, std::memory_order_relaxed);
  }

  LimitOrderBook& book() noexcept { return book_; }

  void request_stop() noexcept { stop_.store(true, std::memory_order_release); }

  [[nodiscard]] std::uint64_t trade_count() const noexcept { return trades_; }
  [[nodiscard]] std::uint64_t command_count() const noexcept { return commands_; }

  void run() noexcept {
    WireMsg msg{};
    while (!stop_.load(std::memory_order_acquire)) {
      bool work = false;
      if (strat_in_->try_pop(msg)) {
        process(msg);
        work = true;
      }
      if (md_in_->try_pop(msg)) {
        process(msg);
        work = true;
      }
      if (!work) {
        cpu_pause();
      }
    }
    while (strat_in_->try_pop(msg)) {
      process(msg);
    }
    while (md_in_->try_pop(msg)) {
      process(msg);
    }
  }

  void process(const WireMsg& cmd) noexcept {
    ++commands_;
    switch (static_cast<CmdType>(cmd.kind)) {
      case CmdType::NewOrder:
        on_new(cmd);
        break;
      case CmdType::Cancel:
        on_cancel(cmd);
        break;
      case CmdType::Modify:
        on_modify(cmd);
        break;
    }
    emit_bbo(cmd);
  }

 private:
  void on_new(const WireMsg& cmd) noexcept {
    const Side side = static_cast<Side>(cmd.side);
    const auto otype = static_cast<OrderType>(cmd.otype);
    std::uint32_t leftover = cmd.qty;
    const std::uint64_t id = book_.mint_id(cmd.order_id);

    leftover = match(side, cmd.price, leftover, otype, id, cmd.tsc, cmd.corr_tsc);

    if (otype == OrderType::Limit && leftover > 0) {
      Order* o = book_.rest(id, side, cmd.price, leftover, cmd.tsc);
      emit(EvtType::Ack, cmd, id, cmd.price, leftover, o != nullptr ? 0 : 1);
    } else if (leftover == cmd.qty && otype == OrderType::Market) {
      emit(EvtType::Reject, cmd, id, cmd.price, leftover, 1);
    } else {
      emit(EvtType::Ack, cmd, id, cmd.price, leftover, 0);
    }
  }

  void on_cancel(const WireMsg& cmd) noexcept {
    const std::uint32_t pulled = book_.cancel(cmd.order_id);
    emit(EvtType::CancelAck, cmd, cmd.order_id, cmd.price, pulled, pulled == 0 ? 1 : 0);
  }

  void on_modify(const WireMsg& cmd) noexcept {
    (void)book_.cancel(cmd.order_id);
    WireMsg neu = cmd;
    neu.kind    = static_cast<std::uint8_t>(CmdType::NewOrder);
    on_new(neu);
  }

  std::uint32_t match(Side taker_side,
                      std::int32_t limit_px,
                      std::uint32_t qty,
                      OrderType otype,
                      std::uint64_t taker_id,
                      std::uint64_t tsc,
                      std::uint64_t corr) noexcept {
    const bool is_buy = taker_side == Side::Buy;
    while (qty > 0) {
      if (is_buy) {
        if (!book_.has_ask()) {
          break;
        }
        const std::int32_t px = book_.best_ask();
        if (otype == OrderType::Limit && px > limit_px) {
          break;
        }
        qty = fill_against(Side::Sell, px, qty, taker_id, tsc, corr);
      } else {
        if (!book_.has_bid()) {
          break;
        }
        const std::int32_t px = book_.best_bid();
        if (otype == OrderType::Limit && px < limit_px) {
          break;
        }
        qty = fill_against(Side::Buy, px, qty, taker_id, tsc, corr);
      }
    }
    return qty;
  }

  std::uint32_t fill_against(Side maker_side,
                             std::int32_t px,
                             std::uint32_t qty,
                             std::uint64_t taker_id,
                             std::uint64_t tsc,
                             std::uint64_t corr) noexcept {
    PriceLevel& lvl = book_.level(maker_side, px);
    while (qty > 0 && !lvl.empty()) {
      Order* maker = lvl.head;
      const std::uint32_t take =
          qty < maker->remaining ? qty : maker->remaining;
      const std::uint64_t maker_id = maker->id;
      const std::uint32_t maker_left_after = maker->remaining - take;

      (void)book_.consume_head(maker_side, px, take);
      qty -= take;
      ++trades_;

      WireMsg tr{};
      tr.tsc       = rdtsc();
      tr.corr_tsc  = corr != 0 ? corr : tsc;
      tr.order_id  = taker_id;
      tr.ts_ns     = maker_id;
      tr.price     = px;
      tr.qty       = take;
      tr.aux       = maker_left_after;
      tr.kind      = static_cast<std::uint8_t>(EvtType::Trade);
      tr.side      = static_cast<std::uint8_t>(opposite(maker_side));
      tr.aggressor = 1;
      emit_raw(tr);

      if (maker_left_after == 0) {
        book_.destroy_filled(maker);
      }
    }
    return qty;
  }

  void emit(EvtType t, const WireMsg& src, std::uint64_t id,
            std::int32_t px, std::uint32_t qty, std::uint32_t reject) noexcept {
    WireMsg e{};
    e.tsc       = rdtsc();
    e.corr_tsc  = src.corr_tsc != 0 ? src.corr_tsc : src.tsc;
    e.order_id  = id;
    e.ts_ns     = src.ts_ns;
    e.price     = px;
    e.qty       = qty;
    e.aux       = reject;
    e.kind      = static_cast<std::uint8_t>(t);
    e.side      = src.side;
    e.otype     = src.otype;
    e.aggressor = src.aggressor;
    emit_raw(e);
  }

  void emit_bbo(const WireMsg& src) noexcept {
    WireMsg e{};
    e.tsc       = rdtsc();
    e.corr_tsc  = src.corr_tsc != 0 ? src.corr_tsc : src.tsc;
    e.order_id  = 0;
    e.price     = book_.has_bid() ? book_.best_bid() : 0;
    e.qty       = book_.bid_qty();
    e.aux       = book_.ask_qty();
    e.ts_ns     = static_cast<std::uint64_t>(book_.has_ask() ? book_.best_ask() : 0);
    e.kind      = static_cast<std::uint8_t>(EvtType::BookTop);
    // Propagate origin: strategy ignores BookTops caused by its own quotes.
    e.aggressor = src.aggressor;
    emit_raw(e);
  }

  void emit_raw(const WireMsg& e) noexcept { out_->push_spin(e); }

  LimitOrderBook book_{};
  MdRing*        md_in_{nullptr};
  StratRing*     strat_in_{nullptr};
  OutRing*       out_{nullptr};
  std::uint64_t  trades_{0};
  std::uint64_t  commands_{0};
  alignas(kCacheLine) std::atomic<bool> stop_{false};
};

}  // namespace hft
