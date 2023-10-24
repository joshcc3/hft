//
// Created by jc on 24/10/23.
//

#include "Backtester.h"
#include <functional>

int main() {
    L3OrderBook lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.9, 0.005, 100'000, nextOrderId};

    // Mock market data updates (multiplied by 10^9 for fixed-point representation)
    strategy.onTopLevelUpdate(100'000'000'000, 50, 101'000'000'000, 50);
    strategy.onTopLevelUpdate(100'500'000'000, 50, 101'500'000'000, 50);
    strategy.onTopLevelUpdate(101'000'000'000, 50, 102'000'000'000, 50);
    return 0;
}
