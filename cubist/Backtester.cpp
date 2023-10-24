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

    };

    for(const auto& e : mdEvents) {
        b.mdEvent(e);
    }

    return 0;
}
