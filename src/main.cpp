// Small walkthrough: builds a book, sends an aggressive order, cancels one.
// Prices are integer ticks, e.g. 10100 = Rs 101.00 with a 0.01 tick size.
#include <iostream>
#include <vector>

#include "order_book.hpp"

using namespace ob;

int main() {
    OrderBook book(1024);
    std::vector<Trade> trades;

    auto add = [&](OrderId id, Side side, Price price, Quantity qty) {
        trades.clear();
        book.add_order(id, side, price, qty, trades);
        std::cout << (side == Side::Buy ? "BUY  #" : "SELL #") << id << "  " << qty << " @ "
                  << price << '\n';
        for (const Trade& t : trades)
            std::cout << "   TRADE  maker #" << t.maker_id << "  taker #" << t.taker_id << "  "
                      << t.quantity << " @ " << t.price << '\n';
    };

    add(1, Side::Sell, 10100, 100);
    add(2, Side::Sell, 10100, 50);
    add(3, Side::Sell, 10200, 200);
    add(4, Side::Buy, 10000, 300);
    add(5, Side::Buy, 9900, 100);
    std::cout << "\nBook:\n";
    book.print(std::cout);

    std::cout << "\nAggressive buy sweeps two price levels:\n";
    add(6, Side::Buy, 10200, 180);  // fills #1 then #2 at 10100 (time priority), then 30 of #3
    std::cout << "\nBook:\n";
    book.print(std::cout);

    std::cout << "\nCancel #5\n";
    book.cancel_order(5);
    book.print(std::cout);
}
