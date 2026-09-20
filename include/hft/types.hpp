#pragma once

#include "hft/config.hpp"

#include <cstdint>
#include <type_traits>

namespace hft {

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

// Commands that enter the matching engine (MD replay + strategy).
enum class CmdType : std::uint8_t {
  NewOrder = 0,
  Cancel   = 1,
  Modify   = 2
};

// Events that leave the matching engine toward the strategy.
enum class EvtType : std::uint8_t {
  Ack        = 0,
  CancelAck  = 1,
  Reject     = 2,
  Trade      = 3,
  BookTop    = 4  // BBO snapshot after a mutating command
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

// Wire message: exactly one cache line. Copied by value through SPSC rings.
// Layout is identical for commands and engine events; `kind` discriminates.
struct alignas(kCacheLine) WireMsg {
  std::uint64_t tsc;          // rdtsc captured at origin (tick-to-trade start)
  std::uint64_t corr_tsc;     // MD tsc echoed by strategy for latency pairing
  std::uint64_t order_id;
  std::uint64_t ts_ns;        // feed timestamp (exchange time), 0 if synthetic
  std::int32_t  price;        // integer ticks
  std::uint32_t qty;
  std::uint32_t aux;          // remaining qty on trades, new qty on modify
  std::uint8_t  kind;         // CmdType or EvtType
  std::uint8_t  side;         // Side
  std::uint8_t  otype;        // OrderType
  std::uint8_t  aggressor;    // 1 if this side took liquidity
  std::uint32_t _pad;
};

static_assert(sizeof(WireMsg) == kCacheLine, "WireMsg must occupy exactly one cache line");
static_assert(std::is_trivially_copyable_v<WireMsg>);

}  // namespace hft
