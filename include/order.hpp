#pragma once
#include "types.hpp"

namespace ob {

struct PriceLevel;

// An order resting in the book. prev/next make the order itself a node in
// its price level's FIFO queue (an intrusive list), so no separate list node
// is allocated and removal from the middle of the queue is O(1).
struct Order {
    OrderId     id;
    Side        side;
    Price       price;
    Quantity    quantity;  // remaining quantity
    Order*      prev  = nullptr;
    Order*      next  = nullptr;
    PriceLevel* level = nullptr;

    Order(OrderId id_, Side side_, Price price_, Quantity qty_)
        : id(id_), side(side_), price(price_), quantity(qty_) {}
};

// All resting orders at one price, oldest first (time priority).
struct PriceLevel {
    Price         price;
    Quantity      total_quantity = 0;
    std::uint32_t order_count    = 0;
    Order*        head = nullptr;  // oldest order, matched first
    Order*        tail = nullptr;  // newest order

    explicit PriceLevel(Price p) : price(p) {}

    bool empty() const noexcept { return head == nullptr; }

    void push_back(Order* o) noexcept {
        o->level = this;
        o->prev  = tail;
        o->next  = nullptr;
        if (tail) tail->next = o; else head = o;
        tail = o;
        total_quantity += o->quantity;
        ++order_count;
    }

    void remove(Order* o) noexcept {
        if (o->prev) o->prev->next = o->next; else head = o->next;
        if (o->next) o->next->prev = o->prev; else tail = o->prev;
        total_quantity -= o->quantity;
        --order_count;
        o->prev = o->next = nullptr;
        o->level = nullptr;
    }
};

}  // namespace ob
