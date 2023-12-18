//
// Created by jc on 26/10/23.
//
#include "Backtester.h"
#include <fstream>
#include <fstream>
#include <iostream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <thread>
#include "SPSC.h"

template<typename T, int C>
const basic_istream<char> &getL(SPSC<T, C> &spsc, istream &instream) {
    int i = 7;
    auto _s = std::chrono::system_clock::now();
    string line;
    const basic_istream<char> &basicIstream = std::getline(instream, line);
    if (!line.empty()) {
        spsc.push_back(std::move(line));
    }
    auto _e = std::chrono::system_clock::now();
    timeSpent[i] += elapsed(_s, _e);
    return basicIstream;
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: [./backtester [exchange-strat-latency] [strategy-exchange-latency] [input-file]]"
                  << std::endl;
        exit(1);
    }

    TimeNs exchangeStratLatency = atoi(argv[1]);
    TimeNs stratExchangeLatency = atoi(argv[2]);
    std::string inputFile = std::string(argv[3]);

    if (exchangeStratLatency <= 0 || stratExchangeLatency <= 0) {
        std::cerr << "Latencies must be > 0." << std::endl;
    }

    std::ifstream ifile(inputFile, std::ios_base::in | std::ios_base::binary);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        return 1;
    }

    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf;
    inbuf.push(boost::iostreams::gzip_decompressor());
    inbuf.push(ifile);
    //Convert streambuf to istream
    std::istream instream(&inbuf);


    BacktestCfg cfg{exchangeStratLatency, stratExchangeLatency};

    L3OrderBook<L3Vec> lob;
    auto nextOrderId = [lob]() mutable { return lob.nextOrderId(); };

    constexpr double alpha = 0.1;
    constexpr double aggressThreshold = 0.005;
    constexpr double inventoryLimit = 100'000;
    Strategy ewmaStrategy{alpha, aggressThreshold, inventoryLimit, nextOrderId};
    DummyStrategy dStrategy{};

    Logger l;
    std::vector<BacktestListener *> ls{};

    Backtester<DummyStrategy> b{cfg, dStrategy, lob, ls};

    //Iterate lines
    std::string prev;
    std::string line;
    std::getline(instream, line);
    assert(line == "exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount");


    int iters = 0;
    SPSC<string, 1 << 15> stringBuffer;
    atomic<bool> isComplete{false};

    std::thread producerT{[&stringBuffer, &instream, &b]() {
        CLOCK(
                int i = 8;
                while (getL<string, 1 << 15>(stringBuffer, instream)) {
//                    stringBuffer.pop();
//                    b.mdEvent(stringBuffer.pop());
                }
        )
    }};
    std::thread consumerT{[&stringBuffer, &b, &iters, &isComplete]() {
        CLOCK(
                int i = 9;
                while (!isComplete.load(memory_order_relaxed)) {
                    const string &line = stringBuffer.pop();
                    if (!line.empty()) {
                        b.mdEvent(line);

                        if ((iters & ((1 << 20) - 1)) == 0) {
                            cout << iters << endl;
                        }
                        ++iters;
                    }
                }
        )

    }};

    CLOCK(
            int i = 0;
            producerT.join();
            cout << "Producer complete " << endl;
            isComplete.store(true, memory_order_relaxed);
            consumerT.join();
    )
    cout << "Total time [" << timeSpent[0] << "s], iters [" << iters << "]" << endl;
    cout << "Time per iter [" << timeSpent[0] / double(iters) * 1000000 << "us]" << endl;
    cout << "ProduceTime [" << timeSpent[8] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "ConsumeTime [" << timeSpent[9] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Submit [" << timeSpent[1] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Cancel [" << timeSpent[2] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Modify [" << timeSpent[3] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Snapshotting [" << timeSpent[4] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Message enqueing [" << timeSpent[5] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Parsing [" << timeSpent[6] / double(timeSpent[0]) * 100 << "%]" << endl;
    cout << "Reading [" << timeSpent[7] / double(timeSpent[0]) * 100 << "%]" << endl;

    std::cout << std::endl;


    return 0;

}
