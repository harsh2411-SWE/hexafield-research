#pragma once

#include "hft/config.hpp"
#include "hft/cpu.hpp"
#include "hft/spsc_ring.hpp"
#include "hft/types.hpp"

#include <atomic>
#include <cstdint>

namespace hft {

// CRTP strategy base. Derived implements on_book_top / on_trade / on_ack.
// The engine thread never calls the strategy; the strategy thread pops the
// outbound ring and dispatches here — keeping the matching core wait-free
// w.r.t. strategy logic.
template <typename Derived>
class Strategy {
 public:
  using InRing  = SpscRing<WireMsg, kEngineToStratRing>;
  using OutRing = SpscRing<WireMsg, kStratToEngineRing>;

  void bind(InRing* in, OutRing* out) noexcept {
    in_  = in;
    out_ = out;
    stop_.store(false, std::memory_order_relaxed);
    next_id_ = 1'000'000'000ull;  // disjoint from typical feed ids
  }

  void request_stop() noexcept { stop_.store(true, std::memory_order_release); }

  void run() noexcept {
    WireMsg ev{};
    while (!stop_.load(std::memory_order_acquire)) {
      if (!in_->try_pop(ev)) {
        cpu_pause();
        continue;
      }
      dispatch(ev);
    }
    while (in_->try_pop(ev)) {
      dispatch(ev);
    }
  }

 protected:
  [[nodiscard]] std::uint64_t mint_order_id() noexcept { return next_id_++; }

  void send(const WireMsg& cmd) noexcept { out_->push_spin(cmd); }

  void submit_limit(Side side, std::int32_t px, std::uint32_t qty,
                    std::uint64_t id, std::uint64_t md_tsc) noexcept {
    WireMsg c{};
    c.tsc      = rdtsc();
    c.corr_tsc = md_tsc;
    c.order_id = id;
    c.price    = px;
    c.qty      = qty;
    c.kind     = static_cast<std::uint8_t>(CmdType::NewOrder);
    c.side     = static_cast<std::uint8_t>(side);
    c.otype    = static_cast<std::uint8_t>(OrderType::Limit);
    send(c);
  }

  void submit_cancel(std::uint64_t id, std::uint64_t md_tsc) noexcept {
    WireMsg c{};
    c.tsc      = rdtsc();
    c.corr_tsc = md_tsc;
    c.order_id = id;
    c.kind     = static_cast<std::uint8_t>(CmdType::Cancel);
    send(c);
  }

 private:
  void dispatch(const WireMsg& ev) noexcept {
    Derived* self = static_cast<Derived*>(this);
    switch (static_cast<EvtType>(ev.kind)) {
      case EvtType::BookTop:
        self->on_book_top(ev);
        break;
      case EvtType::Trade:
        self->on_trade(ev);
        break;
      case EvtType::Ack:
      case EvtType::CancelAck:
      case EvtType::Reject:
        self->on_ack(ev);
        break;
    }
  }

  InRing*  in_{nullptr};
  OutRing* out_{nullptr};
  std::uint64_t next_id_{0};
  alignas(kCacheLine) std::atomic<bool> stop_{false};
};

}  // namespace hft
