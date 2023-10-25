#ifndef HFT_MYTYPEDEFS_H
#define HFT_MYTYPEDEFS_H

#include <cstdint>
#include <variant>
#include <cmath>


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
using PriceL = i64;
using NotionalL = i64;
using TimeNs = u64;

inline PriceL PRECISION = 1e9;


// Message types encapsulated in a union
struct InboundMsg {
    struct TopLevelUpdate {
        TimeNs time = 0;
        PriceL bidPrice = 0;
        Qty bidSize = -1;
        PriceL askPrice = 0;
        Qty askSize = -1;


        [[nodiscard]] bool bidPresent() const noexcept {
            return bidSize > 0;
        }

        [[nodiscard]] bool askPresent() const noexcept {
            return askSize > 0;
        }

    };

    struct OrderModified {
        OrderModified() = delete;

        TimeNs timeNs;
        OrderId id;
        Qty newQty;
    };

    struct OrderAccepted {
        OrderAccepted() = delete;

        TimeNs timeNs;
        OrderId id;
    };

    struct OrderCancelled {
        OrderCancelled() = delete;

        TimeNs timeNs;
        OrderId id;
    };

    struct Trade {
        Trade() = delete;

        Trade(TimeNs timeNs, OrderId id, PriceL priceL, Qty qty) : id{id}, price{priceL}, qty{qty}, timeNs{timeNs} {}

        TimeNs timeNs;
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
        TimeNs timeNs;

        Submit(TimeNs timeNs, bool isStrategy,
               OrderId orderId,
               Side side,
               PriceL orderPrice,
               Qty size) : isStrategy{isStrategy}, orderId{orderId}, side{side}, orderPrice{orderPrice},
                                size{size}, timeNs{timeNs} {}
    };

    struct Cancel {
        OrderId id;
        TimeNs timeNs;

        Cancel() = delete;

        Cancel(TimeNs timeNs, OrderId id) : id{id}, timeNs{timeNs} {}
    };

    struct Modify {
        OrderId id;
        Qty size;
        TimeNs timeNs;

        Modify() = delete;

        Modify(TimeNs time, OrderId id, Qty size) : id{id}, size{size}, timeNs{time} {}
    };

    std::variant<Submit, Cancel> content;
};


enum class OrderMsgType {
    ADD,
    UPDATE,
    DELETE
};

inline bool operator==(InboundMsg::TopLevelUpdate a, InboundMsg::TopLevelUpdate b) {
    return a.askSize == b.askSize && a.bidSize == b.bidSize && a.askPrice == b.askPrice && a.bidPrice == b.bidPrice;
}

template<typename T>
inline T abs(T x) {
    return x >= 0 ? x : -x;
}

inline double getPriceF(PriceL p) {
    return round(double(p) / (PRECISION / 100)) / 100;
}



#endif //HFT_MYTYPEDEFS_H

