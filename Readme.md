HexaField Research

A C++20 Low-Latency Market Microstructure & Limit Order Book Simulator

A systems-engineering project exploring the architecture and performance considerations behind modern electronic trading systems.

HexaField Research is a single-instrument market simulation engine built in modern C++20. It implements deterministic price-time-priority matching, preallocated order management, cache-conscious data structures, lock-free SPSC message queues, and a separate event-driven market-making strategy.

The project focuses on low-latency execution paths, predictable memory behavior, and concurrent system design.

«Project scope: An experimental market simulator and systems-engineering research project. It is not connected to a live exchange and is not production HFT infrastructure.»

---

Core Features

Component| Implementation
Language| C++20
Matching| Price-time-priority
Order Book| Integer-tick price indexing
Order Storage| Preallocated object pool
Order Lookup| Open-addressed hash map
Concurrency| Lock-free SPSC ring buffers
Memory| Fixed-capacity allocation
Market Data| CSV replay + synthetic event tape
Strategy| Two-sided market maker
Instrumentation| TSC-based latency sampling
Build| CMake + optimized Release configuration

Architecture

HexaField follows a producer-consumer architecture with an isolated matching engine and a dedicated strategy thread.

flowchart TD
    A["CSV Market Data"] --> B["Market Data Replay"]
    B --> C["Market Data SPSC Ring"]
    C --> E["Matching Engine"]
    D["Strategy Commands"] --> F["Strategy SPSC Ring"]
    F --> E
    E --> G["Limit Order Book"]
    G --> H["Preallocated Order Pool"]
    G --> I["Order ID Map"]
    E --> J["Trade / ACK / BBO Events"]
    J --> K["Output SPSC Ring"]
    K --> L["Market Maker Strategy"]
    L --> D
    L --> M["Latency Recorder"]

The matching engine is the sole consumer of incoming market-data and strategy-command queues. The market-making strategy processes engine events on its own thread.

1. Matching Engine

The matching engine is the central execution component.

It supports:

- Limit and market order processing.
- Price-time-priority matching.
- Resting orders.
- Partial and complete fills.
- Order cancellation and modification.
- Order acknowledgements and rejection events.
- Trade event generation.
- Best bid and ask updates.

The matching path is designed around predictable operations, avoiding dynamic allocation, mutexes, and virtual dispatch in the core processing loop.

2. Limit Order Book

The order book uses an integer-tick price representation with separate bid and ask sides.

Key design choices:

- Direct price-level indexing.
- FIFO queues at individual price levels.
- Cached best bid and best ask.
- Intrusive order linking.
- Constant-time order lookup by ID through a pre-sized hash map.

The design prioritizes predictable access patterns and efficient order lifecycle management.

3. Memory Management

Dynamic allocation in latency-sensitive processing paths can introduce unpredictable overhead.

HexaField uses:

- Fixed-capacity object pools.
- Preallocated order storage.
- Open-addressed order-ID mapping.
- Compile-time capacity configuration.
- Intrusive free-list management.

The objective is to keep the matching hot path free from routine heap allocation.

4. Concurrent Message Passing

The engine uses fixed-size, trivially copyable messages exchanged through lock-free Single-Producer Single-Consumer (SPSC) ring buffers.

Implementation details:

- Power-of-two queue capacities.
- Atomic producer and consumer indices.
- Acquire-release synchronization.
- Cache-line separation of queue metadata.
- Spin-based backpressure with CPU pause instructions.

This architecture separates market-data ingestion, order processing, and strategy execution without introducing mutex-based coordination into the message-passing path.

5. Market Data Replay

The simulator supports CSV-based market-data input.

It also generates a deterministic synthetic two-sided event tape, allowing the matching engine and strategy to operate without depending entirely on an external dataset.

Supported CSV event types:

- Limit orders
- Market orders
- Cancellations
- Modifications

6. Market-Making Strategy

The project includes an event-driven two-sided market-making strategy.

It:

- Reacts to best bid/ask updates.
- Maintains symmetric quotes around the calculated midpoint.
- Cancels and replaces quotes when market conditions change.
- Tracks inventory from executed fills.
- Processes acknowledgements and trade events.

This provides a practical environment for exploring the interaction between order-book dynamics and automated trading strategies.

7. Performance Instrumentation

HexaField includes a preallocated latency recorder based on CPU timestamp-counter measurements.

It provides:

- Timestamp-counter calibration.
- Correlated event measurements.
- Preallocated sample storage.
- Percentile-based reporting.
- P50, P90, P99 and maximum latency output.

These measurements are intended for experimentation and profiling, not as independently verified exchange-grade latency benchmarks.

Build & Run

Requirements

- C++20-compatible compiler
- CMake 3.20+
- Threads support

Build

git clone https://github.com/harsh2411-SWE/hexafield-research.git

cd hexafield-research

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release

Execute

./build/hft_sim

Alternatively, provide a custom CSV feed:

./build/hft_sim path/to/market_data.csv

CSV Format

timestamp,type,side,price,qty,order_id
100000,L,B,10000,50,1
100001,L,S,10020,40,2
100002,M,B,0,10,3

Supported event codes:

Code| Meaning
L| Limit
M| Market
C| Cancel
R| Modify
B| Buy
S| Sell

Engineering Focus

HexaField Research explores several important systems-programming concepts:

- Low-latency-oriented C++ design.
- Cache-conscious data structures.
- Lock-free communication primitives.
- Memory allocation control.
- Concurrent producer-consumer architectures.
- Deterministic event processing.
- Market microstructure simulation.
- Performance measurement and profiling.

Disclaimer

This project is intended for educational purposes, systems-engineering experimentation, and research into electronic market infrastructure.

It does not provide live exchange connectivity, production-grade risk controls, or verified high-frequency trading performance.

---

HexaField Research
C++20 · Systems Engineering · Market Microstructure · Low-Latency Architecture