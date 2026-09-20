#pragma once

#include <cstddef>
#include <cstdint>

namespace hft {

// Compile-time capacity knobs. All hot-path storage is sized from these;
// growing any of them requires a rebuild, never a runtime allocation.

inline constexpr std::size_t kCacheLine = 64;

// Power-of-two ring capacities (one slot is reserved so head never laps tail).
inline constexpr std::size_t kMdToEngineRing   = 1u << 16;
inline constexpr std::size_t kStratToEngineRing = 1u << 12;
inline constexpr std::size_t kEngineToStratRing = 1u << 16;

// Order arena / id map. 2^18 live orders is ample for a single-instrument sim.
inline constexpr std::size_t kMaxOrders     = 1u << 18;
inline constexpr std::size_t kOrderMapSlots = 1u << 19; // 2x load factor headroom

// Tick-indexed book: price in integer ticks, inclusive range.
inline constexpr std::int32_t kMinTick   = 0;
inline constexpr std::int32_t kMaxTick   = 200'000; // e.g. $0.00 .. $2,000.00 in cents
inline constexpr std::size_t  kNumLevels = static_cast<std::size_t>(kMaxTick - kMinTick + 1);

inline constexpr std::size_t kMaxFeedEvents     = 1u << 20;
inline constexpr std::size_t kMaxLatencySamples = 1u << 20;

// Market-maker quote half-spread in ticks.
inline constexpr std::int32_t kMmHalfSpread = 2;
inline constexpr std::uint32_t kMmQuoteSize  = 10;

}  // namespace hft
