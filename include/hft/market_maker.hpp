#pragma once

#include "hft/config.hpp"
#include "hft/latency.hpp"
#include "hft/strategy.hpp"

#include <cstdint>

namespace hft {

// Two-sided market maker. On every BBO change, if the spread is wide enough
// it parks one bid and one ask one tick inside the market. Quotes are
// cancel/replaced when the mid moves. All identifiers live in registers /
// the strategy object — no heap.
class MarketMaker : public Strategy<MarketMaker> {
 public:
  void bind_latency(LatencyRecorder* rec) noexcept { latency_ = rec; }

  void on_book_top(const WireMsg& ev) noexcept {
    if (ev.aggressor == 0) {
      return;  // ignore echoes of our own quotes
    }
    const std::int32_t bid = ev.price;
    const std::int32_t ask = static_cast<std::int32_t>(ev.ts_ns);
    const bool have_bid    = bid > 0;
    const bool have_ask    = ask > 0;
    if (!have_bid || !have_ask || ask <= bid) {
      return;
    }
    const std::int32_t mid = bid + (ask - bid) / 2;
    const std::int32_t qbid = mid - kMmHalfSpread;
    const std::int32_t qask = mid + kMmHalfSpread;
    if (qbid <= kMinTick || qask >= kMaxTick) {
      return;
    }
    if (qbid == last_bid_px_ && qask == last_ask_px_ && live_bid_ && live_ask_) {
      return;
    }
    requote(Side::Buy,  qbid, ev.corr_tsc != 0 ? ev.corr_tsc : ev.tsc);
    requote(Side::Sell, qask, ev.corr_tsc != 0 ? ev.corr_tsc : ev.tsc);
    last_bid_px_ = qbid;
    last_ask_px_ = qask;
  }

  void on_trade(const WireMsg& ev) noexcept {
    // Inventory sketch: +qty on buy fills, -qty on sell fills.
    if (static_cast<Side>(ev.side) == Side::Buy) {
      inventory_ += static_cast<std::int32_t>(ev.qty);
    } else {
      inventory_ -= static_cast<std::int32_t>(ev.qty);
    }
    ++fills_;
  }

  void on_ack(const WireMsg& ev) noexcept {
    if (latency_ != nullptr && ev.corr_tsc != 0 && ev.tsc >= ev.corr_tsc) {
      latency_->record(ev.tsc - ev.corr_tsc);
    }
    if (static_cast<EvtType>(ev.kind) == EvtType::Ack) {
      if (static_cast<Side>(ev.side) == Side::Buy) {
        bid_id_   = ev.order_id;
        live_bid_ = ev.aux == 0;
      } else {
        ask_id_   = ev.order_id;
        live_ask_ = ev.aux == 0;
      }
    } else if (static_cast<EvtType>(ev.kind) == EvtType::CancelAck) {
      if (ev.order_id == bid_id_) {
        live_bid_ = false;
      }
      if (ev.order_id == ask_id_) {
        live_ask_ = false;
      }
    }
  }

  [[nodiscard]] std::int32_t inventory() const noexcept { return inventory_; }
  [[nodiscard]] std::uint64_t fill_count() const noexcept { return fills_; }

 private:
  void requote(Side side, std::int32_t px, std::uint64_t md_tsc) noexcept {
    const bool is_bid = side == Side::Buy;
    const std::uint64_t live_id = is_bid ? bid_id_ : ask_id_;
    const bool live             = is_bid ? live_bid_ : live_ask_;
    if (live && live_id != 0) {
      submit_cancel(live_id, md_tsc);
      if (is_bid) {
        live_bid_ = false;
      } else {
        live_ask_ = false;
      }
    }
    const std::uint64_t id = mint_order_id();
    if (is_bid) {
      bid_id_ = id;
    } else {
      ask_id_ = id;
    }
    submit_limit(side, px, kMmQuoteSize, id, md_tsc);
  }

  LatencyRecorder* latency_{nullptr};
  std::uint64_t    bid_id_{0};
  std::uint64_t    ask_id_{0};
  std::int32_t     last_bid_px_{0};
  std::int32_t     last_ask_px_{0};
  std::int32_t     inventory_{0};
  std::uint64_t    fills_{0};
  bool             live_bid_{false};
  bool             live_ask_{false};
};

}  // namespace hft
