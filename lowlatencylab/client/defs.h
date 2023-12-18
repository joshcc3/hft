//
// Created by jc on 03/11/23.
//

#ifndef DEFS_H
#define DEFS_H



#include <arpa/inet.h>

#include "../../cppkern/defs.h"
#include "../../cppkern/IGB82576Interop.h"

// #define NDEBUG




inline double timeSpent[10] = {0, 0, 0, 0, 0};

template<typename T>
inline double elapsed(T t1, T t2) {
    return 0; // std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / 1000000000.0;
}

#define CLOCK(c, a) { \
    auto _s = currentTimeNs(); \
    a;             \
    auto _e = currentTimeNs(); \
    GET_PC(c) += elapsed(_s, _e);                      \
}


#define MYASSERT(b, msg) if(!(b)) {     pr_err(msg); ssleep__(10); mybug();  }
#define MSG_COUNT 10



// using std::cerr;
// using std::endl;
// using std::cout;

using PriceL = i64;
using SeqNo = i64;
using Qty = i32;
using OrderId = u64;
using MDMsgId = SeqNo;

using TimeNs = i64;

constexpr static int PINNED_CPU = 1;
constexpr static int SQ_POLL_PINNED_CPU = 2;

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

struct ErrorCode {
    u8 IP_PROTO: 1;
    u8 TCP_RECV_SYN: 1;
    u8 TCP_RECV_OLD_SEQ: 1;
    u8 TCP_RECV_ACK: 1;
    u8 TCP_RECV_WINDOW: 1;
    u8 TCP_RECV_IS_RST: 1;
    u8 TCP_RECV_UNEXPECTED_ACK: 1;

    void clear() {
        *reinterpret_cast<u8 *>(this) = 0;
    }

    [[nodiscard]] bool isErr() const {
        return *reinterpret_cast<const u8 *>(this);
    }

    [[nodiscard]] u8 getBitRep() const {
        assert(sizeof(ErrorCode) == 1);
        return *reinterpret_cast<const u8 *>(this);
    }

    void append(const ErrorCode& e) {
        *reinterpret_cast<u8 *>(this) |= e.getBitRep();
    }
} __attribute__((packed));


inline void logErrAndExit(const ErrorCode& err) {
    pr_info__("Error while receiving data [0x%x]\n", err.getBitRep());
    assert(false);
}

template<typename T>
auto max(T a, T b) {
    return b > a ? b : a;
}

struct Order {
    u8 packetType{OE_PACKET_TYPE};
    u8 _padding[5]{};
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
        flags{flags} {
    }
} __attribute__((packed));

template<typename T>
struct PacketHdr {
    ethhdr eth;
    iphdr ip;
    tcphdr tcp;
    T data;

    [[nodiscard]] u32 dataSz() const {
        return ntohs__(ip.tot_len) - ip.ihl * 4 - tcp.doff * 4;
    }
} __attribute((packed));


using OrderFrame = PacketHdr<Order>;


struct OrderInfo {
    Order orderInfo;

    TimeNs receivedTime = 0;
    TimeNs triggerSubmitTime = 0;

    OrderInfo(const Order& o, TimeNs r, TimeNs t) : orderInfo{o}, receivedTime{r}, triggerSubmitTime{t} {
    }
};


#define GET_PC(c) timeSpent[c]

inline int TOT_RECV_PC = 0;
inline int BOOK_UPDATE_PC = 1;
inline int ORDER_SUBMISSION_PC = 2;
inline int MSG_HANDLING_PC = 3;
inline int SYS_RECV_PC = 4;


struct MDFlags {
    bool isBid: 1;
    bool isSnapshot: 1;
    bool isTerm: 1;
};

struct MDPayload {
    u8 packetType{MD_PACKET_TYPE};
    char _padding[5]{};
    SeqNo seqNo = -1;
    TimeNs localTimestamp = 0;
    PriceL price = 0;
    Qty qty = 0;
    MDFlags flags{.isBid = false, .isSnapshot = false, .isTerm = true};
    char _padding2[5]{};


    MDPayload() {
        assert(sizeof(MDFlags) == 1);
        assert(sizeof(MDPayload) == 40);
    }

    MDPayload(SeqNo seqNo, TimeNs localTimestamp, PriceL price, Qty qty, MDFlags flags) : seqNo{seqNo},
        localTimestamp{localTimestamp},
        price{price}, qty{qty},
        flags{flags}, _padding{} {
        assert(sizeof(MDFlags) == 1);

    }

    MDPayload(const MDPayload& other) = default;
} __attribute__((packed));

template<typename X, typename Y>
union IOBlockers {
    X* fst;
    Y* snd;
};


inline const char* MCAST_ADDR = "239.255.0.1";
inline constexpr int MCAST_PORT = 12345;

inline const char* MD_UNICAST_HOSTNAME = "lll-1.md.client";

inline static constexpr PriceL TRADE_THRESHOLD = PRECISION_L * 3'000'000'000;


inline bool checkMessageDigest(const u8* buf, ssize_t bytes) {
    // static std::unordered_map<int, MDPayload> seenHashes{};
    // const MDPayload& p = *reinterpret_cast<const MDPayload *>(buf);

    // const auto& [e, inserted] = seenHashes.emplace(p.seqNo, p);

    // assert(inserted);
    return true;
}


static inline unsigned short from32to16(unsigned int x) {
    /* add up 16-bit and 16-bit for 16+c bit */
    x = (x & 0xffff) + (x >> 16);
    /* add up carry.. */
    x = (x & 0xffff) + (x >> 16);
    return x;
}

static unsigned int do_csum(const unsigned char* buff, int len) {
    unsigned int result = 0;

    assert(len == 20);
    assert((reinterpret_cast<unsigned long>(buff) & 0xf) == 0xe);

    if (2 & reinterpret_cast<unsigned long>(buff)) {
        result += *(unsigned short *) buff;
        len -= 2;
        buff += 2;
    }
    assert(len >= 4);

    const unsigned char* end = buff + (static_cast<unsigned>(len) & ~3);
    unsigned int carry = 0;

    do {
        const auto w = *(unsigned int *) buff;
        buff += 4;
        result += carry;
        result += w;
        carry = (w > result);
    } while (buff < end);

    result += carry;
    result = (result & 0xffff) + (result >> 16);

    if (len & 2) {
        result += *(unsigned short *) buff;
        buff += 2;
    }

    result = from32to16(result);

    return result;
}


inline u16 ip_fast_csum(const u8* iph, u32 ihl) {
    return (u16) ~do_csum(iph, ihl * 4);
}


/*
 * Fold a partial checksum
 * This function code has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline __sum16 csum_fold(__wsum csum) {
    u32 sum = (u32) csum;

    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);
    return (__sum16) ~sum;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static inline u32 from64to32(u64 x) {
    /* add up 32-bit and 32-bit for 32+c bit */
    x = (x & 0xffffffff) + (x >> 32);
    /* add up carry.. */
    x = (x & 0xffffffff) + (x >> 32);
    return (u32) x;
}

__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
                          __u32 len, __u8 proto, __wsum sum);

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
                          __u32 len, __u8 proto, __wsum sum) {
    unsigned long long s = (u32) sum;

    s += (u32) saddr;
    s += (u32) daddr;
#ifdef __BIG_ENDIAN__
	s += proto + len;
#else
    s += (proto + len) << 8;
#endif
    return (__wsum) from64to32(s);
}

/*
 * This function has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline u16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
                  __u8 proto, __wsum sum) {
    return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

static inline u16 tcpudp_csum(u32 saddr, u32 daddr, u32 len,
                           u8 proto, const u16* udp_pkt) {
    u32 csum = 0;
    u32 cnt = 0;

    /* udp hdr and data */
    for (; cnt < len; cnt += 2)
        csum += udp_pkt[cnt >> 1];

    return csum_tcpudp_magic(saddr, daddr, len, proto, csum);
}



#endif //DEFS_H
