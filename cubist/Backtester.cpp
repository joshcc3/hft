#include "Backtester.h"
#include <functional>

int main() {
    L3OrderBook<L3Vec> lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.1, 0.005, 100'000, nextOrderId};

    BacktestCfg cfg{1, 20};

    Logger l;
    vector<BacktestListener*> ls{&l};

    Backtester b{cfg, strategy, lob, ls};

    vector<string> mdEvents = {
            "10, ADD, 1, B, 10, 100",
            "20, ADD, 2, S, 10, 101",
            "30, ADD, 3, B, 10, 95",
            "40, ADD, 4, S, 10, 105",
            "50, DELETE, 1",
            "60, ADD, 5, S, 10, 96",
            "70, DELETE, 5",
            "85, ADD, 6, S, 10, 96",
            "100000000, ADD, 5, S, 10, 100"
    };

    for(const auto& e : mdEvents) {
        b.mdEvent(e);
    }

    return 0;
}
