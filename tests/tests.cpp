#include <cstdio>
#include <vector>

#include "memory_pool.hpp"
#include "order_book.hpp"

using namespace ob;

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool same(const Trade& t, OrderId maker, OrderId taker, Price p, Quantity q) {
    return t.maker_id == maker && t.taker_id == taker && t.price == p && t.quantity == q;
}

static void test_orders_rest_when_not_crossing() {
    OrderBook b(16);
    std::vector<Trade> t;
    CHECK(b.add_order(1, Side::Buy, 99, 10, t));
    CHECK(b.add_order(2, Side::Sell, 100, 10, t));
    CHECK(t.empty());
    CHECK(b.best_bid() == 99);
    CHECK(b.best_ask() == 100);
    CHECK(b.order_count() == 2);
}

static void test_full_match_empties_book() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Sell, 100, 10, t);
    b.add_order(2, Side::Buy, 100, 10, t);
    CHECK(t.size() == 1);
    CHECK(same(t[0], 1, 2, 100, 10));
    CHECK(!b.best_bid() && !b.best_ask());
    CHECK(b.order_count() == 0);
}

static void test_partial_fill_rests_remainder_at_maker_price() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Sell, 100, 10, t);
    b.add_order(2, Side::Buy, 101, 15, t);
    CHECK(t.size() == 1);
    CHECK(same(t[0], 1, 2, 100, 10));  // executes at the resting price, not 101
    CHECK(b.best_bid() == 101);
    CHECK(b.volume_at(Side::Buy, 101) == 5);
    CHECK(!b.best_ask());
}

static void test_price_then_time_priority() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Sell, 100, 5, t);
    b.add_order(2, Side::Sell, 100, 5, t);
    b.add_order(3, Side::Sell, 99, 5, t);  // better price, arrived last
    b.add_order(4, Side::Buy, 100, 12, t);
    CHECK(t.size() == 3);
    CHECK(same(t[0], 3, 4, 99, 5));   // best price first
    CHECK(same(t[1], 1, 4, 100, 5));  // then oldest at 100
    CHECK(same(t[2], 2, 4, 100, 2));
    CHECK(b.volume_at(Side::Sell, 100) == 3);
    CHECK(b.order_count() == 1);
}

static void test_sell_side_matching() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Buy, 100, 5, t);
    b.add_order(2, Side::Buy, 101, 5, t);
    b.add_order(3, Side::Sell, 100, 8, t);
    CHECK(t.size() == 2);
    CHECK(same(t[0], 2, 3, 101, 5));  // highest bid first
    CHECK(same(t[1], 1, 3, 100, 3));
    CHECK(b.volume_at(Side::Buy, 100) == 2);
}

static void test_cancel() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Buy, 100, 10, t);
    b.add_order(2, Side::Buy, 100, 5, t);
    CHECK(b.cancel_order(1));
    CHECK(b.volume_at(Side::Buy, 100) == 5);
    CHECK(b.cancel_order(2));
    CHECK(!b.best_bid());           // empty level was removed
    CHECK(!b.cancel_order(2));      // already gone
    CHECK(!b.cancel_order(999));    // never existed
}

static void test_modify_priority_rules() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Sell, 100, 10, t);
    b.add_order(2, Side::Sell, 100, 10, t);

    CHECK(b.modify_order(1, 100, 4, t));  // reduce: keeps priority
    CHECK(b.volume_at(Side::Sell, 100) == 14);
    t.clear();
    b.add_order(3, Side::Buy, 100, 4, t);
    CHECK(t.size() == 1 && t[0].maker_id == 1);

    b.add_order(4, Side::Sell, 100, 10, t);  // queue: 2, 4
    CHECK(b.modify_order(2, 100, 20, t));    // increase: loses priority -> 4, 2
    t.clear();
    b.add_order(5, Side::Buy, 100, 1, t);
    CHECK(t.size() == 1 && t[0].maker_id == 4);

    CHECK(b.modify_order(4, 101, 9, t));  // price change moves the order
    CHECK(b.volume_at(Side::Sell, 101) == 9);
    CHECK(b.best_ask() == 100);

    CHECK(b.modify_order(2, 100, 0, t));  // zero quantity cancels
    CHECK(!b.contains(2));
    CHECK(!b.modify_order(999, 100, 1, t));
}

static void test_modify_can_cross() {
    OrderBook b(16);
    std::vector<Trade> t;
    b.add_order(1, Side::Buy, 99, 5, t);
    b.add_order(2, Side::Sell, 101, 5, t);
    CHECK(b.modify_order(1, 101, 5, t));
    CHECK(t.size() == 1 && same(t[0], 2, 1, 101, 5));
    CHECK(b.order_count() == 0);
}

static void test_rejections_and_capacity() {
    OrderBook b(2);
    std::vector<Trade> t;
    CHECK(b.add_order(1, Side::Buy, 100, 10, t));
    CHECK(!b.add_order(1, Side::Buy, 100, 10, t));  // duplicate id
    CHECK(!b.add_order(9, Side::Buy, 100, 0, t));   // zero quantity
    CHECK(b.add_order(2, Side::Buy, 99, 10, t));
    CHECK(!b.add_order(3, Side::Buy, 98, 10, t));   // pool full
    CHECK(b.cancel_order(1));
    CHECK(b.add_order(3, Side::Buy, 98, 10, t));    // slot reused
}

static void test_memory_pool() {
    MemoryPool<long> p(3);
    long* a = p.allocate(1);
    long* b = p.allocate(2);
    long* c = p.allocate(3);
    CHECK(a && b && c && a != b && b != c);
    CHECK(*a == 1 && *b == 2 && *c == 3);
    CHECK(p.allocate(4) == nullptr);
    CHECK(p.in_use() == 3);
    p.deallocate(b);
    CHECK(p.allocate(5) == b);  // LIFO reuse of the most recently freed slot
    CHECK(*b == 5);
}

int main() {
    test_orders_rest_when_not_crossing();
    test_full_match_empties_book();
    test_partial_fill_rests_remainder_at_maker_price();
    test_price_then_time_priority();
    test_sell_side_matching();
    test_cancel();
    test_modify_priority_rules();
    test_modify_can_cross();
    test_rejections_and_capacity();
    test_memory_pool();

    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
