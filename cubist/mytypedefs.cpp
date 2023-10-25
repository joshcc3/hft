//
// Created by jc on 25/10/23.
//

#include "mytypedefs.h"


bool operator==(InboundMsg::TopLevelUpdate a, InboundMsg::TopLevelUpdate b) {
    return a.askSize == b.askSize && a.bidSize == b.bidSize && a.askPrice == b.askPrice && a.bidPrice == b.bidPrice;
}

template<typename T>
T abs(T x) {
    return x >= 0 ? x : -x;
}

double getPriceF(PriceL p) {
    return round(double(p) / (PRECISION / 100)) / 100;
}

