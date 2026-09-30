# Low-Latency Limit Order Book (C++17)

A limit order book and matching engine with price-time priority, built around a
pre-allocated **memory pool** so the hot path never calls `malloc`.

## Features

- Limit orders on both sides, matched by **price-time priority** (best price first, then oldest order first)
- Partial fills; trades execute at the resting (maker) order's price
- **O(1) cancel** via an order-id hash map plus intrusive linked lists
- **Modify** that follows exchange rules: reducing quantity keeps queue position; changing price or increasing quantity loses it
- Prices stored as **integer ticks** (never floating point)
- Fixed-capacity **memory pool** with an intrusive free list for all order objects
- Test suite (58 checks, clean under AddressSanitizer/UBSan) and a benchmark comparing the pool with `new`/`delete`

## Design

```
OrderBook
 ├── bids_ : std::map<Price, PriceLevel, greater>   best bid = begin()
 ├── asks_ : std::map<Price, PriceLevel, less>      best ask = begin()
 ├── orders_ : unordered_map<OrderId, Order*>       O(1) lookup for cancel/modify
 └── pool_ : MemoryPool<Order>                      all Order objects live here

PriceLevel:  head -> [Order] <-> [Order] <-> [Order] <- tail    (FIFO, oldest at head)
```

| Operation | Complexity |
|---|---|
| Add (no match, existing price level) | O(log L) where L = number of price levels |
| Match | O(1) per fill, plus O(log L) per level emptied |
| Cancel | O(1) to unlink, plus O(log L) if the level becomes empty |
| Best bid / ask | O(1) |

### Why a memory pool?

A trading system cares about **tail latency**, not just the average. `malloc`
can take an unpredictable amount of time: it may search free lists, take a lock,
or ask the OS for memory, which triggers page faults. Those rare slow calls show
up as p99 spikes.

`MemoryPool<T>` allocates all memory once at startup. Each free slot stores a
pointer to the next free slot inside its own bytes (an intrusive free list), so
the pool needs no extra bookkeeping memory:

- `allocate()`: pop the head of the free list, placement-new the object — two pointer operations
- `deallocate()`: call the destructor, push the slot back on the head
- Slots are reused LIFO, so a new order lands in memory that was just touched and is likely still in cache
- The constructor writes to every slot, so all pages are faulted in before trading starts

## Build and run

With CMake:
```bash
cmake -B build && cmake --build build
./build/tests
./build/demo
./build/bench_pool && ./build/bench_heap
```

Without CMake:
```bash
F="-std=c++17 -O3 -march=native -Wall -Wextra -Iinclude"
g++ $F src/order_book.cpp tests/tests.cpp -o tests && ./tests
g++ $F src/order_book.cpp src/main.cpp -o demo && ./demo
g++ $F src/order_book.cpp bench/benchmark.cpp -o bench_pool && ./bench_pool
g++ $F -DOB_USE_HEAP_ALLOCATOR src/order_book.cpp bench/benchmark.cpp -o bench_heap && ./bench_heap
```

## Benchmark

2 million pre-generated operations: 70% new limit orders (1 in 7 crossing the
spread), 30% cancels. The same binary is built twice, once with the pool and
once with `new`/`delete`.

<!-- Replace with numbers from your own machine -->
| | MemoryPool | new/delete |
|---|---|---|
| Throughput | _ M ops/sec | _ M ops/sec |
| p50 latency | _ ns | _ ns |
| p99 latency | _ ns | _ ns |
| p99.9 latency | _ ns | _ ns |

Machine: _CPU, OS, compiler_. Per-operation latencies include roughly 20 ns of
`steady_clock` overhead.

## Limitations and next steps

- `std::map` still allocates a node when a new price level appears, and `unordered_map` allocates on insert. A flat array indexed by price tick and an open-addressing hash map would remove the remaining heap allocations.
- Single-threaded by design. A real engine would receive orders from the network thread through a lock-free single-producer/single-consumer queue.
- Only limit orders (good-till-cancel). Market, IOC, and FOK orders are natural extensions.
