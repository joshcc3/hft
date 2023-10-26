#include "Backtester.h"
#include <functional>
#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char *argv[]) {
    if(argc != 4) {
        cerr << "Usage: [./backtester [exchange-strat-latency] [strategy-exchange-latency] [input-file]]" << endl;
        exit(1);
    }

    TimeNs exchangeStratLatency = atoi(argv[1]);
    TimeNs stratExchangeLatency = atoi(argv[2]);
    string inputFile = string(argv[3]);

    if(exchangeStratLatency <= 0 || stratExchangeLatency <= 0) {
        cerr << "Latencies must be > 0." << endl;
    }

    std::ifstream ifile(inputFile);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        return 1;
    }

    BacktestCfg cfg{exchangeStratLatency, stratExchangeLatency};

    L3OrderBook<L3Vec> lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.1, 0.005, 100'000, nextOrderId};

    Logger l;
    vector<BacktestListener*> ls{&l};

    Backtester b{cfg, strategy, lob, ls};

    std::string line;
    while (std::getline(ifile, line)) {
        b.mdEvent(line);
    }

    cout << endl;

    return 0;

}
