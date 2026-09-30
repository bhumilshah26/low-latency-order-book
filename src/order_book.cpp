#include "order_book.hpp"

#include <algorithm>
#include <iomanip>

namespace ob {

OrderBook::OrderBook(std::size_t max_orders) : pool_(max_orders) {
    orders_.reserve(max_orders);
}

// Walks the opposite side from the best price inward, filling the incoming
// order against resting orders oldest-first until it is filled or the next
// price no longer crosses. Returns the unfilled quantity.
template <typename Levels, typename Crosses>
Quantity OrderBook::match(Levels& levels, OrderId taker, Quantity qty, Crosses crosses,
                          std::vector<Trade>& trades) {
    while (qty > 0 && !levels.empty()) {
        auto it = levels.begin();  // best price on the opposite side
        PriceLevel& level = it->second;
        if (!crosses(level.price)) break;

        while (qty > 0 && !level.empty()) {
            Order* maker = level.head;  // oldest order at this price
            const Quantity fill = std::min(qty, maker->quantity);
            trades.push_back({maker->id, taker, level.price, fill});
            qty -= fill;
            maker->quantity -= fill;
            level.total_quantity -= fill;
            if (maker->quantity == 0) {
                level.remove(maker);
                orders_.erase(maker->id);
                pool_.deallocate(maker);
            }
        }
        if (level.empty()) levels.erase(it);
    }
    return qty;
}

template <typename Levels>
void OrderBook::rest(Levels& levels, OrderId id, Side side, Price price, Quantity qty) {
    // Capacity was checked in add_order and matching only frees slots,
    // so this allocation cannot fail.
    Order* o = pool_.allocate(id, side, price, qty);
    auto [it, inserted] = levels.try_emplace(price, price);
    it->second.push_back(o);
    orders_.emplace(id, o);
}

bool OrderBook::add_order(OrderId id, Side side, Price price, Quantity qty,
                          std::vector<Trade>& trades) {
    if (qty == 0 || contains(id) || pool_.in_use() == pool_.capacity()) return false;

    if (side == Side::Buy) {
        qty = match(asks_, id, qty, [price](Price ask) { return ask <= price; }, trades);
        if (qty > 0) rest(bids_, id, side, price, qty);
    } else {
        qty = match(bids_, id, qty, [price](Price bid) { return bid >= price; }, trades);
        if (qty > 0) rest(asks_, id, side, price, qty);
    }
    return true;
}

void OrderBook::remove_order(Order* o) {
    PriceLevel* level = o->level;
    const Price price = o->price;
    const Side side = o->side;
    level->remove(o);
    if (level->empty()) {
        if (side == Side::Buy) bids_.erase(price); else asks_.erase(price);
    }
    orders_.erase(o->id);
    pool_.deallocate(o);
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;
    remove_order(it->second);
    return true;
}

bool OrderBook::modify_order(OrderId id, Price new_price, Quantity new_qty,
                             std::vector<Trade>& trades) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;
    Order* o = it->second;

    if (new_qty == 0) {
        remove_order(o);
        return true;
    }
    if (new_price == o->price && new_qty <= o->quantity) {
        o->level->total_quantity -= (o->quantity - new_qty);  // keeps queue position
        o->quantity = new_qty;
        return true;
    }
    const Side side = o->side;
    remove_order(o);
    return add_order(id, side, new_price, new_qty, trades);  // back of the queue
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::volume_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.total_quantity;
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total_quantity;
}

void OrderBook::print(std::ostream& os, std::size_t depth) const {
    std::vector<const PriceLevel*> asks;
    for (auto it = asks_.begin(); it != asks_.end() && asks.size() < depth; ++it)
        asks.push_back(&it->second);

    os << "        Price       Qty  Orders\n";
    for (auto it = asks.rbegin(); it != asks.rend(); ++it)  // highest ask on top
        os << "ASK " << std::setw(9) << (*it)->price << std::setw(10) << (*it)->total_quantity
           << std::setw(8) << (*it)->order_count << '\n';
    os << "-------------------------------\n";
    std::size_t shown = 0;
    for (auto it = bids_.begin(); it != bids_.end() && shown < depth; ++it, ++shown)
        os << "BID " << std::setw(9) << it->second.price << std::setw(10)
           << it->second.total_quantity << std::setw(8) << it->second.order_count << '\n';
}

}  // namespace ob
