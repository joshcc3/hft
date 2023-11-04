//
// Created by jc on 03/11/23.
//

#ifndef TICTACTOE_DEFS_H
#define TICTACTOE_DEFS_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstddef>


using u64 = uint64_t;
using i64 = int64_t;
using u32 = uint32_t;
using i32 = int32_t;
using u8 = uint8_t;
using i8 = int8_t;

using PriceL = i64;
using Qty = i32;
using OrderId = u64;
using MDMsgId = u64;

using TimeNs = i64;

enum Side {
    BUY,
    SELL
};

static PriceL PRECISION_L = PriceL(1e9);
static double PRECISION_D = 1e9;

struct Order {
    TimeNs submittedTime = 0;
    MDMsgId triggerEvent = 0;
    TimeNs triggerReceivedTime = 0;
    OrderId id = 0;
    PriceL price = 0;
    Qty qty = 0;

    Order(TimeNs submittedTime, MDMsgId triggerEvent, TimeNs triggerReceivedTime, OrderId id, PriceL price, Qty qty
    ) : submittedTime{submittedTime},
        triggerEvent{triggerEvent},
        triggerReceivedTime{triggerReceivedTime},
        id{id},
        price{price},
        qty{qty} {}
};

struct OrderInfo {
    Order orderInfo;

    TimeNs receivedTime = 0;
    TimeNs triggerSubmitTime = 0;

    OrderInfo(const Order &o, TimeNs r, TimeNs t) : orderInfo{o}, receivedTime{r}, triggerSubmitTime{t} {}
};

#endif //TICTACTOE_DEFS_H
