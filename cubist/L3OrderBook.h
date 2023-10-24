//
// Created by jc on 24/10/23.
//



#ifndef HFT_L3ORDERBOOK_H
#define HFT_L3ORDERBOOK_H

#include <cstdint>
#include <cassert>
#include <list>
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <array>
#include <optional>
#include "mytypedefs.h"

class L3OrderBook {


public:
    bool empty();

    InboundMsg::TopLevelUpdate getBBO();

    TimeNs lastUpdateTs;

    std::optional<InboundMsg::Trade> submit(bool isStrategy, OrderId orderId, Qty size, Side side, PriceL orderPrice);

    void cancel(OrderId id);

    void modify(OrderId orderId, Qty size);
};


#endif //HFT_L3ORDERBOOK_H
