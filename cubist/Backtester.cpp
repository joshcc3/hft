//
// Created by jc on 24/10/23.
//

#include "Backtester.h"
#include <functional>

int main() {
    L3OrderBook lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.9, 0.005, 100'000, nextOrderId};

    BacktestCfg cfg{10'000, 10'000};

    Backtester b{cfg, strategy, lob};

    vector<string> mdEvents = {
            "1, ADD, 1, B, 10, 100",
            "2, ADD, 2, S, 10, 101",
            "3, ADD, 3, B, 10, 90",
            "4, DELETE, 2",
            "5, ADD, 4, S, 10, 91",
            "100000000, ADD, 5, S, 10, 100",
    };

    for(const auto& e : mdEvents) {
        b.mdEvent(e);
    }

    return 0;
}
