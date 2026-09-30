#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "memory_pool.hpp"
#include "order.hpp"
#include "types.hpp"

namespace ob {

// Limit order book with price-time priority matching.
//
// Orders live in a pre-allocated MemoryPool. Compile with
// -DOB_USE_HEAP_ALLOCATOR to swap in plain new/delete for benchmarking.
class OrderBook {
public:
    explicit OrderBook(std::size_t max_orders);

    // Adds a limit order. It first matches against the opposite side; any
    // remaining quantity rests in the book. Fills are appended to `trades`.
    // Returns false if rejected (duplicate id, zero quantity, or book full).
    bool add_order(OrderId id, Side side, Price price, Quantity qty, std::vector<Trade>& trades);

    // Removes a resting order. Returns false if the id is not in the book.
    bool cancel_order(OrderId id);

    // Reducing quantity at the same price keeps queue position. Any other
    // change (new price, larger quantity) is cancel + re-add and loses priority.
    // A quantity of zero cancels the order.
    bool modify_order(OrderId id, Price new_price, Quantity new_qty, std::vector<Trade>& trades);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity volume_at(Side side, Price price) const;
    std::size_t order_count() const noexcept { return orders_.size(); }
    bool contains(OrderId id) const { return orders_.find(id) != orders_.end(); }

    void print(std::ostream& os, std::size_t depth = 5) const;

private:
    using Bids = std::map<Price, PriceLevel, std::greater<Price>>;  // highest bid first
    using Asks = std::map<Price, PriceLevel, std::less<Price>>;     // lowest ask first

    template <typename Levels, typename Crosses>
    Quantity match(Levels& levels, OrderId taker, Quantity qty, Crosses crosses,
                   std::vector<Trade>& trades);

    template <typename Levels>
    void rest(Levels& levels, OrderId id, Side side, Price price, Quantity qty);

    void remove_order(Order* o);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Order*> orders_;  // id -> order, for O(1) cancel
#ifdef OB_USE_HEAP_ALLOCATOR
    HeapAllocator<Order> pool_;
#else
    MemoryPool<Order> pool_;
#endif
};

}  // namespace ob
