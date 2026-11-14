HexaField Research
C++20 limit order book and market-microstructure simulator
HexaField Research is an experimental, single-instrument market simulator written in modern C++. It explores price-time-priority matching, preallocated order storage, cache-conscious data structures, SPSC message passing, and a separate market-making strategy thread.
The project is a systems-engineering and learning exercise. It is not an exchange-connected trading system or production HFT infrastructure.
What it contains
Limit order book: integer-tick price levels, FIFO queues at each price, cached best bid/ask, and order lookup by ID.
Matching engine: limit and market order processing, matching, resting orders, cancellation, modification, acknowledgements, trade events, and book-top events.
Preallocated order storage: fixed-capacity object pool and open-addressed order-ID map.
Concurrent message passing: fixed-size, trivially-copyable messages moved through single-producer/single-consumer rings.
Market-data replay: CSV input followed by a deterministic synthetic two-sided event tape.
Example strategy: a simple two-sided market maker that reacts to book-top updates and tracks a basic inventory.
Latency instrumentation: timestamp-counter sampling and percentile reporting for the current acknowledgement-based measurement path.
Architecture
 data/mock_feed.csv
        |
        v
 CSV loader + deterministic synthetic tape
        |
        v
 MarketDataReplay thread
        |
        | market-data SPSC ring
        v
 +---------------------------+
 |       MatchingEngine      |
 |                           |
 |  LimitOrderBook           |
 |  - price-indexed levels   |
 |  - FIFO orders            |
 |  - order-ID map            |
 |  - preallocated pool      |
 +---------------------------+
        |             ^
        | events      | strategy commands
        v             |
 Engine-to-strategy   Strategy-to-engine
     SPSC ring          SPSC ring
        |             ^
        v             |
  MarketMaker strategy thread
The matching engine is the sole consumer of the market-data and strategy command rings. The strategy consumes engine events on its own thread. The market-data replay runs on a separate thread.
Repository layout
hexafield-research/
├── CMakeLists.txt
├── data/
│   └── mock_feed.csv
├── include/
│   └── hft/
│       ├── config.hpp
│       ├── cpu.hpp
│       ├── latency.hpp
│       ├── market_data.hpp
│       ├── market_maker.hpp
│       ├── matching_engine.hpp
│       ├── memory_pool.hpp
│       ├── order.hpp
│       ├── order_book.hpp
│       ├── order_map.hpp
│       ├── spsc_ring.hpp
│       ├── strategy.hpp
│       └── types.hpp
└── src/
    ├── main.cpp
    └── market_data.cpp
Requirements
CMake 3.20 or newer
A C++20 compiler
A supported native threading toolchain
The CMake configuration has compiler-specific options for MSVC and GNU/Clang. The GNU/Clang Release configuration currently enables -O3, -march=native, LTO, and disables exceptions and RTTI. These settings are intended for local experimentation; -march=native can produce binaries that are not portable to older or different CPUs.
Build
From the repository root:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
The executable target is hft_sim.
Run
Linux / macOS shell:
./build/hft_sim
On a multi-configuration generator such as Visual Studio, the executable is typically under build/Release/.
By default, the program searches for mock_feed.csv, data/mock_feed.csv, and the configured source data directory. You can also pass an explicit feed path:
./build/hft_sim data/mock_feed.csv
The program loads the CSV events, appends a deterministic synthetic tape, starts the market-data, matching-engine, and strategy threads, then prints command/trade counts, inventory, best bid/ask, and latency summary output.
The replay is synthetic and intended for exercising the simulator; it is not a representation of live exchange data.
CSV feed format
The loader expects a header followed by rows in this format:
timestamp_ns,type,side,price,qty,order_id
1000,L,B,10000,50,1
1100,L,B,9990,80,2
1200,L,S,10020,50,3
Field            Meaning
timestamp_ns   Input/feed timestamp type           L limit, M market, C cancel, R modify side           B buy, S sell price          Integer price ticks qty            Order quantity order_id       Order identifier
Prices are represented as integer ticks. The configured book range is defined in include/hft/config.hpp.
Implementation notes
Order book and memory
The book uses a flat array of price levels over a configured integer-tick range. Each price level maintains an intrusive FIFO queue. Orders are stored in a fixed-capacity object pool, and the order-ID map uses open addressing with linear probing and tombstones.
SPSC queues
The ring buffer is designed for exactly one producer and one consumer per queue. It uses power-of-two capacities, acquire/release atomics, cached indices, and cache-line separation. Do not use an instance as a multi-producer or multi-consumer queue.
Strategy dispatch
The example market maker is dispatched through a CRTP strategy base. It reacts to book-top and trade events and submits limit/cancel commands through the strategy-to-engine ring.
CPU affinity
The executable attempts to pin the engine, strategy, and market-data threads to logical CPU indices. Affinity requests may fail or behave differently depending on operating system permissions and available CPUs; the program continues if pinning is unavailable.
Performance measurement
The project includes timestamp-counter sampling and reports percentile values from the current recorder. Treat these numbers as experimental instrumentation, not as independently validated exchange latency or end-to-end tick-to-trade performance.
For meaningful comparisons, record at least:
CPU model and operating system
Compiler and compiler version
Build type and compile flags
Feed size and event mix
Warm-up and measurement procedure
Whether CPU affinity was successfully applied
Repeated-run distributions, not only one result
No throughput or latency figures are claimed in this README. Publish benchmark numbers only alongside the environment and methodology used to obtain them.
Current scope and limitations
This repository is an evolving simulator. Before treating it as a correctness or performance reference, the implementation should be strengthened with:
Automated tests for price-time priority, partial fills, cancel/modify behavior, and book invariants
Collision/tombstone and duplicate-ID tests for the order map
Explicit validation of malformed CSV rows and invalid order fields
A documented shutdown/drain protocol for all producer-consumer queues
Sanitizer and stress-test runs
Reproducible benchmarks with clearly defined latency boundaries
The current example is single-instrument and uses synthetic replay; it does not implement exchange connectivity, a production risk layer, persistence, or a full market-data protocol.
Author
Harsh Kumar Jha
C++20 · Systems Programming · Backend Engineering
GitHub: harsh2411-SWE
HexaField Research is an independent engineering project for exploring market simulation and performance-sensitive C++ design. It is not financial advice and is not intended for live trading.