#ifndef HFT_MYTYPEDEFS_H
#define HFT_MYTYPEDEFS_H


#include <cstdint>
#include <variant>
#include <cmath>

#include <chrono>

#include <cassert>

#ifdef NDEBUG
#undef assert
#define assert(expr) void(0)
#endif

inline double timeSpent[10] = {0, 0, 0, 0, 0};

template<typename T>
inline double elapsed(T t1, T t2) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / 1000000000.0;

}



#define CLOCK(a) { \
    auto _s = std::chrono::system_clock::now(); \
    a;             \
    auto _e = std::chrono::system_clock::now(); \
    timeSpent[i] += elapsed(_s, _e);                      \
}


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

inline constexpr PriceL PRECISION = 1e9;


struct InboundMsg {

    struct TopLevelUpdate {
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

        Trade(OrderId id, PriceL priceL, Qty qty) : id{id}, price{priceL}, qty{qty} {}

        OrderId id;
        PriceL price;
        Qty qty;
    };

    struct Noop {

    };


    std::variant<TopLevelUpdate, OrderModified, OrderAccepted, OrderCancelled, Trade, Noop> content;
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
               Qty size) : isStrategy{isStrategy}, orderId{orderId}, side{side}, orderPrice{orderPrice},
                           size{size} {}
    };

    struct Cancel {
        OrderId id;

        Cancel() = delete;

        Cancel(OrderId id) : id{id} {}
    };

    struct Modify {
        OrderId id;
        PriceL price;
        Qty size;

        Modify() = delete;

        Modify(OrderId id, PriceL priceL, Qty size) : id{id}, size{size}, price{priceL} {}
    };

    std::variant<Submit, Cancel> content;
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

inline OrderId l2OrderId(OrderId orderId, Side side) {
    return (orderId << 1) | (side == Side::BUY ? 0 : 1);
}


#endif //HFT_MYTYPEDEFS_H

