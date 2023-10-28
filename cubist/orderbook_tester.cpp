//
// Created by jc on 28/10/23.
//

#define NDEBUG

#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <iostream>
#include <vector>
#include <fstream>
#include <thread>
#include <fcntl.h>
#include <cerrno>
#include <atomic>
#include <unistd.h>
#include "L3OrderBook.h"

using namespace std;

void test() {


    TimeNs time = 0;
    OrderId id = 0;
    L3OrderBook<L3Vec> book;
//    {
//        ++time;
//        ++id;
//        Qty size = 1;
//        constexpr Side side = Side::BUY;
//        PriceL price = 98 * PRECISION;
//        book.submit<side>(time, false, id, size, price);
//    }
//    {
//        ++time;
//        ++id;
//        Qty size = 1;
//        constexpr Side side = Side::SELL;
//        PriceL price = 102 * PRECISION;
//        book.submit<side>(time, false, id, size, price);
//    }


    OrderId orderId = ++id;
    Qty size = 1;
    constexpr Side side = Side::BUY;
    PriceL price = 99 * PRECISION;

    auto start = chrono::system_clock::now();


    for (int i = 0; i < 1'000'000; ++i) {
        ++time;
        CLOCK(
            int i = 4;
            book.submit<side>(time, false, orderId, size, price);
        )
        CLOCK (
            int i = 5;
            book.cancel(time, orderId);
        )

    }

    auto end = chrono::system_clock::now();


    cout << "Total [" << elapsed(start, end) << "s]" << endl;
    cout << "C1 [" << timeSpent[0] << "s]" << endl;
    cout << "C2 [" << timeSpent[1] << "s]" << endl;
    cout << "C3 [" << timeSpent[2] << "s]" << endl;
    cout << "C4 [" << timeSpent[3] << "s]" << endl;
    cout << "submit [" << timeSpent[4] << "s]" << endl;
    cout << "cancel [" << timeSpent[5] << "s]" << endl;
    cout << "Insert Level [" << timeSpent[2]/timeSpent[0] * 100 << "%]" << endl;

}

int main() {
    for(int i = 0; i < 1; ++i) {
        test();
    }
}
