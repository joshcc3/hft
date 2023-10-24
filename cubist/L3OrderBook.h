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
#include <list>

#include "mytypedefs.h"


class L3 {



};

using Levels = std::list<L3>;

struct OrderLoc {
    Levels::iterator level;
    L3::iterator order;
};

class L3OrderBook {

    Levels bids;
    Levels offers;

    std::map<OrderId, OrderLoc> orderIdMap;

    InboundMsg::TopLevelUpdate topLevelUpdate;

public:

    TimeNs lastUpdateNs;


    bool empty();

    InboundMsg::TopLevelUpdate getBBO();


    std::optional<InboundMsg::Trade> submit(bool isStrategy, OrderId orderId, Qty size, Side side, PriceL orderPrice);

    void cancel(OrderId id);

    void modify(OrderId orderId, Qty size);

    OrderId nextOrderId();
};


#endif //HFT_L3ORDERBOOK_H
