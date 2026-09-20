#include "hft/cpu.hpp"
#include "hft/latency.hpp"
#include "hft/market_data.hpp"
#include "hft/market_maker.hpp"
#include "hft/matching_engine.hpp"
#include "hft/spsc_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

// Static world: ~tens of MiB in BSS. Never constructed on the stack.
struct alignas(hft::kCacheLine) World {
  hft::SpscRing<hft::WireMsg, hft::kMdToEngineRing>     md_ring;
  hft::SpscRing<hft::WireMsg, hft::kStratToEngineRing>  strat_ring;
  hft::SpscRing<hft::WireMsg, hft::kEngineToStratRing>  out_ring;
  hft::MatchingEngine                                   engine;
  hft::MarketMaker                                      mm;
  hft::MarketDataReplay                                 replay;
  hft::LatencyRecorder                                  latency;
  hft::FeedEvent                                        feed[hft::kMaxFeedEvents];
};

World& world() {
  static World w{};
  return w;
}

void append_event(hft::FeedEvent* ev, std::size_t& n, std::size_t cap,
                  hft::CmdType kind, hft::OrderType otype, hft::Side side,
                  std::int32_t px, std::uint32_t qty, std::uint64_t id,
                  std::uint64_t ts) noexcept {
  if (n >= cap) {
    return;
  }
  hft::WireMsg& m = ev[n].msg;
  m               = hft::WireMsg{};
  m.ts_ns         = ts;
  m.order_id      = id;
  m.price         = px;
  m.qty           = qty;
  m.aux           = qty;
  m.kind          = static_cast<std::uint8_t>(kind);
  m.side          = static_cast<std::uint8_t>(side);
  m.otype         = static_cast<std::uint8_t>(otype);
  ++n;
}

// Deterministic two-sided tape so the MM has a living market after the CSV.
std::size_t synthesize(hft::FeedEvent* ev, std::size_t n, std::size_t cap) noexcept {
  std::uint64_t id = 10'000;
  std::uint64_t ts = 100'000;
  std::int32_t  bid = 10'000;
  std::int32_t  ask = 10'020;
  std::uint32_t rng = 0xC0FFEEu;

  const auto rnd = [&]() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
  };

  for (int i = 0; i < 30; ++i) {
    append_event(ev, n, cap, hft::CmdType::NewOrder, hft::OrderType::Limit,
                 hft::Side::Buy, bid - i, 40 + static_cast<std::uint32_t>(i), id++, ts++);
    append_event(ev, n, cap, hft::CmdType::NewOrder, hft::OrderType::Limit,
                 hft::Side::Sell, ask + i, 40 + static_cast<std::uint32_t>(i), id++, ts++);
  }

  constexpr int kTape = 50'000;
  for (int t = 0; t < kTape && n + 4 < cap; ++t) {
    const std::uint32_t r = rnd();
    if ((r & 3u) == 0u && bid > 9'800) {
      --bid;
      --ask;
    } else if ((r & 3u) == 1u && ask < 10'200) {
      ++bid;
      ++ask;
    }
    const std::uint32_t qty = 5 + (r % 20u);
    if (r & 8u) {
      append_event(ev, n, cap, hft::CmdType::NewOrder, hft::OrderType::Limit,
                   hft::Side::Buy, bid, qty, id++, ts++);
    } else {
      append_event(ev, n, cap, hft::CmdType::NewOrder, hft::OrderType::Limit,
                   hft::Side::Sell, ask, qty, id++, ts++);
    }
    if ((r % 17u) == 0u) {
      append_event(ev, n, cap, hft::CmdType::NewOrder, hft::OrderType::Market,
                   (r & 16u) ? hft::Side::Buy : hft::Side::Sell,
                   0, 3 + (r % 8u), id++, ts++);
    }
  }
  return n;
}

const char* pick_feed_path(int argc, char** argv) {
  if (argc > 1) {
    return argv[1];
  }
  static const char* kCandidates[] = {
      "mock_feed.csv",
      "data/mock_feed.csv",
#ifdef HFT_DATA_DIR
      HFT_DATA_DIR "/mock_feed.csv",
#endif
  };
  for (const char* p : kCandidates) {
    if (std::FILE* f = std::fopen(p, "rb")) {
      std::fclose(f);
      return p;
    }
  }
  return nullptr;
}

void wait_quiet(World& w) noexcept {
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::seconds(2);
  int idle            = 0;
  while (clock::now() < deadline && idle < 50'000) {
    const bool empty = w.md_ring.size_approx() == 0 &&
                       w.strat_ring.size_approx() == 0 &&
                       w.out_ring.size_approx() == 0;
    idle             = empty ? idle + 1 : 0;
    hft::cpu_pause();
  }
}

}  // namespace

int main(int argc, char** argv) {
  World& w = world();

  w.latency.calibrate();
  w.latency.reset();
  w.engine.init(&w.md_ring, &w.strat_ring, &w.out_ring);
  w.mm.bind(&w.out_ring, &w.strat_ring);
  w.mm.bind_latency(&w.latency);

  const char* path = pick_feed_path(argc, argv);
  std::size_t n    = 0;
  if (path != nullptr) {
    n = hft::load_csv_feed(path, w.feed, hft::kMaxFeedEvents);
    std::printf("loaded %zu csv events from %s\n", n, path);
  } else {
    std::printf("no csv feed found; using synthetic tape only\n");
  }
  n = synthesize(w.feed, n, hft::kMaxFeedEvents);
  std::printf("feed size %zu events\n", n);

  w.replay.bind(w.feed, n, &w.md_ring);

  const unsigned hc = std::thread::hardware_concurrency();
  const unsigned cpu_engine = 0;
  const unsigned cpu_strat  = hc > 1 ? 1u : 0u;
  const unsigned cpu_md     = hc > 2 ? 2u : 0u;

  std::printf("pinning engine=%u strategy=%u md=%u (hw_concurrency=%u)\n",
              cpu_engine, cpu_strat, cpu_md, hc);

  std::thread engine_thr([&] {
    (void)hft::pin_this_thread(cpu_engine);
    w.engine.run();
  });
  std::thread strat_thr([&] {
    (void)hft::pin_this_thread(cpu_strat);
    w.mm.run();
  });
  std::thread md_thr([&] {
    (void)hft::pin_this_thread(cpu_md);
    w.replay.run();
  });

  md_thr.join();
  wait_quiet(w);

  w.engine.request_stop();
  engine_thr.join();
  w.mm.request_stop();
  strat_thr.join();

  std::printf("commands=%llu trades=%llu mm_fills=%llu inventory=%d\n",
              static_cast<unsigned long long>(w.engine.command_count()),
              static_cast<unsigned long long>(w.engine.trade_count()),
              static_cast<unsigned long long>(w.mm.fill_count()),
              w.mm.inventory());
  std::printf("bbo bid=%d x %u  ask=%d x %u\n",
              w.engine.book().has_bid() ? w.engine.book().best_bid() : 0,
              w.engine.book().bid_qty(),
              w.engine.book().has_ask() ? w.engine.book().best_ask() : 0,
              w.engine.book().ask_qty());
  w.latency.dump(stdout);
  return 0;
}
