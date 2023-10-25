#include "Backtester.h"
#include <functional>

int main() {
    L3OrderBook<L3Vec> lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.1, 0.005, 100'000, nextOrderId};

    BacktestCfg cfg{10'000, 10'000};

    Logger l;
    vector<BacktestListener*> ls{&l};

    Backtester b{cfg, strategy, lob, ls};

    vector<string> mdEvents = {
            "1, ADD, 1, B, 10, 100",
            "2, ADD, 2, S, 10, 101",
            "3, ADD, 3, B, 10, 80",
            "4, DELETE, 1",
            "5, ADD, 4, S, 10, 81",
            "100000000, ADD, 5, S, 10, 100",
    };

    for(const auto& e : mdEvents) {
        b.mdEvent(e);
    }

    return 0;
}
