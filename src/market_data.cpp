#include "hft/market_data.hpp"

#include <cstdio>
#include <cstring>

namespace hft {
namespace {

bool map_side(char c, Side& s) noexcept {
  if (c == 'B' || c == 'b') {
    s = Side::Buy;
    return true;
  }
  if (c == 'S' || c == 's') {
    s = Side::Sell;
    return true;
  }
  return false;
}

bool map_type(char c, CmdType& k, OrderType& o) noexcept {
  switch (c) {
    case 'L':
    case 'l':
      k = CmdType::NewOrder;
      o = OrderType::Limit;
      return true;
    case 'M':
    case 'm':
      k = CmdType::NewOrder;
      o = OrderType::Market;
      return true;
    case 'C':
    case 'c':
      k = CmdType::Cancel;
      o = OrderType::Limit;
      return true;
    case 'R':
    case 'r':
      k = CmdType::Modify;
      o = OrderType::Limit;
      return true;
    default:
      return false;
  }
}

}  // namespace

std::size_t load_csv_feed(const char* path, FeedEvent* out, std::size_t cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    return 0;
  }

  char line[256];
  std::size_t n = 0;
  bool header_skipped = false;

  while (n < cap && std::fgets(line, sizeof(line), f) != nullptr) {
    if (!header_skipped) {
      header_skipped = true;
      if (std::strncmp(line, "timestamp", 9) == 0) {
        continue;
      }
    }
    unsigned long long ts = 0;
    unsigned long long oid = 0;
    int price = 0;
    unsigned qty = 0;
    char t = 0;
    char s = 0;
    // CSV, no spaces required.
    const int got = std::sscanf(line, "%llu,%c,%c,%d,%u,%llu",
                                &ts, &t, &s, &price, &qty, &oid);
    if (got != 6) {
      continue;
    }
    CmdType kind{};
    OrderType otype{};
    Side side{};
    if (!map_type(t, kind, otype) || !map_side(s, side)) {
      continue;
    }
    WireMsg& m = out[n].msg;
    m          = WireMsg{};
    m.ts_ns    = ts;
    m.order_id = oid;
    m.price    = price;
    m.qty      = qty;
    m.aux      = qty;
    m.kind     = static_cast<std::uint8_t>(kind);
    m.side     = static_cast<std::uint8_t>(side);
    m.otype    = static_cast<std::uint8_t>(otype);
    ++n;
  }

  std::fclose(f);
  return n;
}

}  // namespace hft
