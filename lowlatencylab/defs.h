//
// Created by jc on 03/11/23.
//

#ifndef TICTACTOE_DEFS_H
#define TICTACTOE_DEFS_H


#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <cstring>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <liburing.h>
#include <chrono>
#include <cassert>
#include <iostream>
#include <unordered_map>

#define NDEBUG

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


using std::cerr;
using std::endl;
using std::cout;


using u64 = uint64_t;
using i64 = int64_t;
using u32 = uint32_t;
using i32 = int32_t;
using u8 = uint8_t;
using i8 = int8_t;

using PriceL = i64;
using SeqNo = i64;
using Qty = i32;
using OrderId = u64;
using MDMsgId = SeqNo;

using TimeNs = i64;

constexpr static int PINNED_CPU = 0;

inline u64 __attribute__((always_inline)) rotl(u64 a, u32 n) {
    return (a << n) | (a >> (64 - n));
}

inline u64 __attribute__((always_inline)) rotr(u64 a, u32 n) {
    return (a >> n) | (a << (64 - n));
}

enum Side {
    BUY,
    SELL
};

static constexpr PriceL PRECISION_L = PriceL(1e6);
static constexpr double PRECISION_D = 1e6;

struct OrderFlags {
    bool isBid: 1;
};

struct Order {
    TimeNs submittedTime = 0;
    MDMsgId triggerEvent = 0;
    TimeNs triggerReceivedTime = 0;
    OrderId id = 0;
    PriceL price = 0;
    Qty qty = 0;
    OrderFlags flags{};

    Order() = default;

    Order(TimeNs submittedTime, MDMsgId triggerEvent, TimeNs triggerReceivedTime, OrderId id, PriceL price, Qty qty,
          OrderFlags flags
    ) : submittedTime{submittedTime},
        triggerEvent{triggerEvent},
        triggerReceivedTime{triggerReceivedTime},
        id{id},
        price{price},
        qty{qty},
        flags{flags} {}
};

struct OrderInfo {
    Order orderInfo;

    TimeNs receivedTime = 0;
    TimeNs triggerSubmitTime = 0;

    OrderInfo(const Order &o, TimeNs r, TimeNs t) : orderInfo{o}, receivedTime{r}, triggerSubmitTime{t} {}
};


inline TimeNs currentTimeNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            (std::chrono::system_clock::now()).time_since_epoch()).count();
}

struct IOUringState {

    static constexpr int QUEUE_DEPTH = 1 << 10;

    struct io_uring ring{};

    IOUringState() {
        struct io_uring_params params{
                .sq_entries = QUEUE_DEPTH,
                .cq_entries = QUEUE_DEPTH * 2,
                .flags = IORING_SETUP_SINGLE_ISSUER
        };
        if (int error = io_uring_queue_init_params(QUEUE_DEPTH, &ring, &params) < 0) {
            cerr << "Queue init failed [" << error << "]." << endl;
            throw std::runtime_error("Unable to setup io_uring queue");
        }
        assert(params.sq_entries == QUEUE_DEPTH);
        assert(params.cq_entries == QUEUE_DEPTH * 2);
        assert(params.features & IORING_FEAT_SUBMIT_STABLE);
        assert(params.features & IORING_FEAT_CQE_SKIP);

        assert(ring.sq.ring_entries == QUEUE_DEPTH);
        assert(ring.cq.ring_entries == 2 * QUEUE_DEPTH);
        assert(ring.sq.sqe_tail == ring.sq.sqe_head);

    }

    ~IOUringState() {
        io_uring_queue_exit(&ring);
    }

    [[nodiscard]] io_uring_sqe *getSqe(u64 tag) {
        assert(tag < (u64(1) << 31));
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        assert(nullptr != sqe);
        io_uring_sqe_set_data64(sqe, tag);

        return sqe;
    }

    int submit() {
        int submitted = io_uring_submit(&ring);
        assert(submitted > 0);
        return submitted;
    }

    int submitAndWait(int n) {
        return io_uring_submit_and_wait(&ring, n);
    }

    io_uring_cqe *popCqe() {
        io_uring_cqe *cqe;
        // TODO: I get interrupts here - un sure why
        int res = io_uring_wait_cqe(&ring, &cqe);
        assert(res == 0);
        u64 tag = io_uring_cqe_get_data64(cqe);
        assert(tag >= 0);

        return cqe;
    }
};

struct MDFlags {
    bool isBid: 1;
    bool isSnapshot: 1;
    bool isTerm: 1;
};

struct MDPacket {
    SeqNo seqNo = -1;
    TimeNs localTimestamp = 0;
    PriceL price = 0;
    Qty qty = 0;
    MDFlags flags{.isTerm = true};
    char _padding[7]{};

    MDPacket() = default;

    MDPacket(SeqNo seqNo, TimeNs localTimestamp, PriceL price, Qty qty, MDFlags flags) : seqNo{seqNo},
                                                                                         localTimestamp{localTimestamp},
                                                                                         price{price}, qty{qty},
                                                                                         flags{flags}, _padding{} {}

    MDPacket(const MDPacket &other) = default;

};

inline bool operator==(const IOUringState &s1, const IOUringState &s2) {
    return &s1 == &s2;
}

struct cqe_guard {

    io_uring_cqe *completion;
    IOUringState &ring;

    explicit cqe_guard(IOUringState &ring) : ring{ring} {
        completion = ring.popCqe();
    }

    ~cqe_guard() {
        io_uring_cqe_seen(&ring.ring, completion);
    }
};

inline constexpr int OE_PORT = 9012;

inline std::string MCAST_ADDR = "239.255.0.1";
inline constexpr int MCAST_PORT = 12345;

inline static constexpr PriceL TRADE_THRESHOLD = PRECISION_L * 50'000'000;


inline int
parseDeribitMDLine(const char *msg, TimeNs &timestamp, TimeNs &localTimestamp, bool &isSnapshot, Side &side,
                   PriceL &priceL,
                   Qty &qty) {
    int matched = 6;

//        string a = "deribit,BTC-PERPETUAL,";
//        static_assert(sizeof(string) == 24 && a.size() == 22); // small string optimization
    int startIx = 21; // a.size() - 1;

    // 1585699200245000
    int ix = startIx;
    TimeNs _timestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _timestamp = _timestamp * 10 + (msg[++ix] - '0');
    }
//        assert(_timestamp == timestamp);
    ++ix;
    TimeNs _localtimestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _localtimestamp = _localtimestamp * 10 + (msg[++ix] - '0');
    }
//        assert(_localtimestamp == localTimestamp);

    ix += 2;
    bool _isSnapshot = msg[ix] == 't';
//        assert(_isSnapshot == isSnapshot);

    ix += _isSnapshot ? 5 : 6;
    Side _side_ = msg[ix] == 'b' ? Side::BUY : Side::SELL;
//        assert(side == _side_);

    PriceL _price{};
    ix += 3;
    PriceL decimal = 0;
    while (msg[++ix] != ',' && msg[ix] != '.') {
        decimal = decimal * 10 + (msg[ix] - '0');
    }
    PriceL fraction{};
    if (msg[ix] == '.') {
        PriceL mult = PRECISION_L;
        while (msg[++ix] != ',') {
            fraction = fraction * 10 + (msg[ix] - '0');
            mult /= 10;
        }
        _price = decimal * PRECISION_L + fraction * mult;
    } else {
        _price = decimal * PRECISION_L;
    }
//        assert(_price == priceL);

    Qty _qty{};
    while (msg[++ix] != '\0') {
        _qty = _qty * 10 + (msg[ix] - '0');
    }
//        assert(_qty == qty);

    timestamp = _timestamp;
    localTimestamp = _localtimestamp;
    isSnapshot = _isSnapshot;
    side = _side_;
    priceL = _price;
    qty = _qty;
    return matched;
}


inline bool checkMessageDigest(const u8 *buf, ssize_t bytes) {
    static std::unordered_map<int, MDPacket> seenHashes{};


    const MDPacket &p = *reinterpret_cast<const MDPacket *>(buf);

    const auto &[e, inserted] = seenHashes.emplace(p.seqNo, p);

    assert(inserted);
    return true;
}


#endif //TICTACTOE_DEFS_H
