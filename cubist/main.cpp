//
// Created by jc on 26/10/23.
//
#include "Backtester.h"
#include <fstream>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: [./cubist [exchange-strat-latency] [strategy-exchange-latency] [input-file]]"
                  << std::endl;
        exit(1);
    }

    TimeNs exchangeStratLatency = atoi(argv[1]);
    TimeNs stratExchangeLatency = atoi(argv[2]);
    std::string inputFile = std::string(argv[3]);

    if (exchangeStratLatency <= 0 || stratExchangeLatency <= 0) {
        std::cerr << "Latencies must be > 0." << std::endl;
    }

    std::ifstream ifile(inputFile);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        return 1;
    }

    BacktestCfg cfg{exchangeStratLatency, stratExchangeLatency};

    L3OrderBook<L3Vec> lob;
    auto nextOrderId = [lob]() mutable { return lob.nextOrderId(); };

    constexpr double alpha = 0.1;
    constexpr double aggressThreshold = 0.005;
    constexpr double inventoryLimit = 100'000;
    Strategy strategy{alpha, aggressThreshold, inventoryLimit, nextOrderId};

    Logger l;
    std::vector<BacktestListener *> ls{&l};

    Backtester b{cfg, strategy, lob, ls};

    std::string line;
    while (std::getline(ifile, line)) {
        b.mdEvent(line);
    }

    std::cout << std::endl;

    return 0;

}