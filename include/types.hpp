#pragma once
#include <cstdint>

namespace ob {

using OrderId  = std::uint64_t;
using Price    = std::int64_t;   // price in integer ticks, never floating point
using Quantity = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Trade {
    OrderId  maker_id;   // the resting order
    OrderId  taker_id;   // the incoming order
    Price    price;      // trades execute at the resting order's price
    Quantity quantity;
};

}  // namespace ob
