// Replays a pre-generated stream of orders and cancels through the book.
// Build twice (with and without -DOB_USE_HEAP_ALLOCATOR) to compare the
// memory pool against plain new/delete.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "order_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

struct Op {
    enum Type : std::uint8_t { Add, Cancel } type;
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;
};

// 70% new orders (1 in 7 of them aggressive, crossing the spread),
// 30% cancels of recently sent orders. Generated before timing starts.
static std::vector<Op> generate(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99), offset(0, 50), qty(1, 100);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> sent;
    sent.reserve(n);
    OrderId next_id = 1;
    const Price mid = 10'000;

    for (std::size_t i = 0; i < n; ++i) {
        const int r = pct(rng);
        if (r < 30 && !sent.empty()) {
            const std::size_t window = std::min<std::size_t>(sent.size(), 5'000);
            std::uniform_int_distribution<std::size_t> pick(sent.size() - window, sent.size() - 1);
            ops.push_back({Op::Cancel, sent[pick(rng)], Side::Buy, 0, 0});
        } else {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            const bool aggressive = r >= 90;
            const int off = offset(rng);
            Price p;
            if (side == Side::Buy) p = aggressive ? mid + off : mid - 1 - off;
            else                   p = aggressive ? mid - off : mid + 1 + off;
            ops.push_back({Op::Add, next_id, side, p, static_cast<Quantity>(qty(rng))});
            sent.push_back(next_id++);
        }
    }
    return ops;
}

static std::size_t apply(OrderBook& book, const Op& op, std::vector<Trade>& trades) {
    trades.clear();
    if (op.type == Op::Add) book.add_order(op.id, op.side, op.price, op.qty, trades);
    else                    book.cancel_order(op.id);
    return trades.size();
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000;
    const std::vector<Op> ops = generate(n, 42);

#ifdef OB_USE_HEAP_ALLOCATOR
    std::printf("Allocator: new/delete (baseline)\n");
#else
    std::printf("Allocator: MemoryPool\n");
#endif
    std::printf("Operations: %zu\n\n", n);

    std::vector<Trade> trades;
    trades.reserve(4096);

    // 1) Throughput: no per-operation timing overhead.
    {
        OrderBook book(n);
        std::size_t total_trades = 0;
        const auto start = Clock::now();
        for (const Op& op : ops) total_trades += apply(book, op, trades);
        const double ns =
            std::chrono::duration<double, std::nano>(Clock::now() - start).count();
        std::printf("Throughput: %.2f million ops/sec (%.1f ns/op avg)\n", n / ns * 1e3, ns / n);
        std::printf("Trades: %zu, resting orders at end: %zu\n\n", total_trades,
                    book.order_count());
    }

    // 2) Latency distribution: timestamp every operation.
    //    Includes ~20 ns of clock overhead per sample.
    {
        OrderBook book(n);
        std::vector<std::uint32_t> lat;
        lat.reserve(n);
        for (const Op& op : ops) {
            const auto t0 = Clock::now();
            apply(book, op, trades);
            const auto t1 = Clock::now();
            lat.push_back(static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        }
        std::sort(lat.begin(), lat.end());
        auto pct = [&](double p) { return lat[static_cast<std::size_t>(p * (lat.size() - 1))]; };
        std::printf("Latency (ns)  p50: %u  p90: %u  p99: %u  p99.9: %u  max: %u\n", pct(0.50),
                    pct(0.90), pct(0.99), pct(0.999), lat.back());
    }
}
