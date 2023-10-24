//
// Created by jc on 24/10/23.
//

#ifndef HFT_MYTYPEDEFS_H
#define HFT_MYTYPEDEFS_H

#include <cstdint>
#include <variant>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using Qty = i32;
using OrderId = i64;
using PriceL = u64;
using NotionalL = i64;
using TimeNs = u64;

// Message types encapsulated in a union
struct InboundMsg {
    struct TopLevelUpdate {
        PriceL bidPrice;
        Qty bidSize;
        PriceL askPrice;
        Qty askSize;
    };

    struct OrderModified {
        OrderModified() = delete;
        OrderId id;
        Qty newQty;
    };

    struct OrderAccepted {
        OrderAccepted() = delete;
        OrderId id;
    };

    struct OrderCancelled {
        OrderCancelled() = delete;
        OrderId id;
    };

    struct Trade {
        Trade() = delete;
        OrderId id;
        PriceL price;
        Qty qty;
    };


    std::variant<TopLevelUpdate, OrderModified, OrderAccepted, OrderCancelled, Trade> content;
};

enum class Side {
    BUY, SELL, NUL
};

struct OutboundMsg {

    struct Submit {
        bool isStrategy;
        OrderId orderId;
        Side side;
        PriceL orderPrice;
        Qty size;

        Submit(bool isStrategy,
               OrderId orderId,
               Side side,
               PriceL orderPrice,
               Qty size) : isStrategy{isStrategy}, orderId{orderId}, side{side}, orderPrice{orderPrice}, size{size} {}
    };

    struct Cancel {
        OrderId id;
        Cancel() = delete;
        Cancel(OrderId id) : id{id} {}
    };

    struct Modify {
        OrderId id;
        Qty size;
        Modify() = delete;
        Modify(OrderId id, Qty size) : id{id}, size{size} {}
    };

    std::variant<Submit, Cancel> content;
};

enum class OrderMsgType {
    ADD,
    UPDATE,
    DELETE
};

template<typename T>
T abs(T x) {
    return x >= 0 ? x : -x;
}


#endif //HFT_MYTYPEDEFS_H
